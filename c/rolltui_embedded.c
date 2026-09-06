/* rolltui/c/rolltui_embedded.c — see rolltui_embedded.h. The tables themselves are
 * generated (cmake/embed_presets.cmake); this is the one lookup over them. */
#include "rolltui/rolltui.h"

#include <string.h>

const char* rolltui_embedded_text(const RolltuiEmbeddedFile* table, size_t count, const char* name,
                                  size_t name_len) {
  size_t i;
  for (i = 0; i < count; ++i) {
    const char* n = table[i].name;
    if (strlen(n) == name_len && memcmp(n, name, name_len) == 0) return table[i].text;
  }
  return NULL;
}
