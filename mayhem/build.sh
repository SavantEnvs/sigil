#!/usr/bin/env bash
#
# mayhem/build.sh — builds, in /mayhem:
#   * five sanitized libFuzzer targets (+ a *-standalone reproducer each):
#       fuzz_css      Sigil's CSSParser + vendored lexbor CSS parser   (src/Parsers, internal/lexbor)
#       fuzz_gumbo    vendored gumbo HTML5 parser                      (internal/gumbo)
#       fuzz_minizip  vendored minizip + zlib (EPUB container reader)  (3rdparty/minizip, 3rdparty/zlib)
#       fuzz_hunspell vendored hunspell (aff/dic loader, spell, suggest) (3rdparty/hunspell)
#       fuzz_pcre2    vendored PCRE2 16-bit, upstream's own fuzz driver (3rdparty/pcre2)
#   * sigil-kat, the known-answer probe mayhem/test.sh runs, with the project's NORMAL flags.
# Sigil's GUI (Qt Widgets/WebEngine/Python) is not built: the targets compile only the sources that
# need nothing but Qt Core, directly with clang, so nothing is fetched and no upstream file changes.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
BD="$SRC/mayhem/build"           # gitignored (upstream .gitignore: `build`; mayhem/.gitignore: build/)
QT_CFLAGS="$(pkg-config --cflags Qt6Core)"
QT_LIBS="$(pkg-config --libs Qt6Core)"

# compile_lib <tag> <lib> <extra flags> <c|cxx> <sources...>   (tag = fuzz | kat; flags per tag)
compile_lib() {
  local tag="$1" lib="$2" extra="$3" lang="$4"; shift 4
  local od="$BD/$tag/obj/$lib" comp flags
  if [ "$tag" = fuzz ]; then
    flags="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1 -fPIC"
  else
    flags="$COVERAGE_FLAGS -O2 -fPIC"
  fi
  if [ "$lang" = c ]; then comp="$CC"; else comp="$CXX"; fi
  mkdir -p "$od"
  local stamp="$comp $flags $extra"
  if [ "$(cat "$od/.stamp" 2>/dev/null)" != "$stamp" ]; then rm -f "$od"/*.o; fi
  printf '%s' "$stamp" > "$od/.stamp"
  local s o
  for s in "$@"; do
    o="$od/$(echo "$s" | tr '/' '_').o"
    printf '%s\0' "$comp $flags $extra -c $s -o $o"
  done | xargs -0 -P "$MAYHEM_JOBS" -I{} bash -c '{}'
  rm -f "$BD/$tag/lib$lib.a"
  ar rcs "$BD/$tag/lib$lib.a" "$od"/*.o
}

ZLIB_SRCS=(adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil)
ZLIB_SRCS=("${ZLIB_SRCS[@]/#/3rdparty/zlib/}"); ZLIB_SRCS=("${ZLIB_SRCS[@]/%/.c}")
GUMBO_SRCS=(internal/gumbo/*.c)
mapfile -t LEXBOR_SRCS < <(grep -E '^[[:space:]]+lexbor/.*\.c$' internal/lexbor/CMakeLists.txt \
                           | grep -v '/ports/' | sed 's|^[[:space:]]*|internal/lexbor/|')
LEXBOR_SRCS+=(internal/lexbor/lexbor/ports/posix/lexbor/core/{fs,memory,perf}.c)
HUNSPELL_SRCS=(affentry affixmgr csutil filemgr hashmgr hunspell hunzip phonet replist suggestmgr)
HUNSPELL_SRCS=("${HUNSPELL_SRCS[@]/#/3rdparty/hunspell/src/hunspell/}"); HUNSPELL_SRCS=("${HUNSPELL_SRCS[@]/%/.cxx}")
PCRE_SRCS=(auto_possess chkdint compile compile_class config context convert dfa_match error extuni
           find_bracket jit_compile maketables match match_data newline ord2utf pattern_info script_run
           serialize string_utils study substitute substring tables ucd valid_utf xclass)
PCRE_SRCS=("${PCRE_SRCS[@]/#/3rdparty/pcre2/src/pcre2_}"); PCRE_SRCS=("${PCRE_SRCS[@]/%/.c}")

build_libs() {
  local tag="$1"
  local gen="$BD/$tag/gen"
  mkdir -p "$gen"
  # PCRE2 configuration, mirroring 3rdparty/cmake/pcre2.cmake (16-bit, UTF, UCP, JIT, LINK_SIZE 4)
  sed -E -e 's,^/\* #undef (SUPPORT_PCRE2_16|SUPPORT_UNICODE|SUPPORT_JIT|HAVE_MEMMOVE|HAVE_STRERROR) \*/,#define \1 1,' \
      -e 's,^#define LINK_SIZE 2,#define LINK_SIZE 4,' 3rdparty/pcre2/src/config.h.generic > "$gen/config.h"
  cp 3rdparty/pcre2/src/pcre2.h.generic "$gen/pcre2.h"
  cp 3rdparty/pcre2/src/pcre2_chartables.c.dist "$gen/pcre2_chartables.c"

  local zinc="-I3rdparty/zlib -DHAVE_SYS_TYPES_H -DHAVE_STDINT_H -DHAVE_STDDEF_H -DHAVE_STDARG_H -DHAVE_UNISTD_H -D_LARGEFILE64_SOURCE=1"
  local pcre="-DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=16 -DPCRE2_STATIC -I$gen -I3rdparty/pcre2/src"
  compile_lib "$tag" z "$zinc" c "${ZLIB_SRCS[@]}"
  compile_lib "$tag" minizip "$zinc -I3rdparty/minizip" c 3rdparty/minizip/ioapi.c 3rdparty/minizip/unzip.c
  compile_lib "$tag" gumbo "-std=c99 -D_GNU_SOURCE -Iinternal/gumbo" c "${GUMBO_SRCS[@]}"
  compile_lib "$tag" lexbor "-DLEXBOR_STATIC -Iinternal/lexbor" c "${LEXBOR_SRCS[@]}"
  compile_lib "$tag" pcre2 "$pcre" c "${PCRE_SRCS[@]}" "$gen/pcre2_chartables.c"
  compile_lib "$tag" hunspell "-std=c++11 -I3rdparty/extra/hunspell -I3rdparty/hunspell/src/hunspell" cxx "${HUNSPELL_SRCS[@]}"
  compile_lib "$tag" sigilcss_cxx "-std=c++17 $QT_CFLAGS -Isrc -Iinternal/lexbor -DLEXBOR_STATIC" cxx \
      src/Parsers/CSSParser.cpp src/Parsers/CSSUtils.cpp src/Parsers/CSSDeNest.cpp
  compile_lib "$tag" sigilcss_c "-Isrc -Iinternal/lexbor -DLEXBOR_STATIC" c src/Parsers/css_structure_parser.c
}

build_libs fuzz
build_libs kat

# --- fuzz harnesses: libFuzzer binary + standalone reproducer, both with LSan off --------------------
FZ="$SANITIZER_FLAGS $DEBUG_FLAGS -O1"
F="$BD/fuzz"
$CXX $FZ -c mayhem/lsan_off.cc -o "$BD/fuzz/lsan_off.o"
$CC  $FZ -c "$STANDALONE_FUZZ_MAIN" -o "$BD/fuzz/standalone_main.o"

# link_target <name> <compiler> <harness-src> <compile flags> <libs...>
link_target() {
  local name="$1" comp="$2" harness="$3" cflags="$4"; shift 4
  $comp $FZ $cflags $LIB_FUZZING_ENGINE "$harness" "$@" "$BD/fuzz/lsan_off.o" -o "$SRC/$name"
  $comp $FZ $cflags "$harness" "$BD/fuzz/standalone_main.o" "$@" "$BD/fuzz/lsan_off.o" -o "$SRC/$name-standalone"
}

link_target fuzz_gumbo "$CC" mayhem/harnesses/fuzz_gumbo.c "-Iinternal/gumbo" "$F/libgumbo.a"
link_target fuzz_minizip "$CC" mayhem/harnesses/fuzz_minizip.c "-I3rdparty/minizip -I3rdparty/zlib -D_LARGEFILE64_SOURCE=1" \
  "$F/libminizip.a" "$F/libz.a"
link_target fuzz_pcre2 "$CC" 3rdparty/pcre2/src/pcre2_fuzzsupport.c \
  "-DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=16 -DPCRE2_STATIC -I$F/gen -I3rdparty/pcre2/src" "$F/libpcre2.a" -lpthread
link_target fuzz_hunspell "$CXX" mayhem/harnesses/fuzz_hunspell.cc "-std=c++17 -I3rdparty/extra/hunspell -I3rdparty/hunspell/src/hunspell" \
  "$F/libhunspell.a"
link_target fuzz_css "$CXX" mayhem/harnesses/fuzz_css.cpp "-std=c++17 $QT_CFLAGS -Isrc" \
  "$F/libsigilcss_cxx.a" "$F/libsigilcss_c.a" "$F/liblexbor.a" $QT_LIBS

# --- known-answer probe + test data, project's NORMAL flags (no sanitizer, no fuzzer) -----------------
K="$BD/kat"
$CXX $COVERAGE_FLAGS -O2 -std=c++17 -fPIC $QT_CFLAGS -Isrc -Iinternal/gumbo -I3rdparty/minizip -I3rdparty/zlib \
  -I3rdparty/extra/hunspell -I3rdparty/hunspell/src/hunspell -DPCRE2_STATIC -I"$K/gen" \
  mayhem/kat/kat.cpp -o "$SRC/sigil-kat" \
  "$K/libsigilcss_cxx.a" "$K/libsigilcss_c.a" "$K/liblexbor.a" "$K/libgumbo.a" "$K/libminizip.a" "$K/libz.a" \
  "$K/libhunspell.a" "$K/libpcre2.a" $QT_LIBS -lpthread
