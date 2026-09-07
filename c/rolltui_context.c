/* rolltui/c/rolltui_context.c — see rolltui_context.h. */
#include "rolltui/c/rolltui_context.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"

RolltuiContext* rolltui_context_new(void) {
  RolltuiContext* c = (RolltuiContext*)rolltui_mem_alloc(sizeof *c);
  memset(c, 0, sizeof *c);
  return c;
}

void rolltui_context_free(RolltuiContext* c) {
  if (c == NULL) return;
  /* By NAME, in one place, so the set a context owns is readable rather than discovered. */
  rolltui_kind_registry_free(c->kinds);
  rolltui_effect_registry_free(c->effects);
  rolltui_layout_cache_free(c->layouts);
  rolltui_bindings_free(c->bindings);
  rolltui_preset_domains_free(c->presets);
  rolltui_window_config_free(c->window_config);
  rolltui_mem_free(c);
}
