#ifndef ROLLTUI_C_HINTS_H
#define ROLLTUI_C_HINTS_H
/* rolltui/c/rolltui_hints.h — INTERNAL. The public declarations of the hint bar live in
 * `rolltui/rolltui.h`; what is below is the library's own. */
#include "rolltui/rolltui.h"
#ifdef __cplusplus
extern "C" {
#endif
/* How many hints the bar holds, and the i-th one's action — for a suite. */
size_t rolltui_hint_bar_count(const RolltuiHintBar* b);
#ifdef __cplusplus
}
#endif
#endif
