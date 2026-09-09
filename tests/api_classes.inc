/*
 * rolltui/tests/api_classes.inc — THE CLASS OF EVERY PUBLIC FUNCTION.
 *
 * This is the library's intentional public API design as data a test enforces: every
 * function declared in a public header has ONE class, with the reason written where it is not the
 * module's default. The census that drives it is `public_header_test`'s section 6, and the classes
 * are what `rolltui.h` is written from — never a prediction of it.
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
 *
 * `ROLLTUI_PUBLIC_ONLY_TESTS` names the programs that build against the definition ALONE, and it
 * is asserted non-empty with the pure-C consumer required: the instrument is not the opt-in list,
 * it is the suites that stay OUT of it.
 *
 * Reach is measured, not remembered: roll = src/ + include/ + tools/bench/ (its own bench builds
 * frames as a host does); the one remaining tool is `rolltui-paint`; the library = the .c files
 * under rolltui/c/ beyond the function's own definition. **Reach is the INPUT; a stated reason is
 * the DECISION** — that is the whole lesson of the three revisions above. The rules the test
 * enforces: a function roll or paint reaches must be PUBLIC; one a public-only suite reaches must
 * not be INTERNAL; every declared function has a row, and every row is declared; and a row whose
 * only justification would be a test's reach FAILS.
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

/* ---- rolltui_frame_ops.h — drawing into the cell grid; default PUBLIC -------------------- */
ROLLTUI_API(rolltui_draw_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_draw_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_frame_fill, PUBLIC)
ROLLTUI_API(rolltui_frame_text_width, PUBLIC)
ROLLTUI_API(rolltui_frame_put_fields, PUBLIC)
ROLLTUI_API(rolltui_frame_put_text, PUBLIC)
ROLLTUI_API(rolltui_frame_tint, INTERNAL)

/* ---- rolltui_render.h — the grid to bytes; default PUBLIC -------------------------------- */
ROLLTUI_API(rolltui_frame_to_text, PUBLIC)
ROLLTUI_API(rolltui_render_diff, INTERNAL)  /* a step of rolltui_swap_present */
ROLLTUI_API(rolltui_render_full, INTERNAL)

/* ---- rolltui_screen.h — the cell grid; default PUBLIC ------------------------------------ */
ROLLTUI_API(rolltui_frame_cell, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_clear, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_clone, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_cursor, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_equal, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_free, INTERNAL)  /* Phase 24: the pair of `_new`, and it went with it */
ROLLTUI_API(rolltui_frame_glyph, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_height, INTERNAL)  /* Phase 24: the pair of `_width`, and it went with it */
ROLLTUI_API(rolltui_frame_link, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_link_id, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_mark, PUBLIC)  /* KEPT: matching part — `rolltui_effects_apply` is consumer-used, and this is the WIDGET half of that contract — motion is the theme's, a state is the widget's */
ROLLTUI_API(rolltui_frame_mark_at, INTERNAL)  /* a step of mark */
ROLLTUI_API(rolltui_frame_mark_count, PUBLIC)
ROLLTUI_API(rolltui_frame_new, INTERNAL)  /* Phase 24: a host gets its frame from `rolltui_swap_begin`, never by building one. Old KEPT reason was a meta-test's reach */
ROLLTUI_API(rolltui_frame_put, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_reset, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_set_cursor, PUBLIC)
ROLLTUI_API(rolltui_frame_set_style, INTERNAL)  /* a cell-level frame primitive; a host draws through rolltui_frame_ops.h */
ROLLTUI_API(rolltui_frame_width, INTERNAL)  /* Phase 24: a widget is handed a resolved node's rect and the library clips; nothing needs the frame's own bounds. Old KEPT reason was a meta-test's reach */

/* ---- rolltui_swap.h — the double buffer, the newest entry point; default PUBLIC ---------- */
ROLLTUI_API(rolltui_swap_begin, PUBLIC)
ROLLTUI_API(rolltui_swap_free, PUBLIC)
ROLLTUI_API(rolltui_swap_front, PUBLIC)
ROLLTUI_API(rolltui_swap_invalidate, PUBLIC)
ROLLTUI_API(rolltui_swap_new, PUBLIC)
ROLLTUI_API(rolltui_swap_present, PUBLIC)

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
ROLLTUI_API(rolltui_effect_map_clone, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_count, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_empty, PUBLIC)
ROLLTUI_API(rolltui_effect_map_equal, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
ROLLTUI_API(rolltui_effect_map_free, PUBLIC)
ROLLTUI_API(rolltui_effect_map_new, INTERNAL)
ROLLTUI_API(rolltui_effect_register, INTERNAL)  /* an effect step; a host marks a span and the theme maps it */
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
ROLLTUI_API(rolltui_color_to_string, PUBLIC) /* KEPT: the other half of that round trip — a host that stores a colour writes it back to its own file */
ROLLTUI_API(rolltui_detect_color_depth, PUBLIC)
ROLLTUI_API(rolltui_mode_for_background, PUBLIC)
ROLLTUI_API(rolltui_parse_osc11_reply, INTERNAL)  /* a theme-loading step */
ROLLTUI_API(rolltui_sgr, INTERNAL)  /* Phase 24: no consumer writes an escape sequence; `rolltui_swap_present` produces the bytes. Rule 3(a)'s prose is amended rather than a function kept to illustrate it */
ROLLTUI_API(rolltui_theme_builtin_count, INTERNAL)  /* a theme-loading step */
ROLLTUI_API(rolltui_theme_builtin_fill, PUBLIC)
ROLLTUI_API(rolltui_theme_builtin_name, INTERNAL)  /* a theme-loading step */
ROLLTUI_API(rolltui_theme_default_vocab, PUBLIC)
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
ROLLTUI_API(rolltui_bindings_add_row, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_bind, PUBLIC)
ROLLTUI_API(rolltui_bindings_breaks_enter_rule, INTERNAL)  /* a step of bind */
ROLLTUI_API(rolltui_bindings_chord_at, INTERNAL)
ROLLTUI_API(rolltui_bindings_chord_count, INTERNAL)
ROLLTUI_API(rolltui_bindings_chords_text, INTERNAL)
ROLLTUI_API(rolltui_bindings_clear, INTERNAL)
ROLLTUI_API(rolltui_bindings_clone, PUBLIC)
ROLLTUI_API(rolltui_bindings_declare, PUBLIC)
ROLLTUI_API(rolltui_bindings_default, PUBLIC)
ROLLTUI_API(rolltui_bindings_description, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_dump_json, PUBLIC)
ROLLTUI_API(rolltui_bindings_equal, INTERNAL)
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
ROLLTUI_API(rolltui_bindings_report_set_error, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_summary, PUBLIC)  /* Phase 26: its INTERNAL reason argued the opposite of what it concluded — a host that loads its own bindings FILE is the case it named. Phase 21 wall 6: the explorer hand-wrote six loops for it. */
ROLLTUI_API(rolltui_bindings_row_at, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_row_count, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_scope_of, INTERNAL)
ROLLTUI_API(rolltui_bindings_set_enter_rule, INTERNAL)  /* a step of loading or building a table; a host loads a file or clones the default */
ROLLTUI_API(rolltui_bindings_suggest, INTERNAL)  /* a step of declare */
ROLLTUI_API(rolltui_bindings_unbind, INTERNAL)
ROLLTUI_API(rolltui_bindings_undeclare_others, INTERNAL)  /* a step of declare (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_chord_display, INTERNAL)
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
ROLLTUI_API(rolltui_key_deliverable, INTERNAL)  /* a deliverability primitive; the library hands its own reason callback to a domain */
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
ROLLTUI_API(rolltui_draw_border, INTERNAL)  /* a step of loading, saving or resolving a layout */
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
ROLLTUI_API(rolltui_dir_read, PUBLIC)  /* the aligned probe needs it: a host writing a rich browser needs the same four things the picker does, and must not re-implement them */
ROLLTUI_API(rolltui_dir_list_release, PUBLIC)  /* release half of the pair */  /* an app that keeps its screen in files cannot start until it knows where its own files are; every host built that way needs this and three of them were building the path themselves */
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
ROLLTUI_API(rolltui_window_stack_captured, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_window_stack_compose, PUBLIC)
ROLLTUI_API(rolltui_window_stack_cycle_focus, INTERNAL)  /* what the stack does on its own action */
ROLLTUI_API(rolltui_window_stack_depth, PUBLIC)
ROLLTUI_API(rolltui_window_stack_find, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_window_stack_focus, PUBLIC)
ROLLTUI_API(rolltui_window_stack_focus_layer, INTERNAL)  /* a step of loading, saving or resolving a layout */
ROLLTUI_API(rolltui_window_stack_focused, PUBLIC)
ROLLTUI_API(rolltui_window_stack_free, PUBLIC)
ROLLTUI_API(rolltui_window_stack_has_popup, PUBLIC)
ROLLTUI_API(rolltui_window_stack_action_popup, PUBLIC)
ROLLTUI_API(rolltui_windows_set_picker_dir, PUBLIC)  /* a picker with nowhere to start looks at the working directory, which is almost never what a host means */
ROLLTUI_API(rolltui_windows_picker_taken, PUBLIC)  /* the answer half: a chosen path is what the whole kind is for */  /* three hosts wrote the same six lines; the tell says fix the API rather than ship the wrapper a fourth time */
ROLLTUI_API(rolltui_window_stack_layer, PUBLIC)
ROLLTUI_API(rolltui_window_stack_new, PUBLIC)
ROLLTUI_API(rolltui_window_stack_pop, PUBLIC)
ROLLTUI_API(rolltui_window_stack_push, INTERNAL)
ROLLTUI_API(rolltui_window_stack_push_popup, PUBLIC)
ROLLTUI_API(rolltui_window_stack_resolve, PUBLIC)
ROLLTUI_API(rolltui_window_stack_route, PUBLIC)
ROLLTUI_API(rolltui_window_stack_set_base, PUBLIC)

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
ROLLTUI_API(rolltui_scroll_first_for_cell, INTERNAL)  /* scrollbar arithmetic (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_scroll_thumb, INTERNAL)  /* scrollbar arithmetic (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_context_add_help_scope, PUBLIC)
ROLLTUI_API(rolltui_context_add_menu, PUBLIC)
ROLLTUI_API(rolltui_windows_at, INTERNAL)  /* a hit-test the stack already routes */
ROLLTUI_API(rolltui_windows_autosize, PUBLIC)
ROLLTUI_API(rolltui_windows_bind_document, PUBLIC)
ROLLTUI_API(rolltui_windows_bind_note, PUBLIC)
ROLLTUI_API(rolltui_windows_bind_rows, PUBLIC)
ROLLTUI_API(rolltui_windows_bind_sample_document, PUBLIC)
ROLLTUI_API(rolltui_windows_bind_submit, PUBLIC)
ROLLTUI_API(rolltui_windows_bindings, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_call_note, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_call_rows, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_call_submit, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_context_clear_help_scopes, PUBLIC)
ROLLTUI_API(rolltui_windows_content_at, PUBLIC)
ROLLTUI_API(rolltui_windows_default_roles, PUBLIC)
ROLLTUI_API(rolltui_scrollbar_glyphs_default, INTERNAL)  /* the shipped capsule; a host never asks for it, because a theme's answer always arrives complete */
ROLLTUI_API(rolltui_context_set_scrollbar_glyphs, PUBLIC)  /* the look of the thumb is a theme's, and this is where a theme's answer lands */
ROLLTUI_API(rolltui_theme_scrollbar_glyphs, PUBLIC)  /* the read half: every host resolving a theme reads its glyphs beside its styles */
ROLLTUI_API(rolltui_windows_scrollbar_glyphs, INTERNAL)  /* what the draw path reads; no host asks what is in force, it just set it */
ROLLTUI_API(rolltui_windows_dir, INTERNAL)
ROLLTUI_API(rolltui_windows_document, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_draw, PUBLIC)
ROLLTUI_API(rolltui_windows_env, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_free, PUBLIC)
ROLLTUI_API(rolltui_windows_handle, PUBLIC)
ROLLTUI_API(rolltui_windows_has_rows, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_has_submit, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_help_lead, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_help_note, INTERNAL)  /* only the library's own .c reaches it */
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
ROLLTUI_API(rolltui_input_kind_process_event, PUBLIC)
ROLLTUI_API(rolltui_input_max_rows, INTERNAL)  /* Phase 24: its old KEPT reason said "a host applies it too" and no host does — measured 0 consumer files */
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
ROLLTUI_API(rolltui_document_copy, PUBLIC)  /* KEPT: a public C++ member in the definition calls it, so a C consumer must be able to
   perform the same operation. No reach bucket can see this; only compiling the definition
   can, which is how every row with this reason was found. */
ROLLTUI_API(rolltui_document_count, INTERNAL)  /* a public type's read API */
ROLLTUI_API(rolltui_document_release, PUBLIC)  /* KEPT: rule 1's pair: roll holds a RolltuiDocument and C has no destructor */

/* ---- rolltui_input.h — the input widget: a host sets, selects, undoes and reads; default PUBLIC --- */
ROLLTUI_API(rolltui_input_can_redo, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_can_undo, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_caret, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
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
ROLLTUI_API(rolltui_input_history_at, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_history_count, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
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
ROLLTUI_API(rolltui_input_push_history, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_redo, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_rows, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_rows_for, INTERNAL)  /* a step of the input's layout (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_select_all, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_selected_text, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_selection, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_set_caret, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_set_copy, PUBLIC)
ROLLTUI_API(rolltui_input_set_options, PUBLIC)
ROLLTUI_API(rolltui_input_set_text, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_text, PUBLIC)
ROLLTUI_API(rolltui_input_top_row, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_undo, INTERNAL)  /* the input widget's own editing step; a host sends events and reads rolltui_input_text */
ROLLTUI_API(rolltui_input_word_left_of, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_word_right_of, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_str_set, PUBLIC)

/* ---- rolltui_menu.h — the menu widget; default PUBLIC ------------------------------------ */
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
ROLLTUI_API(rolltui_menu_default_actions, PUBLIC)
ROLLTUI_API(rolltui_menu_draw, INTERNAL)
ROLLTUI_API(rolltui_menu_dump_json, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_menu_edit_reason, INTERNAL)
ROLLTUI_API(rolltui_menu_editing, INTERNAL)
ROLLTUI_API(rolltui_menu_editor, INTERNAL)
ROLLTUI_API(rolltui_menu_event_release, PUBLIC)
ROLLTUI_API(rolltui_menu_filter, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_menu_find, PUBLIC)
ROLLTUI_API(rolltui_menu_flat_count, INTERNAL)  /* the palette's own flattening (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_menu_flat_label, INTERNAL)  /* the palette's own flattening (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_menu_free, INTERNAL)
ROLLTUI_API(rolltui_menu_handle, PUBLIC)
ROLLTUI_API(rolltui_menu_item_actions, PUBLIC)  /* KEPT: matching part — `rolltui_menu_parse_json` fills an item tree, and this is how a host checks the action ids in the tree it just parsed */
ROLLTUI_API(rolltui_menu_layout, INTERNAL)
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
ROLLTUI_API(rolltui_menu_selected, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_menu_selected_item, INTERNAL)
ROLLTUI_API(rolltui_menu_set_checked, INTERNAL)
ROLLTUI_API(rolltui_menu_set_enabled, PUBLIC)
ROLLTUI_API(rolltui_menu_set_options, PUBLIC)
ROLLTUI_API(rolltui_menu_set_options_struct, INTERNAL)
ROLLTUI_API(rolltui_menu_set_palette, PUBLIC)
ROLLTUI_API(rolltui_menu_set_root, INTERNAL)
ROLLTUI_API(rolltui_menu_set_validator_fn, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */
ROLLTUI_API(rolltui_menu_set_value, PUBLIC)
ROLLTUI_API(rolltui_menu_visible, INTERNAL)  /* the menu widget's own navigation state; a host drives it with events */

/* ---- rolltui_menu_tree.h — the menu item tree a host builds and walks; default PUBLIC ---- */
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
ROLLTUI_API(rolltui_menu_item_release, INTERNAL)  /* rule 1: a public type's lifecycle; only the library reaches it today */
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

/* ---- rolltui_transcript.h — the transcript widget; default PUBLIC ------------------------ */
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
ROLLTUI_API(rolltui_transcript_total_lines, INTERNAL)
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
ROLLTUI_API(rolltui_json_as_string, INTERNAL)
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

/* ---- rolltui_presets.h — the preset stores and domains; default PUBLIC ------------------- */
ROLLTUI_API(rolltui_bindings_preset_domain_init, INTERNAL)  /* the bring-your-own-vocabulary rung */
ROLLTUI_API(rolltui_bindings_preset_report_clean, PUBLIC)
ROLLTUI_API(rolltui_bindings_preset_report_release, PUBLIC)
ROLLTUI_API(rolltui_bindings_preset_report_summary, PUBLIC)  /* KEPT: matching part — its own `_clean` and `rolltui_theme_preset_report_summary` are consumer-used — asking whether a file is clean but not WHAT is wrong, for one domain of three, is a hole (Phase 21 wall E5/E6) */
ROLLTUI_API(rolltui_layout_preset_domain_init, INTERNAL)  /* the bring-your-own-vocabulary rung */
ROLLTUI_API(rolltui_layout_preset_report_clean, PUBLIC)
ROLLTUI_API(rolltui_layout_preset_report_release, PUBLIC)
ROLLTUI_API(rolltui_layout_preset_report_summary, PUBLIC)  /* KEPT: matching part — its own `_clean` and `rolltui_theme_preset_report_summary` are consumer-used — same hole, same set of three domains */
ROLLTUI_API(rolltui_preset_domain, INTERNAL)
ROLLTUI_API(rolltui_preset_domain_theme, PUBLIC)
ROLLTUI_API(rolltui_preset_domain_layout, PUBLIC)
ROLLTUI_API(rolltui_preset_domain_bindings, PUBLIC)
ROLLTUI_API(rolltui_preset_domain_name, INTERNAL)
ROLLTUI_API(rolltui_preset_domain_release, INTERNAL)  /* rule 1: the pair */
ROLLTUI_API(rolltui_preset_is_shipped, PUBLIC)
ROLLTUI_API(rolltui_preset_json_names_in, INTERNAL)  /* a step of list (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_preset_list_release, PUBLIC)
ROLLTUI_API(rolltui_preset_looks_like_path, INTERNAL)  /* the store already answers a bad name with a sentence */
ROLLTUI_API(rolltui_preset_read_file, PUBLIC)
ROLLTUI_API(rolltui_preset_report_summary, INTERNAL)  /* the generic composer; the per-domain summaries are the API (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_preset_save_result_text, PUBLIC)
ROLLTUI_API(rolltui_preset_shipped, PUBLIC)
ROLLTUI_API(rolltui_preset_shipped_names, PUBLIC)
ROLLTUI_API(rolltui_preset_shipped_text, PUBLIC)
ROLLTUI_API(rolltui_preset_store_edit, PUBLIC)
ROLLTUI_API(rolltui_preset_store_free, PUBLIC)
ROLLTUI_API(rolltui_preset_store_get, PUBLIC)
ROLLTUI_API(rolltui_preset_store_label, PUBLIC)
ROLLTUI_API(rolltui_preset_store_last_error, PUBLIC)  /* KEPT: matching part — `rolltui_preset_store_edit` is consumer-used, persists, and cannot report a failed autosave any other way */
ROLLTUI_API(rolltui_preset_store_list, PUBLIC)
ROLLTUI_API(rolltui_preset_store_load, PUBLIC)
ROLLTUI_API(rolltui_preset_store_modified, PUBLIC)
ROLLTUI_API(rolltui_preset_store_new, PUBLIC)
ROLLTUI_API(rolltui_preset_store_origin, PUBLIC)
ROLLTUI_API(rolltui_preset_store_preset_path, PUBLIC)
ROLLTUI_API(rolltui_preset_store_save_as, PUBLIC)
ROLLTUI_API(rolltui_preset_store_add, PUBLIC)  /* three hosts hold stores; without it each writes its own read-file-guess-domain-copy-in */
ROLLTUI_API(rolltui_preset_store_set_working, INTERNAL)
ROLLTUI_API(rolltui_preset_store_start, PUBLIC)
ROLLTUI_API(rolltui_preset_store_value_free, PUBLIC)
ROLLTUI_API(rolltui_preset_store_version, PUBLIC)
ROLLTUI_API(rolltui_preset_store_working, PUBLIC)
ROLLTUI_API(rolltui_preset_store_working_path, PUBLIC)
ROLLTUI_API(rolltui_preset_valid_name, INTERNAL)  /* the store already answers a bad name with a sentence */
ROLLTUI_API(rolltui_preset_working_value, PUBLIC)
ROLLTUI_API(rolltui_preset_write_file_atomic, PUBLIC)

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
ROLLTUI_API(rolltui_terminal_free, PUBLIC)
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
ROLLTUI_API(rolltui_terminal_restore_now, INTERNAL)  /* a step of leave */
ROLLTUI_API(rolltui_terminal_wake, PUBLIC)
ROLLTUI_API(rolltui_terminal_width, PUBLIC)
ROLLTUI_API(rolltui_terminal_write, PUBLIC)

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
ROLLTUI_API(rolltui_layout_actions, PUBLIC)
ROLLTUI_API(rolltui_layout_min_size, PUBLIC)
ROLLTUI_API(rolltui_layout_name, PUBLIC)
ROLLTUI_API(rolltui_layer_placement, PUBLIC)
ROLLTUI_API(rolltui_layer_id, PUBLIC)
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
