/* Sigil's vendored gumbo HTML5 parser (internal/gumbo): parse as a document or as a
 * fragment in a fuzzer-chosen context, then walk the whole tree. */
#include <stddef.h>
#include <stdint.h>

#include "gumbo.h"

static size_t walk(const GumboNode *node) {
  size_t n = 1;
  if (node->type == GUMBO_NODE_ELEMENT || node->type == GUMBO_NODE_TEMPLATE) {
    const GumboVector *kids = &node->v.element.children;
    for (unsigned i = 0; i < kids->length; i++)
      n += walk((const GumboNode *)kids->data[i]);
    const GumboVector *attrs = &node->v.element.attributes;
    for (unsigned i = 0; i < attrs->length; i++) {
      const GumboAttribute *a = (const GumboAttribute *)attrs->data[i];
      n += (a->name[0] != 0) + (a->value[0] != 0);
    }
  }
  return n;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static const GumboTag contexts[] = {
      GUMBO_TAG_LAST, GUMBO_TAG_BODY,   GUMBO_TAG_TABLE,  GUMBO_TAG_SELECT,
      GUMBO_TAG_HTML, GUMBO_TAG_TEMPLATE, GUMBO_TAG_TR,   GUMBO_TAG_TEXTAREA,
  };
  static const GumboNamespaceEnum spaces[] = {
      GUMBO_NAMESPACE_HTML, GUMBO_NAMESPACE_HTML, GUMBO_NAMESPACE_SVG,
      GUMBO_NAMESPACE_MATHML,
  };
  if (size < 1) return -1;
  uint8_t sel = data[0];
  data++;
  size--;

  GumboOptions opts = kGumboDefaultOptions;
  opts.use_xhtml_rules = sel & 1;
  opts.stop_on_first_error = (sel >> 1) & 1;
  GumboTag ctx = contexts[(sel >> 2) & 7];
  GumboNamespaceEnum ns = spaces[(sel >> 5) & 3];
  if (ctx == GUMBO_TAG_LAST) ns = GUMBO_NAMESPACE_HTML;

  GumboOutput *out =
      gumbo_parse_fragment(&opts, (const char *)data, size, ctx, ns);
  if (!out) return 0;
  volatile size_t sink = walk(out->document) + (out->root ? walk(out->root) : 0);
  (void)sink;
  gumbo_destroy_output(out);
  return 0;
}
