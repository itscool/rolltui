/*
 * rolltui/tests/globals.inc — EVERY PIECE OF MUTABLE PROCESS-WIDE STATE IN THE LIBRARY, and what
 * it is (Phase 25 m1, 2026-09-06).
 *
 * THE USER'S PARENTHESIS IS THE PHASE: `RolltuiContext` should be *"a real single rolltui session
 * hopefully proving nothing is global"*. **It was FALSE when this file was written** — 44 mutable
 * statics across 9 files — and a split that left them where they were would have been cosmetic.
 * This file is the boundary: every one of them is named, with a DISPOSITION and a reason.
 *
 *   CONTEXT   session state. A second `RolltuiContext` in the same process must NOT see it, and
 *             `rolltui_context_free()` must let it go. These move in m2.
 *   PROCESS   legitimately one per process, and the reason is written on the row. **A row here
 *             is a claim, not a shrug** — "it was already global" is not a reason.
 *
 * ---- THE CONTRACT THIS BOUNDARY SERVES, stated in full at `RolltuiContext` itself ------------
 * **A context is a PLAIN OWNED HANDLE with no thread affinity.** It holds registries and caches
 * and nothing else, which is what makes the six points below true rather than aspirational.
 *   1. **One thread at a time per context.** The library locks nothing for you inside one. The
 *      standing exception is `RolltuiPresetStore`, which carries its own mutex so a host may edit
 *      from one thread and render from another.
 *   2. **ANY NUMBER OF CONTEXTS** — same thread or different, configured alike or differently.
 *      They share nothing a context owns; after m2 that is every registry and cache, which is
 *      the user's parenthesis made true.
 *   3. **LAYOUTS, THEMES AND BINDINGS TABLES ARE PLAIN DATA AND PORTABLE BETWEEN CONTEXTS**, and
 *      this is the property that makes several contexts USEFUL rather than merely possible. A
 *      `RolltuiLayout` is parsed data; kind resolution happens at `rolltui_windows_sync`, not at
 *      load — which is exactly why an unknown kind is a runtime error PANEL and not a load
 *      failure. So one layout may drive two contexts and each resolves kinds against its OWN
 *      registry. The same holds for a filled style table and a `RolltuiBindings`.
 *   4. **A CACHED BUILT-IN BELONGS TO THE CONTEXT THAT CACHED IT** — what `rolltui_layout_builtin`,
 *      the shipped-theme cache and `rolltui_preset_shipped` hand back. Reading one from another
 *      context is fine; OUTLIVING its owner is not. This is the bug the phase could otherwise
 *      introduce, and ASan finds it only if a test does it.
 *   5. **ONLY ONE CONTEXT MAY DRIVE A TERMINAL** (one fd, one saved `termios`, one signal
 *      disposition). Any number may build screens, compose and render to TEXT headless, which is
 *      what every test and every `--frame` run already does.
 *   6. **The allocator counters are a process-wide atomic SUM**, not per context: assert zero
 *      after freeing ALL contexts, never per context while several are alive.
 *
 * ---- WHAT IS NOT ON THIS LIST, and why it is not an omission -------------------------------
 * A `static const` table is a constant, not state. So is `static const char* const`. What counts
 * is a mutable pointer (`static const T* p` — the POINTER moves) or any mutable object. Two
 * things left the list rather than earning a row, because a constant that merely looked mutable
 * is worse than either (Phase 25 m1, under the standing "leave it in its best condition" policy):
 *   - `rolltui_menu.c`'s four `static const char* kHint` are literals never reassigned; they are
 *     `const char* const` now.
 *   - `rolltui_theme.c`'s default vocab was FOUR function-local statics filled behind a `built`
 *     flag — correct and allocation-free, but state a boundary would have had to justify. Both
 *     name arrays come from the same X-macros `rolltui_style.c` expands, so they are a
 *     COMPILE-TIME table now: the runtime fill, the per-call branch and four rows all went.
 */

/* ---- CONTEXT: session state — EMPTY as of m2, 2026-09-07 (0) --------------------------------------------- */

/* ~~THE SHIPPED DEFAULT BINDINGS TABLE~~ — MOVED 2026-09-07 (m2). Every caller BORROWS it, so
 * `rolltui_context_free` releasing it is what bounds the borrow. */

/* ~~THE EFFECT-KIND REGISTRY~~ — MOVED 2026-09-06 (m2), the second subsystem across, and the
 * same shape as the widget kinds: `g_kinds`/`_count`/`_cap` plus a releaser flag, now
 * `RolltuiContext::effects` and released by name.
 *
 * `g_mu` WENT WITH THEM RATHER THAN MOVING, and that is the contract doing work rather than a
 * relaxation of it. A mutex was right for a table any thread could reach; a context is entered
 * by ONE THREAD AT A TIME (point 1), so the lock guarded nothing a caller was still permitted
 * to do. It was not free: `rolltui_effects_apply` took it once per MARK, on the per-frame draw
 * path, to read a table nothing had written since startup. */

/* ~~THE WIDGET-KIND REGISTRY~~ — MOVED 2026-09-06 (m2), the first subsystem across and the one
 * the two-context proof registers into. It was `g_host_kinds`/`_count`/`_cap` plus a
 * `g_kind_releaser_registered` flag; it is `RolltuiContext::kinds` now, released by name in
 * `rolltui_context_free` rather than by a shutdown hook — so the flag went too, along with the
 * hook it guarded. `c_consumer_test.c` section 3e is the assertion: a kind registered in one
 * context is UNKNOWN in the other, and both resolve the library's own rung identically. */

/* ~~THE SHIPPED ACTIONS AND THE BUILT-IN LAYOUTS~~ — MOVED 2026-09-07 (m2), together, as
 * `RolltuiContext::layouts`. Both are parsed once from the same embedded const bytes, so every
 * session gets identical CONTENT and its own STORAGE — contract point 4 exactly: a cached
 * built-in belongs to the context that cached it, and a `RolltuiLayout*` handed out of
 * `rolltui_layout_builtin` may not outlive the session that parsed it. */


/* ~~THE THREE PRESET DESCRIPTORS AND THEIR CONFIGURATION~~ — MOVED 2026-09-07 (m2), and this
 * one HAD to move rather than merely could. The Layout descriptor holds
 * `rolltui_layout_shipped_default_actions()`'s pointer, and that cache is a context's since
 * this milestone — so a process-wide descriptor outlives the storage it borrows the moment one
 * session is freed. Contract point 4 stated as a defect instead of a rule.
 *
 * The descriptors are `RolltuiContext::presets`. The TEN configuration rows did NOT follow them
 * into the context: they went onto the `RolltuiPresetDomain` each one configures, and the four
 * callbacks that read them are handed their descriptor. That fixed a second defect nobody had
 * met yet — two descriptors of the SAME kind shared one configuration, so a host's own `_init`
 * silently changed the library's descriptor's behaviour. Configuration belongs to the thing it
 * configures. */

/* ---- PROCESS: one per process by nature, each with its reason (17) -------------------------- */

/* THE TRANSITIONAL DEFAULT CONTEXT (m2). The one global this phase ADDS, and it is here rather
 * than hidden precisely because a transitional global nobody is committed to removing is just a
 * global. Every no-context entry point still in the API forwards to it, which is what lets the
 * migration land one subsystem at a time in green commits instead of one unverifiable change —
 * the discipline Phase 14 used with `-DROLLTUI_C`.
 * **REMOVAL CONDITION, stated so it is checkable:** when the last no-context entry point takes a
 * context, this goes, `rolltui_shutdown`'s call to `rolltui_context_default_release` goes with
 * it, and this row goes with both. Its remaining callers today are `rolltui_app_profile_mount`
 * (which takes a `RolltuiWindows` that carries a context in m3) and two suites whose subject IS
 * the process-wide shutdown path. */
ROLLTUI_GLOBAL("rolltui_context.c", "g_default", PROCESS)

/* ~~THE RELEASER LIST~~ — RECLASSIFIED PROCESS 2026-09-07 (m2), which is the milestone's real
 * result and not a concession. m1 filed it CONTEXT on the assumption that `rolltui_shutdown()`
 * BECOMES `rolltui_context_free()`. It does not: once every library subsystem moved into a
 * context and was freed BY NAME, nothing in `rolltui/c/` registers a hook at all. What is left
 * on the list is what HOSTS and TESTS put there, for things THEY own with process lifetime —
 * `tool_actions.hpp`'s editor table, the studio's, this suite's. A host facility on a host's
 * schedule is process state by definition, so the row moves rather than the state. */
ROLLTUI_GLOBAL("rolltui_lifetime.c", "g_process", PROCESS)

/* THE KEYBOARD PROTOCOL LEVEL the terminal negotiated. It describes THE TTY, not a session, and
 * a process has one controlling terminal. Two contexts share it because they share the terminal. */
ROLLTUI_GLOBAL("rolltui_keys.c", "g_protocol", PROCESS)

/* PER-THREAD by design, not per-session: the scratch buffers the draw path lends, released by
 * `rolltui_release_thread`. A thread's scratch belongs to the thread that allocated it. */
ROLLTUI_GLOBAL("rolltui_lifetime.c", "g_thread", PROCESS)

/* THE ALLOCATOR COUNTERS. `rolltui_mem_alloc` takes no context and must not: threading one
 * through every allocation touches every call for a property nothing needs. They are `_Atomic`,
 * so two contexts may allocate concurrently, and they are a process-wide SUM that is NOT
 * attributable to a context. **m2's proof works with deltas instead** — free contexts in
 * sequence and assert the total returns, which also proves neither leaked into the other. */
ROLLTUI_GLOBAL("rolltui_mem.c", "g_allocations", PROCESS)
ROLLTUI_GLOBAL("rolltui_mem.c", "g_frees", PROCESS)
ROLLTUI_GLOBAL("rolltui_mem.c", "g_bytes", PROCESS)
ROLLTUI_GLOBAL("rolltui_mem.c", "g_live", PROCESS)
ROLLTUI_GLOBAL("rolltui_mem.c", "g_live_bytes", PROCESS)
ROLLTUI_GLOBAL("rolltui_mem.c", "g_peak_bytes", PROCESS)

/* THE ONE CONTROLLING TERMINAL: an output fd, a wake fd, the saved `termios` to restore, the
 * leave sequence an emergency handler writes, and whether the signal handlers are installed.
 * Already `_Atomic` for ASYNC-SIGNAL-SAFETY rather than for threading. A process has one tty,
 * one saved mode and one signal disposition, so **only one context may DRIVE a terminal** — a
 * second can still build screens, compose and render to text, which is what every test and every
 * `--frame` run already does. */
ROLLTUI_GLOBAL("rolltui_terminal.c", "g_out_fd", PROCESS)
ROLLTUI_GLOBAL("rolltui_terminal.c", "g_wake_fd", PROCESS)
ROLLTUI_GLOBAL("rolltui_terminal.c", "g_leave", PROCESS)
ROLLTUI_GLOBAL("rolltui_terminal.c", "g_leave_len", PROCESS)
ROLLTUI_GLOBAL("rolltui_terminal.c", "g_saved_tio", PROCESS)
ROLLTUI_GLOBAL("rolltui_terminal.c", "g_have_tio", PROCESS)
ROLLTUI_GLOBAL("rolltui_terminal.c", "g_handlers_installed", PROCESS)
