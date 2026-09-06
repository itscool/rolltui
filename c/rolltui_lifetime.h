#ifndef ROLLTUI_C_LIFETIME_H
#define ROLLTUI_C_LIFETIME_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_lifetime.h — THE RELEASE POINT, reachable from C (Phase 15 m2; the
 * implementation itself — process-wide AND per-thread releasers — moved here from
 * `rolltui/Lifetime.cpp` at Phase 17 m1; `rolltui/Lifetime.hpp` is now a thin forwarding
 * shim over this file, same as `rolltui/c/rolltui_mem.h` is for `rolltui/Memory.hpp`).
 *
 * `rolltui::shutdown()` (here, `rolltui_shutdown`) is where the library hands back
 * everything it retains, and `rolltui/Lifetime.hpp` states the rule for taking part:
 * **register a releaser where the retained thing is MADE, not in a central list — a
 * central list is a second place to forget.** Until Phase 15 m2 no C module retained
 * anything (the frame, the wrap engine and the Unicode algorithms have no `static` at
 * all, by design), so the registration had no C spelling. The effect-kind registry was
 * the first, and it was not the last: the widget registry, the layout cache and two
 * action tables followed.
 *
 * FOUR FUNCTIONS, and they are the same ones C++ calls (`rolltui::on_shutdown`,
 * `rolltui::shutdown`, `rolltui::release_thread`, `rolltui::detail::on_thread_release`).
 * `rolltui_shutdown` runs the process-wide releasers in reverse order of registration —
 * so a module that retains something built out of another module's thing is released
 * first — then releases every thread's own scratch the same way `rolltui_release_thread`
 * does on its own.
 *
 * **THERE IS NO INIT, AND THAT IS THE DESIGN** (carried over from `rolltui/Lifetime.hpp`,
 * which states the reasoning at length: a library you must initialise is worse than one
 * you need not, and teardown carries none of that ordering cost). `rolltui_shutdown` is
 * safe to never call, safe to call twice, and needs nothing to have happened first.
 */

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
