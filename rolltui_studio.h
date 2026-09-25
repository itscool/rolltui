#ifndef ROLLTUI_STUDIO_H
#define ROLLTUI_STUDIO_H
/*
 * rolltui/rolltui_studio.h — rolltui's own authoring tool's API, curated: everything
 * `rolltui-studio` and its editors reach beyond `rolltui.h`, and nobody else's.
 *
 * IT STARTED AS ONLY THE PRESET STORES' CREATE-AND-EDIT SURFACE. `rolltui.h`'s theme/layout/
 * bindings stores are the whole of what an ORDINARY host needs: create one, read what's loaded,
 * switch to a different existing preset by name. Checked against every real caller — dirktui,
 * paint, roll — not one of them ever mutates a preset's content, saves a new one, or imports a
 * file; only `rolltui-studio` (and, through it, the theme/layout/keys editors) does any of that.
 * So the three functions that make a preset's content EDITABLE — `_edit`, `_save_as`, `_add` —
 * lived here instead, in a header an ordinary host never includes and never needs to know exists.
 *
 * IT IS NOW THE WHOLE OF THE STUDIO'S CURATED REACH, not just that first tenant: everything the
 * studio and its five editor files call from the library's internal `c/` headers, moved or copied
 * here symbol by symbol (see the banner partway down for which, and why some are copied rather
 * than moved). What is NOT here — `c/rolltui_theme_editor.h`, `c/rolltui_keys_editor.h`,
 * `c/rolltui_undo.h`, `c/rolltui_layout_tree.h` — is already correctly scoped to one editor's own
 * job (each 88%+ used, or, for the layout tree, reached constantly through its own opaque types);
 * folding an already-narrow header in here would relocate exposure, not reduce it, so those stay
 * on `rolltui/CMakeLists.txt`'s `ROLLTUI_INTERNAL_OPT_IN` list directly.
 *
 * `#include`s `rolltui.h` for the store types and value types; adds nothing beyond the functions
 * below. Only `rolltui-studio` and its editors may include this header — enforced the same way
 * `public_header_test` already enforces that only they may include an internal header from c/.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the theme store's CREATE-AND-EDIT surface ----------------------------------------------- */

/* An in-place edit of the theme's own colours, under the store's lock. `fn` sees the LIVE working value — not a copy handed back
 * and forth, the way `rolltui_theme_store_working()` reading and a later call writing would be — so one lock covers the whole edit.
 * `persist` 0 is what a live preview (the theme editor's, while a person is still typing) uses; a commit passes 1. */
typedef void (*RolltuiThemeEditFn)(RolltuiThemePresetValue* value, void* ctx);
void rolltui_theme_store_edit(RolltuiThemeStore* s, RolltuiThemeEditFn fn, void* ctx, int persist);

/* Saves the CURRENT working value under a new name. Always autosaves afterwards (a save-as is an explicit write, unlike `_load`/
 * `_edit`, which respect `persist`): there is no `persist` parameter because no caller wants 0. Returns one of the ROLLTUI_SAVE_*
 * codes; `err` (may be NULL) carries the sentence for every result but SAVED. */
int rolltui_theme_store_save_as(RolltuiThemeStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err);

/* Copies an external FILE in as a new preset (a theme someone sent you) — additive, never replacing an existing one. Named by
 * `as`, or by the file's own stem when `as` is empty. Same ROLLTUI_SAVE_* codes as `_save_as`, plus BAD_NAME for a file that does
 * not parse as a theme. */
int rolltui_theme_store_add(RolltuiThemeStore* s, const char* path, size_t path_len, const char* as, size_t as_len,
                            RolltuiStr* err);

/* REPLACES the working value wholesale — the theme editor's own commit, when it has built a whole new value rather than changing
 * one field of the live one (`_edit` is for that). TAKES OWNERSHIP of `v`: it must be a fresh, independently-owned value (from
 * `rolltui_theme_store_get`, say, or built by hand), never a pointer this store already owns. */
void rolltui_theme_store_set_working(RolltuiThemeStore* s, RolltuiThemePresetValue* v, int persist);

/* ---- the layout store's CREATE-AND-EDIT surface ------------------------------------------------ */

typedef void (*RolltuiLayoutEditFn)(RolltuiLayout* value, void* ctx);
void rolltui_layout_store_edit(RolltuiLayoutStore* s, RolltuiLayoutEditFn fn, void* ctx, int persist);
int rolltui_layout_store_save_as(RolltuiLayoutStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err);
int rolltui_layout_store_add(RolltuiLayoutStore* s, const char* path, size_t path_len, const char* as, size_t as_len,
                             RolltuiStr* err);

/* REPLACES the working value wholesale, same as the theme store's. TAKES OWNERSHIP of `v`. */
void rolltui_layout_store_set_working(RolltuiLayoutStore* s, RolltuiLayout* v, int persist);

/* ---- the bindings store's CREATE-AND-EDIT surface ---------------------------------------------- */

typedef void (*RolltuiBindingsEditFn)(RolltuiBindings* value, void* ctx);
void rolltui_bindings_store_edit(RolltuiBindingsStore* s, RolltuiBindingsEditFn fn, void* ctx, int persist);
int rolltui_bindings_store_save_as(RolltuiBindingsStore* s, const char* name, size_t len, int overwrite,
                                   RolltuiStr* err);
int rolltui_bindings_store_add(RolltuiBindingsStore* s, const char* path, size_t path_len, const char* as,
                               size_t as_len, RolltuiStr* err);

/* REPLACES the working table wholesale, same as the theme store's. TAKES OWNERSHIP of `v`. */
void rolltui_bindings_store_set_working(RolltuiBindingsStore* s, RolltuiBindings* v, int persist);

/* ---- everything below: curated from the studio's own internal-header reach ------------------
 * Each function below still lives in its original internal `c/` header too — most are called
 * from elsewhere inside the library's own `.c` files, so removing them there would break the
 * library's own build. This file is ADDITIVE to that vocabulary, never a replacement for it: a
 * declaration appearing twice, verbatim, across two headers is ordinary and harmless in C. What
 * changes is what the studio and its editors `#include` — this file, not the raw internal one —
 * so they no longer see whatever ELSE that header declares. A few functions (the ones with no
 * "REDECLARED" note) were reached by nothing but the studio to begin with and were moved outright
 * rather than copied. */

/* ---- the bindings table's row enumeration (c/rolltui_bindings.h) ----------------------------- */
size_t rolltui_bindings_row_count(const RolltuiBindings* b);
const char* rolltui_bindings_row_at(const RolltuiBindings* b, size_t i, size_t* len);

/* ---- source-language recognition for the preview's colouriser (c/rolltui_diff.h) ------------- */
/* True for the info strings this colouriser answers to: "diff", "patch", "udiff". The
 * FENCE decides; content is never sniffed (Diff.hpp). */
int rolltui_diff_is_language(const char* lang, size_t lang_len);

/* ---- the effect-state vocabulary, by name (c/rolltui_effects.h) ------------------------------ */
/* The state of that name, or -1 when there is none — what a theme LOADER needs. */
int rolltui_effect_state_from_name(const char* name, size_t len);

/* ---- tinting a rect in place (c/rolltui_frame_ops.h) — MOVED, reached by nothing else ------- */
/* Amends every cell's style in `r` (clipped): a set colour replaces, an attribute bit is
 * OR'd in. Needs no scratch — it reads and writes styles and never looks at text. */
void rolltui_frame_tint(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style);

/* ---- generic JSON construction and inspection (c/rolltui_json.h) ----------------------------- */
RolltuiJsonValue* rolltui_json_object(void);
int rolltui_json_equal(const RolltuiJsonValue* a, const RolltuiJsonValue* b);
int rolltui_json_is_array(const RolltuiJsonValue* v);
int rolltui_json_is_object(const RolltuiJsonValue* v);
int rolltui_json_has(const RolltuiJsonValue* v, const char* key, size_t key_len);

/* ---- the layout parser, its carrier, and its small vocabulary (c/rolltui_layout.h) ----------- */
/* `RolltuiLoadedLayout`'s full definition lives in c/rolltui_layout_tree.h, which the layout
 * editor already includes for its own reasons (it walks and mutates a tree) — forward-declared
 * here only so these prototypes parse; a pointer to an incomplete type needs nothing more. */
typedef struct RolltuiLoadedLayout RolltuiLoadedLayout;

int rolltui_parse_dim(const char* text, size_t len, RolltuiDim* out);
size_t rolltui_dim_to_string(RolltuiDim d, char* out, size_t cap);
/* The same, plus a bare integer as cells — a size as TYPED. */
int rolltui_parse_size_text(const char* text, size_t len, RolltuiSplitSizeRaw* out);
size_t rolltui_split_size_to_string(RolltuiSplitSizeRaw s, char* out, size_t cap);

/* Lays out one tree inside `box` (a layer's resolved placement). Hidden nodes are omitted,
 * and a hidden ROOT emits nothing at all. */
void rolltui_resolve_tree(const RolltuiLayoutNode* root, RolltuiRect box, RolltuiRect screen, size_t layer,
                          RolltuiResolvedSink emit, void* ctx);

const char* rolltui_widget_kind_name(const RolltuiContext* c, size_t row, size_t* len);

const char* rolltui_anchor_name(unsigned char a, size_t* len); /* "" when `a` is out of range */
int rolltui_anchor_from_name(const char* name, size_t len, unsigned char* out);
const char* rolltui_border_name(unsigned char b, size_t* len);
int rolltui_border_from_name(const char* name, size_t len, unsigned char* out);

/* "which scopes are the library's" — that stays Bindings' vocabulary (rolltui_bindings.h's
 * own rule), never duplicated here. */
void rolltui_action_decl_problem(const char* name, size_t len, const RolltuiLayoutHooks* hooks, RolltuiStr* out);

/* MOVED, reached by nothing else. */
RolltuiLayer* rolltui_window_stack_base(RolltuiWindowStack* s);

/* The parsed layout's own lifecycle: `RolltuiLayout` itself (`_init`/`_release`, `rolltui.h`'s
 * opaque handle) and the transient carrier a load fills before it becomes one. */
void rolltui_layout_init(RolltuiLayout* l);    /* zeroes; inits `base` */
void rolltui_layout_release(RolltuiLayout* l); /* frees name/actions/base/popups; zeroes */
void rolltui_loaded_layout_init(RolltuiLoadedLayout* l);
void rolltui_loaded_layout_release(RolltuiLoadedLayout* l);
void rolltui_loaded_layout_to_layout(RolltuiLoadedLayout* loaded, RolltuiLayout* out);
/* The carrier-filling loader, which the owning one above is written in terms of. */
int rolltui_load_layout_text_into(const char* text, size_t len, RolltuiLoadedLayout* out,
                                  const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                                  const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report);
/* Dumps straight to TEXT, indent 2, REPLACING `*out` — `layout_to_json`'s port. */
void rolltui_layout_to_json_text(const char* name, size_t name_len, int min_width, int min_height,
                                const RolltuiLayoutAction* actions, size_t actions_n, const RolltuiLayer* base,
                                const RolltuiLayer* popups, size_t popups_n, const RolltuiLayoutHooks* hooks,
                                RolltuiStr* out);

/* ---- registering a releaser (c/rolltui_lifetime.h) — MOVED, reached by nothing else --------- */
/* Registers a releaser to run at `rolltui_shutdown()`, in reverse order of registration.
 * Registering the same function twice registers it twice; register once, where the thing
 * is made. */
void rolltui_on_shutdown(void (*fn)(void));

/* ---- the menu widget's own surface (c/rolltui_widget_menu.h) --------------------------------- */
/* The type's name, and the reverse. A BORROW of a constant; NULL for an unknown name. */
const char* rolltui_input_type_name(unsigned char type, size_t* len);
int rolltui_input_type_from_name(const char* name, size_t len, unsigned char* out);

RolltuiMenu* rolltui_menu_new(void);
/* The menu's editor, BORROWED, valid for the menu's life. */
RolltuiInput* rolltui_menu_editor(const RolltuiMenu* m);
void rolltui_menu_free(RolltuiMenu* m);
void rolltui_menu_set_root(RolltuiMenu* m, const RolltuiMenuItem* root); /* by COPY; also resets */
const RolltuiMenuItem* rolltui_menu_level(const RolltuiMenu* m);
int rolltui_menu_dropdown_open(const RolltuiMenu* m);
size_t rolltui_menu_dropdown_selected(const RolltuiMenu* m);
const RolltuiMenuItem* rolltui_menu_selected_item(const RolltuiMenu* m);
void rolltui_menu_breadcrumb(const RolltuiMenu* m, RolltuiStr* out);
/* The breadcrumb rooted at `base` instead of the root's label — the WINDOW's title, so a level
 * reads "settings › Sort by" on the border and nowhere else; an empty base is the root's label. */
void rolltui_menu_title(const RolltuiMenu* m, const char* base, size_t base_len, RolltuiStr* out);
int rolltui_menu_editing(const RolltuiMenu* m);
const char* rolltui_menu_edit_reason(const RolltuiMenu* m, size_t* len);
void rolltui_menu_set_options_struct(RolltuiMenu* m, const RolltuiMenuOptions* o);
void rolltui_menu_layout(RolltuiMenu* m, RolltuiRect area);
void rolltui_menu_draw(const RolltuiMenu* m, RolltuiFrame* f, RolltuiDrawScratch* draw,
                       const RolltuiStyle* styles, const RolltuiMenuRoles* roles,
                       const RolltuiInputRoles* input_roles, int focused);
void rolltui_menu_dump_json(const RolltuiMenuItem* root, RolltuiStr* out);

/* ---- presenting a whole frame (c/rolltui_render.h) -------------------------------------------- */
/* The whole frame, from a cleared screen. Appends to `out`; never clears it. */
void rolltui_render_full(const RolltuiFrame* next, unsigned char depth, RolltuiStr* out);

/* ---- the role vocabulary, by name (c/rolltui_style.h) ----------------------------------------- */
/* The role's name, as the themes and layout files spell it (one word, lowercase, underscored). BORROWS a
 * static literal, valid for the life of the process; `*len` may be NULL. Returns "" for an
 * out-of-range value rather than reading past the table. */
const char* rolltui_role_name(unsigned char role, size_t* len);
/* The role of that name, or -1 when there is none. This is what a theme LOADER needs and had
 * no way to ask for in C — `rolltui_theme_load` was handed a vocabulary by its caller for
 * exactly this reason, and can now be handed the library's own. */
int rolltui_role_from_name(const char* name, size_t len);

/* ---- reading a theme file's depth name, and dumping a theme to JSON (c/rolltui_theme.h) ------ */
/* BORROWS a static literal; `*len` may be NULL. An out-of-range depth reads back as "mono"
 * and an out-of-range mode as "dark", which is what the C++ `color_depth_name` did and what a
 * zeroed byte means. */
const char* rolltui_color_depth_name(unsigned char depth, size_t* len);
/* Builds a fresh, OWNED tree (caller frees with `rolltui_json_free`) with a "roles" object
 * and, when anything is marked, an "effects" object. `light_styles`/`light_effects` NULL
 * together dump ONE variant plainly; non-NULL dumps BOTH as one object, a role or attribute
 * written as {"dark":..,"light":..} only where the two differ. */
RolltuiJsonValue* rolltui_theme_dump(const RolltuiStyle* dark_styles, const RolltuiEffectMap* dark_effects,
                                     const RolltuiStyle* light_styles, const RolltuiEffectMap* light_effects,
                                     const RolltuiThemeVocab* vocab);

/* ---- theme analysis: contrast, distinctness, badges, generation (c/rolltui_theme_analysis.h) -
 * `RolltuiLin`/`RolltuiBadges`/`RolltuiStrArray`/`RolltuiRoleCheck`/`RolltuiPairCheck` are this
 * module's own vocabulary. Every one of the seven functions below takes each only by POINTER,
 * never by value, so — unlike `RolltuiThemeVocab` and the rest of what those signatures need,
 * already public in rolltui.h — these five need nothing more than a forward declaration each:
 * the same shape `RolltuiLoadedLayout` and `RolltuiThemeEditor` above already use, and for the
 * same reason. Their full bodies stay solely in c/rolltui_theme_analysis.h, which the theme
 * editor's own kept internal header already pulls in wherever this file's copies would
 * otherwise collide with them — a full body copied here too would be a genuine duplicate
 * definition, which C (unlike a repeated function declaration) does not allow. */
typedef struct RolltuiLin RolltuiLin;
typedef struct RolltuiBadges RolltuiBadges;
typedef struct RolltuiStrArray RolltuiStrArray;
typedef struct RolltuiRoleCheck RolltuiRoleCheck;
typedef struct RolltuiPairCheck RolltuiPairCheck;

/* 1 on success, 0 for `Color::none()` (the terminal's own colour — unknown, not assumed). */
int rolltui_to_linear(RolltuiStyleColorRaw c, RolltuiLin* out);
void rolltui_str_array_release(RolltuiStrArray* a);
size_t rolltui_must_differ_count(void); /* returns ROLLTUI_MUST_DIFFER_COUNT */
/* CALLER-FILLED: out_roles[0..role_count), out_pairs[0..rolltui_must_differ_count()). Mirrors
 * `rolltui::analyse` exactly, MINUS the notes. */
int rolltui_theme_analyse(const RolltuiStyle* styles, size_t role_count, RolltuiRoleCheck* out_roles,
                          RolltuiPairCheck* out_pairs, RolltuiBadges* out_badges);
/* Composes the full report exactly as `rolltui::report_text` did. APPENDS to `*out`. */
void rolltui_theme_report_text(const RolltuiRoleCheck* roles, size_t role_count, const RolltuiPairCheck* pairs,
                               size_t pair_count, const RolltuiBadges* badges, const RolltuiStr* notes,
                               size_t notes_n, const RolltuiThemeVocab* vocab, RolltuiStr* out);
/* A theme's claimed badges against the computed ones: appends each claim that does NOT hold. */
void rolltui_check_claims(const RolltuiJsonValue* meta, const RolltuiBadges* badges, RolltuiStrArray* out);
int rolltui_theme_generate(uint64_t seed, unsigned char ruleset, double chaos, int has_dark, int dark_value,
                           int max_repair_passes, const RolltuiThemeVocab* vocab, RolltuiStyle* out_styles,
                           size_t role_count, RolltuiStr* out_name, RolltuiJsonValue** out_meta, int* out_repairs,
                           RolltuiRoleCheck* out_roles, RolltuiPairCheck* out_pairs, RolltuiBadges* out_badges);

/* ---- the ruleset vocabulary, by name (c/rolltui_theme_gen.h) --------------------------------- */
/* 1 and `*out` set on a match, 0 (leaving `*out` untouched) otherwise. */
int rolltui_ruleset_from_name(const char* name, size_t len, unsigned char* out);

/* ---- reading a transcript's own state (c/rolltui_widget_transcript.h) ------------------------ */
void rolltui_transcript_scroll(const RolltuiTranscript* t, RolltuiScrollAnchor* out);
size_t rolltui_transcript_total_lines(const RolltuiTranscript* t);
size_t rolltui_transcript_top_line(const RolltuiTranscript* t);
const char* rolltui_transcript_query(const RolltuiTranscript* t, size_t* len);
size_t rolltui_transcript_match_count(const RolltuiTranscript* t);
size_t rolltui_transcript_current_match_number(const RolltuiTranscript* t);

/* ---- the built-in kinds' own roles, and the theme editor's window (c/rolltui_widget_kinds.h) — MOVED, reached by nothing else */
const RolltuiBuiltinRoles* rolltui_windows_builtin_roles(const RolltuiWindows* w);
const RolltuiMenuRoles* rolltui_windows_menu_roles(const RolltuiWindows* w);
/* The full type is in rolltui_theme_editor.h, which the theme editor already includes for its
 * own reasons; forward-declared here only so these prototypes parse. */
typedef struct RolltuiThemeEditor RolltuiThemeEditor;
/* The hint line a host that DRIVES this editor wants said. */
void rolltui_windows_set_theme_hint(RolltuiWindows* w, const char* content, size_t len,
                                    const char* text, size_t text_len, int is_problem);
RolltuiThemeEditor* rolltui_windows_theme_editor(RolltuiWindows* w, const char* content, size_t len);

/* ---- the window registry's own accessors (c/rolltui_widgets.h) ------------------------------- */
void rolltui_context_set_error_factory(RolltuiContext* ctx, RolltuiWidgetFactory factory, void* factory_ctx);
const char* rolltui_windows_dir(const RolltuiWindows* w, size_t* len);
const char* rolltui_windows_host_menu(const RolltuiWindows* w, const char* name, size_t len, size_t* out_len);
/* MOVED, reached by nothing else. */
size_t rolltui_windows_host_menu_count(const RolltuiWindows* w);
const char* rolltui_windows_host_menu_name_at(const RolltuiWindows* w, size_t i, size_t* len);

/* ---- appending one string to a growing list (c/rolltui_str.h) -------------------------------- */
RolltuiStr* rolltui_str_list_add(RolltuiStrListRaw* l, const char* s, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_STUDIO_H */
