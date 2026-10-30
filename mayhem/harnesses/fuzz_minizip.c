/* Sigil's vendored minizip (3rdparty/minizip) + zlib: open an EPUB-style zip from memory,
 * walk the central directory and inflate every entry. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unzip.h"

typedef struct {
  const uint8_t *data;
  uint64_t size;
  uint64_t pos;
} memzip;

static voidpf ZCALLBACK mem_open(voidpf opaque, const void *filename, int mode) {
  (void)filename;
  (void)mode;
  return opaque;
}

static uLong ZCALLBACK mem_read(voidpf opaque, voidpf stream, void *buf, uLong size) {
  (void)opaque;
  memzip *m = (memzip *)stream;
  uint64_t left = m->size - m->pos;
  if (size > left) size = (uLong)left;
  memcpy(buf, m->data + m->pos, size);
  m->pos += size;
  return size;
}

static uLong ZCALLBACK mem_write(voidpf opaque, voidpf stream, const void *buf, uLong size) {
  (void)opaque; (void)stream; (void)buf; (void)size;
  return 0;
}

static ZPOS64_T ZCALLBACK mem_tell(voidpf opaque, voidpf stream) {
  (void)opaque;
  return ((memzip *)stream)->pos;
}

static long ZCALLBACK mem_seek(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) {
  (void)opaque;
  memzip *m = (memzip *)stream;
  uint64_t np;
  switch (origin) {
    case ZLIB_FILEFUNC_SEEK_CUR: np = m->pos + offset; break;
    case ZLIB_FILEFUNC_SEEK_END: np = m->size + offset; break;
    case ZLIB_FILEFUNC_SEEK_SET: np = offset; break;
    default: return -1;
  }
  if (np > m->size) return -1;
  m->pos = np;
  return 0;
}

static int ZCALLBACK mem_close(voidpf opaque, voidpf stream) {
  (void)opaque; (void)stream;
  return 0;
}

static int ZCALLBACK mem_error(voidpf opaque, voidpf stream) {
  (void)opaque; (void)stream;
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  memzip m = {data, size, 0};
  zlib_filefunc64_def ff;
  ff.zopen64_file = mem_open;
  ff.zread_file = mem_read;
  ff.zwrite_file = mem_write;
  ff.ztell64_file = mem_tell;
  ff.zseek64_file = mem_seek;
  ff.zclose_file = mem_close;
  ff.zerror_file = mem_error;
  ff.opaque = &m;

  unzFile uf = unzOpen2_64("mem.zip", &ff);
  if (!uf) return -1;

  unz_global_info64 gi;
  if (unzGetGlobalInfo64(uf, &gi) == UNZ_OK) {
    static char buf[1 << 16];
    char name[512];
    unz_file_info64 fi;
    int rc = unzGoToFirstFile(uf);
    int entries = 0;
    while (rc == UNZ_OK && entries++ < 64) {
      if (unzGetCurrentFileInfo64(uf, &fi, name, sizeof(name), NULL, 0, NULL, 0) == UNZ_OK &&
          unzOpenCurrentFile(uf) == UNZ_OK) {
        size_t total = 0;
        int n;
        while ((n = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0 && total < (1u << 22))
          total += (size_t)n;
        unzCloseCurrentFile(uf);
      }
      rc = unzGoToNextFile(uf);
    }
    unzLocateFile(uf, "mimetype", 0);
  }
  unzClose(uf);
  return 0;
}
