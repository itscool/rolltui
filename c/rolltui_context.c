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

/* The transitional rung (see the header). Deliberately NOT freed at exit: it is owned by the
 * process for as long as a no-context entry point can be called, and the tests that assert
 * `live_bytes == 0` release it explicitly through `rolltui_shutdown`. */
static RolltuiContext* g_default;

RolltuiContext* rolltui_context_default(void) {
  if (g_default == NULL) g_default = rolltui_context_new();
  return g_default;
}

/* What `rolltui_shutdown()` now means for the default context: release what it owns and let it
 * be rebuilt on next use, which is exactly the "safe to call at any moment" contract that
 * function has always had. */
void rolltui_context_default_release(void) {
  rolltui_context_free(g_default);
  g_default = NULL;
}
