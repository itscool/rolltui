/*
 * rolltui/tests/api_classes.inc — THE CLASS OF EVERY PUBLIC FUNCTION (Phase 19 m1, 2026-09-06).
 *
 * This is the "intentional public API design" the user asked for, as data a test enforces: every
 * function declared in a public header has ONE class, with the reason written where it is not the
 * module's default. The census that drives it is `public_header_test`'s section 6, and the classes
 * are what Phase 19 m2 writes `rolltui.h` from — never a prediction of it.
 *
 *   PUBLIC       a host (roll) reaches it, or a tool reaches it in a public module, or a stated
 *                reason keeps it (rule 1's pairs; the leak gauge; the bring-your-own rungs; the
 *                complete API of a public handle that a test stands in for a host to exercise).
 *   TOOL_FACING  a tool (the studio, paint, an editor) reaches it and roll does not — the third
 *                category Phase 17 m4 predicted: undo, theme analysis, the theme generator.
 *   INTERNAL     only the library's own .c reaches it, or it is a step of a public operation (a
 *                loader's report builder, a parser step, a handler's primitive, a shutdown hook the
 *                library registers itself). A test may reach one through an explicit opt-in.
 *   DELETE       reached by nothing, anywhere, and not a public type's lifecycle. Removed in m3;
 *                a row with this class after m3 fails the test.
 *
 * Reach is measured, not remembered: roll = src/ + include/; tools = rolltui/tools/; tests =
 * tests/ + rolltui/tests/; the library = the .c files under rolltui/c/ beyond the function's own definition; a
 * header's inline C++ member counts as a consumer's reach. The rules the test enforces: a function
 * roll reaches must be PUBLIC; one a tool reaches must not be INTERNAL; every declared function
 * has a row, and every row is declared.
 */

/* ---- rolltui_geom.h — RolltuiRect, the vocabulary every layout speaks; default PUBLIC ---- */
ROLLTUI_API(rolltui_rect_intersect, PUBLIC)  /* geometry every host does */

/* ---- rolltui_str.h — RolltuiStr and RolltuiStrList: the text vocabulary every signature speaks; default PUBLIC ---- */
ROLLTUI_API(rolltui_ptrvec_clear, PUBLIC)  /* rule 1: a public type's lifecycle, reached by nothing today */
ROLLTUI_API(rolltui_ptrvec_free, INTERNAL)  /* the library's own vector */
ROLLTUI_API(rolltui_ptrvec_insert, INTERNAL)  /* the library's own vector */
ROLLTUI_API(rolltui_ptrvec_push, INTERNAL)  /* the library's own vector */
ROLLTUI_API(rolltui_ptrvec_take, INTERNAL)  /* the library's own vector */
ROLLTUI_API(rolltui_str_append, PUBLIC)  /* the string type's own API */
ROLLTUI_API(rolltui_str_append_str, PUBLIC)  /* the string type's own API */
ROLLTUI_API(rolltui_str_clear, PUBLIC)
ROLLTUI_API(rolltui_str_eq, PUBLIC)
ROLLTUI_API(rolltui_str_free, PUBLIC)
ROLLTUI_API(rolltui_str_get, PUBLIC)
ROLLTUI_API(rolltui_str_list_add, PUBLIC)  /* the list's own API */
ROLLTUI_API(rolltui_str_list_clear, PUBLIC)  /* the list's own API */
ROLLTUI_API(rolltui_str_list_put, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_str_list_release, PUBLIC)
ROLLTUI_API(rolltui_str_move, PUBLIC)  /* the string type's own API */
ROLLTUI_API(rolltui_str_put, PUBLIC)

/* ---- rolltui_style.h — RolltuiStyle and the role list; default PUBLIC ---- */
ROLLTUI_API(rolltui_role_from_name, PUBLIC)
ROLLTUI_API(rolltui_role_name, PUBLIC)

/* ---- rolltui_frame_ops.h — drawing into the cell grid; default PUBLIC ---- */
ROLLTUI_API(rolltui_draw_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_draw_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_frame_fill, PUBLIC)
ROLLTUI_API(rolltui_frame_put_text, PUBLIC)
ROLLTUI_API(rolltui_frame_tint, PUBLIC)

/* ---- rolltui_render.h — the grid to bytes; default PUBLIC ---- */
ROLLTUI_API(rolltui_frame_to_text, PUBLIC)
ROLLTUI_API(rolltui_render_diff, PUBLIC)
ROLLTUI_API(rolltui_render_full, PUBLIC)

/* ---- rolltui_screen.h — the cell grid; default PUBLIC ---- */
ROLLTUI_API(rolltui_frame_cell, PUBLIC)
ROLLTUI_API(rolltui_frame_clear, PUBLIC)
ROLLTUI_API(rolltui_frame_clone, PUBLIC)
ROLLTUI_API(rolltui_frame_cursor, PUBLIC)
ROLLTUI_API(rolltui_frame_equal, PUBLIC)
ROLLTUI_API(rolltui_frame_free, PUBLIC)
ROLLTUI_API(rolltui_frame_glyph, PUBLIC)
ROLLTUI_API(rolltui_frame_height, PUBLIC)
ROLLTUI_API(rolltui_frame_link, PUBLIC)
ROLLTUI_API(rolltui_frame_link_id, PUBLIC)
ROLLTUI_API(rolltui_frame_mark, PUBLIC)
ROLLTUI_API(rolltui_frame_mark_at, INTERNAL)  /* a step of mark */
ROLLTUI_API(rolltui_frame_mark_count, PUBLIC)
ROLLTUI_API(rolltui_frame_new, PUBLIC)
ROLLTUI_API(rolltui_frame_put, PUBLIC)
ROLLTUI_API(rolltui_frame_reset, PUBLIC)
ROLLTUI_API(rolltui_frame_set_cursor, PUBLIC)
ROLLTUI_API(rolltui_frame_set_style, PUBLIC)
ROLLTUI_API(rolltui_frame_width, PUBLIC)

/* ---- rolltui_swap.h — the double buffer, the newest entry point; default PUBLIC ---- */
ROLLTUI_API(rolltui_swap_begin, PUBLIC)
ROLLTUI_API(rolltui_swap_free, PUBLIC)
ROLLTUI_API(rolltui_swap_front, PUBLIC)
ROLLTUI_API(rolltui_swap_invalidate, PUBLIC)
ROLLTUI_API(rolltui_swap_new, PUBLIC)
ROLLTUI_API(rolltui_swap_present, PUBLIC)

/* ---- rolltui_diff.h — diff highlighting a host draws; default PUBLIC ---- */
ROLLTUI_API(rolltui_diff_default_roles, PUBLIC)
ROLLTUI_API(rolltui_diff_is_language, PUBLIC)
ROLLTUI_API(rolltui_diff_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_diff_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_diff_spans, PUBLIC)

/* ---- rolltui_markdown.h — the markdown parser over md4c; the transcript renders through md_lines, and no host calls it; default INTERNAL ---- */
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

/* ---- rolltui_md_lines.h — the transcript's line store; default INTERNAL ---- */
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
ROLLTUI_API(rolltui_md_lines_mark, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_md_lines_new, INTERNAL)
ROLLTUI_API(rolltui_md_lines_open, INTERNAL)
ROLLTUI_API(rolltui_md_lines_reset, INTERNAL)
ROLLTUI_API(rolltui_md_lines_rewind, DELETE)  /* reached by nothing, anywhere */
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

/* ---- rolltui_unicode.h — the library's Unicode algorithms; three functions a host reaches are public, the rest are the wrap engine's; default INTERNAL ---- */
ROLLTUI_API(rolltui_u_append_utf8, INTERNAL)
ROLLTUI_API(rolltui_u_cluster_width, INTERNAL)
ROLLTUI_API(rolltui_u_codepoint_width, INTERNAL)
ROLLTUI_API(rolltui_u_decode_one, INTERNAL)
ROLLTUI_API(rolltui_u_decode_utf8, PUBLIC)  /* one of the three Unicode functions a host needs (Phase 17 m4) */
ROLLTUI_API(rolltui_u_decode_utf8_chars, PUBLIC)
ROLLTUI_API(rolltui_u_display_width, PUBLIC)
ROLLTUI_API(rolltui_u_east_asian_width, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_u_general_category, INTERNAL)
ROLLTUI_API(rolltui_u_grapheme_boundaries, INTERNAL)
ROLLTUI_API(rolltui_u_grapheme_break, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_u_graphemes, INTERNAL)
ROLLTUI_API(rolltui_u_indic_conjunct_break, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_u_is_default_ignorable, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_u_is_extended_pictographic, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_u_line_break_class, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_u_line_break_opportunities, INTERNAL)
ROLLTUI_API(rolltui_u_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_u_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_u_strip_escape_sequences, PUBLIC)
ROLLTUI_API(rolltui_u_word_boundaries, INTERNAL)
ROLLTUI_API(rolltui_u_word_break, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_u_word_range, INTERNAL)

/* ---- rolltui_wrap.h — the wrap engine roll draws with; default PUBLIC ---- */
ROLLTUI_API(rolltui_wrap, PUBLIC)
ROLLTUI_API(rolltui_wrap_clone, PUBLIC)
ROLLTUI_API(rolltui_wrap_free, PUBLIC)
ROLLTUI_API(rolltui_wrap_line, PUBLIC)
ROLLTUI_API(rolltui_wrap_line_count, PUBLIC)
ROLLTUI_API(rolltui_wrap_new, PUBLIC)
ROLLTUI_API(rolltui_wrap_reset, PUBLIC)

/* ---- rolltui_marker.h — the "N more" rule; default PUBLIC ---- */
ROLLTUI_API(rolltui_scroll_marker_text, PUBLIC)

/* ---- rolltui_effects.h — a host registers effect kinds and holds an effect map; default PUBLIC ---- */
ROLLTUI_API(rolltui_effect_clear_registered, INTERNAL)  /* the registry's own shutdown hook (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_effect_is_builtin, PUBLIC)
ROLLTUI_API(rolltui_effect_kind_count, PUBLIC)
ROLLTUI_API(rolltui_effect_kind_name, PUBLIC)
ROLLTUI_API(rolltui_effect_kind_resolves, PUBLIC)
ROLLTUI_API(rolltui_effect_map_add, PUBLIC)
ROLLTUI_API(rolltui_effect_map_add_frame, PUBLIC)
ROLLTUI_API(rolltui_effect_map_add_role, PUBLIC)
ROLLTUI_API(rolltui_effect_map_at, PUBLIC)
ROLLTUI_API(rolltui_effect_map_clear, PUBLIC)
ROLLTUI_API(rolltui_effect_map_clone, PUBLIC)
ROLLTUI_API(rolltui_effect_map_count, PUBLIC)
ROLLTUI_API(rolltui_effect_map_empty, PUBLIC)
ROLLTUI_API(rolltui_effect_map_equal, PUBLIC)
ROLLTUI_API(rolltui_effect_map_free, PUBLIC)
ROLLTUI_API(rolltui_effect_map_new, PUBLIC)
ROLLTUI_API(rolltui_effect_register, PUBLIC)
ROLLTUI_API(rolltui_effect_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_effect_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_effect_state_from_name, PUBLIC)
ROLLTUI_API(rolltui_effect_state_name, PUBLIC)
ROLLTUI_API(rolltui_effect_steps, INTERNAL)  /* a step of apply */
ROLLTUI_API(rolltui_effects_apply, PUBLIC)
ROLLTUI_API(rolltui_effects_tick_ms, PUBLIC)

/* ---- rolltui_theme.h — a host holds a style table; default PUBLIC ---- */
ROLLTUI_API(rolltui_ansi_index_rgb, INTERNAL)  /* a step of downgrade */
ROLLTUI_API(rolltui_color_depth_from_name, PUBLIC)
ROLLTUI_API(rolltui_color_depth_name, PUBLIC)
ROLLTUI_API(rolltui_color_depth_setting_valid, PUBLIC)
ROLLTUI_API(rolltui_color_downgrade, PUBLIC)
ROLLTUI_API(rolltui_color_parse, PUBLIC)
ROLLTUI_API(rolltui_color_to_string, PUBLIC)
ROLLTUI_API(rolltui_detect_color_depth, PUBLIC)
ROLLTUI_API(rolltui_mode_for_background, PUBLIC)
ROLLTUI_API(rolltui_parse_osc11_reply, PUBLIC)
ROLLTUI_API(rolltui_sgr, PUBLIC)
ROLLTUI_API(rolltui_theme_builtin_count, PUBLIC)
ROLLTUI_API(rolltui_theme_builtin_fill, PUBLIC)
ROLLTUI_API(rolltui_theme_builtin_name, PUBLIC)
ROLLTUI_API(rolltui_theme_default_vocab, PUBLIC)
ROLLTUI_API(rolltui_theme_dump, PUBLIC)
ROLLTUI_API(rolltui_theme_load, PUBLIC)
ROLLTUI_API(rolltui_theme_mode_from_name, PUBLIC)
ROLLTUI_API(rolltui_theme_mode_name, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_theme_mode_setting_valid, PUBLIC)
ROLLTUI_API(rolltui_theme_report_add_bad_value, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_theme_report_add_missing_role, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_theme_report_add_unknown_key, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_theme_report_release, PUBLIC)
ROLLTUI_API(rolltui_theme_report_set_error, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_theme_set_style, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_theme_style, PUBLIC)

/* ---- rolltui_theme_analysis.h — the studio and the theme editor check themes; roll never does; default TOOL_FACING ---- */
ROLLTUI_API(rolltui_apca_contrast, TOOL_FACING)
ROLLTUI_API(rolltui_apply_fix, TOOL_FACING)
ROLLTUI_API(rolltui_badge_names, TOOL_FACING)
ROLLTUI_API(rolltui_check_claims, TOOL_FACING)
ROLLTUI_API(rolltui_cvd_name, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_delta_e, TOOL_FACING)
ROLLTUI_API(rolltui_fix_array_release, TOOL_FACING)
ROLLTUI_API(rolltui_fix_confusable, TOOL_FACING)
ROLLTUI_API(rolltui_fix_contrast, TOOL_FACING)
ROLLTUI_API(rolltui_fix_release, TOOL_FACING)
ROLLTUI_API(rolltui_from_linear, TOOL_FACING)
ROLLTUI_API(rolltui_has_badge, TOOL_FACING)
ROLLTUI_API(rolltui_into_gamut, INTERNAL)  /* a step of fix */
ROLLTUI_API(rolltui_linear_channel_to_srgb, TOOL_FACING)
ROLLTUI_API(rolltui_linear_to_oklab, TOOL_FACING)
ROLLTUI_API(rolltui_must_differ_count, TOOL_FACING)
ROLLTUI_API(rolltui_oklab_to_linear, TOOL_FACING)
ROLLTUI_API(rolltui_oklab_to_oklch, TOOL_FACING)
ROLLTUI_API(rolltui_oklch_to_oklab, TOOL_FACING)
ROLLTUI_API(rolltui_propose_fixes, TOOL_FACING)
ROLLTUI_API(rolltui_relative_luminance, TOOL_FACING)
ROLLTUI_API(rolltui_simulate_cvd, TOOL_FACING)
ROLLTUI_API(rolltui_srgb_channel_to_linear, TOOL_FACING)
ROLLTUI_API(rolltui_str_array_release, TOOL_FACING)
ROLLTUI_API(rolltui_theme_analyse, TOOL_FACING)
ROLLTUI_API(rolltui_theme_report_text, TOOL_FACING)
ROLLTUI_API(rolltui_to_linear, TOOL_FACING)
ROLLTUI_API(rolltui_wcag_contrast, TOOL_FACING)

/* ---- rolltui_theme_gen.h — the theme generator is the editor's; default TOOL_FACING ---- */
ROLLTUI_API(rolltui_rng_next, INTERNAL)  /* the generator's own */
ROLLTUI_API(rolltui_rng_unit, INTERNAL)  /* the generator's own */
ROLLTUI_API(rolltui_ruleset_from_name, TOOL_FACING)
ROLLTUI_API(rolltui_ruleset_name, TOOL_FACING)
ROLLTUI_API(rolltui_theme_generate, TOOL_FACING)

/* ---- rolltui_bindings.h — a host holds a bindings table and its report; default PUBLIC ---- */
ROLLTUI_API(rolltui_bindings_action_at, PUBLIC)
ROLLTUI_API(rolltui_bindings_action_count, PUBLIC)
ROLLTUI_API(rolltui_bindings_action_for, PUBLIC)
ROLLTUI_API(rolltui_bindings_add_action, PUBLIC)
ROLLTUI_API(rolltui_bindings_add_chord, PUBLIC)
ROLLTUI_API(rolltui_bindings_add_row, PUBLIC)
ROLLTUI_API(rolltui_bindings_bind, PUBLIC)
ROLLTUI_API(rolltui_bindings_breaks_enter_rule, INTERNAL)  /* a step of bind */
ROLLTUI_API(rolltui_bindings_chord_at, PUBLIC)
ROLLTUI_API(rolltui_bindings_chord_count, PUBLIC)
ROLLTUI_API(rolltui_bindings_chords_text, PUBLIC)
ROLLTUI_API(rolltui_bindings_clear, PUBLIC)
ROLLTUI_API(rolltui_bindings_clone, PUBLIC)
ROLLTUI_API(rolltui_bindings_declare, PUBLIC)
ROLLTUI_API(rolltui_bindings_default, PUBLIC)
ROLLTUI_API(rolltui_bindings_description, PUBLIC)
ROLLTUI_API(rolltui_bindings_dump_json, PUBLIC)
ROLLTUI_API(rolltui_bindings_equal, PUBLIC)
ROLLTUI_API(rolltui_bindings_free, PUBLIC)
ROLLTUI_API(rolltui_bindings_has, PUBLIC)
ROLLTUI_API(rolltui_bindings_has_row, PUBLIC)
ROLLTUI_API(rolltui_bindings_holder, INTERNAL)  /* a step of bind */
ROLLTUI_API(rolltui_bindings_library_scope, PUBLIC)  /* the vocabulary callback a host passes */
ROLLTUI_API(rolltui_bindings_load_json, PUBLIC)
ROLLTUI_API(rolltui_bindings_new, PUBLIC)
ROLLTUI_API(rolltui_bindings_new_seeded, PUBLIC)
ROLLTUI_API(rolltui_bindings_report_add_bad_chord, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_add_bad_value, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_add_conflict, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_add_undeliverable, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_add_unknown_action, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_add_unknown_key, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_clean, PUBLIC)
ROLLTUI_API(rolltui_bindings_report_release, PUBLIC)
ROLLTUI_API(rolltui_bindings_report_set_error, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_bindings_report_summary, PUBLIC)
ROLLTUI_API(rolltui_bindings_row_at, PUBLIC)
ROLLTUI_API(rolltui_bindings_row_count, PUBLIC)
ROLLTUI_API(rolltui_bindings_scope_of, PUBLIC)
ROLLTUI_API(rolltui_bindings_set_enter_rule, PUBLIC)
ROLLTUI_API(rolltui_bindings_suggest, INTERNAL)  /* a step of declare */
ROLLTUI_API(rolltui_bindings_unbind, PUBLIC)
ROLLTUI_API(rolltui_bindings_undeclare_others, INTERNAL)  /* a step of declare (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_chord_display, PUBLIC)
ROLLTUI_API(rolltui_chord_parse, PUBLIC)
ROLLTUI_API(rolltui_chord_to_string, PUBLIC)
ROLLTUI_API(rolltui_library_action_count, PUBLIC)
ROLLTUI_API(rolltui_library_action_description, PUBLIC)
ROLLTUI_API(rolltui_library_action_name, PUBLIC)
ROLLTUI_API(rolltui_undeliverable_reason_fn, PUBLIC)  /* the vocabulary callback a host with its own bindings domain passes */

/* ---- rolltui_keys.h — the chord and protocol vocabulary a host shows; default PUBLIC ---- */
ROLLTUI_API(rolltui_key_active_protocol, PUBLIC)
ROLLTUI_API(rolltui_key_decoder_feed, INTERNAL)  /* the terminal's decoder (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_decoder_flush, INTERNAL)  /* the terminal's decoder (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_decoder_free, INTERNAL)  /* the terminal's decoder (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_decoder_in_paste, INTERNAL)  /* the terminal's decoder (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_decoder_new, INTERNAL)  /* the terminal's decoder; a host reads events (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_decoder_pending, INTERNAL)  /* the terminal's decoder */
ROLLTUI_API(rolltui_key_deliverable, PUBLIC)
ROLLTUI_API(rolltui_key_display_name, PUBLIC)
ROLLTUI_API(rolltui_key_encode, INTERNAL)  /* the terminal's own (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_key_file_name, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_key_from_display_name, PUBLIC)
ROLLTUI_API(rolltui_key_protocol_from_name, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_key_protocol_name, PUBLIC)
ROLLTUI_API(rolltui_key_set_active_protocol, PUBLIC)
ROLLTUI_API(rolltui_key_undeliverable_reason, PUBLIC)
ROLLTUI_API(rolltui_key_undeliverable_text, PUBLIC)

/* ---- rolltui_layout.h — a host holds a RolltuiLayout and a window stack; default PUBLIC ---- */
ROLLTUI_API(rolltui_action_decl_problem, PUBLIC)
ROLLTUI_API(rolltui_action_list_add, PUBLIC)
ROLLTUI_API(rolltui_action_list_at, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_action_list_clear, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_action_list_copy, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_action_list_count, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_action_list_equal, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_action_list_release, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_action_list_remove, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_action_list_remove_name, PUBLIC)
ROLLTUI_API(rolltui_anchor_from_name, PUBLIC)
ROLLTUI_API(rolltui_anchor_name, PUBLIC)
ROLLTUI_API(rolltui_border_from_name, PUBLIC)
ROLLTUI_API(rolltui_border_name, PUBLIC)
ROLLTUI_API(rolltui_compose_layer, INTERNAL)  /* a step of compose */
ROLLTUI_API(rolltui_compose_scratch_free, PUBLIC)
ROLLTUI_API(rolltui_compose_scratch_new, PUBLIC)
ROLLTUI_API(rolltui_content_copy, PUBLIC)  /* rule 1: a public type's copy */
ROLLTUI_API(rolltui_content_equal, PUBLIC)  /* a public type's equal */
ROLLTUI_API(rolltui_content_format, PUBLIC)
ROLLTUI_API(rolltui_content_init, PUBLIC)  /* rule 1: a public type's init */
ROLLTUI_API(rolltui_content_parse, PUBLIC)
ROLLTUI_API(rolltui_content_release, PUBLIC)  /* rule 1: the pair */
ROLLTUI_API(rolltui_dim_to_string, PUBLIC)
ROLLTUI_API(rolltui_draw_border, PUBLIC)
ROLLTUI_API(rolltui_inner_rect, INTERNAL)  /* a step of layout */
ROLLTUI_API(rolltui_layout_actions_free, PUBLIC)
ROLLTUI_API(rolltui_layout_builtin, PUBLIC)
ROLLTUI_API(rolltui_layout_builtin_count, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_layout_builtin_json, PUBLIC)
ROLLTUI_API(rolltui_layout_builtin_name, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_layout_copy, PUBLIC)
ROLLTUI_API(rolltui_layout_default_hooks, PUBLIC)
ROLLTUI_API(rolltui_layout_default_roles, PUBLIC)
ROLLTUI_API(rolltui_layout_equal, PUBLIC)
ROLLTUI_API(rolltui_layout_init, PUBLIC)
ROLLTUI_API(rolltui_layout_popup, PUBLIC)
ROLLTUI_API(rolltui_layout_read_actions_key, INTERNAL)  /* a step of the layout loader (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_layout_release, PUBLIC)
ROLLTUI_API(rolltui_layout_report_clean, PUBLIC)
ROLLTUI_API(rolltui_layout_report_release, PUBLIC)
ROLLTUI_API(rolltui_layout_shipped_default_actions, PUBLIC)
ROLLTUI_API(rolltui_layout_to_json_text, PUBLIC)
ROLLTUI_API(rolltui_layout_to_json_value, PUBLIC)
ROLLTUI_API(rolltui_load_layout, INTERNAL)  /* the loader over a parsed tree; the text form is the entry point */
ROLLTUI_API(rolltui_load_layout_text, PUBLIC)
ROLLTUI_API(rolltui_loaded_layout_init, PUBLIC)
ROLLTUI_API(rolltui_loaded_layout_release, PUBLIC)
ROLLTUI_API(rolltui_loaded_layout_to_layout, PUBLIC)
ROLLTUI_API(rolltui_parse_dim, PUBLIC)
ROLLTUI_API(rolltui_parse_size_text, PUBLIC)
ROLLTUI_API(rolltui_parse_split_size, INTERNAL)  /* a step of the layout loader (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_placement_resolve, PUBLIC)
ROLLTUI_API(rolltui_resolve_dim, INTERNAL)  /* a step of layout */
ROLLTUI_API(rolltui_resolve_tree, PUBLIC)
ROLLTUI_API(rolltui_split_size_to_string, PUBLIC)
ROLLTUI_API(rolltui_stack_default_actions, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_clear, INTERNAL)  /* the registry's own shutdown hook (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_widget_kind_count, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_library_count, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_name, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_register, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_resolve, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_rule, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_source_is, PUBLIC)
ROLLTUI_API(rolltui_widget_kind_source_shape, PUBLIC)
ROLLTUI_API(rolltui_window_stack_base, PUBLIC)
ROLLTUI_API(rolltui_window_stack_captured, PUBLIC)
ROLLTUI_API(rolltui_window_stack_compose, PUBLIC)
ROLLTUI_API(rolltui_window_stack_cycle_focus, INTERNAL)  /* what the stack does on its own action */
ROLLTUI_API(rolltui_window_stack_depth, PUBLIC)
ROLLTUI_API(rolltui_window_stack_find, PUBLIC)
ROLLTUI_API(rolltui_window_stack_focus, PUBLIC)
ROLLTUI_API(rolltui_window_stack_focus_layer, PUBLIC)
ROLLTUI_API(rolltui_window_stack_focused, PUBLIC)
ROLLTUI_API(rolltui_window_stack_free, PUBLIC)
ROLLTUI_API(rolltui_window_stack_has_popup, PUBLIC)
ROLLTUI_API(rolltui_window_stack_layer, PUBLIC)
ROLLTUI_API(rolltui_window_stack_new, PUBLIC)
ROLLTUI_API(rolltui_window_stack_pop, PUBLIC)
ROLLTUI_API(rolltui_window_stack_push, PUBLIC)
ROLLTUI_API(rolltui_window_stack_push_popup, PUBLIC)
ROLLTUI_API(rolltui_window_stack_resolve, PUBLIC)
ROLLTUI_API(rolltui_window_stack_route, PUBLIC)
ROLLTUI_API(rolltui_window_stack_set_base, PUBLIC)

/* ---- rolltui_layout_tree.h — the tree inside RolltuiLayout: a public type, so its lifecycle is public; default PUBLIC ---- */
ROLLTUI_API(rolltui_layer_copy, PUBLIC)
ROLLTUI_API(rolltui_layer_equal, PUBLIC)
ROLLTUI_API(rolltui_layer_init, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layer_list_add, PUBLIC)
ROLLTUI_API(rolltui_layer_list_at, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_layer_list_clear, PUBLIC)
ROLLTUI_API(rolltui_layer_list_copy, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layer_list_count, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_layer_list_equal, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layer_list_release, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layer_list_remove, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_layer_list_remove_id, PUBLIC)
ROLLTUI_API(rolltui_layer_move, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_layer_release, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layout_node_copy, PUBLIC)
ROLLTUI_API(rolltui_layout_node_equal, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layout_node_free, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layout_node_init, PUBLIC)
ROLLTUI_API(rolltui_layout_node_new, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_layout_node_release, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_node_list_add, PUBLIC)
ROLLTUI_API(rolltui_node_list_at, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_node_list_clear, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_node_list_copy, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_node_list_count, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_node_list_insert, PUBLIC)
ROLLTUI_API(rolltui_node_list_push, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_node_list_release, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_node_list_remove, PUBLIC)

/* ---- rolltui_widgets.h — the window registry and the plugin-facing half a host kind reaches its sources through; default PUBLIC ---- */
ROLLTUI_API(rolltui_content_rect, PUBLIC)
ROLLTUI_API(rolltui_note_clear, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_rows_add, PUBLIC)
ROLLTUI_API(rolltui_rows_release, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_rows_reset, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_scroll_first_for_cell, INTERNAL)  /* scrollbar arithmetic (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_scroll_thumb, INTERNAL)  /* scrollbar arithmetic (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_windows_add_help_scope, PUBLIC)
ROLLTUI_API(rolltui_windows_add_menu, PUBLIC)
ROLLTUI_API(rolltui_windows_at, PUBLIC)
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
ROLLTUI_API(rolltui_windows_clear_help_scopes, PUBLIC)
ROLLTUI_API(rolltui_windows_content_at, PUBLIC)
ROLLTUI_API(rolltui_windows_default_roles, PUBLIC)
ROLLTUI_API(rolltui_windows_dir, PUBLIC)
ROLLTUI_API(rolltui_windows_document, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_draw, PUBLIC)
ROLLTUI_API(rolltui_windows_env, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_free, PUBLIC)
ROLLTUI_API(rolltui_windows_handle, PUBLIC)
ROLLTUI_API(rolltui_windows_has_note, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_windows_has_rows, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_has_submit, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_help_lead, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_help_note, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_help_scope_at, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_help_scope_count, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_host_menu, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_host_menu_count, PUBLIC)
ROLLTUI_API(rolltui_windows_host_menu_name_at, PUBLIC)
ROLLTUI_API(rolltui_windows_input, PUBLIC)
ROLLTUI_API(rolltui_windows_input_at, PUBLIC)
ROLLTUI_API(rolltui_windows_layout, PUBLIC)
ROLLTUI_API(rolltui_windows_menu, PUBLIC)
ROLLTUI_API(rolltui_windows_menu_at, PUBLIC)
ROLLTUI_API(rolltui_windows_menu_origin, PUBLIC)
ROLLTUI_API(rolltui_windows_new, PUBLIC)
ROLLTUI_API(rolltui_windows_on_submit, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_register_kind, PUBLIC)
ROLLTUI_API(rolltui_windows_report_at, PUBLIC)
ROLLTUI_API(rolltui_windows_report_count, PUBLIC)
ROLLTUI_API(rolltui_windows_report_summary, PUBLIC)
ROLLTUI_API(rolltui_windows_set_bindings, PUBLIC)
ROLLTUI_API(rolltui_windows_set_dir, PUBLIC)
ROLLTUI_API(rolltui_windows_set_env, PUBLIC)
ROLLTUI_API(rolltui_windows_set_error_factory, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_set_help, PUBLIC)
ROLLTUI_API(rolltui_windows_set_highlight, PUBLIC)
ROLLTUI_API(rolltui_windows_set_input_min_outer, PUBLIC)
ROLLTUI_API(rolltui_windows_set_panel_factory, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_windows_styles, PUBLIC)
ROLLTUI_API(rolltui_windows_sync, PUBLIC)
ROLLTUI_API(rolltui_windows_transcript, PUBLIC)
ROLLTUI_API(rolltui_windows_transcript_at, PUBLIC)
ROLLTUI_API(rolltui_windows_widget_for, PUBLIC)

/* ---- rolltui_widget_kinds.h — the built-in kinds and their registration; default PUBLIC ---- */
ROLLTUI_API(rolltui_help_document, PUBLIC)
ROLLTUI_API(rolltui_help_scope_lines, PUBLIC)
ROLLTUI_API(rolltui_input_kind_process_event, PUBLIC)
ROLLTUI_API(rolltui_input_max_rows, PUBLIC)
ROLLTUI_API(rolltui_input_widget_ctx_set_min_outer, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_input_widget_plugin, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_input_window_rows, PUBLIC)
ROLLTUI_API(rolltui_menu_widget_ctx_origin, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_menu_widget_ctx_refresh, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_menu_widget_plugin, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_scroll_by_action, PUBLIC)
ROLLTUI_API(rolltui_scroll_text_default_actions, PUBLIC)
ROLLTUI_API(rolltui_widget_kinds_register, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_windows_builtin_roles, PUBLIC)
ROLLTUI_API(rolltui_windows_code_fold, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_windows_input_actions, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_windows_menu_roles, PUBLIC)
ROLLTUI_API(rolltui_windows_scroll_text_actions, INTERNAL)  /* the built-in kinds' own */
ROLLTUI_API(rolltui_windows_set_builtin_roles, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_windows_set_code_fold, PUBLIC)
ROLLTUI_API(rolltui_windows_set_input_actions, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_windows_set_library_defaults, PUBLIC)
ROLLTUI_API(rolltui_windows_set_menu_roles, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_windows_set_scroll_text_actions, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_windows_set_transcript_actions, INTERNAL)  /* behind set_library_defaults */
ROLLTUI_API(rolltui_windows_transcript_actions, INTERNAL)  /* the built-in kinds' own */

/* ---- rolltui_document.h — the transcript document a host appends to; default PUBLIC ---- */
ROLLTUI_API(rolltui_doc_entry_copy, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_doc_entry_init, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_doc_entry_release, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_document_add, PUBLIC)
ROLLTUI_API(rolltui_document_at, PUBLIC)  /* a public type's read API */
ROLLTUI_API(rolltui_document_clear, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_document_copy, PUBLIC)
ROLLTUI_API(rolltui_document_count, PUBLIC)  /* a public type's read API */
ROLLTUI_API(rolltui_document_release, PUBLIC)

/* ---- rolltui_input.h — the input widget: a host sets, selects, undoes and reads; default PUBLIC ---- */
ROLLTUI_API(rolltui_input_area, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_input_can_redo, PUBLIC)
ROLLTUI_API(rolltui_input_can_undo, PUBLIC)
ROLLTUI_API(rolltui_input_caret, PUBLIC)
ROLLTUI_API(rolltui_input_cell_of, PUBLIC)
ROLLTUI_API(rolltui_input_clear, PUBLIC)
ROLLTUI_API(rolltui_input_clear_selection, PUBLIC)  /* an operation a host performs */
ROLLTUI_API(rolltui_input_default_actions, PUBLIC)
ROLLTUI_API(rolltui_input_draw, PUBLIC)
ROLLTUI_API(rolltui_input_erase_backward, INTERNAL)  /* the handler's step (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_erase_forward, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_erase_selection, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_input_free, PUBLIC)
ROLLTUI_API(rolltui_input_handle, PUBLIC)
ROLLTUI_API(rolltui_input_history_at, PUBLIC)
ROLLTUI_API(rolltui_input_history_count, PUBLIC)
ROLLTUI_API(rolltui_input_history_cursor, PUBLIC)
ROLLTUI_API(rolltui_input_history_next, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_input_history_prev, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_input_hit, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_input_insert, PUBLIC)
ROLLTUI_API(rolltui_input_kill_to_line_end, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_kill_to_line_start, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_kill_word_backward, INTERNAL)  /* the handler's step (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_kill_word_forward, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_layout, PUBLIC)
ROLLTUI_API(rolltui_input_move_down, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_left, INTERNAL)  /* the handler's step; a host sends the event (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_move_line_end, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_line_start, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_right, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_up, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_word_left, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_move_word_right, INTERNAL)  /* the handler's step (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_new, PUBLIC)
ROLLTUI_API(rolltui_input_options, PUBLIC)
ROLLTUI_API(rolltui_input_options_copy, PUBLIC)
ROLLTUI_API(rolltui_input_options_equal, PUBLIC)
ROLLTUI_API(rolltui_input_options_init, PUBLIC)
ROLLTUI_API(rolltui_input_options_release, PUBLIC)
ROLLTUI_API(rolltui_input_preview_insert, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_push_history, PUBLIC)
ROLLTUI_API(rolltui_input_redo, PUBLIC)
ROLLTUI_API(rolltui_input_rows, PUBLIC)
ROLLTUI_API(rolltui_input_rows_for, INTERNAL)  /* a step of the input's layout (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_input_select_all, PUBLIC)
ROLLTUI_API(rolltui_input_selected_text, PUBLIC)
ROLLTUI_API(rolltui_input_selection, PUBLIC)
ROLLTUI_API(rolltui_input_set_caret, PUBLIC)
ROLLTUI_API(rolltui_input_set_copy, PUBLIC)
ROLLTUI_API(rolltui_input_set_options, PUBLIC)
ROLLTUI_API(rolltui_input_set_text, PUBLIC)
ROLLTUI_API(rolltui_input_text, PUBLIC)
ROLLTUI_API(rolltui_input_top_row, PUBLIC)
ROLLTUI_API(rolltui_input_undo, PUBLIC)
ROLLTUI_API(rolltui_input_word_left_of, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_input_word_right_of, INTERNAL)  /* the handler's step */
ROLLTUI_API(rolltui_str_set, PUBLIC)

/* ---- rolltui_menu.h — the menu widget; default PUBLIC ---- */
ROLLTUI_API(rolltui_check_input, INTERNAL)  /* a step of the menu loader */
ROLLTUI_API(rolltui_input_check_release, INTERNAL)  /* a step of the menu loader */
ROLLTUI_API(rolltui_input_hint, PUBLIC)
ROLLTUI_API(rolltui_input_type_from_name, INTERNAL)  /* the menu loader's vocabulary */
ROLLTUI_API(rolltui_input_type_name, INTERNAL)  /* the menu loader's vocabulary */
ROLLTUI_API(rolltui_menu_action_list_release, PUBLIC)
ROLLTUI_API(rolltui_menu_apply_shortcuts, PUBLIC)
ROLLTUI_API(rolltui_menu_area, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_menu_breadcrumb, PUBLIC)
ROLLTUI_API(rolltui_menu_default_actions, PUBLIC)
ROLLTUI_API(rolltui_menu_draw, PUBLIC)
ROLLTUI_API(rolltui_menu_dump_json, PUBLIC)
ROLLTUI_API(rolltui_menu_edit_reason, PUBLIC)
ROLLTUI_API(rolltui_menu_editing, PUBLIC)
ROLLTUI_API(rolltui_menu_editor, PUBLIC)
ROLLTUI_API(rolltui_menu_event_release, PUBLIC)
ROLLTUI_API(rolltui_menu_filter, PUBLIC)
ROLLTUI_API(rolltui_menu_find, PUBLIC)
ROLLTUI_API(rolltui_menu_flat_count, INTERNAL)  /* the palette's own flattening (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_menu_flat_label, INTERNAL)  /* the palette's own flattening (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_menu_flat_path, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_menu_free, PUBLIC)
ROLLTUI_API(rolltui_menu_handle, PUBLIC)
ROLLTUI_API(rolltui_menu_item_actions, PUBLIC)
ROLLTUI_API(rolltui_menu_layout, PUBLIC)
ROLLTUI_API(rolltui_menu_level, PUBLIC)
ROLLTUI_API(rolltui_menu_load_report_add_bad_value, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_menu_load_report_add_unknown_key, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_menu_load_report_clean, PUBLIC)
ROLLTUI_API(rolltui_menu_load_report_release, PUBLIC)
ROLLTUI_API(rolltui_menu_load_report_set_error, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_menu_new, PUBLIC)
ROLLTUI_API(rolltui_menu_options, INTERNAL)  /* a step of the menu widget */
ROLLTUI_API(rolltui_menu_palette, PUBLIC)
ROLLTUI_API(rolltui_menu_parse_json, PUBLIC)
ROLLTUI_API(rolltui_menu_path, PUBLIC)
ROLLTUI_API(rolltui_menu_reset, PUBLIC)
ROLLTUI_API(rolltui_menu_root, PUBLIC)
ROLLTUI_API(rolltui_menu_rows_for, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_menu_scroll_first, INTERNAL)  /* a step of the menu widget */
ROLLTUI_API(rolltui_menu_scroll_visible, INTERNAL)  /* a step of the menu widget */
ROLLTUI_API(rolltui_menu_selected, PUBLIC)
ROLLTUI_API(rolltui_menu_selected_item, PUBLIC)
ROLLTUI_API(rolltui_menu_set_checked, PUBLIC)
ROLLTUI_API(rolltui_menu_set_enabled, PUBLIC)
ROLLTUI_API(rolltui_menu_set_options, PUBLIC)
ROLLTUI_API(rolltui_menu_set_options_struct, PUBLIC)
ROLLTUI_API(rolltui_menu_set_palette, PUBLIC)
ROLLTUI_API(rolltui_menu_set_root, PUBLIC)
ROLLTUI_API(rolltui_menu_set_validator_fn, PUBLIC)
ROLLTUI_API(rolltui_menu_set_value, PUBLIC)
ROLLTUI_API(rolltui_menu_unknown_validators, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_menu_visible, PUBLIC)

/* ---- rolltui_menu_tree.h — the menu item tree a host builds and walks; default PUBLIC ---- */
ROLLTUI_API(rolltui_input_spec_copy, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_input_spec_equal, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_input_spec_init, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_input_spec_release, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_item_copy, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_item_equal, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_item_free, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_item_init, PUBLIC)
ROLLTUI_API(rolltui_menu_item_new, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_item_release, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_list_add, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_menu_list_at, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_menu_list_clear, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_list_copy, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */
ROLLTUI_API(rolltui_menu_list_count, INTERNAL)  /* only the library's own .c reaches it */
ROLLTUI_API(rolltui_menu_list_release, PUBLIC)  /* rule 1: a public type's lifecycle; only the library reaches it today */

/* ---- rolltui_transcript.h — the transcript widget; default PUBLIC ---- */
ROLLTUI_API(rolltui_entry_layout_release, INTERNAL)  /* the transcript's own store */
ROLLTUI_API(rolltui_entry_layout_store, INTERNAL)  /* the transcript's own store */
ROLLTUI_API(rolltui_selection_range_in, INTERNAL)  /* a step of select */
ROLLTUI_API(rolltui_text_pos_less, INTERNAL)  /* a step of select */
ROLLTUI_API(rolltui_transcript_area, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_transcript_clear_selection, PUBLIC)  /* an operation a host performs */
ROLLTUI_API(rolltui_transcript_copy_selection, PUBLIC)
ROLLTUI_API(rolltui_transcript_current_match, PUBLIC)
ROLLTUI_API(rolltui_transcript_current_match_number, PUBLIC)
ROLLTUI_API(rolltui_transcript_default_actions, PUBLIC)
ROLLTUI_API(rolltui_transcript_draw, PUBLIC)
ROLLTUI_API(rolltui_transcript_find_next, PUBLIC)
ROLLTUI_API(rolltui_transcript_find_prev, PUBLIC)
ROLLTUI_API(rolltui_transcript_free, PUBLIC)
ROLLTUI_API(rolltui_transcript_handle, PUBLIC)
ROLLTUI_API(rolltui_transcript_hit, PUBLIC)
ROLLTUI_API(rolltui_transcript_is_folded, PUBLIC)
ROLLTUI_API(rolltui_transcript_layout, PUBLIC)
ROLLTUI_API(rolltui_transcript_layout_of, PUBLIC)
ROLLTUI_API(rolltui_transcript_lines_below, PUBLIC)
ROLLTUI_API(rolltui_transcript_match_at, PUBLIC)
ROLLTUI_API(rolltui_transcript_match_count, PUBLIC)
ROLLTUI_API(rolltui_transcript_new, PUBLIC)
ROLLTUI_API(rolltui_transcript_query, PUBLIC)
ROLLTUI_API(rolltui_transcript_scroll, PUBLIC)
ROLLTUI_API(rolltui_transcript_scroll_by, PUBLIC)
ROLLTUI_API(rolltui_transcript_scroll_page, PUBLIC)
ROLLTUI_API(rolltui_transcript_scroll_to_bottom, PUBLIC)
ROLLTUI_API(rolltui_transcript_scroll_to_top, PUBLIC)
ROLLTUI_API(rolltui_transcript_select, PUBLIC)
ROLLTUI_API(rolltui_transcript_selected_text, PUBLIC)
ROLLTUI_API(rolltui_transcript_selection, PUBLIC)
ROLLTUI_API(rolltui_transcript_set_code_folded, PUBLIC)
ROLLTUI_API(rolltui_transcript_set_code_uncapped, PUBLIC)
ROLLTUI_API(rolltui_transcript_set_copy, PUBLIC)
ROLLTUI_API(rolltui_transcript_set_folded, PUBLIC)
ROLLTUI_API(rolltui_transcript_set_highlight, INTERNAL)  /* a step of find */
ROLLTUI_API(rolltui_transcript_set_query, PUBLIC)
ROLLTUI_API(rolltui_transcript_set_roles, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_transcript_stats, PUBLIC)
ROLLTUI_API(rolltui_transcript_text_area, PUBLIC)
ROLLTUI_API(rolltui_transcript_tick, PUBLIC)
ROLLTUI_API(rolltui_transcript_toggle_fold_nearest_top, PUBLIC)
ROLLTUI_API(rolltui_transcript_top_line, PUBLIC)
ROLLTUI_API(rolltui_transcript_total_lines, PUBLIC)
ROLLTUI_API(rolltui_transcript_viewport_height, INTERNAL)  /* a step of layout */
ROLLTUI_API(rolltui_transcript_wants_tick, PUBLIC)

/* ---- rolltui_undo.h — the editors' undo stack; default TOOL_FACING ---- */
ROLLTUI_API(rolltui_undo_can_redo, TOOL_FACING)
ROLLTUI_API(rolltui_undo_can_undo, TOOL_FACING)
ROLLTUI_API(rolltui_undo_commit, TOOL_FACING)
ROLLTUI_API(rolltui_undo_current, TOOL_FACING)
ROLLTUI_API(rolltui_undo_empty, TOOL_FACING)
ROLLTUI_API(rolltui_undo_free, TOOL_FACING)
ROLLTUI_API(rolltui_undo_new, TOOL_FACING)
ROLLTUI_API(rolltui_undo_redo, TOOL_FACING)
ROLLTUI_API(rolltui_undo_redo_depth, TOOL_FACING)
ROLLTUI_API(rolltui_undo_replace_current, TOOL_FACING)
ROLLTUI_API(rolltui_undo_reset, TOOL_FACING)
ROLLTUI_API(rolltui_undo_undo, TOOL_FACING)
ROLLTUI_API(rolltui_undo_undo_depth, TOOL_FACING)

/* ---- rolltui_app_profile.h — what a layout may name in an app: written by one host, read by another; default PUBLIC ---- */
ROLLTUI_API(rolltui_app_profile_action_count, PUBLIC)
ROLLTUI_API(rolltui_app_profile_action_description, PUBLIC)
ROLLTUI_API(rolltui_app_profile_action_name, PUBLIC)
ROLLTUI_API(rolltui_app_profile_add_action, PUBLIC)
ROLLTUI_API(rolltui_app_profile_add_document, PUBLIC)
ROLLTUI_API(rolltui_app_profile_add_help_scope, PUBLIC)
ROLLTUI_API(rolltui_app_profile_add_kind, PUBLIC)
ROLLTUI_API(rolltui_app_profile_add_menu, PUBLIC)
ROLLTUI_API(rolltui_app_profile_add_note, PUBLIC)
ROLLTUI_API(rolltui_app_profile_add_row, PUBLIC)
ROLLTUI_API(rolltui_app_profile_add_submit, PUBLIC)
ROLLTUI_API(rolltui_app_profile_app, PUBLIC)
ROLLTUI_API(rolltui_app_profile_content_at, PUBLIC)
ROLLTUI_API(rolltui_app_profile_content_count, PUBLIC)
ROLLTUI_API(rolltui_app_profile_document_count, PUBLIC)
ROLLTUI_API(rolltui_app_profile_document_name, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_document_sample, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_dump, PUBLIC)
ROLLTUI_API(rolltui_app_profile_free, PUBLIC)
ROLLTUI_API(rolltui_app_profile_help_lead, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_help_note, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_help_scope_at, PUBLIC)
ROLLTUI_API(rolltui_app_profile_help_scope_count, PUBLIC)
ROLLTUI_API(rolltui_app_profile_kind_count, PUBLIC)
ROLLTUI_API(rolltui_app_profile_kind_describes, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_kind_name, PUBLIC)
ROLLTUI_API(rolltui_app_profile_kind_rule, PUBLIC)
ROLLTUI_API(rolltui_app_profile_menu_count, PUBLIC)
ROLLTUI_API(rolltui_app_profile_menu_json, PUBLIC)
ROLLTUI_API(rolltui_app_profile_menu_name, PUBLIC)
ROLLTUI_API(rolltui_app_profile_min_height, PUBLIC)
ROLLTUI_API(rolltui_app_profile_min_width, PUBLIC)
ROLLTUI_API(rolltui_app_profile_mount, PUBLIC)
ROLLTUI_API(rolltui_app_profile_new, PUBLIC)
ROLLTUI_API(rolltui_app_profile_note_at, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_note_count, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_parse, PUBLIC)
ROLLTUI_API(rolltui_app_profile_report_add_bad_value, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_app_profile_report_add_unknown_key, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_app_profile_report_clean, PUBLIC)
ROLLTUI_API(rolltui_app_profile_report_release, PUBLIC)
ROLLTUI_API(rolltui_app_profile_report_set_error, INTERNAL)  /* the loader's report builder */
ROLLTUI_API(rolltui_app_profile_report_summary, PUBLIC)
ROLLTUI_API(rolltui_app_profile_row_add_sample, PUBLIC)
ROLLTUI_API(rolltui_app_profile_row_count, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_row_name, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_row_sample_count, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_row_sample_label, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_row_sample_value, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_set_app, PUBLIC)
ROLLTUI_API(rolltui_app_profile_set_help, PUBLIC)
ROLLTUI_API(rolltui_app_profile_set_min_size, PUBLIC)
ROLLTUI_API(rolltui_app_profile_submit_at, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_app_profile_submit_count, PUBLIC)

/* ---- rolltui_embedded.h — the shipped files, by name; default PUBLIC ---- */
ROLLTUI_API(rolltui_embedded_text, PUBLIC)

/* ---- rolltui_json.h — RolltuiJsonValue: a public type (a theme preset holds one), so its API is public; default PUBLIC ---- */
ROLLTUI_API(rolltui_json_array, PUBLIC)
ROLLTUI_API(rolltui_json_array_at, PUBLIC)
ROLLTUI_API(rolltui_json_array_push, PUBLIC)
ROLLTUI_API(rolltui_json_array_size, PUBLIC)
ROLLTUI_API(rolltui_json_as_bool, PUBLIC)
ROLLTUI_API(rolltui_json_as_number, PUBLIC)
ROLLTUI_API(rolltui_json_as_string, PUBLIC)
ROLLTUI_API(rolltui_json_bool, PUBLIC)  /* the JSON API's constructors */
ROLLTUI_API(rolltui_json_clone, PUBLIC)
ROLLTUI_API(rolltui_json_dump, PUBLIC)
ROLLTUI_API(rolltui_json_equal, PUBLIC)
ROLLTUI_API(rolltui_json_free, PUBLIC)
ROLLTUI_API(rolltui_json_get, PUBLIC)
ROLLTUI_API(rolltui_json_has, PUBLIC)
ROLLTUI_API(rolltui_json_is_array, PUBLIC)
ROLLTUI_API(rolltui_json_is_bool, PUBLIC)
ROLLTUI_API(rolltui_json_is_null, PUBLIC)
ROLLTUI_API(rolltui_json_is_number, PUBLIC)
ROLLTUI_API(rolltui_json_is_object, PUBLIC)
ROLLTUI_API(rolltui_json_is_string, PUBLIC)
ROLLTUI_API(rolltui_json_null, PUBLIC)
ROLLTUI_API(rolltui_json_number, PUBLIC)  /* the JSON API's constructors */
ROLLTUI_API(rolltui_json_object, PUBLIC)
ROLLTUI_API(rolltui_json_object_erase, PUBLIC)
ROLLTUI_API(rolltui_json_object_key_at, PUBLIC)
ROLLTUI_API(rolltui_json_object_size, PUBLIC)
ROLLTUI_API(rolltui_json_object_value_at, PUBLIC)
ROLLTUI_API(rolltui_json_parse, PUBLIC)
ROLLTUI_API(rolltui_json_set, PUBLIC)
ROLLTUI_API(rolltui_json_string, PUBLIC)

/* ---- rolltui_presets.h — the preset stores and domains; default PUBLIC ---- */
ROLLTUI_API(rolltui_bindings_preset_domain_init, PUBLIC)  /* the bring-your-own-vocabulary rung */
ROLLTUI_API(rolltui_bindings_preset_report_clean, PUBLIC)
ROLLTUI_API(rolltui_bindings_preset_report_fns, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_bindings_preset_report_release, PUBLIC)
ROLLTUI_API(rolltui_bindings_preset_report_summary, PUBLIC)
ROLLTUI_API(rolltui_layout_preset_domain_init, PUBLIC)  /* the bring-your-own-vocabulary rung */
ROLLTUI_API(rolltui_layout_preset_report_clean, PUBLIC)
ROLLTUI_API(rolltui_layout_preset_report_fns, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_layout_preset_report_release, PUBLIC)
ROLLTUI_API(rolltui_layout_preset_report_summary, PUBLIC)
ROLLTUI_API(rolltui_preset_domain, PUBLIC)
ROLLTUI_API(rolltui_preset_domain_name, PUBLIC)
ROLLTUI_API(rolltui_preset_domain_release, PUBLIC)  /* rule 1: the pair */
ROLLTUI_API(rolltui_preset_is_shipped, PUBLIC)
ROLLTUI_API(rolltui_preset_json_names_in, INTERNAL)  /* a step of list (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_preset_list_release, PUBLIC)
ROLLTUI_API(rolltui_preset_looks_like_path, PUBLIC)  /* a host validates before save_as */
ROLLTUI_API(rolltui_preset_read_file, PUBLIC)
ROLLTUI_API(rolltui_preset_report_summary, INTERNAL)  /* the generic composer; the per-domain summaries are the API (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_preset_resolve_setting, PUBLIC)
ROLLTUI_API(rolltui_preset_rung_name, PUBLIC)
ROLLTUI_API(rolltui_preset_save_result_text, PUBLIC)
ROLLTUI_API(rolltui_preset_setting_index, PUBLIC)
ROLLTUI_API(rolltui_preset_settings_at, PUBLIC)
ROLLTUI_API(rolltui_preset_settings_count, PUBLIC)
ROLLTUI_API(rolltui_preset_shipped, PUBLIC)
ROLLTUI_API(rolltui_preset_shipped_names, PUBLIC)
ROLLTUI_API(rolltui_preset_shipped_text, PUBLIC)
ROLLTUI_API(rolltui_preset_store_edit, PUBLIC)
ROLLTUI_API(rolltui_preset_store_free, PUBLIC)
ROLLTUI_API(rolltui_preset_store_get, PUBLIC)
ROLLTUI_API(rolltui_preset_store_label, PUBLIC)
ROLLTUI_API(rolltui_preset_store_last_error, PUBLIC)
ROLLTUI_API(rolltui_preset_store_list, PUBLIC)
ROLLTUI_API(rolltui_preset_store_load, PUBLIC)
ROLLTUI_API(rolltui_preset_store_modified, PUBLIC)
ROLLTUI_API(rolltui_preset_store_new, PUBLIC)
ROLLTUI_API(rolltui_preset_store_origin, PUBLIC)
ROLLTUI_API(rolltui_preset_store_preset_path, PUBLIC)
ROLLTUI_API(rolltui_preset_store_save_as, PUBLIC)
ROLLTUI_API(rolltui_preset_store_set_working, PUBLIC)
ROLLTUI_API(rolltui_preset_store_start, PUBLIC)
ROLLTUI_API(rolltui_preset_store_value_free, PUBLIC)
ROLLTUI_API(rolltui_preset_store_version, PUBLIC)
ROLLTUI_API(rolltui_preset_store_working, PUBLIC)
ROLLTUI_API(rolltui_preset_store_working_path, PUBLIC)
ROLLTUI_API(rolltui_preset_valid_name, PUBLIC)  /* a host validates before save_as */
ROLLTUI_API(rolltui_preset_working_value, PUBLIC)
ROLLTUI_API(rolltui_preset_write_file_atomic, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_domain_init, PUBLIC)  /* the bring-your-own-vocabulary rung */
ROLLTUI_API(rolltui_theme_preset_parse, INTERNAL)  /* the Theme domain's own parse; the domain is the entry point (a test reaches it: opt-in) */
ROLLTUI_API(rolltui_theme_preset_parse_partial, INTERNAL)  /* the Theme domain's own parse */
ROLLTUI_API(rolltui_theme_preset_report_clean, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_report_fns, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_theme_preset_report_release, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_report_summary, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_to_json, PUBLIC)
ROLLTUI_API(rolltui_theme_preset_value_release, PUBLIC)

/* ---- rolltui_lifetime.h — the release point; default PUBLIC ---- */
ROLLTUI_API(rolltui_on_shutdown, PUBLIC)
ROLLTUI_API(rolltui_release_thread, PUBLIC)  /* the explicit per-thread release the lifetime header documents */
ROLLTUI_API(rolltui_shutdown, PUBLIC)
ROLLTUI_API(rolltui_thread_on_release, PUBLIC)  /* rule 1: a public type's lifecycle, reached by nothing today */

/* ---- rolltui_mem.h — the leak gauge — public by stated reason: it is how a consumer proves it released everything; default PUBLIC ---- */
ROLLTUI_API(rolltui_mem_alloc, PUBLIC)
ROLLTUI_API(rolltui_mem_free, PUBLIC)
ROLLTUI_API(rolltui_mem_reset_stats, DELETE)  /* reached by nothing, anywhere */
ROLLTUI_API(rolltui_mem_stats, PUBLIC)

/* ---- rolltui_terminal.h — the one fd; default PUBLIC ---- */
ROLLTUI_API(rolltui_terminal_enter_sequence, PUBLIC)
ROLLTUI_API(rolltui_terminal_free, PUBLIC)
ROLLTUI_API(rolltui_terminal_height, PUBLIC)
ROLLTUI_API(rolltui_terminal_is_tty, PUBLIC)
ROLLTUI_API(rolltui_terminal_key_protocol, PUBLIC)
ROLLTUI_API(rolltui_terminal_leave_sequence, PUBLIC)
ROLLTUI_API(rolltui_terminal_negotiate_keyboard, INTERNAL)  /* a step of enter */
ROLLTUI_API(rolltui_terminal_new, PUBLIC)
ROLLTUI_API(rolltui_terminal_poll, PUBLIC)
ROLLTUI_API(rolltui_terminal_query_background, PUBLIC)
ROLLTUI_API(rolltui_terminal_refresh_size, INTERNAL)  /* a step of the terminal's own loop */
ROLLTUI_API(rolltui_terminal_restore_now, INTERNAL)  /* a step of leave */
ROLLTUI_API(rolltui_terminal_wake, PUBLIC)
ROLLTUI_API(rolltui_terminal_width, PUBLIC)
ROLLTUI_API(rolltui_terminal_write, PUBLIC)
