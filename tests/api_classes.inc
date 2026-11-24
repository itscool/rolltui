/*
 * rolltui/tests/api_classes.inc — THE CLASS OF EVERY PUBLIC FUNCTION.
 *
 * This is the library's intentional public API design as data a test enforces: every
 * function declared in a public header has ONE class, with the reason written where it is not the
 * module's default. The classes are what `rolltui.h` is written from — never a prediction of it —
 * and `public_header_test`'s section 9 reads them (every PUBLIC row has a reader).
 *
 * ENFORCED STANDING ALONE (`public_header_test` section 6): every function declared in rolltui.h, rolltui_studio.h or a c/ header
 * has exactly one row, every row names a declared function, a PUBLIC row is declared in a public header and an INTERNAL one never
 * in rolltui.h, and the consumers in this tree (dirktui, paint) reach nothing INTERNAL. What a host OUTSIDE this tree needs (roll,
 * mother) no scan here can see, so a PUBLIC row no consumer here reaches says why: KEPT, or the host it is for.
 *
 * ---- REACH IS THE INPUT, NEVER THE CRITERION ------------------------------------------------
 *
 * The two clauses that are always available and always wrong: making a function PUBLIC when
 * *"a test stands in for a host to exercise"* a handle, and keeping a type's lifecycle public
 * *"even when only the library reaches it today"*. Together they put **304 of 624 public
 * functions in front of no consumer at all** — 217 reached only by tests (143 of them by exactly
 * ONE suite) and 87 by nothing.
 *
 * **A TEST'S REACH IS NEVER A REASON. A test exists to reach things.** The general form is that
 * measuring reach cannot tell an entry point from plumbing, because a consumer reaching THROUGH a
 * bad API looks identical to one reaching FOR a good one. Reach as the criterion has produced a
 * wrong answer that looked rigorous every time it has been tried here: deriving the public HEADER
 * set from it made 37 of 39 headers public; asking "is anything dead?" while counting the
 * library's own `.c` files as a consumer found two dead symbols.
 * **DO make reach the INPUT and a stated reason the DECISION.**
 *
 * ---- THE CLASSES ----------------------------------------------------------------------------
 *
 *   PUBLIC       a CONSUMER reaches it, or a stated reason keeps it. The consumers are roll,
 *                the examples, the pure-C consumer and the suites that drive a whole host — and
 *                nothing else (see "who counts as a consumer"). The stated reasons, each written
 *                on its own row as `KEPT:`: the release half of a pair a CONSUMER holds (a C
 *                consumer has no destructor); a public C++ member IN THE DEFINITION calls it, so a
 *                C consumer must be able to perform the same operation; a named rung (the leak
 *                gauge, the release point, the bring-your-own-vocabulary `_domain_init` set, the
 *                library's own scope and reason callbacks); or a public operation nobody calls
 *                yet, ARGUED PER FUNCTION and never per family.
 *   INTERNAL     everything else: only the library's own `.c` reaches it, or only the studio and
 *                its editors, or only a suite that tests implementation. Those all opt in by name
 *                (`ROLLTUI_INTERNAL_OPT_IN` in CMake). INTERNAL IS A PROMISE BOUNDARY, NOT A
 *                GRAVEYARD — the code and its tests are unchanged; what changes is that the
 *                library no longer promises the shape.
 *   DELETE       reached by nothing, anywhere, and not a public type's lifecycle.
 *
 * ---- WHO COUNTS AS A CONSUMER ---------------------------------------------------------------
 *
 * **1. THE STUDIO IS NOT ONE.** It and its three editors are rolltui's OWN authoring tool for
 * rolltui's OWN files — themes, layouts, bindings. Nobody outside this repo builds one, so it
 * joins the opt-in list and its reach stops justifying anything. **The EXAMPLES are consumers**,
 * and the distinction is the point: a generic painting app and a file browser are the closest
 * things in this tree to what an outsider would write.
 *
 * **2. A UNIT OR META TEST'S REACH IS NOT A REASON EITHER**, and "a public-only suite reaches it"
 * is the form this comes back in: some of those suites are meta-tests ABOUT THE SOURCE and some
 * are one editor's unit tests. What counts is a CONSUMER-SHAPED program — the pure-C consumer,
 * which includes `rolltui/rolltui.h` and nothing else, so a need it has is a GAP, and the suites
 * that drive whole host binaries.
 *
 * **THERE IS NO TOOL-FACING CLASS**, because a tool IS a host. Such a class measures the
 * DIRECTORY `rolltui/tools/` rather than a concept, and the DIRECTORY answer disagrees with the
 * reach answer: of the 26 rows it once held, 8 were reached by `studio.cpp` — a host — while the
 * class said roll never reached them. The genuine other category is the EDITORS: models with no
 * terminal in them, mounted inside a host.
 */

/* ---- rolltui_geom.h — RolltuiRect, the vocabulary every layout speaks; default PUBLIC ---- */
ROLLTUI_API(rolltui_rect_intersect, PUBLIC)  /* KEPT: RolltuiRect::intersect, a public C++
   member of a public struct, calls it. The compile loop that found the other 37 could not
   flag this one: its declaration never moved, so the definition kept compiling. */  /* geometry every host does */

/* ---- rolltui_str.h — RolltuiStr and RolltuiStrList: the text vocabulary every signature speaks; default PUBLIC --- */
ROLLTUI_API(rolltui_ptrvec_clear, INTERNAL)  /* the library's own vector, like its four siblings */
ROLLTUI_API(rolltui_ptrvec_free, INTERNAL)  /* the library's own vector */
ROLLTUI_API(rolltui_ptrvec_insert, INTERNAL)  /* the library's own vector */
ROLLTUI_API(rolltui_ptrvec_push, INTERNAL)  /* the library's own vector */
ROLLTUI_API(rolltui_ptrvec_take, INTERNAL)  /* the library's own vector */
ROLLTUI_API(rolltui_str_append, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* the string type's own API */
ROLLTUI_API(rolltui_str_append_str, INTERNAL)  /* the string type's own API */
ROLLTUI_API(rolltui_str_clear, PUBLIC)
ROLLTUI_API(rolltui_str_eq, PUBLIC)
ROLLTUI_API(rolltui_str_free, PUBLIC)
ROLLTUI_API(rolltui_str_get, INTERNAL)  /* Phase 24: RolltuiStr is TRANSPARENT: a consumer reads `.p`/`.n` directly, so there is no hole to fill. Its old KEPT reason was `public_header_test` section 1 — a META-test, which Phase 20 struck as a reason */
ROLLTUI_API(rolltui_labelf, INTERNAL)  /* bounded diagnostic labels, cut on a whole character; the library's own .c files only */
ROLLTUI_API(rolltui_str_list_add, INTERNAL)  /* the list's own API */
ROLLTUI_API(rolltui_str_list_clear, INTERNAL)  /* the list's own API */
ROLLTUI_API(rolltui_str_list_release, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_str_move, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* the string type's own API */
ROLLTUI_API(rolltui_str_put, PUBLIC)  /* KEPT: matching part — `rolltui_preset_read_file` is consumer-used and takes a `RolltuiPutFn`; without the library's own sink over its own string type every consumer writes the same adapter */
ROLLTUI_API(rolltui_role_from_name, INTERNAL)
ROLLTUI_API(rolltui_role_name, INTERNAL)
ROLLTUI_API(rolltui_role_words, INTERNAL)  /* the theme sheet says what a role is for */

/* ---- rolltui_frame_ops.h — drawing into the cell grid; default PUBLIC -------------------- */
ROLLTUI_API(rolltui_draw_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_draw_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_frame_fill, PUBLIC)
ROLLTUI_API(rolltui_frame_text_width, PUBLIC)
ROLLTUI_API(rolltui_frame_put_fields, PUBLIC)
ROLLTUI_API(rolltui_frame_put_text, PUBLIC)
ROLLTUI_API(rolltui_style_patch, PUBLIC)  /* KEPT: a host merges a highlight into its own style, the library's rule for it */
ROLLTUI_API(rolltui_frame_tint, PUBLIC)  /* KEPT: a host's own widget patches a highlight over what it drew */
ROLLTUI_API(rolltui_style_on_window, INTERNAL)  /* plain text in a panel sits on the panel: the list, the menu, the scrollbar */
ROLLTUI_API(rolltui_frame_shade, INTERNAL)
ROLLTUI_API(rolltui_frame_unmark_rect, INTERNAL)

/* ---- rolltui_render.h — the grid to bytes; default PUBLIC -------------------------------- */
ROLLTUI_API(rolltui_frame_to_text, PUBLIC)
ROLLTUI_API(rolltui_render_diff, INTERNAL)  /* a step of rolltui_swap_present */
ROLLTUI_API(rolltui_render_full, INTERNAL)
ROLLTUI_API(rolltui_render_diff_at, INTERNAL)  /* the same from a screen row: an inline region */
ROLLTUI_API(rolltui_render_full_at, INTERNAL)
ROLLTUI_API(rolltui_swap_present_at, INTERNAL)
ROLLTUI_API(rolltui_swap_pending, INTERNAL)
ROLLTUI_API(rolltui_render_row_extent, INTERNAL)  /* an inline region's rows, measured for a resize */
ROLLTUI_API(rolltui_render_lines, INTERNAL)  /* an inline region's committed rows */
ROLLTUI_API(rolltui_frame_ground, INTERNAL)  /* the fill an inline region draws as the terminal's own background */
ROLLTUI_API(rolltui_frame_set_ground, INTERNAL)  /* the compose gives each window its own ground while it draws */
ROLLTUI_API(rolltui_frame_same_cell, INTERNAL)  /* the diff's compare: one memcmp for nearly every cell */
ROLLTUI_API(rolltui_frame_row_hash, INTERNAL)  /* the diff finds a block of rows that moved */
ROLLTUI_API(rolltui_frame_rows_same, INTERNAL)  /* the diff skips a row that did not change, in one compare */
ROLLTUI_API(rolltui_frame_put_ascii, INTERNAL)  /* put_text writes an ASCII run whole */
ROLLTUI_API(rolltui_frame_fill_narrow, INTERNAL)  /* fill writes a window's ground whole */
ROLLTUI_API(rolltui_frame_cell_is_blank, INTERNAL)  /* the diff compares with a row a scrolled region brought in */

/* ---- rolltui_screen.h — the cell grid; default PUBLIC ------------------------------------ */
ROLLTUI_API(rolltui_frame_cell, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_clear, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_clone, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_cursor, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_equal, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_free, PUBLIC)  /* the pair of `_new` */
ROLLTUI_API(rolltui_frame_glyph, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_height, INTERNAL)  /* Phase 24: the pair of `_width`, and it went with it */
ROLLTUI_API(rolltui_frame_link, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_link_count, INTERNAL)  /* the windows look for links only in a frame that holds one */
ROLLTUI_API(rolltui_frame_link_id, PUBLIC)  /* KEPT: the only way to fill `rolltui_frame_put_text`'s `link`, so a host that draws a
                                               * hyperlink of its own needs it; until 2026-11-15 a host could pass 0 and nothing else */
ROLLTUI_API(rolltui_frame_mark, PUBLIC)  /* KEPT: matching part — `rolltui_effects_apply` is consumer-used, and this is the WIDGET half of that contract — motion is the theme's, a state is the widget's */
ROLLTUI_API(rolltui_frame_mark_at, INTERNAL)  /* a step of mark */
ROLLTUI_API(rolltui_frame_mark_count, PUBLIC)
ROLLTUI_API(rolltui_frame_new, PUBLIC)  /* an inline host builds the rows it commits; every frame it presents still comes from `rolltui_swap_begin` */
ROLLTUI_API(rolltui_frame_put, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_reset, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_set_cursor, PUBLIC)
ROLLTUI_API(rolltui_frame_set_cursor_shape, PUBLIC)  /* KEPT: a host's own field says which caret it wants */
ROLLTUI_API(rolltui_cursor_text_style, PUBLIC)  /* KEPT: a host's own row in the theme's cursor shape */
ROLLTUI_API(rolltui_frame_cursor_ends, PUBLIC)  /* KEPT: …and its ends */
ROLLTUI_API(rolltui_frame_set_style, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_width, INTERNAL)  /* Phase 24: a widget is handed a resolved node's rect and the library clips; nothing needs the frame's own bounds. Old KEPT reason was a meta-test's reach */

/* ---- rolltui_swap.h — the double buffer, the newest entry point; default PUBLIC ---------- */
ROLLTUI_API(rolltui_swap_begin, PUBLIC)
ROLLTUI_API(rolltui_swap_free, PUBLIC)
ROLLTUI_API(rolltui_swap_front, PUBLIC)
ROLLTUI_API(rolltui_swap_invalidate, PUBLIC)
ROLLTUI_API(rolltui_swap_new, PUBLIC)
ROLLTUI_API(rolltui_swap_present, PUBLIC)

/* ---- rolltui_syntax.c — source languages; the checker is PUBLIC, the engine INTERNAL ---------- */
ROLLTUI_API(rolltui_syntax_check, PUBLIC)  /* dirktui languages: how a person finds out a language file of their own did not load */

/* ---- rolltui_run.c — the loop around the swap and the terminal; default PUBLIC ------------- */
ROLLTUI_API(rolltui_run, PUBLIC)  /* dirktui and paint: the loop every terminal app wrote for itself, written once */
ROLLTUI_API(rolltui_run_stop, PUBLIC)  /* the callbacks' half: an app that no longer owns the loop stops it by name */
ROLLTUI_API(rolltui_run_terminal, PUBLIC)  /* dirktui reads the terminal's facts and key protocol before its first frame */
ROLLTUI_API(rolltui_run_suspend_job, PUBLIC)  /* a host's key does what Ctrl-Z does in a shell: dirktui */
ROLLTUI_API(rolltui_run_invalidate, PUBLIC)  /* a keypress that asks for a repaint */
ROLLTUI_API(rolltui_run_suspend, PUBLIC)  /* dirktui hands the terminal to an editor... */
ROLLTUI_API(rolltui_run_resume, PUBLIC)  /* ...and takes it back, repainted whole: the half an app used to forget */
ROLLTUI_API(rolltui_run_program, PUBLIC)  /* dirktui hands the terminal to an editor and to brew, and takes it back */
ROLLTUI_API(rolltui_run_watch_stores, PUBLIC)  /* dirktui takes a theme or key file changed in the studio while it runs */
ROLLTUI_API(rolltui_run_tasks, PUBLIC)  /* dirktui's preview reads files on the run's workers */
ROLLTUI_API(rolltui_run_after, PUBLIC)  /* KEPT: a host acts once after a while (a note that clears, a retry) */
ROLLTUI_API(rolltui_run_every, PUBLIC)  /* KEPT: a host's clock, a dashboard's refresh */
ROLLTUI_API(rolltui_run_cancel_timer, PUBLIC)  /* KEPT: the pair to after and every */
ROLLTUI_API(rolltui_run_debounce, PUBLIC)  /* KEPT: filtering once a burst of keys is over */
ROLLTUI_API(rolltui_run_begin, PUBLIC)   /* a host whose own loop must never block (a web page) steps the same loop */
ROLLTUI_API(rolltui_run_step, PUBLIC)    /* …one wake and one frame, without waiting */
ROLLTUI_API(rolltui_run_finish, PUBLIC)  /* …and leaves the terminal as it was found */
ROLLTUI_API(rolltui_run_begin_headless, PUBLIC)  /* KEPT: a host's end-to-end test runs its real loop with no terminal */
ROLLTUI_API(rolltui_run_resize, PUBLIC)  /* KEPT: (a run with no terminal, given a new size) */
ROLLTUI_API(rolltui_run_screen_text, PUBLIC)  /* KEPT: (what such a test asserts on: the screen as text) */

/* ---- rolltui_diff.h — diff highlighting a host draws; default PUBLIC --------------------- */
ROLLTUI_API(rolltui_diff_default_roles, PUBLIC)
ROLLTUI_API(rolltui_diff_is_language, INTERNAL)
ROLLTUI_API(rolltui_diff_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_diff_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_diff_spans, PUBLIC)

/* ---- rolltui_markdown.h — the markdown parser over md4c; the transcript renders through md_lines, and no host calls it; default INTERNAL --- */
ROLLTUI_API(rolltui_md_code_block_summary, INTERNAL)
ROLLTUI_API(rolltui_md_decode_entity, INTERNAL)
ROLLTUI_API(rolltui_md_doc_block_code, INTERNAL)
ROLLTUI_API(rolltui_md_doc_block_count, INTERNAL)
ROLLTUI_API(rolltui_md_doc_block_kind, INTERNAL)
ROLLTUI_API(rolltui_md_doc_free, INTERNAL)
ROLLTUI_API(rolltui_md_doc_new, INTERNAL)
ROLLTUI_API(rolltui_md_parse, INTERNAL)
ROLLTUI_API(rolltui_md_parser_flags, INTERNAL)
ROLLTUI_API(rolltui_md_render, INTERNAL)
ROLLTUI_API(rolltui_md_roles, INTERNAL)

/* ---- rolltui_md_lines.h — the transcript's line store; default INTERNAL ------------------ */
ROLLTUI_API(rolltui_md_lines_add_clamped, INTERNAL)
ROLLTUI_API(rolltui_md_lines_add_code_block, INTERNAL)
ROLLTUI_API(rolltui_md_lines_all, INTERNAL)
ROLLTUI_API(rolltui_md_lines_aux, INTERNAL)
ROLLTUI_API(rolltui_md_lines_clamped_at, INTERNAL)
ROLLTUI_API(rolltui_md_lines_clamped_count, INTERNAL)
ROLLTUI_API(rolltui_md_lines_clear_code_blocks, INTERNAL)
ROLLTUI_API(rolltui_md_lines_close, INTERNAL)
ROLLTUI_API(rolltui_md_lines_clusters, INTERNAL)
ROLLTUI_API(rolltui_md_lines_code_block_count, INTERNAL)
ROLLTUI_API(rolltui_md_lines_code_blocks, INTERNAL)
ROLLTUI_API(rolltui_md_lines_count, INTERNAL)
ROLLTUI_API(rolltui_md_lines_finish, INTERNAL)
ROLLTUI_API(rolltui_md_lines_free, INTERNAL)
ROLLTUI_API(rolltui_md_lines_intern, INTERNAL)
ROLLTUI_API(rolltui_md_lines_interned, INTERNAL)
ROLLTUI_API(rolltui_md_lines_line, INTERNAL)
ROLLTUI_API(rolltui_md_lines_new, INTERNAL)
ROLLTUI_API(rolltui_md_lines_open, INTERNAL)
ROLLTUI_API(rolltui_md_lines_reset, INTERNAL)
ROLLTUI_API(rolltui_md_lines_scratch, INTERNAL)
ROLLTUI_API(rolltui_md_lines_shift_code_blocks, INTERNAL)
ROLLTUI_API(rolltui_md_lines_span, INTERNAL)
ROLLTUI_API(rolltui_md_lines_span_range, INTERNAL)
ROLLTUI_API(rolltui_md_lines_span_ref, INTERNAL)
ROLLTUI_API(rolltui_md_lines_text, INTERNAL)
ROLLTUI_API(rolltui_md_lines_text_append, INTERNAL)
ROLLTUI_API(rolltui_md_lines_text_pop, INTERNAL)
ROLLTUI_API(rolltui_md_lines_text_set, INTERNAL)
ROLLTUI_API(rolltui_md_lines_text_size, INTERNAL)
ROLLTUI_API(rolltui_md_lines_trim_trailing_spaces, INTERNAL)
ROLLTUI_API(rolltui_md_lines_work, INTERNAL)

/* ---- rolltui_unicode.h — the library's Unicode algorithms; three functions a host reaches are public, the rest are the wrap engine's; default INTERNAL --- */
ROLLTUI_API(rolltui_u_append_utf8, INTERNAL)
ROLLTUI_API(rolltui_u_cluster_width, INTERNAL)
ROLLTUI_API(rolltui_u_codepoint_width, INTERNAL)
ROLLTUI_API(rolltui_u_decode_one, INTERNAL)
ROLLTUI_API(rolltui_u_decode_utf8, INTERNAL)  /* one of the three Unicode functions a host needs (Phase 17 m4) */
ROLLTUI_API(rolltui_u_decode_utf8_chars, INTERNAL)
ROLLTUI_API(rolltui_u_combining_class, INTERNAL)
ROLLTUI_API(rolltui_u_nfd, INTERNAL)
ROLLTUI_API(rolltui_u_nfc, INTERNAL)
ROLLTUI_API(rolltui_u_cluster_lead, INTERNAL)
ROLLTUI_API(rolltui_u_display_width, PUBLIC)
ROLLTUI_API(rolltui_u_fit, PUBLIC)  /* the explorer's wall E2: a widget that truncates needs
                                     the cut offset; every list/tree/table/column view does */
ROLLTUI_API(rolltui_u_general_category, INTERNAL)
ROLLTUI_API(rolltui_u_grapheme_boundaries, INTERNAL)
ROLLTUI_API(rolltui_u_graphemes, INTERNAL)
ROLLTUI_API(rolltui_u_line_break_opportunities, INTERNAL)
ROLLTUI_API(rolltui_u_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_u_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_u_strip_escape_sequences, PUBLIC)
ROLLTUI_API(rolltui_u_word_boundaries, INTERNAL)
ROLLTUI_API(rolltui_u_word_range, INTERNAL)
ROLLTUI_API(rolltui_u_case, PUBLIC)  /* KEPT: a host that matches without case folds both sides; the input's word case is built on it */
ROLLTUI_API(rolltui_u_to_upper, INTERNAL)  /* one code point's mapping; a host maps text with rolltui_u_case */
ROLLTUI_API(rolltui_u_to_lower, INTERNAL)  /* one code point's mapping; a host maps text with rolltui_u_case */
ROLLTUI_API(rolltui_u_to_title, INTERNAL)  /* one code point's mapping; a host maps text with rolltui_u_case */
ROLLTUI_API(rolltui_u_fold, INTERNAL)  /* one code point's folding; a host folds text with rolltui_u_case */

/* ---- rolltui_wrap.h — the wrap engine roll draws with; default PUBLIC -------------------- */
ROLLTUI_API(rolltui_wrap, PUBLIC)
ROLLTUI_API(rolltui_wrap_clone, INTERNAL)  /* the wrap scratch's own step */
ROLLTUI_API(rolltui_wrap_free, PUBLIC)
ROLLTUI_API(rolltui_wrap_line, PUBLIC)
ROLLTUI_API(rolltui_wrap_line_count, PUBLIC)
ROLLTUI_API(rolltui_wrap_new, PUBLIC)
ROLLTUI_API(rolltui_wrap_reset, INTERNAL)  /* the wrap scratch's own step */

/* ---- rolltui_marker.h — the "N more" rule; default PUBLIC -------------------------------- */
ROLLTUI_API(rolltui_scroll_marker_text, PUBLIC)

/* ---- rolltui_effects.h — a host registers effect kinds and holds an effect map; default PUBLIC --- */
ROLLTUI_API(rolltui_effect_clear_registered, INTERNAL)  /* the registry's own shutdown hook (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_effect_is_builtin, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_kind_count, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_kind_name, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_kind_resolves, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_add, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_set_jitter, INTERNAL)
ROLLTUI_API(rolltui_effect_map_add_frame, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_add_role, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_at, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_clear, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_clear_state, INTERNAL)
ROLLTUI_API(rolltui_effect_map_clone, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_count, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_empty, PUBLIC)
ROLLTUI_API(rolltui_effect_map_equal, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_free, PUBLIC)
ROLLTUI_API(rolltui_effect_map_new, INTERNAL)
ROLLTUI_API(rolltui_effect_register, PUBLIC)  /* rung 2 of the kind table is a HOST's: dirktui registers three kinds of its own */
ROLLTUI_API(rolltui_effect_state_register, PUBLIC)  /* KEPT: a host whose widget has states of its own registers them by name; the library's own picker moved its three into the library, and the capability is the point, not its reach */
ROLLTUI_API(rolltui_effect_state_resolve, INTERNAL)  /* a test reads back; no consumer marks by a name it did not register */
ROLLTUI_API(rolltui_effect_state_count, INTERNAL)
ROLLTUI_API(rolltui_effect_map_grow, INTERNAL)  /* a step of the merge */
ROLLTUI_API(rolltui_effect_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_effect_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_effect_state_from_name, INTERNAL)
ROLLTUI_API(rolltui_effect_state_name, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_steps, INTERNAL)  /* a step of apply */
ROLLTUI_API(rolltui_effects_apply, PUBLIC)
ROLLTUI_API(rolltui_effects_tick_ms, PUBLIC)

/* ---- rolltui_theme.h — a host holds a style table; default PUBLIC ------------------------ */
ROLLTUI_API(rolltui_ansi_index_rgb, INTERNAL)  /* a step of downgrade */
ROLLTUI_API(rolltui_color_depth_from_name, PUBLIC)
ROLLTUI_API(rolltui_color_depth_name, INTERNAL)
ROLLTUI_API(rolltui_color_depth_setting_valid, PUBLIC)
ROLLTUI_API(rolltui_color_downgrade, INTERNAL)  /* a theme-loading step */
ROLLTUI_API(rolltui_color_parse, PUBLIC)    /* KEPT: the menu's typed color field hands a host TEXT, so without this its value is unusable (Phase 21, rolltui-paint) */
ROLLTUI_API(rolltui_style_from_sgr, PUBLIC)  /* KEPT: a style a person wrote as SGR, read */
ROLLTUI_API(rolltui_colors_distinct, PUBLIC)  /* KEPT: a colour each for authors, hosts or series, readable on the ground */
ROLLTUI_API(rolltui_style_fade, PUBLIC)  /* a fading status note: dirktui; the picker's edge fade */
ROLLTUI_API(rolltui_color_to_string, PUBLIC) /* KEPT: the other half of that round trip — a host that stores a colour writes it back to its own file */
ROLLTUI_API(rolltui_detect_color_depth, PUBLIC)
ROLLTUI_API(rolltui_mode_for_background, PUBLIC)
ROLLTUI_API(rolltui_parse_osc11_reply, INTERNAL)  /* a theme-loading step */
ROLLTUI_API(rolltui_sgr_delta, INTERNAL)  /* the renderer names only what changed */
ROLLTUI_API(rolltui_sgr, INTERNAL)  /* Phase 24: no consumer writes an escape sequence; `rolltui_swap_present` produces the bytes. Rule 3(a)'s prose is amended rather than a function kept to illustrate it */
ROLLTUI_API(rolltui_theme_builtin_count, INTERNAL)  /* a theme-loading step */
ROLLTUI_API(rolltui_theme_builtin_fill, PUBLIC)
ROLLTUI_API(rolltui_theme_builtin_name, INTERNAL)  /* a theme-loading step */
ROLLTUI_API(rolltui_theme_default_vocab, PUBLIC)
ROLLTUI_API(rolltui_theme_vocab, PUBLIC)  /* the SESSION's vocabulary: the library's states, then the host's */
ROLLTUI_API(rolltui_theme_effects_merge, PUBLIC)  /* an app's own mapping file, added onto a person's theme */
ROLLTUI_API(rolltui_theme_dump, INTERNAL)
ROLLTUI_API(rolltui_theme_load, PUBLIC)
ROLLTUI_API(rolltui_theme_mode_from_name, PUBLIC)
ROLLTUI_API(rolltui_theme_mode_setting_valid, PUBLIC)
ROLLTUI_API(rolltui_theme_report_add_bad_value, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_theme_report_add_badge_mismatch, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_theme_report_add_missing_role, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_theme_report_add_unknown_key, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_theme_report_release, PUBLIC)
ROLLTUI_API(rolltui_theme_report_set_error, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_theme_style, PUBLIC)

/* ---- rolltui_theme_editor.h — the theme editor's MODEL. Its consumers are the `theme` widget
 * kind (this library's own .c) and the studio's C++ shape over it, so nothing here is a promise
 * to a host: an app reaches the editor by NAMING the kind in a layout, never by calling it. --- */
ROLLTUI_API(rolltui_theme_editor_badges_line, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_colours_json, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_fix_at, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_preview_colour, INTERNAL)  /* the theme sheet edits a role directly, with no menu walked */
ROLLTUI_API(rolltui_theme_editor_drop_preview, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_commit, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_attr_count, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_attr_name, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_preview_attr, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_apply_fix, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_generate, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_section_at, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_shown_json, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_fix_count, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_focused_role, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_free, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_handle, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_highlighted_color, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_load, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_menu, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_mode, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_new, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_outcome_release, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_palette_color, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_palette_count, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_palette_id, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_palette_label, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_previewing, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_redo, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_redo_depth, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_replace, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_report, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_set_mode, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_set_presets, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_set_shipped, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_status_line, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_styles, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_undo, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_undo_depth, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_variant_meta, INTERNAL)
ROLLTUI_API(rolltui_theme_editor_variant_name, INTERNAL)

/* ---- rolltui_theme_analysis.h — the studio and the theme editor check themes; roll never does; default TOOL_FACING --- */
ROLLTUI_API(rolltui_apca_contrast, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_apply_fix, INTERNAL)
ROLLTUI_API(rolltui_badge_names, INTERNAL)
ROLLTUI_API(rolltui_check_claims, INTERNAL)
ROLLTUI_API(rolltui_theme_badges_json, INTERNAL)  /* what a theme file's writer declares; every writer of one reaches the module's own headers */
ROLLTUI_API(rolltui_theme_check_declaration, INTERNAL)  /* the loader's own recompute-and-compare step */
ROLLTUI_API(rolltui_delta_e, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_fix_array_release, INTERNAL)
ROLLTUI_API(rolltui_fix_confusable, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_fix_contrast, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_fix_release, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_from_linear, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_has_badge, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_into_gamut, INTERNAL)  /* a step of fix */
ROLLTUI_API(rolltui_linear_channel_to_srgb, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_linear_to_oklab, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_must_differ_count, INTERNAL)
ROLLTUI_API(rolltui_oklab_to_linear, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_oklab_to_oklch, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_oklch_to_oklab, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_propose_fixes, INTERNAL)
ROLLTUI_API(rolltui_relative_luminance, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_simulate_cvd, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_srgb_channel_to_linear, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */
ROLLTUI_API(rolltui_str_array_release, INTERNAL)
ROLLTUI_API(rolltui_theme_analyse, INTERNAL)
ROLLTUI_API(rolltui_theme_report_text, INTERNAL)
ROLLTUI_API(rolltui_to_linear, INTERNAL)
ROLLTUI_API(rolltui_wcag_contrast, INTERNAL)  /* an analysis step; the studio reaches the module's results, not its steps */

/* ---- rolltui_theme_gen.h — the theme generator is the editor's; default TOOL_FACING ------ */
ROLLTUI_API(rolltui_rng_next, INTERNAL)  /* the generator's own */
ROLLTUI_API(rolltui_rng_unit, INTERNAL)  /* the generator's own */
ROLLTUI_API(rolltui_ruleset_from_name, INTERNAL)
ROLLTUI_API(rolltui_ruleset_name, INTERNAL)
ROLLTUI_API(rolltui_theme_generate, INTERNAL)

/* ---- rolltui_bindings.h — a host holds a bindings table and its report; default PUBLIC --- */
ROLLTUI_API(rolltui_bindings_action_at, INTERNAL)
ROLLTUI_API(rolltui_bindings_action_count, INTERNAL)
ROLLTUI_API(rolltui_bindings_action_for, PUBLIC)
ROLLTUI_API(rolltui_bindings_add_action, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_add_chord, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_add_sequence, INTERNAL)  /* a step of loading a table */
ROLLTUI_API(rolltui_bindings_sequence_count, INTERNAL)  /* the help text and the writer read a row's sequences */
ROLLTUI_API(rolltui_bindings_sequence_at, INTERNAL)
ROLLTUI_API(rolltui_bindings_sequence_for, INTERNAL)  /* the window table's keys on the way */
ROLLTUI_API(rolltui_bindings_sequence_next, INTERNAL)  /* the panel of what can follow */
ROLLTUI_API(rolltui_chord_bound_form, INTERNAL)  /* every lookup's form of a pressed key */
ROLLTUI_API(rolltui_list_typing, INTERNAL)  /* the list kind says a digit is the filter's */
ROLLTUI_API(rolltui_list_column_width, INTERNAL)  /* the keys sheet reads a cut row in full on its detail line */
ROLLTUI_API(rolltui_list_column_x, INTERNAL)  /* where a sheet draws a row it edits in place */
ROLLTUI_API(rolltui_list_cursor_y, INTERNAL)
ROLLTUI_API(rolltui_list_has_back, INTERNAL)  /* a focused list takes its back key only while it has something to go back from */
ROLLTUI_API(rolltui_list_footer, INTERNAL)  /* the list kind's footer */
ROLLTUI_API(rolltui_frame_put_text_cut, PUBLIC)  /* a host's own widget cuts a name with an ellipsis, at its end, middle or start */
ROLLTUI_API(rolltui_text_cut, PUBLIC)  /* the same cut as text, for a line a host builds */
ROLLTUI_API(rolltui_format_size, PUBLIC)  /* KEPT: a host writes a size or a rate as the library's views do (a status line's free space) */
ROLLTUI_API(rolltui_format_count, PUBLIC)  /* KEPT: a host writes a count with its thousands grouped (k9s's pod counts, btop's) */
ROLLTUI_API(rolltui_log_to, PUBLIC)  /* KEPT: a host names its own log file, in its state directory */
ROLLTUI_API(rolltui_log_set_level, PUBLIC)  /* KEPT: a host's debug setting lets more through */
ROLLTUI_API(rolltui_log_wants, PUBLIC)  /* the terminal and the run ask it before building a line */
ROLLTUI_API(rolltui_log, PUBLIC)  /* KEPT: a host's own line in the log the library writes to */
ROLLTUI_API(rolltui_logf, PUBLIC)  /* the terminal says what it entered; the run, a store read again and each frame */
ROLLTUI_API(rolltui_text_cut_with, PUBLIC)  /* KEPT: a cut with a marker, a split and an extension kept, for a host's own line */
ROLLTUI_API(rolltui_frame_put_text_cut_with, PUBLIC)  /* KEPT: the same cut, drawn */
ROLLTUI_API(rolltui_path_shorten, PUBLIC)  /* KEPT: a path in a status line or a title, each folder to a letter */
ROLLTUI_API(rolltui_frame_put_spans, PUBLIC)  /* KEPT: a host's own line of styled parts, aligned, cut or wrapped */
ROLLTUI_API(rolltui_hotkey_at, PUBLIC)  /* KEPT: the letter a key names in a host's own label */
ROLLTUI_API(rolltui_bindings_add_row, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_bind, PUBLIC)
ROLLTUI_API(rolltui_bindings_breaks_enter_rule, INTERNAL)  /* a step of bind */
ROLLTUI_API(rolltui_bindings_chord_at, PUBLIC)  /* a host says "F2 settings" from the live table (dirktui's status line) */
ROLLTUI_API(rolltui_bindings_chord_count, PUBLIC)
ROLLTUI_API(rolltui_bindings_chords_text, INTERNAL)
ROLLTUI_API(rolltui_bindings_clear, INTERNAL)
ROLLTUI_API(rolltui_bindings_clone, PUBLIC)
ROLLTUI_API(rolltui_bindings_declare, PUBLIC)
ROLLTUI_API(rolltui_bindings_default, PUBLIC)
ROLLTUI_API(rolltui_bindings_description, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_dump_json, PUBLIC)
ROLLTUI_API(rolltui_bindings_equal, INTERNAL)
ROLLTUI_API(rolltui_bindings_take_changes, INTERNAL)  /* a keys sheet commits only what the person changed */
ROLLTUI_API(rolltui_bindings_free, PUBLIC)
ROLLTUI_API(rolltui_bindings_has, PUBLIC)  /* KEPT: matching part — `rolltui_bindings_declare` is consumer-used, and this is how a host tells a LIVE shortcut from a kept-but-inert row */
ROLLTUI_API(rolltui_bindings_has_row, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_holder, INTERNAL)  /* a step of bind */
ROLLTUI_API(rolltui_bindings_library_scope, PUBLIC)  /* KEPT: stated reason — the bring-your-own-vocabulary rung a host passes to the bindings loader */
ROLLTUI_API(rolltui_bindings_load_json, PUBLIC)
ROLLTUI_API(rolltui_bindings_new, INTERNAL)
ROLLTUI_API(rolltui_bindings_new_seeded, PUBLIC)
ROLLTUI_API(rolltui_bindings_report_add_bad_chord, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_add_bad_value, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_add_conflict, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_add_undeliverable, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_add_unknown_action, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_add_unknown_key, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_clean, INTERNAL)
ROLLTUI_API(rolltui_bindings_report_release, PUBLIC)
ROLLTUI_API(rolltui_bindings_fill_unnamed, INTERNAL)  /* a loaded table older than a library action takes its default keys */
ROLLTUI_API(rolltui_bindings_report_set_error, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_summary, PUBLIC)  /* Phase 26: its INTERNAL reason argued the opposite of what it concluded — a host that loads its own bindings FILE is the case it named. Phase 21 wall 6: the explorer hand-wrote six loops for it. */
ROLLTUI_API(rolltui_bindings_row_at, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_row_count, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_scope_of, INTERNAL)
ROLLTUI_API(rolltui_bindings_set_enter_rule, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_suggest, INTERNAL)  /* a step of declare */
ROLLTUI_API(rolltui_bindings_unbind, INTERNAL)
ROLLTUI_API(rolltui_bindings_undeclare_others, INTERNAL)  /* a step of declare (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_chord_display, PUBLIC)  /* the spelling the help popup uses, for a host that spells one itself */
ROLLTUI_API(rolltui_chord_parse, PUBLIC)
ROLLTUI_API(rolltui_chord_to_string, INTERNAL)
ROLLTUI_API(rolltui_library_action_count, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_library_action_description, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_library_action_name, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_undeliverable_reason_fn, INTERNAL)  /* the vocabulary callback a host with its own bindings domain passes */

/* ---- rolltui_keys_editor.h — the keys editor's MODEL. Its consumers are the `keys` widget kind
 * (this library's own .c) and the studio's C++ shape over it, so nothing here is a promise to a
 * host: an app reaches the editor by NAMING the kind in a layout, never by calling it. --------- */
ROLLTUI_API(rolltui_keys_editor_capturing, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_capturing_action, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_committed, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_current, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_free, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_handle, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_load, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_menu, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_new, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_outcome_release, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_redo, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_redo_depth, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_replace, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_set_presets, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_set_shipped, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_status_line, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_capture, INTERNAL)   /* the keys sheet starts a capture on the row Enter was pressed on */
ROLLTUI_API(rolltui_keys_editor_take_key, INTERNAL)  /* …and hands it the key that came */
ROLLTUI_API(rolltui_keys_editor_unbind, INTERNAL)    /* …and removes a row's key */
ROLLTUI_API(rolltui_keys_editor_undo, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_undo_depth, INTERNAL)

/* ---- rolltui_keys.h — the chord and protocol vocabulary a host shows; default PUBLIC ----- */
ROLLTUI_API(rolltui_key_active_protocol, PUBLIC)
ROLLTUI_API(rolltui_key_decoder_feed, INTERNAL)  /* the terminal's decoder (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_decoder_flush, INTERNAL)  /* the terminal's decoder (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_decoder_free, INTERNAL)  /* the terminal's decoder (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_decoder_in_paste, INTERNAL)  /* the terminal's decoder (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_decoder_new, INTERNAL)  /* the terminal's decoder; a host reads events (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_decoder_pending, INTERNAL)  /* the terminal's decoder */
ROLLTUI_API(rolltui_key_deliverable, PUBLIC)  /* a host shows the first chord that can arrive: dirktui's hint bar */
ROLLTUI_API(rolltui_key_display_name, INTERNAL)  /* a deliverability primitive; the library hands its own reason callback to a domain */
ROLLTUI_API(rolltui_key_encode, INTERNAL)  /* the terminal's own (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_from_display_name, INTERNAL)
ROLLTUI_API(rolltui_key_protocol_name, INTERNAL)  /* a deliverability primitive; the library hands its own reason callback to a domain */
ROLLTUI_API(rolltui_key_set_active_protocol, INTERNAL)  /* a deliverability primitive; the library hands its own reason callback to a domain */
ROLLTUI_API(rolltui_key_undeliverable_reason, INTERNAL)  /* a deliverability primitive; the library hands its own reason callback to a domain */
ROLLTUI_API(rolltui_key_undeliverable_text, INTERNAL)  /* a deliverability primitive; the library hands its own reason callback to a domain */

/* ---- rolltui_layout.h — a host holds a RolltuiLayout and a window stack; default PUBLIC --- */
ROLLTUI_API(rolltui_action_decl_problem, INTERNAL)
ROLLTUI_API(rolltui_action_list_add, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_action_list_clear, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_action_list_copy, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_action_list_equal, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_action_list_release, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_action_list_remove, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_action_list_remove_name, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_anchor_from_name, INTERNAL)
ROLLTUI_API(rolltui_anchor_name, INTERNAL)
ROLLTUI_API(rolltui_border_from_name, INTERNAL)
ROLLTUI_API(rolltui_border_name, INTERNAL)
ROLLTUI_API(rolltui_complete_path, PUBLIC)  /* a completer's ready-made candidates: a path typed into a field */
ROLLTUI_API(rolltui_complete_words, PUBLIC)  /* a completer's ready-made candidates: rolltui-chat's commands and setting values */
ROLLTUI_API(rolltui_completions_add, PUBLIC)  /* a completer gives its candidates: rolltui-chat's prompt */
ROLLTUI_API(rolltui_completions_release, INTERNAL)  /* the field frees its candidates; a host never holds the collector */
ROLLTUI_API(rolltui_completions_replace, PUBLIC)  /* a completer says which bytes its candidates replace: a setting's value after its key */
ROLLTUI_API(rolltui_compose_layer, INTERNAL)  /* a step of compose */
ROLLTUI_API(rolltui_compose_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_compose_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_content_copy, INTERNAL)  /* a Content is a field of a node; its lifetime is the node's */
ROLLTUI_API(rolltui_content_equal, INTERNAL)  /* a Content is a field of a node; its lifetime is the node's */
ROLLTUI_API(rolltui_content_format, PUBLIC)
ROLLTUI_API(rolltui_content_init, INTERNAL)  /* a Content is a field of a node; its lifetime is the node's */
ROLLTUI_API(rolltui_content_parse, PUBLIC)
ROLLTUI_API(rolltui_content_release, INTERNAL)  /* a Content is a field of a node; its lifetime is the node's */
ROLLTUI_API(rolltui_dim_to_string, INTERNAL)
ROLLTUI_API(rolltui_draw_border, PUBLIC)  /* KEPT: a widget boxes a group of its own content */
ROLLTUI_API(rolltui_inner_rect, INTERNAL)  /* a step of layout */
ROLLTUI_API(rolltui_layout_actions_free, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_layout_builtin, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_layout_builtin_json, PUBLIC)
ROLLTUI_API(rolltui_layout_copy, INTERNAL)
ROLLTUI_API(rolltui_layout_default_hooks, PUBLIC)
ROLLTUI_API(rolltui_layout_default_roles, PUBLIC)
ROLLTUI_API(rolltui_layout_equal, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_layout_init, INTERNAL)
ROLLTUI_API(rolltui_layout_popup, PUBLIC)
ROLLTUI_API(rolltui_layout_read_actions_key, INTERNAL)  /* a step of the layout loader (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_layout_release, INTERNAL)
ROLLTUI_API(rolltui_layout_report_clean, PUBLIC)
ROLLTUI_API(rolltui_layout_report_release, PUBLIC)
ROLLTUI_API(rolltui_layout_shipped_default_actions, PUBLIC)
ROLLTUI_API(rolltui_layout_to_json_text, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_layout_to_json_value, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_load_layout, INTERNAL)  /* the loader over a parsed tree; the text form is the entry point */
ROLLTUI_API(rolltui_load_layout_text, PUBLIC)
ROLLTUI_API(rolltui_app_file, PUBLIC)
ROLLTUI_API(rolltui_state_dir, PUBLIC)  /* dirktui's update check and the chat's history are kept there */
ROLLTUI_API(rolltui_state_file_open, PUBLIC)  /* KEPT: a host's marks, tags or tabs, kept between runs and shared by its instances */
ROLLTUI_API(rolltui_state_file_get, PUBLIC)  /* KEPT: (a state file's value) */
ROLLTUI_API(rolltui_state_file_set, PUBLIC)  /* KEPT: (a state file's value set or removed) */
ROLLTUI_API(rolltui_state_file_count, PUBLIC)  /* KEPT: (a state file's names, counted) */
ROLLTUI_API(rolltui_state_file_name_at, PUBLIC)  /* KEPT: (a state file's names, walked) */
ROLLTUI_API(rolltui_state_file_save, PUBLIC)  /* KEPT: (merged with another instance's, written atomically) */
ROLLTUI_API(rolltui_state_file_free, PUBLIC)  /* KEPT: (a state file let go of) */
ROLLTUI_API(rolltui_dir_read, PUBLIC)  /* the aligned probe needs it: a host writing a rich browser needs the same four things the picker does, and must not re-implement them */
ROLLTUI_API(rolltui_dir_list_release, PUBLIC)  /* release half of the pair */  /* an app that keeps its screen in files cannot start until it knows where its own files are; every host built that way needs this and three of them were building the path themselves */
ROLLTUI_API(rolltui_watch_new, PUBLIC)  /* KEPT: a host watching files of its own (a project's sources, a log it tails) */
ROLLTUI_API(rolltui_watch_free, PUBLIC)  /* KEPT: release half of the pair */
ROLLTUI_API(rolltui_watch_add, PUBLIC)  /* KEPT: a path a host watches */
ROLLTUI_API(rolltui_watch_remove, PUBLIC)  /* KEPT: a path a host lets go of */
ROLLTUI_API(rolltui_watch_changed, PUBLIC)  /* KEPT: what changed since a host last asked */
ROLLTUI_API(rolltui_watch_how, PUBLIC)  /* KEPT: how a host's files are watched, for a status line or a bug report */
ROLLTUI_API(rolltui_loaded_layout_init, INTERNAL)
ROLLTUI_API(rolltui_loaded_layout_release, INTERNAL)
ROLLTUI_API(rolltui_loaded_layout_to_layout, INTERNAL)
ROLLTUI_API(rolltui_parse_dim, INTERNAL)
ROLLTUI_API(rolltui_parse_size_text, INTERNAL)
ROLLTUI_API(rolltui_parse_split_size, INTERNAL)  /* a step of the layout loader (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_placement_resolve, PUBLIC)
ROLLTUI_API(rolltui_resolve_dim, INTERNAL)  /* a step of layout */
ROLLTUI_API(rolltui_resolve_tree, INTERNAL)
ROLLTUI_API(rolltui_split_size_to_string, INTERNAL)
ROLLTUI_API(rolltui_stack_default_actions, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_clear, INTERNAL)  /* the registry's own shutdown hook (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_widget_kind_count, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_library_count, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_name, INTERNAL)
ROLLTUI_API(rolltui_widget_kind_register, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_resolve, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_rule, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_widget_kind_source_is, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_widget_kind_source_shape, PUBLIC)
ROLLTUI_API(rolltui_window_stack_base, INTERNAL)
ROLLTUI_API(rolltui_window_stack_layer_mut, INTERNAL)  /* the window table sizes a popup to what it holds */
ROLLTUI_API(rolltui_window_stack_remove_layer, INTERNAL)  /* a dialog answered goes, wherever it is in the stack */
ROLLTUI_API(rolltui_window_stack_captured, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_window_stack_compose, PUBLIC)
ROLLTUI_API(rolltui_window_stack_cycle_focus, INTERNAL)  /* what the stack does on its own action */
ROLLTUI_API(rolltui_window_stack_depth, PUBLIC)
ROLLTUI_API(rolltui_window_stack_find, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_window_stack_focus, PUBLIC)
ROLLTUI_API(rolltui_window_stack_focus_layer, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_window_stack_focused, PUBLIC)
ROLLTUI_API(rolltui_window_stack_pointer, PUBLIC)  /* where the pointer rests, for what a host draws outside the windows: dirktui's hints */
ROLLTUI_API(rolltui_window_stack_free, PUBLIC)
ROLLTUI_API(rolltui_window_stack_has_popup, PUBLIC)
ROLLTUI_API(rolltui_window_stack_action_popup, PUBLIC)
ROLLTUI_API(rolltui_windows_set_picker_dir, PUBLIC)  /* a picker with nowhere to start looks at the working directory, which is almost never what a host means */
ROLLTUI_API(rolltui_windows_picker_taken, PUBLIC)  /* the answer half: a chosen path is what the whole kind is for */  /* three hosts wrote the same six lines; the tell says fix the API rather than ship the wrapper a fourth time */
ROLLTUI_API(rolltui_windows_picker_event, PUBLIC)  /* the fuller answer: a cancel and a copy are things the picker's keys say and only a host can do */
ROLLTUI_API(rolltui_picker_event_release, PUBLIC)  /* KEPT: the release half of the event a consumer holds */
ROLLTUI_API(rolltui_windows_set_picker_options, PUBLIC)  /* dotfiles, sort, motion, dividers, take_folders: the host's settings for the picker it shows */
ROLLTUI_API(rolltui_windows_set_picker_previewer, PUBLIC)  /* KEPT: a host's own previewer in the column browser (a document's pages, a command's output), asked before the built-in one */
ROLLTUI_API(rolltui_windows_set_picker_preview_command, PUBLIC)  /* KEPT: a command as the column browser's previewer, started where the cursor rests and stopped when it moves */
ROLLTUI_API(rolltui_windows_picker_marked, PUBLIC)  /* KEPT: the paths marked in the column browser, for a host to copy, move or open together */
ROLLTUI_API(rolltui_windows_set_picker_decor, PUBLIC)  /* KEPT: a host's own marks on the column browser's entries (a git status, a copied or cut tag) */
ROLLTUI_API(rolltui_windows_set_picker_file_colors, PUBLIC)  /* dirktui's Look setting colours its names by LS_COLORS */
ROLLTUI_API(rolltui_picker_set_file_colors, INTERNAL)  /* the window table hands the browser its file colours */
ROLLTUI_API(rolltui_file_colors_new, PUBLIC)  /* dirktui makes one when its Look setting asks */
ROLLTUI_API(rolltui_file_colors_free, PUBLIC)  /* …and frees it */
ROLLTUI_API(rolltui_file_colors_add_env, PUBLIC)  /* …filled from the environment, as `ls` reads it */
ROLLTUI_API(rolltui_file_colors_add_gnu, PUBLIC)  /* KEPT: a host with LS_COLORS of its own, from its settings rather than the environment */
ROLLTUI_API(rolltui_file_colors_add_bsd, PUBLIC)  /* KEPT: …and LSCOLORS */
ROLLTUI_API(rolltui_file_colors_add_rule, PUBLIC)  /* KEPT: a host's own glob and regular-expression rules, ahead of LS_COLORS */
ROLLTUI_API(rolltui_file_colors_match, PUBLIC)  /* KEPT: a host's own list of files coloured as the browser colours them */
ROLLTUI_API(rolltui_picker_options_init, PUBLIC)  /* KEPT: the defaults, so a host sets only what it means to */
ROLLTUI_API(rolltui_windows_picker_selected, PUBLIC)  /* what is under the cursor: a status line, a copy */
ROLLTUI_API(rolltui_windows_picker_dir, PUBLIC)  /* where the cursor is: what a save dialog joins a name onto */
ROLLTUI_API(rolltui_windows_picker_focus_column, PUBLIC)  /* a breadcrumb's segment: dirktui */
ROLLTUI_API(rolltui_windows_picker_status, PUBLIC)  /* the status line's facts, and whether the columns are mid-slide for the frame timer */
ROLLTUI_API(rolltui_picker_status_release, PUBLIC)  /* KEPT: the release half of the status a consumer holds */
ROLLTUI_API(rolltui_picker_status, INTERNAL)
ROLLTUI_API(rolltui_picker_default_actions, INTERNAL)  /* the picker scope's twelve names; the adapter drives the widget */
ROLLTUI_API(rolltui_picker_new, INTERNAL)
ROLLTUI_API(rolltui_picker_free, INTERNAL)
ROLLTUI_API(rolltui_picker_set_options, INTERNAL)
ROLLTUI_API(rolltui_picker_options, INTERNAL)
ROLLTUI_API(rolltui_picker_go_to, INTERNAL)
ROLLTUI_API(rolltui_picker_focus_column, INTERNAL)
ROLLTUI_API(rolltui_picker_reload, INTERNAL)
ROLLTUI_API(rolltui_picker_set_now, INTERNAL)
ROLLTUI_API(rolltui_picker_set_wheel_lines, INTERNAL)  /* the kind hands the column browser the person's wheel step */
ROLLTUI_API(rolltui_picker_set_tasks, INTERNAL)  /* the picker kind lends it the table's workers */
ROLLTUI_API(rolltui_picker_set_previewer, INTERNAL)  /* the picker kind passes on the host's previewer */
ROLLTUI_API(rolltui_picker_set_preview_command, INTERNAL)  /* the picker kind passes on the host's command */
ROLLTUI_API(rolltui_picker_set_decor, INTERNAL)  /* the picker kind passes on the host's entry marks */
ROLLTUI_API(rolltui_picker_marked, INTERNAL)  /* the picker kind answers the marked paths */
ROLLTUI_API(rolltui_picker_layout, INTERNAL)
ROLLTUI_API(rolltui_picker_set_hover, INTERNAL)
ROLLTUI_API(rolltui_picker_draw, INTERNAL)
ROLLTUI_API(rolltui_picker_handle, INTERNAL)
ROLLTUI_API(rolltui_picker_event, INTERNAL)
ROLLTUI_API(rolltui_picker_selected, INTERNAL)
ROLLTUI_API(rolltui_picker_dir, INTERNAL)
ROLLTUI_API(rolltui_picker_hidden_count, INTERNAL)
ROLLTUI_API(rolltui_picker_scroll_extent, INTERNAL)
ROLLTUI_API(rolltui_picker_scroll_to, INTERNAL)
ROLLTUI_API(rolltui_picker_scrolling, INTERNAL)
ROLLTUI_API(rolltui_picker_faded_cells, INTERNAL)
ROLLTUI_API(rolltui_window_stack_layer, PUBLIC)
ROLLTUI_API(rolltui_window_stack_new, PUBLIC)
ROLLTUI_API(rolltui_window_stack_pop, PUBLIC)
ROLLTUI_API(rolltui_window_stack_set_visible, PUBLIC)  /* a host shows or hides a panel at run time */
ROLLTUI_API(rolltui_window_stack_set_size, PUBLIC)  /* a host resizes a panel at run time */
ROLLTUI_API(rolltui_window_stack_maximize, PUBLIC)  /* a host's own command fills the screen with one window */
ROLLTUI_API(rolltui_window_stack_set_half, PUBLIC)  /* KEPT: a host's own command gives a window half the screen */
ROLLTUI_API(rolltui_window_stack_half, PUBLIC)  /* KEPT: which window has half the screen */
ROLLTUI_API(rolltui_window_stack_anchor, PUBLIC)  /* KEPT: a popup opened under the row it is for */
ROLLTUI_API(rolltui_window_stack_maximized, PUBLIC)  /* which one, for a status line */
ROLLTUI_API(rolltui_window_stack_set_tab, PUBLIC)  /* a host shows a tab: a search result in its tab */
ROLLTUI_API(rolltui_window_stack_tab, PUBLIC)  /* which tab is shown, after the route says the layout changed */
ROLLTUI_API(rolltui_node_tab_at, INTERNAL)  /* a tabbed window's tabs, read where the loader, the stack and the editor need them */
ROLLTUI_API(rolltui_node_tab_add, INTERNAL)
ROLLTUI_API(rolltui_window_stack_push, INTERNAL)
ROLLTUI_API(rolltui_window_stack_push_popup, PUBLIC)
ROLLTUI_API(rolltui_window_stack_resolve, PUBLIC)
ROLLTUI_API(rolltui_window_stack_route, PUBLIC)
ROLLTUI_API(rolltui_window_stack_set_base, PUBLIC)
ROLLTUI_API(rolltui_window_stack_set_level_fn, PUBLIC)  /* a close closes one level: every host with a menu */

/* ---- rolltui_layout_tree.h — the tree inside RolltuiLayout: a public type, so its lifecycle is public; default PUBLIC --- */
ROLLTUI_API(rolltui_layer_copy, INTERNAL)
ROLLTUI_API(rolltui_layer_equal, INTERNAL)
ROLLTUI_API(rolltui_layer_init, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layer_list_add, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_layer_list_clear, INTERNAL)
ROLLTUI_API(rolltui_layer_list_copy, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layer_list_equal, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layer_list_release, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layer_list_remove, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_layer_list_remove_id, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_layer_move, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* the move, written down for C; the C++ move members call it */
ROLLTUI_API(rolltui_layer_release, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layout_node_copy, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_layout_node_equal, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layout_node_free, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layout_node_init, INTERNAL)  /* KEPT: rule 1's pair: a consumer builds node trees */
ROLLTUI_API(rolltui_layout_node_new, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layout_node_release, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_node_list_add, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_node_list_clear, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_node_list_copy, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_node_list_insert, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_node_list_push, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_node_list_release, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_node_list_remove, INTERNAL)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */

/* ---- rolltui_widgets.h — the window registry and the plugin-facing half a host kind reaches its sources through; default PUBLIC --- */
ROLLTUI_API(rolltui_content_rect, PUBLIC)
ROLLTUI_API(rolltui_note_clear, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_rows_add, PUBLIC)
ROLLTUI_API(rolltui_rows_release, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_rows_reset, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_scroll_first_for_cell, INTERNAL)  /* the drag half of the thumb pair, likewise */
ROLLTUI_API(rolltui_scroll_thumb, INTERNAL)  /* scrollbar arithmetic; the widget that draws thumbs of its own is the library's picker */
ROLLTUI_API(rolltui_context_add_help_scope, PUBLIC)
ROLLTUI_API(rolltui_context_add_menu, PUBLIC)
ROLLTUI_API(rolltui_windows_at, INTERNAL)  /* a hit-test the stack already routes */
ROLLTUI_API(rolltui_windows_autosize, PUBLIC)
ROLLTUI_API(rolltui_windows_bind_document, PUBLIC)
ROLLTUI_API(rolltui_windows_bind_note, PUBLIC)
ROLLTUI_API(rolltui_windows_bind_rows, PUBLIC)
ROLLTUI_API(rolltui_windows_bind_gauge, PUBLIC)  /* KEPT: a host with work that takes a while shows how far it has got in a `gauge:` window */
ROLLTUI_API(rolltui_windows_bind_count, PUBLIC)  /* rolltui-tour's dashboard counts its processes in a `count:` window */
ROLLTUI_API(rolltui_windows_bind_output, PUBLIC)  /* KEPT: a host shows a program's output in an `output:` window (a build log, a previewer's pane) */
ROLLTUI_API(rolltui_windows_set_output_wrap, PUBLIC)  /* KEPT: a log that scrolls sideways rather than wrapping */
ROLLTUI_API(rolltui_windows_output, INTERNAL)  /* the output kind reads what is bound */
ROLLTUI_API(rolltui_windows_set_output_options, PUBLIC)  /* KEPT: a log window's timestamps, footer and filter */
ROLLTUI_API(rolltui_windows_run_output, PUBLIC)  /* KEPT: a program run into an output window, pumped, sized and stopped with it */
ROLLTUI_API(rolltui_windows_output_running, PUBLIC)  /* KEPT: whether a program run into a window still runs, and how it ended */
ROLLTUI_API(rolltui_windows_output_mark, PUBLIC)  /* KEPT: a rule across a program's output at a boundary (a build started) */
ROLLTUI_API(rolltui_windows_output_pump, INTERNAL)  /* the output kind reads and sizes its programs each frame */
ROLLTUI_API(rolltui_windows_output_options, INTERNAL)  /* the output kind reads its options and filter */
ROLLTUI_API(rolltui_windows_output_source, INTERNAL)  /* the output kind labels a merged line */
ROLLTUI_API(rolltui_windows_output_label_width, INTERNAL)  /* the output kind sizes its gutter */
ROLLTUI_API(rolltui_windows_output_ran, INTERNAL)  /* the output kind's footer says how a program ended */
ROLLTUI_API(rolltui_windows_bind_image, PUBLIC)  /* KEPT: a host shows a picture it made in an `image:` window */
ROLLTUI_API(rolltui_windows_image, INTERNAL)  /* the image kind reads what is bound */
ROLLTUI_API(rolltui_windows_dialog, INTERNAL)  /* the dialog kind reads the dialog asked */
ROLLTUI_API(rolltui_windows_put_dialog, INTERNAL)  /* asking and answering keep it */
ROLLTUI_API(rolltui_dialog_widget_factory, INTERNAL)  /* the library's kinds register it */
ROLLTUI_API(rolltui_windows_set_tasks, PUBLIC)  /* dirktui lends the run's workers to its preview */
ROLLTUI_API(rolltui_windows_hold, PUBLIC)  /* KEPT: input held behind a spinner while work the person waits on runs, Escape cancelling it */
ROLLTUI_API(rolltui_windows_release, PUBLIC)  /* KEPT: a hold ended by the host when its work has no job to watch */
ROLLTUI_API(rolltui_windows_held, PUBLIC)  /* KEPT: whether input is held, for a host routing keys of its own */
ROLLTUI_API(rolltui_windows_tasks, INTERNAL)  /* the built-in kinds read files on them */
ROLLTUI_API(rolltui_tasks_workers, INTERNAL)  /* the column browser waits for a folder only when there is a worker reading it */
ROLLTUI_API(rolltui_windows_bind_changed, PUBLIC)  /* KEPT: a host filters as a person types */
ROLLTUI_API(rolltui_windows_call_changed, INTERNAL)  /* the input kind calls it when the text changes */
ROLLTUI_API(rolltui_windows_has_gauge, INTERNAL)  /* the gauge kind's problem() asks it */
ROLLTUI_API(rolltui_windows_call_gauge, INTERNAL)  /* the gauge kind's draw asks it */
ROLLTUI_API(rolltui_windows_has_count, INTERNAL)  /* the count kind's problem() asks it */
ROLLTUI_API(rolltui_windows_call_count, INTERNAL)  /* the count kind's draw asks it */
ROLLTUI_API(rolltui_window_stack_through, INTERNAL)  /* the window table asks which window a press through a popup reached */
ROLLTUI_API(rolltui_list_peek, INTERNAL)  /* the window table hands a list a press through a popup */
ROLLTUI_API(rolltui_windows_post, PUBLIC)  /* a failure said where it is seen: dirktui, paint, the studio, chat */
ROLLTUI_API(rolltui_windows_message_count, PUBLIC)  /* KEPT: a host that shows the history its own way reads it */
ROLLTUI_API(rolltui_windows_message_at, PUBLIC)  /* KEPT: a host that shows the history its own way reads it */
ROLLTUI_API(rolltui_windows_draw_toasts, PUBLIC)  /* every host here draws them last */
ROLLTUI_API(rolltui_windows_toast_event, PUBLIC)  /* every host here asks it first */
ROLLTUI_API(rolltui_windows_dismiss_toasts, PUBLIC)  /* KEPT: a host clears them on a key of its own ("dismiss all") */
ROLLTUI_API(rolltui_windows_set_timing, PUBLIC)  /* KEPT: a host shows its windows' draw times from a setting (ROLLTUI_TIMING does it for any) */
ROLLTUI_API(rolltui_windows_set_link_handler, PUBLIC)  /* KEPT: a host takes a press on a link of its own scheme (an editor's, a program's internal one) */
ROLLTUI_API(rolltui_dialog_new, PUBLIC)  /* KEPT: a host asks a question: a confirmation, an error page */
ROLLTUI_API(rolltui_dialog_add_item, PUBLIC)  /* KEPT: what a question is about, listed under it */
ROLLTUI_API(rolltui_dialog_add_button, PUBLIC)  /* KEPT: the answers a host offers */
ROLLTUI_API(rolltui_dialog_set_phrase, PUBLIC)  /* KEPT: a confirmation typed before a destructive answer */
ROLLTUI_API(rolltui_dialog_free, PUBLIC)  /* KEPT: a dialog built and never asked */
ROLLTUI_API(rolltui_windows_ask, PUBLIC)  /* KEPT: a host opens the question it built */
ROLLTUI_API(rolltui_windows_answer, PUBLIC)  /* KEPT: a host collects the answer after each event */
ROLLTUI_API(rolltui_windows_messages, INTERNAL)  /* the messages kind reads the table's log */
ROLLTUI_API(rolltui_windows_bind_sample_document, PUBLIC)
ROLLTUI_API(rolltui_windows_bind_submit, PUBLIC)
ROLLTUI_API(rolltui_windows_bindings, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_syntax, INTERNAL)  /* the table's languages, which an input naming a `language` borrows */
ROLLTUI_API(rolltui_windows_call_note, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_call_rows, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_call_submit, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_context_clear_help_scopes, PUBLIC)
ROLLTUI_API(rolltui_windows_content_at, PUBLIC)
ROLLTUI_API(rolltui_windows_default_roles, PUBLIC)
ROLLTUI_API(rolltui_scrollbar_glyphs_default, INTERNAL)  /* the shipped capsule; a host never asks for it, because a theme's answer always arrives complete */
ROLLTUI_API(rolltui_context_set_scrollbar_glyphs, PUBLIC)  /* the look of the thumb is a theme's, and this is where a theme's answer lands */
ROLLTUI_API(rolltui_theme_scrollbar_glyphs, PUBLIC)  /* the read half: every host resolving a theme reads its glyphs beside its styles */
ROLLTUI_API(rolltui_theme_gradient, PUBLIC)  /* KEPT: a theme's gradient by name, for a host's meters and numbers */
ROLLTUI_API(rolltui_windows_dir, INTERNAL)
ROLLTUI_API(rolltui_windows_document, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_draw, PUBLIC)
ROLLTUI_API(rolltui_windows_env, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_free, PUBLIC)
ROLLTUI_API(rolltui_windows_handle, PUBLIC)
ROLLTUI_API(rolltui_windows_pending_keys, PUBLIC)  /* a status line shows the keys on the way, as vim does */
ROLLTUI_API(rolltui_windows_app_key, PUBLIC)  /* a host's own scope gets chords and sequences: `ctrl+x ctrl+s` in dirktui, paint, chat, the tour */
ROLLTUI_API(rolltui_windows_focused_takes, PUBLIC)  /* a host matching keys of its own asks first whether the focused window takes one */
ROLLTUI_API(rolltui_windows_any_key, PUBLIC)  /* the character an `any` slot or an asked key took: a mark's letter */
ROLLTUI_API(rolltui_windows_ask_key, PUBLIC)  /* a host asks for one key (y or n, a mark) and gets it through app_key */
ROLLTUI_API(rolltui_windows_count_arg, INTERNAL)  /* a count handed to the next key as a row, read by the list kind */
ROLLTUI_API(rolltui_run_inject, PUBLIC)  /* an event the program makes, handed to it at the next wake */
ROLLTUI_API(rolltui_run_flush_input, PUBLIC)  /* a typeahead flush */
ROLLTUI_API(rolltui_terminal_flush_input, PUBLIC)
ROLLTUI_API(rolltui_terminal_set_mouse, PUBLIC)  /* mouse reporting on or off while the program runs */
ROLLTUI_API(rolltui_terminal_mouse, PUBLIC)
ROLLTUI_API(rolltui_window_stack_set_numbered, PUBLIC)  /* each window's number before its title, for Alt and that digit */
ROLLTUI_API(rolltui_window_stack_set_busy, PUBLIC)  /* a spinner in a window's title while it works */
ROLLTUI_API(rolltui_window_stack_set_border_role, PUBLIC)  /* KEPT: a window's border in a state's colour */
ROLLTUI_API(rolltui_window_stack_set_subtitle, PUBLIC)  /* KEPT: a live count at the top right of a window */
ROLLTUI_API(rolltui_window_stack_set_title_parts, PUBLIC)  /* KEPT: a title in parts, labels a press runs */
ROLLTUI_API(rolltui_window_stack_pressed_label, PUBLIC)  /* KEPT: the label a press ran */
ROLLTUI_API(rolltui_bindings_sequence_ends_any, INTERNAL)
ROLLTUI_API(rolltui_key_decoder_reset, INTERNAL)
ROLLTUI_API(rolltui_list_set_count, INTERNAL)
ROLLTUI_API(rolltui_list_can, INTERNAL)
ROLLTUI_API(rolltui_chart_set_default_symbols, INTERNAL)
ROLLTUI_API(rolltui_list_set_cursor_shape, INTERNAL)
ROLLTUI_API(rolltui_frame_cursor_shape, INTERNAL)
ROLLTUI_API(rolltui_os_tty_is_console, INTERNAL)
ROLLTUI_API(rolltui_termfacts_underline_shapes, INTERNAL)
ROLLTUI_API(rolltui_list_cursor_column, INTERNAL)
ROLLTUI_API(rolltui_list_freeze, INTERNAL)
ROLLTUI_API(rolltui_list_mark_matching, INTERNAL)
ROLLTUI_API(rolltui_list_reveal, INTERNAL)
ROLLTUI_API(rolltui_list_set_loading, INTERNAL)
ROLLTUI_API(rolltui_list_set_open, INTERNAL)
ROLLTUI_API(rolltui_list_sort, INTERNAL)
ROLLTUI_API(rolltui_list_sort_state, INTERNAL)
ROLLTUI_API(rolltui_list_tick, INTERNAL)
ROLLTUI_API(rolltui_windows_want_frame, INTERNAL)  /* a window asks for its next frame; draw_toasts hands the soonest back */
ROLLTUI_API(rolltui_os_tty_flush_input, INTERNAL)
ROLLTUI_API(rolltui_windows_has_rows, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_has_submit, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_help_lead, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_help_note, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_remember_keys_store, INTERNAL)  /* every keys sheet of a screen, help included, edits into the one store */
ROLLTUI_API(rolltui_windows_keys_store, INTERNAL)
ROLLTUI_API(rolltui_windows_help_scope_at, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_help_scope_count, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_host_menu, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_host_menu_count, INTERNAL)
ROLLTUI_API(rolltui_windows_host_menu_name_at, INTERNAL)
ROLLTUI_API(rolltui_windows_input, PUBLIC)
ROLLTUI_API(rolltui_windows_input_at, INTERNAL)  /* a hit-test the stack already routes */
ROLLTUI_API(rolltui_windows_layout, PUBLIC)
ROLLTUI_API(rolltui_windows_menu, PUBLIC)
ROLLTUI_API(rolltui_windows_menu_at, PUBLIC)
ROLLTUI_API(rolltui_windows_back, PUBLIC)  /* the level function a host hands the stack */
ROLLTUI_API(rolltui_windows_window_rect, PUBLIC)  /* where a window landed: dirktui draws its breadcrumb over one */
ROLLTUI_API(rolltui_windows_menu_origin, PUBLIC)  /* KEPT: matching part — `rolltui_windows_add_menu` is consumer-used, and without the origin a host cannot tell whether its own file or a user's shadow won the three-rung resolution */
ROLLTUI_API(rolltui_windows_new, PUBLIC)
ROLLTUI_API(rolltui_windows_on_submit, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_context_register_kind, PUBLIC)
ROLLTUI_API(rolltui_windows_report_at, PUBLIC)
ROLLTUI_API(rolltui_windows_report_count, PUBLIC)
ROLLTUI_API(rolltui_windows_report_summary, PUBLIC)
ROLLTUI_API(rolltui_context_set_bindings, PUBLIC)
ROLLTUI_API(rolltui_context_set_dir, PUBLIC)
ROLLTUI_API(rolltui_context_set_env, PUBLIC)
ROLLTUI_API(rolltui_context_set_error_factory, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_context_set_help, PUBLIC)
ROLLTUI_API(rolltui_windows_set_highlight, PUBLIC)
ROLLTUI_API(rolltui_windows_set_input_min_outer, PUBLIC)
ROLLTUI_API(rolltui_context_set_panel_factory, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_styles, PUBLIC)
/* ---- rolltui_tasks.c — work off the UI thread --- */
ROLLTUI_API(rolltui_tasks_new, PUBLIC)  /* KEPT: a host with its own loop runs work off its thread (rolltui_run makes its own) */
ROLLTUI_API(rolltui_tasks_free, PUBLIC)  /* KEPT: the pair to new */
ROLLTUI_API(rolltui_tasks_start, PUBLIC)  /* KEPT: a host's own job: a search, a load, a copy */
ROLLTUI_API(rolltui_tasks_cancel, PUBLIC)  /* KEPT: the selection moved on, Ctrl-C */
ROLLTUI_API(rolltui_job_cancelled, PUBLIC)  /* KEPT: a job's work asks it between steps */
ROLLTUI_API(rolltui_tasks_post, PUBLIC)  /* KEPT: a host's own thread hands back a result */
ROLLTUI_API(rolltui_tasks_call, PUBLIC)  /* KEPT: a job reads what only the UI thread may touch, waiting for the answer */
ROLLTUI_API(rolltui_job_ask, PUBLIC)  /* KEPT: a job asks the person (a password mid-command) and carries on with the answer */
ROLLTUI_API(rolltui_tasks_answer, PUBLIC)  /* KEPT: the person's answer handed to the job that asked */
ROLLTUI_API(rolltui_tasks_running, PUBLIC)  /* KEPT: whether a job has ended, for a host showing its own spinner */
ROLLTUI_API(rolltui_tasks_drain, PUBLIC)  /* KEPT: a host with its own loop drains each wake */
ROLLTUI_API(rolltui_tasks_busy, PUBLIC)  /* KEPT: busy while it is not 0 */
ROLLTUI_API(rolltui_tasks_wait_idle, PUBLIC)  /* KEPT: what a test waits on */

/* ---- rolltui_image.c — pictures, fitted and drawn the terminal's best way --- */
ROLLTUI_API(rolltui_image_load, PUBLIC)  /* KEPT: a host shows a picture from a path (the `image:` kind reads files itself) */
ROLLTUI_API(rolltui_image_decode, PUBLIC)  /* paint opens a PNG */
ROLLTUI_API(rolltui_image_from_rgba, PUBLIC)  /* KEPT: a host shows pixels it made (a chart rendered, a thumbnail scaled) */
ROLLTUI_API(rolltui_image_free, PUBLIC)
ROLLTUI_API(rolltui_image_set_filter, PUBLIC)  /* KEPT: pixel art or a screenshot of text kept hard-edged when it is scaled */
ROLLTUI_API(rolltui_image_rgba, PUBLIC)  /* paint reads the pixels it turns into glyphs */
ROLLTUI_API(rolltui_frame_image, PUBLIC)  /* KEPT: a host's own widget draws a picture (the preview and the `image:` kind draw it for a window) */
ROLLTUI_API(rolltui_image_cells, PUBLIC)  /* KEPT: a host sizes or centres a picture before drawing it */
ROLLTUI_API(rolltui_frame_outside_picture, PUBLIC)  /* KEPT: a host whose pictures an outside program draws (icat, chafa, an overlay) */
ROLLTUI_API(rolltui_image_id, INTERNAL)  /* kitty's id, for the cells and the terminal */
ROLLTUI_API(rolltui_image_fit, INTERNAL)
ROLLTUI_API(rolltui_image_scale, INTERNAL)
ROLLTUI_API(rolltui_image_kitty_upload, INTERNAL)  /* the terminal sends these at present */
ROLLTUI_API(rolltui_image_kitty_place, INTERNAL)
ROLLTUI_API(rolltui_image_kitty_delete, INTERNAL)
ROLLTUI_API(rolltui_image_sixel, INTERNAL)
ROLLTUI_API(rolltui_image_iterm2, INTERNAL)
ROLLTUI_API(rolltui_image_diacritic_count, INTERNAL)  /* a test checks the table against the Unicode tables */
ROLLTUI_API(rolltui_image_diacritic, INTERNAL)
ROLLTUI_API(rolltui_frame_add_image, INTERNAL)  /* a picture drawn, recorded for the terminal */
ROLLTUI_API(rolltui_frame_image_count, INTERNAL)
ROLLTUI_API(rolltui_frame_image_at, INTERNAL)
ROLLTUI_API(rolltui_frame_forget_rect, INTERNAL)  /* a picture drawn outside the grid is erased by writing its cells again */
ROLLTUI_API(rolltui_swap_forget_rect, INTERNAL)
ROLLTUI_API(rolltui_termfacts_set_active_images, INTERNAL)  /* the entered terminal publishes how it draws pictures */
ROLLTUI_API(rolltui_termfacts_active_images, INTERNAL)
ROLLTUI_API(rolltui_termfacts_active_cell_pixels, INTERNAL)
ROLLTUI_API(rolltui_os_tty_pixels, INTERNAL)

/* ---- rolltui_ansi.c — another program's coloured output, read as styled lines --- */
ROLLTUI_API(rolltui_ansi_new, PUBLIC)  /* chat reads an approved command's output */
ROLLTUI_API(rolltui_ansi_free, PUBLIC)
ROLLTUI_API(rolltui_ansi_feed, PUBLIC)  /* KEPT: a host feeds output it read itself (a socket, a file being written) */
ROLLTUI_API(rolltui_ansi_clear, PUBLIC)  /* KEPT: a host runs the command again into the same pane */
ROLLTUI_API(rolltui_ansi_set_limit, PUBLIC)  /* chat keeps a command's newest 2000 lines */
ROLLTUI_API(rolltui_ansi_set_tab_width, PUBLIC)  /* KEPT: another program's tab stop, as its own settings have it */
ROLLTUI_API(rolltui_ansi_line_count, PUBLIC)
ROLLTUI_API(rolltui_ansi_line, PUBLIC)
ROLLTUI_API(rolltui_ansi_rows, PUBLIC)  /* KEPT: a host's own widget scrolling over output */
ROLLTUI_API(rolltui_ansi_width, PUBLIC)  /* KEPT: a host's own widget scrolling sideways */
ROLLTUI_API(rolltui_ansi_draw, PUBLIC)  /* KEPT: a host's own widget drawing output (the output kind draws it for a window) */
ROLLTUI_API(rolltui_ansi_set_stamp, PUBLIC)  /* KEPT: a host's own log stamped with when and from where each line came */
ROLLTUI_API(rolltui_ansi_mark, PUBLIC)  /* KEPT: a rule in a host's own log */
ROLLTUI_API(rolltui_ansi_line_info, PUBLIC)  /* KEPT: a line's time, source and number, for a host's own log view */
ROLLTUI_API(rolltui_ansi_line_rows, PUBLIC)  /* KEPT: the rows one line wraps to, for a host's own view */
ROLLTUI_API(rolltui_ansi_draw_line, PUBLIC)  /* KEPT: one line drawn with matches lit, for a host's own view */

/* ---- the list, the table and the tree; ordering and finding text --- */
ROLLTUI_API(rolltui_natural_compare, PUBLIC)  /* a host orders its own names as a person reads them: file2 before file10 */
ROLLTUI_API(rolltui_fuzzy_match, PUBLIC)  /* a host ranks its own candidates and lights what matched: a command palette, a completion */
ROLLTUI_API(rolltui_text_find, PUBLIC)  /* find as a person means it: case and accents aside, smart case */
ROLLTUI_API(rolltui_text_find_each, PUBLIC)
ROLLTUI_API(rolltui_filter_new, PUBLIC)  /* the filter every list, menu and palette reads, for a host's own rows too */
ROLLTUI_API(rolltui_filter_free, PUBLIC)
ROLLTUI_API(rolltui_filter_match, PUBLIC)
ROLLTUI_API(rolltui_filter_terms, PUBLIC)
ROLLTUI_API(rolltui_filter_term, PUBLIC)
ROLLTUI_API(rolltui_filter_term_negated, PUBLIC)
ROLLTUI_API(rolltui_filter_spans, PUBLIC)  /* what matched, to light it */
ROLLTUI_API(rolltui_list_row_cell, PUBLIC)  /* a host's row function fills a row's cells */
ROLLTUI_API(rolltui_list_row_span, PUBLIC)  /* a host's row function colours part of a cell */
ROLLTUI_API(rolltui_list_event_release, PUBLIC)  /* KEPT: the release half of the event a host holds */
ROLLTUI_API(rolltui_windows_bind_list, PUBLIC)  /* a host's data in a `list:` window */
ROLLTUI_API(rolltui_windows_set_list_options, PUBLIC)  /* a table, a tree, marks: the host's to choose */
ROLLTUI_API(rolltui_windows_set_list_columns, PUBLIC)
ROLLTUI_API(rolltui_windows_list_refresh, PUBLIC)  /* the host's data changed */
ROLLTUI_API(rolltui_windows_list_set_filter, PUBLIC)  /* a filter the host sets: a search box of its own, a saved view */
ROLLTUI_API(rolltui_windows_list_set_cursor, PUBLIC)  /* the host puts the cursor on a row: a selection restored, a search result */
ROLLTUI_API(rolltui_windows_list_cursor, PUBLIC)  /* what the host acts on */
ROLLTUI_API(rolltui_windows_list_marked, PUBLIC)  /* what the host acts on, several at once */
ROLLTUI_API(rolltui_windows_list_event, PUBLIC)  /* what the person did */
ROLLTUI_API(rolltui_windows_list_perform, PUBLIC)  /* a host's own menu entry or command does what a key would */
ROLLTUI_API(rolltui_windows_list_cursor_column, PUBLIC)  /* the cell a host copies or edits */
ROLLTUI_API(rolltui_windows_list_freeze, PUBLIC)  /* one list paused while the rest update */
ROLLTUI_API(rolltui_windows_list_mark_matching, PUBLIC)  /* marks by pattern */
ROLLTUI_API(rolltui_windows_list_reveal, PUBLIC)  /* a row shown and chosen, its parents opened */
ROLLTUI_API(rolltui_windows_list_set_loading, PUBLIC)  /* "loading" instead of "no rows" */
ROLLTUI_API(rolltui_windows_list_set_open, PUBLIC)  /* a tree node opened or closed by the host */
ROLLTUI_API(rolltui_windows_push_view, PUBLIC)  /* KEPT: a drill-down into a window */
ROLLTUI_API(rolltui_windows_pop_view, PUBLIC)  /* KEPT: …and back out of it */
ROLLTUI_API(rolltui_windows_view_depth, PUBLIC)  /* KEPT: how deep it is, for a host's crumbs */
ROLLTUI_API(rolltui_windows_list_sort, PUBLIC)  /* the sort, set */
ROLLTUI_API(rolltui_windows_list_sort_state, PUBLIC)  /* the sort, read */
ROLLTUI_API(rolltui_windows_list, INTERNAL)  /* the kind reaches the table's list by name */
ROLLTUI_API(rolltui_windows_has_list, INTERNAL)
ROLLTUI_API(rolltui_list_default_actions, INTERNAL)  /* the list scope's names; the kind drives the widget */
ROLLTUI_API(rolltui_list_new, INTERNAL)
ROLLTUI_API(rolltui_list_free, INTERNAL)
ROLLTUI_API(rolltui_list_set_source, INTERNAL)
ROLLTUI_API(rolltui_list_has_source, INTERNAL)
ROLLTUI_API(rolltui_list_set_options, INTERNAL)
ROLLTUI_API(rolltui_list_set_columns, INTERNAL)
ROLLTUI_API(rolltui_list_refresh, INTERNAL)
ROLLTUI_API(rolltui_list_set_filter, INTERNAL)
ROLLTUI_API(rolltui_list_set_cursor, INTERNAL)
ROLLTUI_API(rolltui_list_cursor, INTERNAL)
ROLLTUI_API(rolltui_list_marked, INTERNAL)
ROLLTUI_API(rolltui_list_event, INTERNAL)
ROLLTUI_API(rolltui_list_perform, INTERNAL)
ROLLTUI_API(rolltui_list_back, INTERNAL)
ROLLTUI_API(rolltui_list_layout, INTERNAL)
ROLLTUI_API(rolltui_list_draw, INTERNAL)
ROLLTUI_API(rolltui_list_handle, INTERNAL)
ROLLTUI_API(rolltui_list_scroll_extent, INTERNAL)
ROLLTUI_API(rolltui_list_scroll_to, INTERNAL)

/* ---- charts: numbers drawn --- */
ROLLTUI_API(rolltui_chart_new, PUBLIC)  /* a host draws a chart in its own widget: a status line's sparkline, a dashboard cell */
ROLLTUI_API(rolltui_chart_free, PUBLIC)  /* KEPT: the release half of a pair a consumer holds */
ROLLTUI_API(rolltui_chart_clear, PUBLIC)  /* reused frame to frame */
ROLLTUI_API(rolltui_chart_set_kind, PUBLIC)
ROLLTUI_API(rolltui_chart_set_series, PUBLIC)
ROLLTUI_API(rolltui_chart_set_range, PUBLIC)
ROLLTUI_API(rolltui_chart_set_symbols, PUBLIC)  /* KEPT: a chart in braille, blocks or shades */
ROLLTUI_API(rolltui_chart_set_flags, PUBLIC)  /* KEPT: a chart that flows, has axes, or a meter that fills from its right */
ROLLTUI_API(rolltui_chart_capacity, PUBLIC)  /* KEPT: what a history for a chart keeps */
ROLLTUI_API(rolltui_chart_set_time, PUBLIC)  /* KEPT: time along a chart's foot */
ROLLTUI_API(rolltui_chart_set_gradient, PUBLIC)  /* KEPT: a series coloured by height, value or position */
ROLLTUI_API(rolltui_gradient_at, PUBLIC)  /* KEPT: a number coloured by where it falls */
ROLLTUI_API(rolltui_history_new, PUBLIC)  /* KEPT: the values a flowing chart shows, kept */
ROLLTUI_API(rolltui_history_free, PUBLIC)  /* KEPT */
ROLLTUI_API(rolltui_history_push, PUBLIC)  /* KEPT */
ROLLTUI_API(rolltui_history_resize, PUBLIC)  /* KEPT */
ROLLTUI_API(rolltui_history_count, PUBLIC)  /* KEPT */
ROLLTUI_API(rolltui_chart_set_series_history, PUBLIC)  /* KEPT: a series from a history */
ROLLTUI_API(rolltui_chart_draw, PUBLIC)
ROLLTUI_API(rolltui_sparkline_text, PUBLIC)  /* a sparkline a list cell or a status line shows as text */
ROLLTUI_API(rolltui_windows_bind_chart, PUBLIC)  /* a host's numbers in a `chart:` window */
ROLLTUI_API(rolltui_chart_draw_roles, INTERNAL)  /* the chart kind's body, in roles handed in */
ROLLTUI_API(rolltui_windows_fill_chart, INTERNAL)  /* the kind asks the table for its chart, filled */
ROLLTUI_API(rolltui_windows_has_chart, INTERNAL)

/* ---- rolltui_feedback.h — a gauge, a spinner, the message log and its toasts --- */
ROLLTUI_API(rolltui_gauge_release, PUBLIC)  /* KEPT: rule 1's pair: a C host fills a RolltuiGauge it owns and C has no destructor */
ROLLTUI_API(rolltui_gauge_draw, PUBLIC)  /* KEPT: a host draws a gauge inside its own widget: a status line, a row of a list */
ROLLTUI_API(rolltui_count_release, PUBLIC)  /* KEPT: rule 1's pair: a C host fills a RolltuiCount it owns and C has no destructor */
ROLLTUI_API(rolltui_count_draw_roles, INTERNAL)  /* the count kind's body, in roles handed in */
ROLLTUI_API(rolltui_big_digits_draw, PUBLIC)  /* KEPT: a host draws a big number in its own widget: a clock, a score */
ROLLTUI_API(rolltui_big_digits_width, PUBLIC)  /* KEPT: …and centres it */
ROLLTUI_API(rolltui_spinner_draw, PUBLIC)  /* KEPT: a host marks a row of its own list whose operation is running */
ROLLTUI_API(rolltui_gauge_draw_roles, INTERNAL)  /* the gauge kind's body, in roles handed in */
ROLLTUI_API(rolltui_message_log_init, INTERNAL)  /* the window table's own log */
ROLLTUI_API(rolltui_message_log_release, INTERNAL)  /* the window table's own log */
ROLLTUI_API(rolltui_message_log_post, INTERNAL)  /* a host posts through rolltui_windows_post */
ROLLTUI_API(rolltui_message_log_count, INTERNAL)  /* a host reads through rolltui_windows_message_count */
ROLLTUI_API(rolltui_message_log_at, INTERNAL)  /* a host reads through rolltui_windows_message_at */
ROLLTUI_API(rolltui_message_log_draw_toasts, INTERNAL)  /* a host draws through rolltui_windows_draw_toasts */
ROLLTUI_API(rolltui_message_log_toast_event, INTERNAL)  /* a host asks through rolltui_windows_toast_event */
ROLLTUI_API(rolltui_message_log_dismiss, INTERNAL)  /* a host clears through rolltui_windows_dismiss_toasts */
ROLLTUI_API(rolltui_toast_ms, INTERNAL)  /* how long a level shows, for the toasts and the messages kind */
ROLLTUI_API(rolltui_message_role, INTERNAL)  /* a level's role, for the toasts and the messages kind */
ROLLTUI_API(rolltui_message_glyph, INTERNAL)  /* a level's glyph, for the toasts and the messages kind */

ROLLTUI_API(rolltui_hint_bar_new, PUBLIC)  /* a status line's keys, clickable: dirktui */
ROLLTUI_API(rolltui_hint_bar_free, PUBLIC)  /* KEPT: the release half of a pair a consumer holds */
ROLLTUI_API(rolltui_hint_bar_clear, PUBLIC)  /* rebuilt when the bindings change */
ROLLTUI_API(rolltui_hint_bar_add, PUBLIC)
ROLLTUI_API(rolltui_hint_bar_add_scope, PUBLIC)  /* every action of a scope as a hint, in one call */
ROLLTUI_API(rolltui_hint_bar_draw, PUBLIC)
ROLLTUI_API(rolltui_hint_bar_hit, PUBLIC)
ROLLTUI_API(rolltui_hint_bar_set_separator, PUBLIC)  /* a breadcrumb: dirktui */
ROLLTUI_API(rolltui_hint_bar_set_keep_tail, PUBLIC)  /* a breadcrumb: dirktui */
ROLLTUI_API(rolltui_hint_bar_enable, PUBLIC)  /* a hint that cannot be taken now: dirktui */  /* the press half: a hint answers with its action */
ROLLTUI_API(rolltui_hint_bar_set_priority, PUBLIC)  /* KEPT: which hints go first when the bar is short of room */
ROLLTUI_API(rolltui_hint_bar_hover, PUBLIC)  /* the hint under the pointer, lit: dirktui */
ROLLTUI_API(rolltui_hint_bar_count, INTERNAL)
ROLLTUI_API(rolltui_windows_scrollbar_glyphs, INTERNAL)  /* what the draw path reads; the picker draws the same capsule the window does */
ROLLTUI_API(rolltui_windows_set_theme_hint, INTERNAL)
ROLLTUI_API(rolltui_windows_theme_editor, INTERNAL)
ROLLTUI_API(rolltui_windows_set_theme_store, PUBLIC)  /* KEPT: `theme` is a library widget kind, so a
   screen names an editor with no host code — but the theme is the APP's (the style table a frame is drawn
   with is handed to the library by the host), so this is the only way an app can say where a commit lands.
   A kind whose edits can reach nothing is a kind nobody can mount. */
ROLLTUI_API(rolltui_windows_set_bindings_store, PUBLIC)  /* KEPT: same reason one rung over. `keys` is a
   library widget kind, and the table it edits needs no call — the library holds it — but where a commit
   LANDS is the app's, so this is the only way an app can say. */
ROLLTUI_API(rolltui_windows_sync, PUBLIC)
ROLLTUI_API(rolltui_windows_transcript, PUBLIC)
ROLLTUI_API(rolltui_windows_transcript_at, INTERNAL)  /* a hit-test the stack already routes */
ROLLTUI_API(rolltui_windows_widget_for, PUBLIC)

/* ---- rolltui_gaps.c — the gap report: what a screen NAMES that an app does not PROVIDE ------
 * All four PUBLIC and none by measured reach — this module is BRAND NEW, so nobody
 * calls it yet by construction. They are public because a host author is the reader they exist
 * for: the report is how an app tells its own developer what a screen wants and the code has
 * not built. The pair rule (rule 1) covers `_release`; `_clean` and `_summary` are the shape
 * every other report on this boundary has, and shipping three of the four would be the lopsided
 * API the caveat is about. */
ROLLTUI_API(rolltui_gap_report_clean, PUBLIC)
ROLLTUI_API(rolltui_gap_report_release, PUBLIC)
ROLLTUI_API(rolltui_gap_report_summary, PUBLIC)
ROLLTUI_API(rolltui_gaps_collect, PUBLIC)

/* ---- rolltui_widget_kinds.h — the built-in kinds and their registration; default PUBLIC --- */
ROLLTUI_API(rolltui_help_document, PUBLIC)
ROLLTUI_API(rolltui_help_scope_lines, PUBLIC)
ROLLTUI_API(rolltui_input_completion_at, INTERNAL)  /* the input widget's own editing step; a test reads the candidates (opt-in) */
ROLLTUI_API(rolltui_input_completion_cancel, INTERNAL)  /* Escape as a level: the field kind closes the list */
ROLLTUI_API(rolltui_input_completion_open, INTERNAL)  /* the field kind and the overlay ask whether the list is open */
ROLLTUI_API(rolltui_input_draw_completions, INTERNAL)  /* the overlay pass draws the focused field's list */
ROLLTUI_API(rolltui_input_has_completer, INTERNAL)  /* the field kind asks whether Tab is the field's */
ROLLTUI_API(rolltui_input_kind_process_event, PUBLIC)
ROLLTUI_API(rolltui_input_max_rows, INTERNAL)  /* Phase 24: its old KEPT reason said "a host applies it too" and no host does — measured 0 consumer files */
ROLLTUI_API(rolltui_input_set_completer, PUBLIC)  /* rolltui-chat's prompt completes its commands */
ROLLTUI_API(rolltui_input_suggestion, INTERNAL)  /* the input widget's own editing step; a test reads the suggestion (opt-in) */
ROLLTUI_API(rolltui_input_widget_ctx_set_min_outer, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_input_widget_plugin, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_input_window_rows, INTERNAL)  /* Phase 24: its old KEPT reason said "a host applies it too" and no host does — measured 0 consumer files */
ROLLTUI_API(rolltui_menu_widget_ctx_origin, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_menu_widget_ctx_refresh, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_menu_widget_plugin, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_scroll_by_action, PUBLIC)
ROLLTUI_API(rolltui_scroll_text_default_actions, PUBLIC)
ROLLTUI_API(rolltui_widget_kinds_register, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_windows_builtin_roles, INTERNAL)
ROLLTUI_API(rolltui_windows_code_fold, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_windows_input_actions, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_windows_menu_roles, INTERNAL)
ROLLTUI_API(rolltui_windows_scroll_text_actions, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_context_set_builtin_roles, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_context_set_code_fold, PUBLIC)
ROLLTUI_API(rolltui_context_set_input_actions, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_context_set_library_defaults, PUBLIC)
ROLLTUI_API(rolltui_context_set_menu_roles, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_context_set_scroll_text_actions, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_context_set_transcript_actions, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_windows_transcript_actions, INTERNAL)  /* the built-in kinds' own */

/* ---- rolltui_document.h — the transcript document a host appends to; default PUBLIC ------ */
ROLLTUI_API(rolltui_doc_entry_copy, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_doc_entry_init, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_doc_entry_release, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_document_add, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to as well */
ROLLTUI_API(rolltui_document_at, INTERNAL)  /* a public type's read API */
ROLLTUI_API(rolltui_document_clear, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_document_remove, PUBLIC)  /* an inline host drops a committed entry */
ROLLTUI_API(rolltui_document_copy, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_document_count, INTERNAL)  /* a public type's read API */
ROLLTUI_API(rolltui_document_release, PUBLIC)  /* KEPT: rule 1's pair: roll holds a RolltuiDocument and C has no destructor */

/* ---- rolltui_widget_input.h — the input widget: a host sets, selects, undoes and reads; default PUBLIC --- */
ROLLTUI_API(rolltui_input_can_redo, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_can_undo, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_caret, PUBLIC)  /* KEPT: a rename reads where the caret is to put it before a file's extension */
ROLLTUI_API(rolltui_input_cell_of, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_clear, PUBLIC)
ROLLTUI_API(rolltui_input_clear_selection, INTERNAL)  /* the widget clears its own selection on the next event */
ROLLTUI_API(rolltui_input_default_actions, PUBLIC)  /* KEPT: the *_default_actions vocabulary; its transcript and stack siblings are host-reached */
ROLLTUI_API(rolltui_input_draw, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_erase_backward, INTERNAL)  /* the handler's step (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_erase_forward, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_erase_selection, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_input_free, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_handle, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_history_at, PUBLIC)  /* KEPT: a host that shows its history (a picker of past commands) reads it */
ROLLTUI_API(rolltui_input_history_count, PUBLIC)  /* KEPT: a host that shows its history reads how long it is */
ROLLTUI_API(rolltui_input_history_cursor, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_history_next, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_input_history_prev, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_input_hit, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_input_insert, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_kill_to_line_end, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_kill_to_line_start, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_kill_word_backward, INTERNAL)  /* the handler's step (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_kill_word_forward, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_layout, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_move_down, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_left, INTERNAL)  /* the handler's step; a host sends the event (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_move_line_end, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_line_start, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_right, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_up, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_word_left, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_word_right, INTERNAL)  /* the handler's step (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_new, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_options, PUBLIC)
ROLLTUI_API(rolltui_input_options_copy, PUBLIC)
ROLLTUI_API(rolltui_input_options_equal, PUBLIC)
ROLLTUI_API(rolltui_input_options_init, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_options_release, PUBLIC)
ROLLTUI_API(rolltui_input_preview_insert, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_push_history, PUBLIC)  /* KEPT: a host that submits by its own key keeps the entry as Submit would */
ROLLTUI_API(rolltui_input_redo, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_rows, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_rows_for, INTERNAL)  /* a step of the input's layout (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_select_all, PUBLIC)  /* an address bar's rule on reaching it: dirktui's path line */
ROLLTUI_API(rolltui_input_selected_text, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_selection, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_set_caret, PUBLIC)  /* KEPT: a rename puts the caret before a file's extension, or selects the name without it */
ROLLTUI_API(rolltui_input_set_copy, PUBLIC)
ROLLTUI_API(rolltui_input_set_options, PUBLIC)
ROLLTUI_API(rolltui_input_set_text, PUBLIC)  /* a host prefills: dirktui's path line */
ROLLTUI_API(rolltui_input_text, PUBLIC)
ROLLTUI_API(rolltui_input_top_row, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_wrapped_text, PUBLIC)  /* KEPT: a commit message typed at 72 columns is read back with its breaks */
ROLLTUI_API(rolltui_input_perform, PUBLIC)  /* KEPT: a host runs one of the input's commands by name */
ROLLTUI_API(rolltui_input_mode, PUBLIC)  /* KEPT: a host shows which of vim's modes the field is in (a status line's "-- INSERT --") */
ROLLTUI_API(rolltui_input_perform_modal, PUBLIC)  /* KEPT: a host that drives the field itself resolves `d w` to one command */
ROLLTUI_API(rolltui_input_history_clear, PUBLIC)  /* KEPT: a host forgets what was typed ("clear history") */
ROLLTUI_API(rolltui_input_history_load, PUBLIC)
ROLLTUI_API(rolltui_input_history_save, PUBLIC)
ROLLTUI_API(rolltui_input_yank, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_transpose_chars, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_transpose_words, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_change_case, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_set_overwrite, INTERNAL)  /* the handler's step (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_overwrite, INTERNAL)  /* the handler's state (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_killed, INTERNAL)  /* the kill buffer; a host sends events (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_set_syntax, INTERNAL)  /* the input kind hands it the window table's languages */
ROLLTUI_API(rolltui_input_undo, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_word_left_of, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_word_right_of, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_str_set, PUBLIC)

/* ---- rolltui_widget_menu.h — the menu widget; default PUBLIC ------------------------------------ */
ROLLTUI_API(rolltui_check_input, INTERNAL)  /* a step of the menu loader */
ROLLTUI_API(rolltui_input_check_release, INTERNAL)  /* a step of the menu loader */
ROLLTUI_API(rolltui_input_hint, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_input_type_from_name, INTERNAL)  /* the menu loader's vocabulary */
ROLLTUI_API(rolltui_input_type_name, INTERNAL)  /* the menu loader's vocabulary */
ROLLTUI_API(rolltui_menu_action_list_release, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* a public C++ member calls it (~RolltuiMenuActionList); a C consumer must be able to do the same */
ROLLTUI_API(rolltui_menu_apply_shortcuts, PUBLIC)  /* KEPT: matching part — `rolltui_bindings_declare` is consumer-used — a menu shows an action's LIVE chords, and this is how a host refreshes them after the table changes */
ROLLTUI_API(rolltui_menu_breadcrumb, INTERNAL)
ROLLTUI_API(rolltui_menu_title, INTERNAL)
ROLLTUI_API(rolltui_menu_default_actions, PUBLIC)
ROLLTUI_API(rolltui_menu_draw, INTERNAL)
ROLLTUI_API(rolltui_menu_dump_json, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_menu_edit_reason, INTERNAL)
ROLLTUI_API(rolltui_menu_editing, INTERNAL)
ROLLTUI_API(rolltui_menu_editor, INTERNAL)
ROLLTUI_API(rolltui_menu_event_release, PUBLIC)
ROLLTUI_API(rolltui_menu_filter, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_menu_find, PUBLIC)
ROLLTUI_API(rolltui_menu_focused, PUBLIC)  /* a host that describes the focused item (mother's build screen, its info panel) needs where the cursor is before any MOVED: a menu just made, a window just given the focus */
ROLLTUI_API(rolltui_menu_flat_count, INTERNAL)  /* the palette's own flattening (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_menu_flat_label, INTERNAL)  /* the palette's own flattening (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_menu_free, INTERNAL)
ROLLTUI_API(rolltui_menu_handle, PUBLIC)
ROLLTUI_API(rolltui_menu_item_actions, PUBLIC)  /* KEPT: matching part — `rolltui_menu_parse_json` fills an item tree, and this is how a host checks the action ids in the tree it just parsed */
ROLLTUI_API(rolltui_menu_layout, INTERNAL)
ROLLTUI_API(rolltui_menu_set_hover, INTERNAL)  /* the pointer for the next draw, from the window's resolved node */
ROLLTUI_API(rolltui_menu_level, INTERNAL)
ROLLTUI_API(rolltui_menu_load_report_add_bad_value, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_menu_load_report_add_unknown_key, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_menu_load_report_clean, PUBLIC)  /* KEPT: matching part — the report of `rolltui_menu_parse_json`, which is kept for the same reason as `rolltui_layout_report_clean` */
ROLLTUI_API(rolltui_menu_load_report_release, PUBLIC)  /* KEPT: matching part — rule 1's pair for the report `rolltui_menu_parse_json` fills */
ROLLTUI_API(rolltui_menu_load_report_set_error, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_menu_new, INTERNAL)
ROLLTUI_API(rolltui_menu_options, INTERNAL)  /* a step of the menu widget */
ROLLTUI_API(rolltui_menu_palette, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_menu_parse_json, PUBLIC)  /* KEPT: matching part — `rolltui_load_layout_text` + `rolltui_layout_report_clean` are consumer-used and let a host validate the LAYOUT file it ships; a host that ships a MENU file and cannot validate it is the same hole, and roll's own test does exactly this */
ROLLTUI_API(rolltui_menu_path, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_menu_reset, PUBLIC)
ROLLTUI_API(rolltui_menu_root, PUBLIC)  /* KEPT: matching part — `rolltui_windows_menu` is consumer-used and hands back a RolltuiMenu*; a menu you can obtain but whose tree you cannot reach is a hole, and `_find` only answers "is this id present" */
ROLLTUI_API(rolltui_menu_scroll_first, INTERNAL)  /* a step of the menu widget */
ROLLTUI_API(rolltui_menu_scroll_visible, INTERNAL)  /* a step of the menu widget */
ROLLTUI_API(rolltui_menu_scroll_to, INTERNAL)  /* the window's bar, a handle: the kind adapter's */
ROLLTUI_API(rolltui_menu_back, INTERNAL)  /* the menu kind's `back` slot */
ROLLTUI_API(rolltui_menu_selected, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_menu_selected_item, INTERNAL)
ROLLTUI_API(rolltui_menu_dropdown_open, INTERNAL)  /* a test reads the dropdown back */
ROLLTUI_API(rolltui_menu_dropdown_selected, INTERNAL)
ROLLTUI_API(rolltui_menu_set_checked, PUBLIC)  /* the third plain setter: a host opens its menu with a toggle set from its own saved setting (dirktui) */
ROLLTUI_API(rolltui_menu_set_enabled, PUBLIC)
ROLLTUI_API(rolltui_menu_set_options, PUBLIC)
ROLLTUI_API(rolltui_menu_set_options_struct, INTERNAL)
ROLLTUI_API(rolltui_menu_set_palette, PUBLIC)
ROLLTUI_API(rolltui_menu_set_root, INTERNAL)
ROLLTUI_API(rolltui_menu_set_validator_fn, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_menu_set_value, PUBLIC)
ROLLTUI_API(rolltui_menu_visible, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */

/* ---- rolltui_widget_menu_tree.h — the menu item tree a host builds and walks; default PUBLIC ---- */
ROLLTUI_API(rolltui_input_spec_copy, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_input_spec_equal, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_input_spec_init, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_input_spec_release, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_item_copy, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_item_equal, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_item_free, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_item_init, PUBLIC)  /* KEPT: matching part — `rolltui_menu_item_set` is kept, and a C caller must init before set or the struct is garbage */
ROLLTUI_API(rolltui_menu_item_new, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_item_set, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_menu_item_release, PUBLIC)  /* rule 1: the pair to `_init`, which is public and which the public parser fills into */
ROLLTUI_API(rolltui_menu_item_add_cell, PUBLIC)  /* a host fills a table's rows (mother's builds pane); the only way to write a row's cells */
ROLLTUI_API(rolltui_menu_item_set_columns, PUBLIC)  /* a host makes a level a table; the only way to write its columns, and it holds the file's rules */
ROLLTUI_API(rolltui_menu_cells_release, PUBLIC)  /* KEPT: a public C++ member in the definition calls it (`RolltuiMenuCells`'s destructor), and a row's cells refill from it */
ROLLTUI_API(rolltui_menu_columns_release, PUBLIC)  /* KEPT: a public C++ member in the definition calls it (`RolltuiMenuColumns`'s destructor) */
ROLLTUI_API(rolltui_menu_list_add, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* called from a public C++ member, so every C++ consumer already reaches it; a C consumer needs the same operation */
ROLLTUI_API(rolltui_menu_list_at, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_menu_list_clear, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_list_copy, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_list_count, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_menu_list_release, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* rule 1: a public type's lifecycle; only the library reaches it today */

/* ---- rolltui_widget_transcript.h — the transcript widget; default PUBLIC ------------------------ */
ROLLTUI_API(rolltui_entry_layout_release, INTERNAL)  /* the transcript's own store */
ROLLTUI_API(rolltui_entry_layout_store, INTERNAL)  /* the transcript's own store */
ROLLTUI_API(rolltui_selection_range_in, INTERNAL)  /* a step of select */
ROLLTUI_API(rolltui_text_pos_less, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */  /* a public C++ member calls it (RolltuiSelection::first); a C consumer must be able to do the same */
ROLLTUI_API(rolltui_transcript_clear_selection, INTERNAL)  /* the widget clears its own selection on the next event */
ROLLTUI_API(rolltui_transcript_copy_selection, PUBLIC)
ROLLTUI_API(rolltui_transcript_current_match, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_current_match_number, INTERNAL)
ROLLTUI_API(rolltui_transcript_default_actions, PUBLIC)
ROLLTUI_API(rolltui_transcript_draw, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_find_next, PUBLIC)
ROLLTUI_API(rolltui_transcript_find_prev, PUBLIC)
ROLLTUI_API(rolltui_transcript_free, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_handle, PUBLIC)
ROLLTUI_API(rolltui_transcript_hit, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_is_folded, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_layout, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_layout_of, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_lines_below, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_match_at, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_match_count, INTERNAL)
ROLLTUI_API(rolltui_transcript_new, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_query, INTERNAL)
ROLLTUI_API(rolltui_transcript_scroll, INTERNAL)
ROLLTUI_API(rolltui_transcript_scroll_by, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_scroll_page, PUBLIC)
ROLLTUI_API(rolltui_transcript_scroll_to_bottom, PUBLIC)
ROLLTUI_API(rolltui_transcript_scroll_to_top, PUBLIC)
ROLLTUI_API(rolltui_transcript_select, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_selected_text, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_selection, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_set_code_folded, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_set_code_uncapped, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_set_copy, PUBLIC)
ROLLTUI_API(rolltui_transcript_set_folded, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_set_highlight, INTERNAL)  /* a step of find */
ROLLTUI_API(rolltui_transcript_set_query, PUBLIC)
ROLLTUI_API(rolltui_transcript_stats, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_text_area, INTERNAL)  /* the transcript widget's own step; a host scrolls, finds and folds through the public set */
ROLLTUI_API(rolltui_transcript_tick, PUBLIC)
ROLLTUI_API(rolltui_transcript_toggle_fold_nearest_top, PUBLIC)
ROLLTUI_API(rolltui_transcript_top_line, INTERNAL)
ROLLTUI_API(rolltui_transcript_total_lines, PUBLIC)  /* an inline host sizes its region by it */
ROLLTUI_API(rolltui_transcript_entry_frame, PUBLIC)  /* an inline host commits a finished entry drawn by it */
ROLLTUI_API(rolltui_transcript_viewport_height, INTERNAL)  /* a step of layout */
ROLLTUI_API(rolltui_transcript_wants_tick, PUBLIC)

/* ---- rolltui_undo.h — the editors' undo stack; default TOOL_FACING ----------------------- */
ROLLTUI_API(rolltui_undo_can_redo, INTERNAL)
ROLLTUI_API(rolltui_undo_can_undo, INTERNAL)
ROLLTUI_API(rolltui_undo_commit, INTERNAL)
ROLLTUI_API(rolltui_undo_current, INTERNAL)
ROLLTUI_API(rolltui_undo_empty, INTERNAL)
ROLLTUI_API(rolltui_undo_free, INTERNAL)
ROLLTUI_API(rolltui_undo_new, INTERNAL)
ROLLTUI_API(rolltui_undo_redo, INTERNAL)
ROLLTUI_API(rolltui_undo_redo_depth, INTERNAL)
ROLLTUI_API(rolltui_undo_replace_current, INTERNAL)
ROLLTUI_API(rolltui_undo_reset, INTERNAL)
ROLLTUI_API(rolltui_undo_undo, INTERNAL)
ROLLTUI_API(rolltui_undo_undo_depth, INTERNAL)

/* ---- rolltui_embedded.h — the shipped files, by name; default PUBLIC --------------------- */
ROLLTUI_API(rolltui_embedded_text, PUBLIC)

/* ---- rolltui_json.h — RolltuiJsonValue: a public type (a theme preset holds one), so its API is public; default PUBLIC --- */
ROLLTUI_API(rolltui_json_array, PUBLIC)
ROLLTUI_API(rolltui_json_array_at, PUBLIC)  /* KEPT: matching part — `rolltui_json_array_push` is host-suite-reached — an array you can build and not read back is a hole */
ROLLTUI_API(rolltui_json_array_push, PUBLIC)
ROLLTUI_API(rolltui_json_array_size, PUBLIC)  /* KEPT: matching part — `rolltui_json_array_push` is host-suite-reached — an array you can build and not read back is a hole */
ROLLTUI_API(rolltui_json_as_bool, PUBLIC)  /* KEPT: matching part — `rolltui_json_get` is host-suite-reached and returns a value only an `_as_*` can read */
ROLLTUI_API(rolltui_json_as_number, INTERNAL)
ROLLTUI_API(rolltui_json_as_string, PUBLIC)  /* a host reads a string from its own settings file (dirktui): the matching read for a value only an `_as_*` can read */
ROLLTUI_API(rolltui_json_bool, INTERNAL)  /* the JSON API's constructors */
ROLLTUI_API(rolltui_json_clone, PUBLIC)
ROLLTUI_API(rolltui_json_dump, PUBLIC)
ROLLTUI_API(rolltui_json_equal, INTERNAL)
ROLLTUI_API(rolltui_json_free, PUBLIC)
ROLLTUI_API(rolltui_json_get, PUBLIC)
ROLLTUI_API(rolltui_json_has, INTERNAL)
ROLLTUI_API(rolltui_json_is_array, INTERNAL)
ROLLTUI_API(rolltui_json_is_bool, PUBLIC)  /* KEPT: matching part — `rolltui_json_get` is host-suite-reached and returns a value only an `_is_*` can type-test */
ROLLTUI_API(rolltui_json_is_null, INTERNAL)
ROLLTUI_API(rolltui_json_is_number, PUBLIC)  /* KEPT: matching part — `rolltui_json_get` is host-suite-reached and returns a value only an `_is_*` can type-test */
ROLLTUI_API(rolltui_json_is_object, INTERNAL)
ROLLTUI_API(rolltui_json_is_string, INTERNAL)
ROLLTUI_API(rolltui_json_null, PUBLIC)  /* KEPT: matching part — `rolltui_json_string`/`_array` are host-suite-reached makers; a value set without null is a hole */
ROLLTUI_API(rolltui_json_number, INTERNAL)  /* the JSON API's constructors */
ROLLTUI_API(rolltui_json_object, INTERNAL)
ROLLTUI_API(rolltui_json_object_erase, PUBLIC)
ROLLTUI_API(rolltui_json_object_key_at, INTERNAL)
ROLLTUI_API(rolltui_json_object_size, INTERNAL)
ROLLTUI_API(rolltui_json_object_value_at, INTERNAL)
ROLLTUI_API(rolltui_json_parse, PUBLIC)
ROLLTUI_API(rolltui_json_set, PUBLIC)
ROLLTUI_API(rolltui_json_string, PUBLIC)

/* ---- c/rolltui_presets.h — the generic engine behind all three stores; default INTERNAL ---
 * Once PUBLIC, when a `RolltuiPresetStore` dispatched over a `RolltuiPresetDomain` descriptor
 * was the whole surface. Every real caller always knew which of the three domains it meant, so
 * the public surface moved onto three concrete types — see the `rolltui.h` and `rolltui_studio.h`
 * sections below — and the engine itself, unchanged, is now reached only by `rolltui_presets.c`
 * (which builds the three concrete stores from it) and by a suite that opts in to test it
 * directly (`presets_test.cpp`). */
ROLLTUI_API(rolltui_bindings_preset_domain_init, INTERNAL)  /* the bring-your-own-vocabulary rung */
ROLLTUI_API(rolltui_bindings_preset_report_clean, PUBLIC)
ROLLTUI_API(rolltui_bindings_preset_report_release, PUBLIC)
ROLLTUI_API(rolltui_bindings_preset_report_summary, PUBLIC)  /* KEPT: matching part — its own `_clean` and `rolltui_theme_preset_report_summary` are consumer-used — asking whether a file is clean but not WHAT is wrong, for one domain of three, is a hole (Phase 21 wall E5/E6) */
ROLLTUI_API(rolltui_layout_preset_domain_init, INTERNAL)  /* the bring-your-own-vocabulary rung */
ROLLTUI_API(rolltui_layout_preset_report_clean, PUBLIC)
ROLLTUI_API(rolltui_layout_preset_report_release, PUBLIC)
ROLLTUI_API(rolltui_layout_preset_report_summary, PUBLIC)  /* KEPT: matching part — its own `_clean` and `rolltui_theme_preset_report_summary` are consumer-used — same hole, same set of three domains */
ROLLTUI_API(rolltui_preset_domain, INTERNAL)
ROLLTUI_API(rolltui_preset_domain_theme, INTERNAL)  /* the three concrete stores' own `_new` are this file's only callers now */
ROLLTUI_API(rolltui_preset_domain_layout, INTERNAL)
ROLLTUI_API(rolltui_preset_domain_bindings, INTERNAL)
ROLLTUI_API(rolltui_preset_domain_name, INTERNAL)
ROLLTUI_API(rolltui_preset_domain_release, INTERNAL)  /* rule 1: the pair */
ROLLTUI_API(rolltui_preset_is_shipped, INTERNAL)  /* each concrete store has its own `_is_shipped(RolltuiContext*, ...)` now */
ROLLTUI_API(rolltui_preset_json_names_in, INTERNAL)  /* a step of list (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_preset_list_release, PUBLIC)
ROLLTUI_API(rolltui_preset_looks_like_path, INTERNAL)  /* the store already answers a bad name with a sentence */
ROLLTUI_API(rolltui_preset_read_file, PUBLIC)
ROLLTUI_API(rolltui_preset_report_summary, INTERNAL)  /* the generic composer; the per-domain summaries are the API (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_preset_save_result_text, PUBLIC)
ROLLTUI_API(rolltui_preset_shipped, INTERNAL)  /* each concrete store has its own `_shipped(RolltuiContext*, ...)` now */
ROLLTUI_API(rolltui_preset_shipped_names, INTERNAL)  /* each concrete store has its own `_shipped_names(RolltuiContext*, ...)` now */
ROLLTUI_API(rolltui_preset_shipped_text, INTERNAL)
ROLLTUI_API(rolltui_preset_store_edit, INTERNAL)  /* each concrete store has its own typed `_edit`, in `rolltui_studio.h` */
ROLLTUI_API(rolltui_preset_store_free, INTERNAL)  /* each concrete store has its own `_free` now */
ROLLTUI_API(rolltui_preset_store_get, INTERNAL)  /* each concrete store has its own `_get` now */
ROLLTUI_API(rolltui_preset_store_label, INTERNAL)  /* each concrete store has its own `_label` now */
ROLLTUI_API(rolltui_preset_store_last_error, INTERNAL)
ROLLTUI_API(rolltui_preset_store_list, INTERNAL)  /* each concrete store has its own `_list` now */
ROLLTUI_API(rolltui_preset_store_load, INTERNAL)  /* each concrete store has its own `_load` now */
ROLLTUI_API(rolltui_preset_store_modified, INTERNAL)  /* each concrete store has its own `_modified` now */
ROLLTUI_API(rolltui_preset_store_new, INTERNAL)  /* each concrete store has its own `_new` now */
ROLLTUI_API(rolltui_preset_store_origin, INTERNAL)  /* each concrete store has its own `_origin` now */
ROLLTUI_API(rolltui_preset_store_preset_path, INTERNAL)  /* merged into each concrete store's own `_path` now */
ROLLTUI_API(rolltui_preset_store_save_as, INTERNAL)  /* each concrete store has its own `_save_as` now */
ROLLTUI_API(rolltui_preset_store_add, INTERNAL)  /* each concrete store has its own `_add` now */
ROLLTUI_API(rolltui_preset_store_set_working, INTERNAL)
ROLLTUI_API(rolltui_preset_store_start, INTERNAL)  /* each concrete store has its own `_start` now */
ROLLTUI_API(rolltui_preset_store_refresh, INTERNAL)  /* each concrete store has its own `_refresh` */
ROLLTUI_API(rolltui_preset_store_watch_paths, INTERNAL)  /* the run watches the files a refresh looks at */
ROLLTUI_API(rolltui_preset_store_value_free, INTERNAL)  /* `_working` is a BORROW on each concrete store now; `_get`'s own OWNED return frees through its domain's own `_free` */
ROLLTUI_API(rolltui_preset_store_version, INTERNAL)  /* each concrete store has its own `_version` now */
ROLLTUI_API(rolltui_preset_store_working, INTERNAL)  /* each concrete store has its own `_working` now, as a BORROW */
ROLLTUI_API(rolltui_preset_store_working_path, INTERNAL)  /* merged into each concrete store's own `_path` now */
ROLLTUI_API(rolltui_preset_valid_name, INTERNAL)  /* the store already answers a bad name with a sentence */
ROLLTUI_API(rolltui_preset_working_value, INTERNAL)  /* reached only by `rolltui_settings.c`, over whichever concrete store's cast it holds for a key's domain */
ROLLTUI_API(rolltui_preset_write_file_atomic, PUBLIC)

/* ---- rolltui.h — the theme/layout/bindings stores' USE surface; default PUBLIC ------------
 * The three concrete types that replaced the generic engine above: create one, read what's
 * loaded, switch to a different existing preset by name, save the working copy under a name, add
 * a file someone sent. The whole surface a host needs — roll reaches `_save_as` and `_add`
 * (`theme save`/`theme add`, `bindings save`, a settings-menu save-as) — while editing a
 * preset's content in place is `rolltui_studio.h`'s, below. */
ROLLTUI_API(rolltui_theme_store_new, PUBLIC)
ROLLTUI_API(rolltui_theme_store_free, PUBLIC)
ROLLTUI_API(rolltui_theme_store_start, PUBLIC)
ROLLTUI_API(rolltui_theme_store_refresh, PUBLIC)  /* KEPT: a host reading its theme file again on its own signal or schedule */
ROLLTUI_API(rolltui_theme_store_working, PUBLIC)
ROLLTUI_API(rolltui_theme_store_copy_working, PUBLIC)
ROLLTUI_API(rolltui_theme_store_modified, PUBLIC)
ROLLTUI_API(rolltui_theme_store_label, PUBLIC)
ROLLTUI_API(rolltui_theme_store_version, PUBLIC)
ROLLTUI_API(rolltui_theme_store_origin, PUBLIC)
ROLLTUI_API(rolltui_theme_store_list, PUBLIC)
ROLLTUI_API(rolltui_theme_store_get, PUBLIC)
ROLLTUI_API(rolltui_theme_store_load, PUBLIC)
ROLLTUI_API(rolltui_theme_store_save_as, PUBLIC)
ROLLTUI_API(rolltui_theme_store_add, PUBLIC)
ROLLTUI_API(rolltui_theme_store_set_mode, PUBLIC)
ROLLTUI_API(rolltui_theme_store_set_depth, PUBLIC)
ROLLTUI_API(rolltui_theme_store_path, PUBLIC)
ROLLTUI_API(rolltui_theme_is_shipped, PUBLIC)
ROLLTUI_API(rolltui_theme_shipped_names, PUBLIC)
ROLLTUI_API(rolltui_theme_shipped, PUBLIC)
ROLLTUI_API(rolltui_layout_store_new, PUBLIC)
ROLLTUI_API(rolltui_layout_store_free, PUBLIC)
ROLLTUI_API(rolltui_layout_store_start, PUBLIC)
ROLLTUI_API(rolltui_layout_store_refresh, PUBLIC)  /* KEPT: a host reading its layout file again on its own signal or schedule */
ROLLTUI_API(rolltui_layout_store_working, PUBLIC)
ROLLTUI_API(rolltui_layout_store_copy_working, PUBLIC)
ROLLTUI_API(rolltui_layout_store_modified, PUBLIC)
ROLLTUI_API(rolltui_layout_store_label, PUBLIC)
ROLLTUI_API(rolltui_layout_store_version, PUBLIC)
ROLLTUI_API(rolltui_layout_store_origin, PUBLIC)
ROLLTUI_API(rolltui_layout_store_list, PUBLIC)
ROLLTUI_API(rolltui_layout_store_get, PUBLIC)
ROLLTUI_API(rolltui_layout_store_load, PUBLIC)
ROLLTUI_API(rolltui_layout_store_save_as, PUBLIC)
ROLLTUI_API(rolltui_layout_store_add, PUBLIC)
ROLLTUI_API(rolltui_layout_store_path, PUBLIC)
ROLLTUI_API(rolltui_layout_is_shipped, PUBLIC)
ROLLTUI_API(rolltui_layout_shipped_names, PUBLIC)
ROLLTUI_API(rolltui_layout_shipped, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_new, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_free, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_start, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_refresh, PUBLIC)  /* KEPT: a host reading its key file again on its own signal or schedule */
ROLLTUI_API(rolltui_bindings_store_working, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_copy_working, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_modified, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_label, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_version, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_origin, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_list, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_get, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_load, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_save_as, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_add, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_path, PUBLIC)
ROLLTUI_API(rolltui_bindings_is_shipped, PUBLIC)
ROLLTUI_API(rolltui_bindings_shipped_names, PUBLIC)
ROLLTUI_API(rolltui_bindings_shipped, PUBLIC)

/* ---- rolltui_studio.h — the stores' in-place EDIT surface; default PUBLIC -----------------
 * PUBLIC here means public to the header, not to every host: only rolltui-studio and its three
 * editors may include `rolltui_studio.h` at all (enforced by `public_header_test` section 3b,
 * the same mechanism as an internal `c/` header's opt-in list), so nothing here is reached by
 * dirktui, paint or roll — only studio ever edits a preset's content in place. */
ROLLTUI_API(rolltui_theme_store_edit, PUBLIC)
ROLLTUI_API(rolltui_layout_store_set_working, PUBLIC)
ROLLTUI_API(rolltui_bindings_store_set_working, PUBLIC)

/* ---- rolltui_settings.c — the keys a person changes; default PUBLIC ------------------------
 * A HOST'S ENTRY POINT TO ITS OWN SETTINGS, which is the stated reason for every row here and
 * not the fact that roll reaches them. The five keys, the precedence between rungs and the
 * routing to a working copy are one subject with one handle, so a host spells keys and never
 * a store. The rows replace `rolltui_preset_settings_*` and `_resolve_setting`, which made a
 * host answer which store a key lived in and then switch on the answer. */
ROLLTUI_API(rolltui_setting_rung_name, PUBLIC)
ROLLTUI_API(rolltui_settings_at, PUBLIC)
ROLLTUI_API(rolltui_settings_count, PUBLIC)
ROLLTUI_API(rolltui_settings_find, PUBLIC)
ROLLTUI_API(rolltui_settings_free, PUBLIC)
ROLLTUI_API(rolltui_settings_get, PUBLIC)
ROLLTUI_API(rolltui_settings_label, PUBLIC)
ROLLTUI_API(rolltui_settings_new, PUBLIC)
ROLLTUI_API(rolltui_settings_report_release, PUBLIC)
ROLLTUI_API(rolltui_settings_resolve, PUBLIC)
ROLLTUI_API(rolltui_settings_set, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_domain_init, INTERNAL)  /* the bring-your-own-vocabulary rung */
ROLLTUI_API(rolltui_theme_preset_parse, PUBLIC)  /* the rung a host that ships its own theme FILES stands on — the C consumer reaches it */
ROLLTUI_API(rolltui_theme_preset_parse_partial, INTERNAL)  /* the Theme domain's own parse */
ROLLTUI_API(rolltui_theme_preset_report_clean, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_report_release, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_report_summary, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_to_json, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_value_free, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_value_release, PUBLIC)

/* ---- rolltui_lifetime.h — the release point; default PUBLIC ------------------------------ */
ROLLTUI_API(rolltui_on_shutdown, INTERNAL)
ROLLTUI_API(rolltui_release_thread, INTERNAL)  /* the explicit per-thread release the lifetime header documents */
ROLLTUI_API(rolltui_shutdown, PUBLIC)
ROLLTUI_API(rolltui_thread_on_release, INTERNAL)  /* the library registers its own scratch; rolltui_release_thread is the public half */

/* ---- rolltui_mem.h — the leak gauge — public by stated reason: it is how a consumer proves it released everything; default PUBLIC --- */
ROLLTUI_API(rolltui_mem_alloc, PUBLIC)
ROLLTUI_API(rolltui_mem_free, PUBLIC)
ROLLTUI_API(rolltui_mem_stats, PUBLIC)

/* ---- rolltui_terminal.h — the one fd; default PUBLIC ------------------------------------- */
ROLLTUI_API(rolltui_terminal_enter_sequence, INTERNAL)  /* an escape sequence the library writes itself */
ROLLTUI_API(rolltui_terminal_remember_copy, INTERNAL)  /* the clipboard copy keeps what it copied on the terminal */
ROLLTUI_API(rolltui_terminal_last_copy, INTERNAL)  /* the clipboard read falls back to it */
ROLLTUI_API(rolltui_terminal_free, PUBLIC)
ROLLTUI_API(rolltui_terminal_suspend, PUBLIC)  /* a host runs an editor on its own terminal and comes back: dirktui */
ROLLTUI_API(rolltui_terminal_resume, PUBLIC)   /* the other half of that pair */
ROLLTUI_API(rolltui_terminal_height, PUBLIC)
ROLLTUI_API(rolltui_terminal_is_tty, PUBLIC)
ROLLTUI_API(rolltui_terminal_key_protocol, INTERNAL)  /* an escape sequence the library writes itself */
ROLLTUI_API(rolltui_terminal_leave_sequence, INTERNAL)  /* an escape sequence the library writes itself */
ROLLTUI_API(rolltui_terminal_negotiate_keyboard, INTERNAL)  /* a step of enter */
ROLLTUI_API(rolltui_terminal_new, PUBLIC)
ROLLTUI_API(rolltui_terminal_poll, PUBLIC)
ROLLTUI_API(rolltui_terminal_query_background, PUBLIC)
ROLLTUI_API(rolltui_terminal_query_ambiguous_wide, PUBLIC)  /* how wide this terminal draws an ambiguous glyph is a FACT a host must know before it draws, and only the terminal can answer */
ROLLTUI_API(rolltui_terminal_refresh_size, INTERNAL)  /* a step of the terminal's own loop */
ROLLTUI_API(rolltui_terminal_set_size, INTERNAL)  /* a run with no terminal is given its size */
ROLLTUI_API(rolltui_terminal_new_sized, INTERNAL)  /* …from before it enters */
ROLLTUI_API(rolltui_terminal_restore_now, INTERNAL)  /* a step of leave */
/* the operating system, behind rolltui_os.h: no host reaches it */
ROLLTUI_API(rolltui_os_monotonic_us, INTERNAL)
ROLLTUI_API(rolltui_os_config_home, INTERNAL)
ROLLTUI_API(rolltui_os_state_home, INTERNAL)
ROLLTUI_API(rolltui_os_release, INTERNAL)
ROLLTUI_API(rolltui_os_host_name, INTERNAL)
ROLLTUI_API(rolltui_os_pread, INTERNAL)
ROLLTUI_API(rolltui_os_monotonic_ms, INTERNAL)
ROLLTUI_API(rolltui_cond_init, INTERNAL)  /* the task pool's workers wait on one */
ROLLTUI_API(rolltui_cond_destroy, INTERNAL)
ROLLTUI_API(rolltui_cond_wait, INTERNAL)
ROLLTUI_API(rolltui_cond_signal, INTERNAL)
ROLLTUI_API(rolltui_cond_broadcast, INTERNAL)
ROLLTUI_API(rolltui_thread_start, INTERNAL)  /* a host starts work through rolltui_tasks_start */
ROLLTUI_API(rolltui_thread_join, INTERNAL)
ROLLTUI_API(rolltui_os_sleep_ms, INTERNAL)
ROLLTUI_API(rolltui_os_thread_self, INTERNAL)
ROLLTUI_API(rolltui_os_listen_reload, INTERNAL)  /* the run hears SIGUSR1 while it watches stores */
ROLLTUI_API(rolltui_os_take_reload, INTERNAL)  /* the run reads the stores again when SIGUSR1 came */
ROLLTUI_API(rolltui_os_child_spawn, INTERNAL)  /* a host starts a program through rolltui_child_spawn */
ROLLTUI_API(rolltui_os_child_refuse_pty, INTERNAL)  /* a test reaches the pipes a program falls back to without a pseudo-terminal */
ROLLTUI_API(rolltui_os_child_read, INTERNAL)
ROLLTUI_API(rolltui_os_child_write, INTERNAL)
ROLLTUI_API(rolltui_os_child_close_input, INTERNAL)
ROLLTUI_API(rolltui_os_child_resize, INTERNAL)
ROLLTUI_API(rolltui_os_child_done, INTERNAL)
ROLLTUI_API(rolltui_os_child_stop, INTERNAL)
ROLLTUI_API(rolltui_os_child_free, INTERNAL)
ROLLTUI_API(rolltui_os_child_wait_terminal, INTERNAL)  /* a host runs one through rolltui_run_program */
ROLLTUI_API(rolltui_os_open_with_system, INTERNAL)
ROLLTUI_API(rolltui_os_clipboard_native, INTERNAL)  /* a host copies through rolltui_clipboard_copy */
ROLLTUI_API(rolltui_os_clipboard_read, INTERNAL)  /* a host reads through rolltui_clipboard_paste */
ROLLTUI_API(rolltui_os_temp_file, INTERNAL)  /* a kitty on this machine is sent a picture's pixels in a temp file */
ROLLTUI_API(rolltui_os_open_terminal, INTERNAL)
ROLLTUI_API(rolltui_os_close_terminal, INTERNAL)
ROLLTUI_API(rolltui_os_localtime, INTERNAL)
ROLLTUI_API(rolltui_mutex_init, INTERNAL)
ROLLTUI_API(rolltui_mutex_destroy, INTERNAL)
ROLLTUI_API(rolltui_mutex_lock, INTERNAL)
ROLLTUI_API(rolltui_mutex_unlock, INTERNAL)
ROLLTUI_API(rolltui_os_stat, INTERNAL)
ROLLTUI_API(rolltui_os_mkdir, INTERNAL)
ROLLTUI_API(rolltui_os_rename_replace, INTERNAL)
ROLLTUI_API(rolltui_os_fopen, INTERNAL)
ROLLTUI_API(rolltui_os_getcwd, INTERNAL)
ROLLTUI_API(rolltui_os_exe_path, INTERNAL)
ROLLTUI_API(rolltui_os_pid, INTERNAL)
ROLLTUI_API(rolltui_os_open_read, INTERNAL)
ROLLTUI_API(rolltui_os_fstat, INTERNAL)
ROLLTUI_API(rolltui_os_read, INTERNAL)
ROLLTUI_API(rolltui_os_close, INTERNAL)
ROLLTUI_API(rolltui_os_dir_open, INTERNAL)
ROLLTUI_API(rolltui_os_dir_next, INTERNAL)
ROLLTUI_API(rolltui_os_dir_stat, INTERNAL)
ROLLTUI_API(rolltui_os_dir_hidden, INTERNAL)
ROLLTUI_API(rolltui_os_dir_close, INTERNAL)
ROLLTUI_API(rolltui_os_isatty, INTERNAL)
ROLLTUI_API(rolltui_os_headless_fds, INTERNAL)
ROLLTUI_API(rolltui_os_close_fd, INTERNAL)
ROLLTUI_API(rolltui_os_tty_open, INTERNAL)
ROLLTUI_API(rolltui_os_tty_close, INTERNAL)
ROLLTUI_API(rolltui_os_tty_is_tty, INTERNAL)
ROLLTUI_API(rolltui_os_tty_raw, INTERNAL)
ROLLTUI_API(rolltui_os_tty_restore, INTERNAL)
ROLLTUI_API(rolltui_os_tty_size, INTERNAL)
ROLLTUI_API(rolltui_os_tty_write, INTERNAL)
ROLLTUI_API(rolltui_os_tty_wait, INTERNAL)
ROLLTUI_API(rolltui_os_tty_read, INTERNAL)
ROLLTUI_API(rolltui_os_tty_wake, INTERNAL)
ROLLTUI_API(rolltui_os_tty_publish, INTERNAL)
ROLLTUI_API(rolltui_os_tty_withdraw, INTERNAL)
ROLLTUI_API(rolltui_os_tty_restore_now, INTERNAL)
ROLLTUI_API(rolltui_os_tty_install_handlers, INTERNAL)
ROLLTUI_API(rolltui_os_tty_take_resumed, INTERNAL)  /* the poll enters the terminal again after a stop from outside */
ROLLTUI_API(rolltui_os_stop_process, INTERNAL)  /* rolltui_run_suspend_job's stop */
ROLLTUI_API(rolltui_os_tty_listen_resize, INTERNAL)
ROLLTUI_API(rolltui_fatal, INTERNAL)  /* every stop in the library: the terminal put back, then the message */
ROLLTUI_API(rolltui_terminal_wake, PUBLIC)
ROLLTUI_API(rolltui_terminal_width, PUBLIC)
ROLLTUI_API(rolltui_terminal_write, PUBLIC)
ROLLTUI_API(rolltui_terminal_set_title, PUBLIC)  /* chat names what the conversation is doing */
ROLLTUI_API(rolltui_terminal_set_window_name, PUBLIC)  /* KEPT: a program named in tmux's window list, the old name put back on leaving */
ROLLTUI_API(rolltui_terminal_set_cell_pixels, PUBLIC)  /* KEPT: a host that measured the font itself, where the terminal's size is wrong */
ROLLTUI_API(rolltui_terminal_pending_ms, PUBLIC)  /* KEPT: a host's own loop waking when a held picture is due (rolltui_run does it itself) */
ROLLTUI_API(rolltui_terminal_record, PUBLIC)  /* KEPT: a host records a demo from its own script (ROLLTUI_RECORD does it for any) */
ROLLTUI_API(rolltui_terminal_set_outside_pictures, PUBLIC)  /* KEPT: the host told when to draw and clear an outside program's picture */
ROLLTUI_API(rolltui_terminal_report_cwd, PUBLIC)  /* dirktui reports the folder it is in */
ROLLTUI_API(rolltui_child_spawn, PUBLIC)  /* chat runs an approved command; dirktui checks for an update, hands a file to an app */
ROLLTUI_API(rolltui_child_free, PUBLIC)
ROLLTUI_API(rolltui_child_read, PUBLIC)  /* KEPT: a host reading a program's bytes as bytes (a binary, a protocol) rather than as lines */
ROLLTUI_API(rolltui_child_pump, PUBLIC)  /* chat streams a command's output */
ROLLTUI_API(rolltui_child_drained, PUBLIC)
ROLLTUI_API(rolltui_child_write, PUBLIC)  /* KEPT: a host feeding a program's input (a line editor bound to it) */
ROLLTUI_API(rolltui_child_close_input, PUBLIC)  /* KEPT: the end of what a host fed a program */
ROLLTUI_API(rolltui_child_resize, PUBLIC)  /* KEPT: a PTY program follows the window it is shown in */
ROLLTUI_API(rolltui_child_done, PUBLIC)
ROLLTUI_API(rolltui_child_wait, PUBLIC)  /* dirktui waits for a program it hands a file to */
ROLLTUI_API(rolltui_child_stop, PUBLIC)  /* chat stops a command on Escape */
ROLLTUI_API(rolltui_child_wait_ms, PUBLIC)
ROLLTUI_API(rolltui_open_with_system, PUBLIC)  /* dirktui's "open with the system" */
ROLLTUI_API(rolltui_clipboard_copy, PUBLIC)  /* dirktui and the studio copy */
ROLLTUI_API(rolltui_clipboard_paste, PUBLIC)  /* KEPT: a host's own paste key, which works over ssh from what the program copied */
ROLLTUI_API(rolltui_open_terminal, PUBLIC)  /* dirktui draws on the terminal while its stdout is the answer */
ROLLTUI_API(rolltui_close_terminal, PUBLIC)

/* rolltui_alloc.h + rolltui_map.h — two headers that were internal before the census covered
   every header under c/, and so were outside it. The census now takes in every
   header under c/, and these are their rows: INTERNAL by the same measure as the rest — the
   library's own storage primitives, reached by budget_test only (a test may). */
ROLLTUI_API(rolltui_fit, INTERNAL)
ROLLTUI_API(rolltui_grow, INTERNAL)
ROLLTUI_API(rolltui_grow_zeroed, INTERNAL)
ROLLTUI_API(rolltui_map_clear, INTERNAL)
ROLLTUI_API(rolltui_map_count, INTERNAL)
ROLLTUI_API(rolltui_map_get, INTERNAL)
ROLLTUI_API(rolltui_map_key_at, INTERNAL)
ROLLTUI_API(rolltui_map_mark, INTERNAL)
ROLLTUI_API(rolltui_map_marked_at, INTERNAL)
ROLLTUI_API(rolltui_map_put, INTERNAL)
ROLLTUI_API(rolltui_map_release, INTERNAL)
ROLLTUI_API(rolltui_map_remove_at, INTERNAL)
ROLLTUI_API(rolltui_map_take, INTERNAL)
ROLLTUI_API(rolltui_map_unmark_all, INTERNAL)
ROLLTUI_API(rolltui_map_value_at, INTERNAL)
ROLLTUI_API(rolltui_mem_realloc, INTERNAL)
ROLLTUI_API(rolltui_pack_add, INTERNAL)
ROLLTUI_API(rolltui_pack_alloc, INTERNAL)
ROLLTUI_API(rolltui_pack_begin, INTERNAL)

/* ---- the layout family's opaque handle and its seven doors ------------------------------- */
ROLLTUI_API(rolltui_layout_new, PUBLIC)
ROLLTUI_API(rolltui_layout_free, PUBLIC)
ROLLTUI_API(rolltui_layout_clone, PUBLIC)
ROLLTUI_API(rolltui_layout_base, PUBLIC)
ROLLTUI_API(rolltui_layout_fits, PUBLIC)  /* KEPT: a preset refused when it would not fit the screen */
ROLLTUI_API(rolltui_layout_actions, PUBLIC)
ROLLTUI_API(rolltui_layout_min_size, PUBLIC)
ROLLTUI_API(rolltui_layout_inline, PUBLIC)  /* a host makes its terminal inline for a layout that says so */
ROLLTUI_API(rolltui_layout_name, PUBLIC)
ROLLTUI_API(rolltui_layer_placement, PUBLIC)
ROLLTUI_API(rolltui_layer_id, PUBLIC)
ROLLTUI_API(rolltui_layer_focus, PUBLIC)  /* a host hands the focus back to the layout's own first window: dirktui */
ROLLTUI_API(rolltui_layout_node_id, PUBLIC)
ROLLTUI_API(rolltui_layout_node_is_window, PUBLIC)
ROLLTUI_API(rolltui_load_layout_text_into, INTERNAL)

/* ---- rolltui_context.h — THE SESSION: the registries and caches an app owns.
 * Public because a host makes one FIRST and everything else hangs off it. ---- */
ROLLTUI_API(rolltui_context_new, PUBLIC)
ROLLTUI_API(rolltui_context_free, PUBLIC)   /* rule 1's pair */
/* The kind registry's own new/free: INTERNAL, and the shape every subsystem the context owns
 * follows — the type stays private to its `.c`, the context holds only the pointer. */
ROLLTUI_API(rolltui_kind_registry_new, INTERNAL)
ROLLTUI_API(rolltui_kind_registry_free, INTERNAL)
ROLLTUI_API(rolltui_effect_registry_new, INTERNAL)
ROLLTUI_API(rolltui_effect_states_new, INTERNAL)
ROLLTUI_API(rolltui_effect_states_free, INTERNAL)
ROLLTUI_API(rolltui_effect_registry_free, INTERNAL)
ROLLTUI_API(rolltui_layout_cache_new, INTERNAL)
ROLLTUI_API(rolltui_layout_cache_free, INTERNAL)
ROLLTUI_API(rolltui_preset_domains_new, INTERNAL)
ROLLTUI_API(rolltui_preset_domains_free, INTERNAL)
ROLLTUI_API(rolltui_window_config_new, INTERNAL)
ROLLTUI_API(rolltui_window_config_free, INTERNAL)
ROLLTUI_API(rolltui_context_window_config, INTERNAL)
/* The session a Windows was made for — a host that holds a Windows can reach its context
 * without carrying a second pointer beside it, which is what the built-in widget factories do. */
ROLLTUI_API(rolltui_windows_context, PUBLIC)

/* ---- MODULES ADDED AFTER ROLLTUI STOOD ALONE, classified 2026-11-13; default INTERNAL, each group's reason above it ----
 * Every one is declared in an internal c/ header and reached by no host in this tree (dirktui, paint) or by the studio only. */
/* keys: the key decoder's own wiring: the terminal sets it */
ROLLTUI_API(rolltui_key_decoder_set_reply_sink, INTERNAL)
/* keys_editor: the keys editor's own controls, reached by the studio and the keys window */
ROLLTUI_API(rolltui_keys_editor_back, INTERNAL)
ROLLTUI_API(rolltui_keys_editor_set_show_presets, INTERNAL)
/* md_lines: the transcript's line store */
ROLLTUI_API(rolltui_md_lines_span_on, INTERNAL)
/* mermaid: diagrams drawn as text inside Markdown: the Markdown renderer's */
ROLLTUI_API(rolltui_mermaid_free, INTERNAL)
ROLLTUI_API(rolltui_mermaid_kind, INTERNAL)
ROLLTUI_API(rolltui_mermaid_line, INTERNAL)
ROLLTUI_API(rolltui_mermaid_line_count, INTERNAL)
ROLLTUI_API(rolltui_mermaid_new, INTERNAL)
ROLLTUI_API(rolltui_mermaid_render, INTERNAL)
ROLLTUI_API(rolltui_mermaid_width, INTERNAL)
/* preview: the file preview the picker draws: a host turns it on through the picker's options */
ROLLTUI_API(rolltui_fileinfo_mode_text, INTERNAL)
ROLLTUI_API(rolltui_fileinfo_size_text, INTERNAL)
ROLLTUI_API(rolltui_fileinfo_when_text, INTERNAL)
ROLLTUI_API(rolltui_filesig_racy, INTERNAL)
ROLLTUI_API(rolltui_filesig_read, INTERNAL)
ROLLTUI_API(rolltui_filesig_same, INTERNAL)
ROLLTUI_API(rolltui_preview_draw, INTERNAL)
ROLLTUI_API(rolltui_preview_free, INTERNAL)
ROLLTUI_API(rolltui_preview_kind, INTERNAL)
ROLLTUI_API(rolltui_preview_message, INTERNAL)
ROLLTUI_API(rolltui_preview_new, INTERNAL)
ROLLTUI_API(rolltui_preview_refresh, INTERNAL)
ROLLTUI_API(rolltui_preview_scroll_by, INTERNAL)
ROLLTUI_API(rolltui_preview_scroll_sideways, INTERNAL)
ROLLTUI_API(rolltui_preview_scroll_edge, INTERNAL)
ROLLTUI_API(rolltui_preview_scroll_extent, INTERNAL)
ROLLTUI_API(rolltui_preview_scroll_page, INTERNAL)
ROLLTUI_API(rolltui_preview_scroll_to, INTERNAL)
ROLLTUI_API(rolltui_preview_set_info, INTERNAL)
ROLLTUI_API(rolltui_preview_set_path, INTERNAL)
ROLLTUI_API(rolltui_preview_path_is, INTERNAL)
ROLLTUI_API(rolltui_preview_set_layout, INTERNAL)
ROLLTUI_API(rolltui_preview_set_syntax, INTERNAL)
ROLLTUI_API(rolltui_preview_set_tasks, INTERNAL)  /* the picker lends its preview the workers */
ROLLTUI_API(rolltui_preview_set_now, INTERNAL)
ROLLTUI_API(rolltui_preview_wake_ms, INTERNAL)  /* the picker wakes for "loading" */
/* regex: the matcher a language file is written in: the syntax engine's, nobody else's */
ROLLTUI_API(rolltui_regex_can_be_empty, INTERNAL)
ROLLTUI_API(rolltui_regex_compile, INTERNAL)
ROLLTUI_API(rolltui_regex_compile_flags, INTERNAL)
ROLLTUI_API(rolltui_regex_first_byte, INTERNAL)
ROLLTUI_API(rolltui_regex_free, INTERNAL)
ROLLTUI_API(rolltui_regex_groups, INTERNAL)
ROLLTUI_API(rolltui_regex_match_at, INTERNAL)
ROLLTUI_API(rolltui_regex_match_memo, INTERNAL)
ROLLTUI_API(rolltui_regex_memo_slots, INTERNAL)
ROLLTUI_API(rolltui_regex_uses_refs, INTERNAL)
/* syntax: source colouring's engine and its languages: reached through the picker's preview and the Markdown seam, never by a host */
ROLLTUI_API(rolltui_highlight_free, INTERNAL)
ROLLTUI_API(rolltui_highlight_line, INTERNAL)
ROLLTUI_API(rolltui_highlight_line_count, INTERNAL)
ROLLTUI_API(rolltui_highlight_new, INTERNAL)
ROLLTUI_API(rolltui_highlight_run, INTERNAL)
ROLLTUI_API(rolltui_syntax_add, INTERNAL)
ROLLTUI_API(rolltui_syntax_add_dir, INTERNAL)
ROLLTUI_API(rolltui_syntax_add_shipped, INTERNAL)
ROLLTUI_API(rolltui_syntax_class_name, INTERNAL)
ROLLTUI_API(rolltui_syntax_find_file, INTERNAL)
ROLLTUI_API(rolltui_syntax_find_name, INTERNAL)
ROLLTUI_API(rolltui_syntax_flags, INTERNAL)
ROLLTUI_API(rolltui_syntax_free, INTERNAL)
ROLLTUI_API(rolltui_syntax_share, INTERNAL)  /* a preview read on a worker colours what it read with the languages it holds */
ROLLTUI_API(rolltui_syntax_language_count, INTERNAL)
ROLLTUI_API(rolltui_syntax_language_name, INTERNAL)
ROLLTUI_API(rolltui_syntax_md_free, INTERNAL)
ROLLTUI_API(rolltui_syntax_md_highlight, INTERNAL)
ROLLTUI_API(rolltui_syntax_md_new, INTERNAL)
ROLLTUI_API(rolltui_syntax_new, INTERNAL)
ROLLTUI_API(rolltui_syntax_new_standard, INTERNAL)
ROLLTUI_API(rolltui_syntax_role, INTERNAL)
/* termfacts: what the terminal is, found and remembered by the library: a host reads the result through rolltui_terminal_facts */
ROLLTUI_API(rolltui_termcache_forget, INTERNAL)
ROLLTUI_API(rolltui_termcache_fresh, INTERNAL)
ROLLTUI_API(rolltui_termcache_load, INTERNAL)
ROLLTUI_API(rolltui_termcache_path, INTERNAL)
ROLLTUI_API(rolltui_termcache_store, INTERNAL)
ROLLTUI_API(rolltui_termenv_multiplexed, INTERNAL)
ROLLTUI_API(rolltui_termenv_no_24bit_known, INTERNAL)
ROLLTUI_API(rolltui_termenv_read, INTERNAL)
ROLLTUI_API(rolltui_termfacts_active_mode, INTERNAL)
ROLLTUI_API(rolltui_termfacts_active_wide, INTERNAL)
ROLLTUI_API(rolltui_termfacts_active_focused, INTERNAL)
ROLLTUI_API(rolltui_termfacts_active_split, INTERNAL)
ROLLTUI_API(rolltui_termfacts_apply_replies, INTERNAL)
ROLLTUI_API(rolltui_termfacts_apply_tmux, INTERNAL)
ROLLTUI_API(rolltui_termfacts_clamp_depth, INTERNAL)
ROLLTUI_API(rolltui_termfacts_exe_identity, INTERNAL)
ROLLTUI_API(rolltui_termfacts_fingerprint, INTERNAL)
ROLLTUI_API(rolltui_termfacts_from_env, INTERNAL)
ROLLTUI_API(rolltui_termfacts_set_active, INTERNAL)
ROLLTUI_API(rolltui_termfacts_set_active_mode, INTERNAL)
ROLLTUI_API(rolltui_termfacts_set_active_wide, INTERNAL)
ROLLTUI_API(rolltui_termfacts_set_active_focused, INTERNAL)
ROLLTUI_API(rolltui_termfacts_set_active_split, INTERNAL)
ROLLTUI_API(rolltui_termfacts_set_active_erase, INTERNAL)
ROLLTUI_API(rolltui_termfacts_active_erase, INTERNAL)  /* the renderer's short forms stand on it */
ROLLTUI_API(rolltui_termprobe_queries, INTERNAL)
ROLLTUI_API(rolltui_termreplies_take, INTERNAL)
/* theme_editor: the theme editor's own controls, reached by the studio and the theme window */
ROLLTUI_API(rolltui_theme_editor_back, INTERNAL)
/* widget_picker: the picker's own state, driven through its events and options */
ROLLTUI_API(rolltui_picker_preview_focused, INTERNAL)
ROLLTUI_API(rolltui_picker_refresh, INTERNAL)
/* ---- declared in rolltui.h and never rowed: the public ones a host reaches, and two kept on a stated reason ---- */
ROLLTUI_API(rolltui_frame_put_swatch, PUBLIC)  /* KEPT: the swatch every colour value is drawn as (a theme role, a menu colour input, a painter's ink), for a host's own widget kind that shows one */
ROLLTUI_API(rolltui_rows_add_colour, PUBLIC)  /* a status row whose value is a colour, shown as one (paint) */
ROLLTUI_API(rolltui_style_on, PUBLIC)  /* what `none` in a theme means for a role drawn over a ground (dirktui) */
ROLLTUI_API(rolltui_terminal_cache_path, PUBLIC)  /* where the remembered terminal facts live, to show or clear (dirktui) */
ROLLTUI_API(rolltui_terminal_facts, PUBLIC)  /* what the terminal is, found out once by the library (dirktui, paint, the studio) */
ROLLTUI_API(rolltui_terminal_present, PUBLIC)  /* KEPT: a frame to the terminal at the depth it draws, as one call whose depth cannot be forgotten — what a host running its own loop presents with (rolltui_run does it for the rest) */
ROLLTUI_API(rolltui_terminal_commit, PUBLIC)  /* an inline host's finished rows, into the terminal's own history */
ROLLTUI_API(rolltui_terminal_reprobe, PUBLIC)  /* ask the terminal again (dirktui) */
ROLLTUI_API(rolltui_terminal_set_depth, PUBLIC)  /* the person's own colour depth over the one found out (dirktui) */
ROLLTUI_API(rolltui_windows_set_keys_editor_show_presets, PUBLIC)  /* whether the keys editor offers presets (dirktui) */
