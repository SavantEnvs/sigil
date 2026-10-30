// Sigil's own CSS handling: CSSParser (lexbor css tokenizer/structure parser) + serializer.
#include <stddef.h>
#include <stdint.h>

#include <QString>

#include "Parsers/CSSParser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  QString src = QString::fromUtf8(reinterpret_cast<const char *>(data), static_cast<qsizetype>(size));
  CSSParser parser;
  parser.parse_css(src);
  parser.get_parse_errors();
  parser.get_charset();
  parser.get_imports();
  parser.get_namespace();
  parser.get_layer();
  QString multi = parser.serialize_css(false, true);
  QString flat = parser.serialize_css(false, false);

  CSSParser again;
  again.parse_css(multi);
  again.serialize_css(false, true);

  CSSParser::splitGroupSelector(flat);
  CSSParser::findNextClassInSelector(flat, 0);
  return 0;
}
