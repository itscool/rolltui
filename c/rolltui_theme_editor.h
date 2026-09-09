#ifndef ROLLTUI_C_THEME_EDITOR_H
#define ROLLTUI_C_THEME_EDITOR_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, by rolltui's authoring tool, and by a suite
 * that opts in by including this header by name. */
/*
 * rolltui/c/rolltui_theme_editor.h — EDITING A THEME, as a model with no terminal in it.
 *
 * A `RolltuiMenu` over a theme's two variants — Roles > <role> > fg > <palette entry> is three
 * levels of the same navigation every other menu uses — plus the recovery model that makes an
 * editor drawn IN the theme it is editing safe: every change applies LIVE to the preview as the
 * selection moves or the text is typed, Enter COMMITS, Escape (or Left) CANCELS the focused
 * change and the field returns to its committed value, and undo/redo walk whole-theme snapshots.
 * Undo and reset work blind, which is what makes an unreadable choice recoverable by someone who
 * can no longer see the screen.
 *
 * IT IS THE LIBRARY'S AND NOT A TOOL'S, which is the whole point of it living here. A widget
 * kind (`theme`, rolltui_widget_kinds.c) is a model plus a draw and a handle, and this is the
 * model; an app gets a theme editor by naming the kind in a layout and binding a key, with no
 * editor code of its own. Before this file the editor was one program's, and every other app
 * could load a theme and not change one.
 *
 * WHAT THIS MODEL DOES NOT DO, and who does it instead:
 *   - it never draws. The `theme` widget kind draws its menu, its role sample and its two
 *     status lines; a host that wants a different arrangement drives the same model.
 *   - it never writes a file. It hands back an OUTCOME — save as, load, reset, write shipped —
 *     and whoever mounted it decides what that means. The `theme` kind answers them against the
 *     preset store it is given.
 *   - it never decides what a theme classifies AS. `rolltui_theme_badges_json` computes that
 *     from the colours, and `colours_json` writes the answer into the file it produces.
 *
 * THE BOUNDARY'S RULES:
 *   1. **THE CALLER OWNS EVERY BUFFER**: text out goes into a caller's `RolltuiStr`, and the
 *      style tables, palette entries and fixes come back as BORROWS with a stated window.
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function except plain scalars and
 *      those borrows.
 *   3. **THE EDITOR IS OPAQUE.** Its undo-tracked value owns two JSON trees, and a struct with
 *      an owning pointer in it is a struct whose `=` in C++ is a synthesised deep copy and in C
 *      a shallow one — the same double free that shape has already produced here. Nothing
 *      crosses but scalars, borrows and ownership transfers that say so.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- what the editor asks of whoever mounted it --------------------------------------------
 * `Changed` means redraw; `Committed` means the committed theme moved and should be written
 * where the app keeps it. The rest name a decision only the host can take. */
#define ROLLTUI_THEME_EDIT_NONE 0
#define ROLLTUI_THEME_EDIT_CHANGED 1
#define ROLLTUI_THEME_EDIT_COMMITTED 2
#define ROLLTUI_THEME_EDIT_SAVE_AS 3       /* `value`: the preset name typed */
#define ROLLTUI_THEME_EDIT_WRITE_SHIPPED 4 /* `value`: the shipped preset chosen */
#define ROLLTUI_THEME_EDIT_LOAD_PRESET 5   /* `value`: the preset chosen */
#define ROLLTUI_THEME_EDIT_RESET_LOADED 6
#define ROLLTUI_THEME_EDIT_RESET_BUILTIN 7
#define ROLLTUI_THEME_EDIT_CHECK 8
#define ROLLTUI_THEME_EDIT_CLOSED 9

typedef struct RolltuiThemeEditorOutcome {
  unsigned char kind ROLLTUI_DEFAULT(0);
  RolltuiStr value; /* OWNED — release with `rolltui_theme_editor_outcome_release` */
} RolltuiThemeEditorOutcome;

/* Frees `value` and zeroes. Safe on a zeroed outcome and on repeated calls. */
void rolltui_theme_editor_outcome_release(RolltuiThemeEditorOutcome* o);

typedef struct RolltuiThemeEditor RolltuiThemeEditor;

/* A fresh editor over the built-in `default-dark`/`default-light` pair, so one that has been
 * handed nothing still has a theme to show. Free with `rolltui_theme_editor_free`. */
RolltuiThemeEditor* rolltui_theme_editor_new(void);
void rolltui_theme_editor_free(RolltuiThemeEditor* e); /* a no-op on NULL */

/* Loads a preset's colours object (both variants) as the baseline; the undo stack restarts.
 * `report` receives the DARK variant's load problems (the light variant's are the same file's
 * and are dropped). 0, with the editor keeping what it had, when the colours are unusable. */
int rolltui_theme_editor_load(RolltuiThemeEditor* e, const RolltuiJsonValue* colours, RolltuiThemeReport* report);

/* The Load and Write-shipped choices' options. Both COPY; neither takes ownership. */
void rolltui_theme_editor_set_presets(RolltuiThemeEditor* e, const RolltuiStrList* names);
void rolltui_theme_editor_set_shipped(RolltuiThemeEditor* e, const RolltuiStrList* names, int may_write);

void rolltui_theme_editor_set_mode(RolltuiThemeEditor* e, unsigned char mode); /* ROLLTUI_MODE_* */
unsigned char rolltui_theme_editor_mode(const RolltuiThemeEditor* e);

/* A variant's style table — `committed` 0 for what is being previewed right now (the committed
 * value plus any live, uncommitted change), 1 for the committed value itself. A BORROW of the
 * editor's own storage, valid until the next event, load or replace. */
const RolltuiStyle* rolltui_theme_editor_styles(const RolltuiThemeEditor* e, unsigned char mode, int committed);
/* The committed variant's own name, and its "meta" tree (NULL when it claims none). Both
 * BORROWS with the same window. */
const char* rolltui_theme_editor_variant_name(const RolltuiThemeEditor* e, unsigned char mode, size_t* len);
const RolltuiJsonValue* rolltui_theme_editor_variant_meta(const RolltuiThemeEditor* e, unsigned char mode);
int rolltui_theme_editor_previewing(const RolltuiThemeEditor* e);

/* The committed variants as ONE colours object: {"name", "meta", "roles", ["effects"]} in that
 * order, which is the order a preset store's own comparison expects. "meta" always carries a
 * freshly computed "badges" — the classification is a required field and a carried-through one
 * is stale under the edit being saved. OWNED: free with `rolltui_json_free`, or hand it
 * straight to a store. */
RolltuiJsonValue* rolltui_theme_editor_colours_json(const RolltuiThemeEditor* e, const char* name, size_t len);

/* The menu to lay out and draw. A BORROW of the editor's own; it outlives every call but not
 * the editor. */
RolltuiMenu* rolltui_theme_editor_menu(RolltuiThemeEditor* e);

/* An event already routed to the editor. `nav` is the live bindings table (the `menu`, `edit`
 * and `editor` scopes are read). `*out` is RESET first and is the caller's to release. */
void rolltui_theme_editor_handle(RolltuiThemeEditor* e, const RolltuiEvent* ev, const RolltuiBindings* nav,
                                 RolltuiThemeEditorOutcome* out);

int rolltui_theme_editor_undo(RolltuiThemeEditor* e); /* 0 when there is nothing to undo */
int rolltui_theme_editor_redo(RolltuiThemeEditor* e);
size_t rolltui_theme_editor_undo_depth(const RolltuiThemeEditor* e);
size_t rolltui_theme_editor_redo_depth(const RolltuiThemeEditor* e);

/* Puts a whole theme in as the new committed value (a reset, a generated theme). `dark_meta`,
 * `light_meta` and `dark_effects` are ADOPTED and may each be NULL — a NULL meta means the
 * variant claims none, and NULL effects keep whatever the editor already has, which is what a
 * generated theme wants since it brings no motion of its own. `dark`/`light` are read and not
 * kept. */
void rolltui_theme_editor_replace(RolltuiThemeEditor* e, const RolltuiStyle* dark, const RolltuiStyle* light,
                                  const char* dark_name, size_t dark_name_len, const char* light_name,
                                  size_t light_name_len, RolltuiJsonValue* dark_meta, RolltuiJsonValue* light_meta,
                                  RolltuiEffectMap* dark_effects);

/* For a host's sample box: the role the menu is on (0 when it is at a level that names none),
 * and the colour highlighted in a palette choice or being typed into a custom field. Each
 * returns 1 when there is an answer. */
int rolltui_theme_editor_focused_role(const RolltuiThemeEditor* e, unsigned char* out);
int rolltui_theme_editor_highlighted_color(const RolltuiThemeEditor* e, RolltuiStyleColor* out);

/* One line each, APPENDED to `out` (a fresh caller passes a zeroed `RolltuiStr`): what the
 * editor is doing and how deep undo goes; the badges the variant being edited computes as; and
 * the full contrast / colour-vision report. */
void rolltui_theme_editor_status_line(const RolltuiThemeEditor* e, RolltuiStr* out);
void rolltui_theme_editor_badges_line(const RolltuiThemeEditor* e, RolltuiStr* out);
void rolltui_theme_editor_report(const RolltuiThemeEditor* e, RolltuiStr* out);

/* The palette the fg/bg choices offer: "none", then every distinct colour either variant uses.
 * `id` is the colour's own spelling (what a choice's value is); `label` names it through the
 * theme's `defs` where one exists. Both BORROW until the next commit, load or replace. */
size_t rolltui_theme_editor_palette_count(const RolltuiThemeEditor* e);
const char* rolltui_theme_editor_palette_id(const RolltuiThemeEditor* e, size_t i, size_t* len);
const char* rolltui_theme_editor_palette_label(const RolltuiThemeEditor* e, size_t i, size_t* len);
int rolltui_theme_editor_palette_color(const RolltuiThemeEditor* e, size_t i, RolltuiStyleColor* out);

/* The auto-fix proposals for the variant being edited, refreshed on every commit. A BORROW
 * with the same window. */
size_t rolltui_theme_editor_fix_count(const RolltuiThemeEditor* e);
const RolltuiFix* rolltui_theme_editor_fix_at(const RolltuiThemeEditor* e, size_t i);

/* ---- THE LINES A THEME EDITOR DRAWS, IN ONE PLACE -------------------------------------------
 *
 * The library's `theme` widget kind and rolltui's own studio both draw a theme editor, and both
 * used to spell these four strings themselves. THEY DRIFTED, and not subtly: three of the
 * studio's copies were UTF-8 read as Latin-1 and shipped that way for months, while the
 * library's stayed correct — two spellings of one thing, and only one of them wrong, which is
 * the failure the vocabulary rule names.
 *
 * Spelled as hex escapes so no encoding round trip can corrupt them again, and defined once so a
 * change reaches both drawings. The two editors are still two implementations — collapsing THAT
 * is a restructure, and it is recorded rather than pretended away — but they can no longer
 * disagree about their own words. */
#define ROLLTUI_THEME_EDITOR_SAMPLE " Aa  the quick brown fox \xE2\x80\x94 sample in this role "
#define ROLLTUI_THEME_EDITOR_SWATCH_MARK "  \xE2\x96\xB6 "
#define ROLLTUI_THEME_EDITOR_BREADCRUMB \
  "Roles \xE2\x80\xBA a role \xE2\x80\xBA fg \xE2\x80\xBA a colour; the screen is the preview"
#define ROLLTUI_THEME_EDITOR_KEYS_HINT \
  "type to filter \xC2\xB7 Enter commits \xC2\xB7 Esc cancels \xC2\xB7 Ctrl-Z / Ctrl-Y"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_THEME_EDITOR_H */
