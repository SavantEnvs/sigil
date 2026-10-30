// Sigil's vendored hunspell (3rdparty/hunspell): load an affix + dictionary pair, then spell,
// suggest, analyze and stem words taken from the dictionary. Input = <aff> "\n===DIC===\n" <dic>.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "hunspell.hxx"

static const char kMarker[] = "\n===DIC===\n";

static bool write_temp(const char *dir, const uint8_t *p, size_t n, std::string &path) {
  path = std::string(dir) + "/hsXXXXXX";
  std::vector<char> tmpl(path.begin(), path.end());
  tmpl.push_back(0);
  int fd = mkstemp(tmpl.data());
  if (fd < 0) return false;
  size_t off = 0;
  while (off < n) {
    ssize_t w = write(fd, p + off, n - off);
    if (w <= 0) break;
    off += (size_t)w;
  }
  close(fd);
  path = tmpl.data();
  return off == n;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const uint8_t *sep = NULL;
  for (size_t i = 0; i + sizeof(kMarker) - 1 <= size; i++) {
    if (memcmp(data + i, kMarker, sizeof(kMarker) - 1) == 0) { sep = data + i; break; }
  }
  if (!sep) return -1;
  const uint8_t *dic = sep + sizeof(kMarker) - 1;
  size_t aff_len = (size_t)(sep - data), dic_len = (size_t)(data + size - dic);

  const char *tmp = getenv("TMPDIR");
  if (!tmp || !*tmp) tmp = "/tmp";
  std::string affp, dicp;
  if (!write_temp(tmp, data, aff_len, affp)) return 0;
  if (!write_temp(tmp, dic, dic_len, dicp)) { unlink(affp.c_str()); return 0; }

  {
    Hunspell hs(affp.c_str(), dicp.c_str());
    std::vector<std::string> words = {"hello", "colour", "unfriendly", "x"};
    size_t pos = 0;
    while (words.size() < 12 && pos < dic_len) {
      size_t e = pos;
      while (e < dic_len && dic[e] != '\n' && dic[e] != '/' && dic[e] != '\t') e++;
      if (e > pos && e - pos < 64) words.emplace_back((const char *)dic + pos, e - pos);
      while (e < dic_len && dic[e] != '\n') e++;
      pos = e + 1;
    }
    for (const std::string &w : words) {
      if (hs.spell(w)) {
        hs.analyze(w);
        hs.stem(w);
      } else {
        hs.suggest(w);
      }
    }
    hs.add("zzword");
    hs.spell("zzword");
    hs.remove("zzword");
  }
  unlink(affp.c_str());
  unlink(dicp.c_str());
  return 0;
}
