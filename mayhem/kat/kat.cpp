// Known-answer probe for the pieces of Sigil that mayhem/ fuzzes. Built with the project's NORMAL
// flags (no sanitizer) by mayhem/build.sh; mayhem/test.sh diffs its output against expected/*.txt.
#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include <QString>
#include <QVector>

#include "Parsers/CSSParser.h"
#include "gumbo.h"
#include "hunspell.hxx"
#include "unzip.h"

static void walk(const GumboNode *n, int depth) {
  if (n->type == GUMBO_NODE_ELEMENT) {
    printf("%*s<%s>", depth * 2, "", gumbo_normalized_tagname(n->v.element.tag));
    const GumboVector *at = &n->v.element.attributes;
    for (unsigned i = 0; i < at->length; i++) {
      const GumboAttribute *a = (const GumboAttribute *)at->data[i];
      printf(" %s=\"%s\"", a->name, a->value);
    }
    printf("\n");
    const GumboVector *kids = &n->v.element.children;
    for (unsigned i = 0; i < kids->length; i++) walk((const GumboNode *)kids->data[i], depth + 1);
  } else if (n->type == GUMBO_NODE_TEXT) {
    printf("%*s#text \"%s\"\n", depth * 2, "", n->v.text.text);
  }
}

static int kat_gumbo() {
  const char *html =
      "<!DOCTYPE html><html><body><p class=\"a b\" id=x>Hello <b>big</b> world"
      "<ul><li>one<li>two</ul><br/></body></html>";
  GumboOutput *out = gumbo_parse(html);
  if (!out) return 1;
  walk(out->root, 0);
  gumbo_destroy_output(out);
  return 0;
}

static int kat_css(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return 1;
  std::string buf;
  char tmp[4096];
  size_t n;
  while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf.append(tmp, n);
  fclose(f);
  CSSParser p;
  p.parse_css(QString::fromUtf8(buf.data(), (qsizetype)buf.size()));
  printf("charset=[%s]\n", p.get_charset().toUtf8().constData());
  for (const QString &i : p.get_imports()) printf("import=[%s]\n", i.toUtf8().constData());
  printf("%s", p.serialize_css(false, true).toUtf8().constData());
  QStringList parts = CSSParser::splitGroupSelector("h1, p.intro > a:hover,div");
  for (const QString &s : parts) printf("sel=[%s]\n", s.toUtf8().constData());
  return 0;
}

static int kat_zip(const char *path) {
  unzFile uf = unzOpen64(path);
  if (!uf) return 1;
  unz_global_info64 gi;
  unzGetGlobalInfo64(uf, &gi);
  printf("entries=%llu\n", (unsigned long long)gi.number_entry);
  int rc = unzGoToFirstFile(uf);
  while (rc == UNZ_OK) {
    char name[256];
    unz_file_info64 fi;
    unzGetCurrentFileInfo64(uf, &fi, name, sizeof name, NULL, 0, NULL, 0);
    unzOpenCurrentFile(uf);
    char buf[4096];
    int len = unzReadCurrentFile(uf, buf, sizeof buf - 1);
    if (len < 0) len = 0;
    buf[len] = 0;
    int crc = unzCloseCurrentFile(uf);
    printf("%s size=%llu method=%lu crc=%s body=[%s]\n", name, (unsigned long long)fi.uncompressed_size,
           (unsigned long)fi.compression_method, crc == UNZ_OK ? "ok" : "BAD", buf);
    rc = unzGoToNextFile(uf);
  }
  unzClose(uf);
  return 0;
}

static int kat_spell(const char *aff, const char *dic) {
  Hunspell hs(aff, dic);
  const char *words[] = {"hello", "world", "helo", "worlds", "wrold", "xyzzyq"};
  for (const char *w : words) {
    bool ok = hs.spell(w);
    printf("%s %s", w, ok ? "ok" : "wrong");
    if (!ok) {
      std::vector<std::string> sug = hs.suggest(w);
      printf(" ->");
      for (size_t i = 0; i < sug.size() && i < 3; i++) printf(" %s", sug[i].c_str());
    }
    printf("\n");
  }
  return 0;
}

static int kat_pcre() {
  int err;
  PCRE2_SIZE eoff;
  static const char16_t pat[] = u"(\\d+)-(?<word>\\w+)";
  static const char16_t subj[] = u"abc 123-foo bar 45-baz";
  pcre2_code *re = pcre2_compile((PCRE2_SPTR16)pat, PCRE2_ZERO_TERMINATED, PCRE2_UTF, &err, &eoff, NULL);
  if (!re) { PCRE2_UCHAR16 buf[256]; pcre2_get_error_message(err, buf, 256); printf("compile error %d at %d: %c\n", err, (int)eoff, (char)buf[0]); return 1; }
  pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, NULL);
  PCRE2_SIZE slen = sizeof(subj) / sizeof(subj[0]) - 1, start = 0;
  int found = 0;
  while (pcre2_match(re, (PCRE2_SPTR16)subj, slen, start, 0, md, NULL) > 0) {
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    printf("match %d: [%d,%d) g1=[%d,%d) g2=[%d,%d)\n", ++found, (int)ov[0], (int)ov[1], (int)ov[2], (int)ov[3],
           (int)ov[4], (int)ov[5]);
    start = ov[1];
  }
  printf("total=%d\n", found);
  pcre2_match_data_free(md);
  pcre2_code_free(re);
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "gumbo")) return kat_gumbo();
  if (argc >= 3 && !strcmp(argv[1], "css")) return kat_css(argv[2]);
  if (argc >= 3 && !strcmp(argv[1], "zip")) return kat_zip(argv[2]);
  if (argc >= 4 && !strcmp(argv[1], "spell")) return kat_spell(argv[2], argv[3]);
  if (argc >= 2 && !strcmp(argv[1], "pcre")) return kat_pcre();
  fprintf(stderr, "usage: kat gumbo|css <f>|zip <f>|spell <aff> <dic>|pcre\n");
  return 2;
}
