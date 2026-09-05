/* rolltui/c/rolltui_style.c — the role name table, generated from the ONE list.
 *
 * There is deliberately no second listing here: the names come from `ROLLTUI_ROLE_LIST` in
 * the header, so a role added there gains its name in the same edit. That is the whole reason
 * the list is an X-macro (see the header's own note on why a second spelling drifts silently
 * rather than loudly). */
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
