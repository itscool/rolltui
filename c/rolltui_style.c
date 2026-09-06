/* rolltui/c/rolltui_style.c — the role name table, generated from the ONE list.
 *
 * There is deliberately no second listing here: the names come from `ROLLTUI_ROLE_LIST` in
 * the header, so a role added there gains its name in the same edit. That is the whole reason
 * the list is an X-macro (see the header's own note on why a second spelling drifts silently
 * rather than loudly). */
#include "rolltui/rolltui.h"

#include "rolltui/c/rolltui_style.h"

#include <string.h>

static const char* const kRoleNames[] = {
#define ROLLTUI_ROLE_NAME_(lower, UPPER) #lower,
    ROLLTUI_ROLE_LIST(ROLLTUI_ROLE_NAME_)
#undef ROLLTUI_ROLE_NAME_
};

const char* rolltui_role_name(unsigned char role, size_t* len) {
  const char* s = role < ROLLTUI_ROLE_COUNT ? kRoleNames[role] : "";
  if (len) *len = strlen(s);
  return s;
}

int rolltui_role_from_name(const char* name, size_t len) {
  size_t i;
  for (i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    const size_t n = strlen(kRoleNames[i]);
    if (n == len && memcmp(kRoleNames[i], name, len) == 0) return (int)i;
  }
  return -1;
}

/* ---- the effect-state names, from `ROLLTUI_EFFECT_STATE_LIST` -----------------------------
 * Here rather than in `rolltui_effects.c` for one reason: this file is the library's
 * VOCABULARY table, and both lists are the same kind of thing — a closed set of names a theme
 * file is written in. Keeping them together is what stops the next one being invented
 * somewhere else. */
#include "rolltui/c/rolltui_effects.h"

static const char* const kEffectStateNames[] = {
#define ROLLTUI_EFFECT_STATE_NAME_(lower, UPPER, Camel) #lower,
    ROLLTUI_EFFECT_STATE_LIST(ROLLTUI_EFFECT_STATE_NAME_)
#undef ROLLTUI_EFFECT_STATE_NAME_
};

const char* rolltui_effect_state_name(unsigned char state, size_t* len) {
  /* "none" for anything out of range, matching the C++ this replaces: a mark whose state does
   * not exist is not marked. */
  const char* s = state < ROLLTUI_EFFECT_STATE_COUNT ? kEffectStateNames[state] : kEffectStateNames[0];
  if (len) *len = strlen(s);
  return s;
}

int rolltui_effect_state_from_name(const char* name, size_t len) {
  size_t i;
  for (i = 0; i < ROLLTUI_EFFECT_STATE_COUNT; ++i) {
    const size_t n = strlen(kEffectStateNames[i]);
    if (n == len && memcmp(kEffectStateNames[i], name, len) == 0) return (int)i;
  }
  return -1;
}
