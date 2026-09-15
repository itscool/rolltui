/*
 * rolltui/tests/globals.inc — EVERY PIECE OF MUTABLE PROCESS-WIDE STATE IN THE LIBRARY, and what
 * it is.
 *
 * A `RolltuiContext` is a single rolltui session, and the claim that nothing in the library is
 * global is only as true as this list. Every mutable process-wide static is named here with a
 * DISPOSITION and a reason; a static that is not on this list fails the test that reads it.
 *
 *   CONTEXT   session state. A second `RolltuiContext` in the same process must NOT see it, and
 *             `rolltui_context_free()` must let it go.
 *   PROCESS   legitimately one per process, and the reason is written on the row. **A row here
 *             is a claim, not a shrug** — "it was already global" is not a reason.
 *
 * ---- THE CONTRACT THIS BOUNDARY SERVES, stated in full at `RolltuiContext` itself -----------
 * **A context is a PLAIN OWNED HANDLE with no thread affinity.** It holds registries and caches
 * and nothing else, which is what makes the six points below true rather than aspirational.
 *   1. **One thread at a time per context.** The library locks nothing for you inside one. The
 *      standing exception is `RolltuiPresetStore`, which carries its own mutex so a host may edit
 *      from one thread and render from another.
 *   2. **ANY NUMBER OF CONTEXTS** — same thread or different, configured alike or differently.
 *      They share nothing a context owns — every registry and cache belongs to one.
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
 * ---- WHAT IS NOT ON THIS LIST, and why it is not an omission --------------------------------
 * A `static const` table is a constant, not state. So is `static const char* const`. What counts
 * is a mutable pointer (`static const T* p` — the POINTER moves) or any mutable object. Two
 * things left the list rather than earning a row, because a constant that merely looked mutable
 * is worse than either:
 *   - `rolltui_widget_menu.c`'s four `static const char* kHint` are literals never reassigned; they are
 *     `const char* const` now.
 *   - `rolltui_theme.c`'s default vocab was FOUR function-local statics filled behind a `built`
 *     flag — correct and allocation-free, but state a boundary would have had to justify. Both
 *     name arrays come from the same X-macros `rolltui_style.c` expands, so they are a
 *     COMPILE-TIME table now: the runtime fill, the per-call branch and four rows all went.
 */

/* ---- CONTEXT: session state — EMPTY (0) -------------------------------------------------
 *
 * EMPTY IS THE CLAIM, not an accident of what has been done so far. Every registry, cache and
 * descriptor the library once kept process-wide lives on `RolltuiContext` and is released BY
 * NAME in `rolltui_context_free`: the shipped default bindings table, the widget-kind and
 * effect-kind registries, the shipped actions and built-in layouts, and the three preset
 * descriptors. Nothing in `rolltui/c/` registers a shutdown hook, and a static that turns up
 * here fails the test that reads this file.
 *
 * FOUR RULES CAME OUT OF PUTTING THEM THERE, and each one is still load-bearing:
 *
 *   - A MUTEX ROUND A CONTEXT'S TABLE GUARDS NOTHING. Contract point 1 is one thread at a time
 *     per context, so a lock on a per-context registry protects against a caller who is
 *     already forbidden. It is not free either: the effect registry's lock was taken once per
 *     MARK on the per-frame draw path, to read a table nothing had written since startup.
 *
 *   - A CACHED BUILT-IN BELONGS TO THE CONTEXT THAT CACHED IT (point 4). The built-in layouts
 *     are parsed from the same embedded const bytes in every session, so each gets identical
 *     CONTENT and its own STORAGE, and a `RolltuiLayout*` from `rolltui_layout_builtin` may not
 *     outlive the session that parsed it.
 *
 *   - A PROCESS-WIDE DESCRIPTOR MAY NOT BORROW A CONTEXT'S STORAGE. The Layout preset
 *     descriptor holds `rolltui_layout_shipped_default_actions()`'s pointer, so leaving it
 *     process-wide would outlive that storage the moment one session was freed. That is
 *     contract point 4 stated as a defect rather than as a rule.
 *
 *   - CONFIGURATION BELONGS TO THE THING IT CONFIGURES. The ten preset configuration rows sit
 *     on the `RolltuiPresetDomain` each one configures, not on the context, and the callbacks
 *     that read them are handed their descriptor. Process-wide configuration meant two
 *     descriptors of the SAME kind shared one, so a host's own `_init` silently changed the
 *     library descriptor's behaviour.
 *
 * A SCAFFOLD GETS ITS REMOVAL CONDITION IN WRITING. Moving this state took one temporary
 * process-wide default context, which was recorded here with the condition under which it
 * would go — "when the last no-context entry point takes a context, this goes" — precisely so
 * it could not quietly become permanent. It is gone. Write the condition down, then delete the
 * scaffold; a migration that lands in green commits is worth a scaffold, and a scaffold with no
 * stated end is a global.
 */

/* ---- PROCESS: one per process by nature, each with its reason (16) ----------------------- */

/* THE RELEASER LIST, and it is PROCESS rather than CONTEXT even though it looks like a
 * context's business. `rolltui_shutdown()` is not `rolltui_context_free()` under another name:
 * every library subsystem is freed BY NAME when its context goes, and nothing in `rolltui/c/`
 * registers a hook at all. What is left on the list is what HOSTS and SUITES put there, for
 * things THEY own with process lifetime — `tool_actions.hpp`'s editor table, the studio's, this
 * suite's. A host facility on a host's schedule is process state by definition. */
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
 * attributable to a context. **Prove per-context release with DELTAS instead** — free contexts
 * in sequence and assert the total returns, which also proves neither leaked into the other. */
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
