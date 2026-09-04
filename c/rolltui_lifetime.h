#ifndef ROLLTUI_C_LIFETIME_H
#define ROLLTUI_C_LIFETIME_H
/*
 * rolltui/c/rolltui_lifetime.h — THE RELEASE POINT, reachable from C (Phase 15 m2).
 *
 * `rolltui::shutdown()` is where the library hands back everything it retains, and
 * `rolltui/Lifetime.hpp` states the rule for taking part: **register a releaser where the
 * retained thing is MADE, not in a central list — a central list is a second place to
 * forget.** Until m2 no C module retained anything (the frame, the wrap engine and the
 * Unicode algorithms have no `static` at all, by design), so the registration had no C
 * spelling. The effect-kind registry is the first, and it will not be the last: the widget
 * registry, the layout cache and two action tables are all still ahead.
 *
 * ONE FUNCTION, and it is the same one C++ calls. `shutdown()` runs the releasers in
 * reverse order of registration, so a module that retains something built out of another
 * module's thing is released first.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Registers a releaser to run at `rolltui::shutdown()`. Registering the same function
 * twice registers it twice; register once, where the thing is made. */
void rolltui_on_shutdown(void (*fn)(void));

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_LIFETIME_H */
