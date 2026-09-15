/* rolltui/c/rolltui_theme_editor.c — see rolltui_theme_editor.h for the contract and for what
 * this model deliberately does not do. The recovery rules it implements (live preview, Enter
 * commits, Escape cancels, undo/redo over whole-theme snapshots) are stated there and asserted
 * in `rolltui/tests/theme_editor_test.cpp`; none of it is repeated here. */
#include "rolltui/c/rolltui_theme_editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_widget_input.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_widget_menu.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_theme_analysis.h"
#include "rolltui/c/rolltui_theme_gen.h"
#include "rolltui/c/rolltui_undo.h"

#define K(s) (s), strlen(s)

/* A mode's own word. `K()` cannot sit in a conditional (it is a comma expression), and the two
 * spellings are the theme file format's own. */
static const char* mode_word(unsigned char m) { return m == ROLLTUI_MODE_LIGHT ? "light" : "dark"; }

/* The five attribute names a role carries, in the order the menu lists them. One table, read
 * by the menu builder, the value sync and the toggle handler. */
static const char* const kAttrs[] = {"bold", "italic", "underline", "dim", "reverse"};
#define ATTR_COUNT 5

static unsigned char* attr_of(RolltuiStyle* s, const char* name, size_t len) {
  if (len == 4 && memcmp(name, "bold", 4) == 0) return &s->bold;
  if (len == 6 && memcmp(name, "italic", 6) == 0) return &s->italic;
  if (len == 9 && memcmp(name, "underline", 9) == 0) return &s->underline;
  if (len == 3 && memcmp(name, "dim", 3) == 0) return &s->dim;
  if (len == 7 && memcmp(name, "reverse", 7) == 0) return &s->reverse;
  return NULL;
}

/* ---- the undo-tracked value ---------------------------------------------------------------
 * Both variants' styles, names and "meta". META IS CARRIED because it holds a generated
 * theme's provenance ("generator": {ruleset, seed, chaos}); dropping it would lose that the
 * moment the theme was saved. EFFECTS are not part of it: nothing in this menu touches them and
 * only the dark variant's are ever dumped, so the editor keeps the one live map as a plain
 * member instead of asking every commit to clone one nothing changed. */
typedef struct Edit {
  RolltuiStyle dark[ROLLTUI_ROLE_COUNT], light[ROLLTUI_ROLE_COUNT];
  RolltuiStr dark_name, light_name;
  RolltuiJsonValue* dark_meta;  /* OWNED; NULL when the variant claims none */
  RolltuiJsonValue* light_meta; /* OWNED; ditto */
} Edit;

static void edit_init(Edit* e) { memset(e, 0, sizeof *e); }

static void edit_release(Edit* e) {
  if (!e) return;
  rolltui_str_free(&e->dark_name);
  rolltui_str_free(&e->light_name);
  rolltui_json_free(e->dark_meta);
  rolltui_json_free(e->light_meta);
  memset(e, 0, sizeof *e);
}

static void edit_copy(Edit* to, const Edit* from) {
  memcpy(to->dark, from->dark, sizeof to->dark);
  memcpy(to->light, from->light, sizeof to->light);
  rolltui_str_set(&to->dark_name, from->dark_name.p, from->dark_name.n);
  rolltui_str_set(&to->light_name, from->light_name.p, from->light_name.n);
  rolltui_json_free(to->dark_meta);
  to->dark_meta = rolltui_json_clone(from->dark_meta);
  rolltui_json_free(to->light_meta);
  to->light_meta = rolltui_json_clone(from->light_meta);
}

/* GROWING HEAP (rolltui_alloc.h strategy 5): an undo snapshot outlives the call that makes it
 * and there is no bound on how many an editing session produces. */
static Edit* edit_clone_owned(const Edit* from) {
  Edit* e = (Edit*)rolltui_mem_alloc(sizeof *e);
  edit_init(e);
  edit_copy(e, from);
  return e;
}

static void edit_free_owned(void* p) {
  edit_release((Edit*)p);
  rolltui_mem_free(p);
}

/* The COLOURS are the value; a name or a provenance note is not an edit anyone undoes. */
static int edit_equal(const Edit* a, const Edit* b) {
  return memcmp(a->dark, b->dark, sizeof a->dark) == 0 && memcmp(a->light, b->light, sizeof a->light) == 0;
}

/* ---- the palette ---------------------------------------------------------------------------
 * GROWING AMORTISED (strategy 2): rebuilt on every load and on any commit that introduces a
 * colour, appended one entry at a time. */
typedef struct PaletteEntry {
  RolltuiStyleColor color;
  RolltuiStr id;    /* the colour's own spelling — a choice's option id */
  RolltuiStr label; /* the id, prefixed by the `defs` name for that colour where there is one */
} PaletteEntry;

struct RolltuiThemeEditor {
  RolltuiMenu* menu;
  Edit current;              /* committed + any live, uncommitted change */
  RolltuiUndoStack* undo;    /* snapshots of Edit */
  Edit preview;              /* the committed value while a live change is shown */
  int previewing;
  PaletteEntry* palette;
  size_t palette_n, palette_cap;
  RolltuiFixArray fixes;
  RolltuiStrList presets, shipped;
  int may_write_shipped;
  unsigned char mode;
  RolltuiEffectMap* dark_effects; /* OWNED, never NULL */
  RolltuiJsonValue* defs;         /* OWNED clone of the loaded theme's `defs`; may be NULL */
  RolltuiStr status;
};

static const Edit* committed_of(const RolltuiThemeEditor* e) { return (const Edit*)rolltui_undo_current(e->undo); }

static RolltuiStyle* style_table(RolltuiThemeEditor* e, unsigned char mode) {
  return mode == ROLLTUI_MODE_LIGHT ? e->current.light : e->current.dark;
}

static unsigned char default_effect_fallback_role(void) {
  const int r = rolltui_role_from_name(K("accent_1"));
  return r >= 0 ? (unsigned char)r : 0;
}

/* ---- menu glue ---------------------------------------------------------------------------- */

static void set_options_list(RolltuiMenu* m, const char* id, RolltuiMenuItemList* list) {
  rolltui_menu_set_options(m, id, strlen(id), list);
  rolltui_menu_list_release(list);
}

static RolltuiMenuItem* list_add(RolltuiMenuItemList* l, unsigned char kind, const char* id, size_t id_len,
                                 const char* label, size_t label_len) {
  RolltuiMenuItem* it = rolltui_menu_list_add(l);
  rolltui_menu_item_set(it, kind, id, id_len, label, label_len, NULL, 0);
  return it;
}

static void list_add_action(RolltuiMenuItemList* l, const char* id, size_t id_len, const char* label,
                            size_t label_len) {
  list_add(l, ROLLTUI_MENU_ACTION, id, id_len, label, label_len);
}

/* ---- colour text -------------------------------------------------------------------------- */

static void color_id(RolltuiStyleColor c, char* buf, size_t cap, size_t* len) { *len = rolltui_color_to_string(c, buf, cap); }

/* ---- "role.<name>.<field>": the id shape every colour and attribute field uses -------------
 * A ".custom" suffix is the typed-in form of the same field, so it resolves to the same one. */
typedef struct Field {
  unsigned char role;
  const char* name; /* into the caller's id bytes */
  size_t name_len;
  unsigned char ok;
} Field;

static Field field_of(const char* id, size_t len) {
  Field f;
  size_t i, dot;
  memset(&f, 0, sizeof f);
  if (len < 5 || memcmp(id, "role.", 5) != 0) return f;
  id += 5;
  len -= 5;
  dot = len;
  for (i = 0; i < len; ++i)
    if (id[i] == '.') { dot = i; break; }
  if (dot == len) return f;
  {
    const int r = rolltui_role_from_name(id, dot);
    if (r < 0) return f;
    f.role = (unsigned char)r;
  }
  f.name = id + dot + 1;
  f.name_len = len - dot - 1;
  if (f.name_len > 7 && memcmp(f.name + f.name_len - 7, ".custom", 7) == 0) f.name_len -= 7;
  f.ok = 1;
  return f;
}

/* The role a LEVEL or an item id names, without needing a field after it. */
static int role_in(const char* id, size_t len, unsigned char* out) {
  size_t i, dot;
  int r;
  if (len < 5 || memcmp(id, "role.", 5) != 0) return 0;
  id += 5;
  len -= 5;
  dot = len;
  for (i = 0; i < len; ++i)
    if (id[i] == '.') { dot = i; break; }
  r = rolltui_role_from_name(id, dot);
  if (r < 0) return 0;
  *out = (unsigned char)r;
  return 1;
}

static int field_is_colour(const Field* f) {
  return f->ok && ((f->name_len == 2 && memcmp(f->name, "fg", 2) == 0) ||
                   (f->name_len == 2 && memcmp(f->name, "bg", 2) == 0));
}

/* ---- construction --------------------------------------------------------------------------*/

static void rebuild_palette(RolltuiThemeEditor* e);
static void rebuild_menu(RolltuiThemeEditor* e);
static void sync_values(RolltuiThemeEditor* e);
static void refresh_fixes(RolltuiThemeEditor* e);

RolltuiThemeEditor* rolltui_theme_editor_new(void) {
  RolltuiThemeEditor* e = (RolltuiThemeEditor*)rolltui_mem_alloc(sizeof *e);
  memset(e, 0, sizeof *e);
  e->menu = rolltui_menu_new();
  e->undo = rolltui_undo_new((size_t)-1, edit_free_owned);
  e->mode = ROLLTUI_MODE_DARK;
  edit_init(&e->current);
  edit_init(&e->preview);
  e->dark_effects = rolltui_theme_builtin_fill(K("default-dark"), e->current.dark, ROLLTUI_ROLE_COUNT);
  if (!e->dark_effects) e->dark_effects = rolltui_effect_map_new(ROLLTUI_EFFECT_STATE_COUNT, default_effect_fallback_role());
  rolltui_str_set(&e->current.dark_name, K("default-dark"));
  rolltui_effect_map_free(rolltui_theme_builtin_fill(K("default-light"), e->current.light, ROLLTUI_ROLE_COUNT));
  rolltui_str_set(&e->current.light_name, K("default-light"));
  rolltui_undo_reset(e->undo, edit_clone_owned(&e->current));
  rebuild_palette(e);
  rebuild_menu(e);
  return e;
}

void rolltui_theme_editor_free(RolltuiThemeEditor* e) {
  size_t i;
  if (!e) return;
  rolltui_menu_free(e->menu);
  rolltui_undo_free(e->undo);
  edit_release(&e->current);
  edit_release(&e->preview);
  for (i = 0; i < e->palette_n; ++i) {
    rolltui_str_free(&e->palette[i].id);
    rolltui_str_free(&e->palette[i].label);
  }
  rolltui_mem_free(e->palette);
  rolltui_fix_array_release(&e->fixes);
  rolltui_str_list_release(&e->presets);
  rolltui_str_list_release(&e->shipped);
  rolltui_effect_map_free(e->dark_effects);
  rolltui_json_free(e->defs);
  rolltui_str_free(&e->status);
  rolltui_mem_free(e);
}

void rolltui_theme_editor_outcome_release(RolltuiThemeEditorOutcome* o) {
  if (!o) return;
  rolltui_str_free(&o->value);
  memset(o, 0, sizeof *o);
}

/* A variant's own "meta" out of an already-parsed colours tree: clone whatever object is
 * there, then resolve a colour-pair-shaped "badges" ({"dark":[...], "light":[...]}) down to the
 * one list for `mode`, the same way a role's fg/bg pair resolves. */
static RolltuiJsonValue* extract_meta(const RolltuiJsonValue* colours, unsigned char mode) {
  const RolltuiJsonValue* src = rolltui_json_get(colours, K("meta"));
  const RolltuiJsonValue* b;
  const char* key = mode == ROLLTUI_MODE_LIGHT ? "light" : "dark";
  RolltuiJsonValue* meta;
  if (!rolltui_json_is_object(src)) return NULL;
  meta = rolltui_json_clone(src);
  b = rolltui_json_get(meta, K("badges"));
  if (rolltui_json_is_object(b) && rolltui_json_has(b, key, strlen(key)))
    rolltui_json_set(meta, K("badges"), rolltui_json_clone(rolltui_json_get(b, key, strlen(key))));
  return meta;
}

int rolltui_theme_editor_load(RolltuiThemeEditor* e, const RolltuiJsonValue* colours, RolltuiThemeReport* report) {
  RolltuiStyle d[ROLLTUI_ROLE_COUNT], l[ROLLTUI_ROLE_COUNT];
  RolltuiStr d_name, l_name;
  RolltuiThemeReport light_rep;
  RolltuiEffectMap *d_eff, *l_eff;
  memset(&d_name, 0, sizeof d_name);
  memset(&l_name, 0, sizeof l_name);
  memset(&light_rep, 0, sizeof light_rep);
  d_eff = rolltui_theme_load(colours, ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), d, &d_name, report);
  l_eff = rolltui_theme_load(colours, ROLLTUI_MODE_LIGHT, rolltui_theme_default_vocab(), l, &l_name, &light_rep);
  rolltui_theme_report_release(&light_rep);
  if (!d_eff || !l_eff) {
    rolltui_str_free(&d_name);
    rolltui_str_free(&l_name);
    rolltui_effect_map_free(d_eff);
    rolltui_effect_map_free(l_eff);
    return 0;
  }
  memcpy(e->current.dark, d, sizeof d);
  memcpy(e->current.light, l, sizeof l);
  rolltui_str_move(&e->current.dark_name, &d_name);
  rolltui_str_move(&e->current.light_name, &l_name);
  rolltui_str_free(&d_name);
  rolltui_str_free(&l_name);
  rolltui_json_free(e->current.dark_meta);
  e->current.dark_meta = extract_meta(colours, ROLLTUI_MODE_DARK);
  rolltui_json_free(e->current.light_meta);
  e->current.light_meta = extract_meta(colours, ROLLTUI_MODE_LIGHT);
  rolltui_effect_map_free(e->dark_effects);
  e->dark_effects = d_eff;
  rolltui_effect_map_free(l_eff); /* discarded: only dark's effects are ever dumped */
  rolltui_json_free(e->defs);
  e->defs = rolltui_json_clone(rolltui_json_get(colours, K("defs")));
  rolltui_undo_reset(e->undo, edit_clone_owned(&e->current));
  e->previewing = 0;
  rebuild_palette(e);
  rebuild_menu(e);
  rolltui_str_set(&e->status, K("loaded"));
  return 1;
}

static void names_to_options(const RolltuiStrList* names, RolltuiMenuItemList* out) {
  size_t i;
  for (i = 0; i < names->n; ++i)
    list_add_action(out, names->v[i].p, names->v[i].n, names->v[i].p, names->v[i].n);
}

static void copy_str_list(RolltuiStrList* to, const RolltuiStrList* from) {
  size_t i;
  rolltui_str_list_clear(to);
  if (!from) return;
  for (i = 0; i < from->n; ++i) rolltui_str_list_add(to, from->v[i].p, from->v[i].n);
}

void rolltui_theme_editor_set_presets(RolltuiThemeEditor* e, const RolltuiStrList* names) {
  RolltuiMenuItemList opts;
  memset(&opts, 0, sizeof opts);
  copy_str_list(&e->presets, names);
  names_to_options(&e->presets, &opts);
  set_options_list(e->menu, "load", &opts);
}

void rolltui_theme_editor_set_shipped(RolltuiThemeEditor* e, const RolltuiStrList* names, int may_write) {
  RolltuiMenuItemList opts;
  memset(&opts, 0, sizeof opts);
  copy_str_list(&e->shipped, names);
  e->may_write_shipped = may_write;
  names_to_options(&e->shipped, &opts);
  set_options_list(e->menu, "write_shipped", &opts);
  rolltui_menu_set_enabled(e->menu, K("write_shipped"), may_write && e->shipped.n != 0);
}

static void cancel_preview(RolltuiThemeEditor* e) {
  if (!e->previewing) return;
  edit_copy(&e->current, &e->preview);
  e->previewing = 0;
}

static void begin_preview(RolltuiThemeEditor* e) {
  if (e->previewing) return;
  edit_copy(&e->preview, &e->current);
  e->previewing = 1;
}

void rolltui_theme_editor_set_mode(RolltuiThemeEditor* e, unsigned char mode) {
  cancel_preview(e);
  e->mode = mode == ROLLTUI_MODE_LIGHT ? ROLLTUI_MODE_LIGHT : ROLLTUI_MODE_DARK;
  rolltui_menu_set_value(e->menu, K("mode"), K(mode_word(e->mode)));
  sync_values(e);
  refresh_fixes(e);
}

unsigned char rolltui_theme_editor_mode(const RolltuiThemeEditor* e) { return e->mode; }

const RolltuiStyle* rolltui_theme_editor_styles(const RolltuiThemeEditor* e, unsigned char mode, int committed) {
  const Edit* v = committed ? committed_of(e) : &e->current;
  return mode == ROLLTUI_MODE_LIGHT ? v->light : v->dark;
}

const char* rolltui_theme_editor_variant_name(const RolltuiThemeEditor* e, unsigned char mode, size_t* len) {
  const Edit* v = committed_of(e);
  const RolltuiStr* s = mode == ROLLTUI_MODE_LIGHT ? &v->light_name : &v->dark_name;
  if (len) *len = s->n;
  return s->p ? s->p : "";
}

const RolltuiJsonValue* rolltui_theme_editor_variant_meta(const RolltuiThemeEditor* e, unsigned char mode) {
  const Edit* v = committed_of(e);
  return mode == ROLLTUI_MODE_LIGHT ? v->light_meta : v->dark_meta;
}

int rolltui_theme_editor_previewing(const RolltuiThemeEditor* e) { return e->previewing; }

RolltuiJsonValue* rolltui_theme_editor_colours_json(const RolltuiThemeEditor* e, const char* name, size_t len) {
  /* {"name", "meta", "roles", ["effects"]} built in THAT order: a preset store compares this
   * tree against what it loaded, and a key-order difference alone reads as "modified". */
  const Edit* c = committed_of(e);
  RolltuiJsonValue* root = rolltui_json_object();
  RolltuiJsonValue* meta;
  RolltuiJsonValue* badges;
  RolltuiJsonValue* dump;
  const RolltuiJsonValue* fx;
  rolltui_json_set(root, K("name"), rolltui_json_string(name, len));
  /* THE CLASSIFICATION IS WRITTEN FRESH, NEVER CARRIED. `meta.badges` is a required field and
   * the loader recomputes it, so a writer that passed the loaded value through would emit a
   * declaration already stale under the edit being saved. Whatever else the loaded meta holds
   * (a generated theme's provenance) is kept. */
  meta = rolltui_json_is_object(c->dark_meta) ? rolltui_json_clone(c->dark_meta) : rolltui_json_object();
  badges = rolltui_theme_badges_json(c->dark, c->light, ROLLTUI_ROLE_COUNT);
  if (badges) rolltui_json_set(meta, K("badges"), badges);
  rolltui_json_set(root, K("meta"), meta);
  /* ONE effects object for both variants: motion is the theme's, not the terminal
   * background's, so the dumper takes a light effects map only to document that it is never
   * consulted. */
  dump = rolltui_theme_dump(c->dark, e->dark_effects, c->light, NULL, rolltui_theme_default_vocab());
  rolltui_json_set(root, K("roles"), rolltui_json_clone(rolltui_json_get(dump, K("roles"))));
  fx = rolltui_json_get(dump, K("effects"));
  if (!rolltui_json_is_null(fx)) rolltui_json_set(root, K("effects"), rolltui_json_clone(fx));
  rolltui_json_free(dump);
  return root;
}

RolltuiMenu* rolltui_theme_editor_menu(RolltuiThemeEditor* e) { return e->menu; }

/* ---- the palette --------------------------------------------------------------------------- */

static int palette_has(const RolltuiThemeEditor* e, const char* id, size_t len) {
  size_t i;
  for (i = 0; i < e->palette_n; ++i)
    if (rolltui_str_eq(&e->palette[i].id, id, len)) return 1;
  return 0;
}

/* `defs` names a colour when the entry is a plain colour string; the label is then
 * "<def name> = <colour>" rather than the colour alone. */
static void palette_label(const RolltuiThemeEditor* e, const char* id, size_t len, RolltuiStr* out) {
  size_t i, n;
  rolltui_str_clear(out);
  n = e->defs ? rolltui_json_object_size(e->defs) : 0;
  for (i = 0; i < n; ++i) {
    size_t klen = 0, vlen = 0, clen = 0;
    const char* k = rolltui_json_object_key_at(e->defs, i, &klen);
    const RolltuiJsonValue* v = rolltui_json_object_value_at(e->defs, i);
    const char* vs;
    RolltuiStyleColor c;
    char buf[ROLLTUI_COLOR_STRING_MAX];
    if (!rolltui_json_is_string(v)) continue;
    vs = rolltui_json_as_string(v, "", 0, &vlen);
    if (!rolltui_color_parse(vs, vlen, &c)) continue;
    color_id(c, buf, sizeof buf, &clen);
    if (clen != len || memcmp(buf, id, len) != 0) continue;
    rolltui_str_append(out, k, klen);
    rolltui_str_append(out, K(" = "));
    break;
  }
  rolltui_str_append(out, id, len);
}

static void palette_add(RolltuiThemeEditor* e, RolltuiStyleColor c) {
  char buf[ROLLTUI_COLOR_STRING_MAX];
  size_t len = 0;
  PaletteEntry* p;
  color_id(c, buf, sizeof buf, &len);
  if (palette_has(e, buf, len)) return;
  /* GROWING AMORTISED (strategy 2). */
  e->palette = (PaletteEntry*)rolltui_grow_zeroed(e->palette, &e->palette_cap, e->palette_n + 1, sizeof *e->palette);
  p = &e->palette[e->palette_n++];
  p->color = c;
  rolltui_str_set(&p->id, buf, len);
  palette_label(e, buf, len, &p->label);
}

static void rebuild_palette(RolltuiThemeEditor* e) {
  size_t i;
  RolltuiStyleColor none;
  for (i = 0; i < e->palette_n; ++i) {
    rolltui_str_free(&e->palette[i].id);
    rolltui_str_free(&e->palette[i].label);
  }
  e->palette_n = 0;
  memset(&none, 0, sizeof none);
  palette_add(e, none);
  for (i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    palette_add(e, e->current.dark[i].fg);
    palette_add(e, e->current.dark[i].bg);
  }
  for (i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    palette_add(e, e->current.light[i].fg);
    palette_add(e, e->current.light[i].bg);
  }
}

static void palette_options(const RolltuiThemeEditor* e, RolltuiMenuItemList* out) {
  size_t i;
  for (i = 0; i < e->palette_n; ++i)
    list_add_action(out, e->palette[i].id.p, e->palette[i].id.n, e->palette[i].label.p, e->palette[i].label.n);
}

size_t rolltui_theme_editor_palette_count(const RolltuiThemeEditor* e) { return e->palette_n; }

const char* rolltui_theme_editor_palette_id(const RolltuiThemeEditor* e, size_t i, size_t* len) {
  if (i >= e->palette_n) { if (len) *len = 0; return NULL; }
  if (len) *len = e->palette[i].id.n;
  return e->palette[i].id.p;
}

const char* rolltui_theme_editor_palette_label(const RolltuiThemeEditor* e, size_t i, size_t* len) {
  if (i >= e->palette_n) { if (len) *len = 0; return NULL; }
  if (len) *len = e->palette[i].label.n;
  return e->palette[i].label.p;
}

int rolltui_theme_editor_palette_color(const RolltuiThemeEditor* e, size_t i, RolltuiStyleColor* out) {
  if (i >= e->palette_n) return 0;
  *out = e->palette[i].color;
  return 1;
}

/* ---- the menu ------------------------------------------------------------------------------ */

static void role_base(unsigned char role, char* out, size_t cap, size_t* len) {
  size_t rn = 0;
  const char* r = rolltui_role_name(role, &rn);
  int n = snprintf(out, cap, "role.%.*s", (int)rn, r ? r : "");
  *len = n > 0 ? (size_t)n : 0;
}

static void rebuild_menu(RolltuiThemeEditor* e) {
  RolltuiMenuItemList top, roles, fields, opts, modes, rulesets, gen;
  RolltuiMenuItem root, *it;
  size_t i;
  char base[80], id[96];

  memset(&top, 0, sizeof top);
  memset(&roles, 0, sizeof roles);

  for (i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    size_t blen = 0, rn = 0;
    const char* rname = rolltui_role_name((unsigned char)i, &rn);
    size_t a;
    role_base((unsigned char)i, base, sizeof base, &blen);
    memset(&fields, 0, sizeof fields);
    for (a = 0; a < 2; ++a) {
      const char* which = a == 0 ? "fg" : "bg";
      int n = snprintf(id, sizeof id, "%.*s.%s", (int)blen, base, which);
      it = list_add(&fields, ROLLTUI_MENU_CHOICE, id, (size_t)n, which, strlen(which));
      memset(&opts, 0, sizeof opts);
      palette_options(e, &opts);
      rolltui_menu_list_release(&it->children);
      it->children = opts;
      memset(&opts, 0, sizeof opts);
    }
    for (a = 0; a < 2; ++a) {
      const char* which = a == 0 ? "fg" : "bg";
      const char* label = a == 0 ? "custom fg" : "custom bg";
      int n = snprintf(id, sizeof id, "%.*s.%s.custom", (int)blen, base, which);
      it = list_add(&fields, ROLLTUI_MENU_INPUT, id, (size_t)n, label, strlen(label));
      it->spec.type = ROLLTUI_INPUT_TYPE_COLOR;
    }
    for (a = 0; a < ATTR_COUNT; ++a) {
      int n = snprintf(id, sizeof id, "%.*s.%s", (int)blen, base, kAttrs[a]);
      list_add(&fields, ROLLTUI_MENU_TOGGLE, id, (size_t)n, kAttrs[a], strlen(kAttrs[a]));
    }
    it = list_add(&roles, ROLLTUI_MENU_SUBMENU, base, blen, rname, rn);
    rolltui_menu_list_release(&it->children);
    it->children = fields;
    memset(&fields, 0, sizeof fields);
  }

  it = list_add(&top, ROLLTUI_MENU_SUBMENU, K("roles"), K("Roles"));
  rolltui_menu_list_release(&it->children);
  it->children = roles;
  memset(&roles, 0, sizeof roles);

  memset(&modes, 0, sizeof modes);
  list_add_action(&modes, K("dark"), K("dark"));
  list_add_action(&modes, K("light"), K("light"));
  it = list_add(&top, ROLLTUI_MENU_CHOICE, K("mode"), K("Mode (edit + preview)"));
  rolltui_menu_list_release(&it->children);
  it->children = modes;
  memset(&modes, 0, sizeof modes);
  rolltui_str_set(&it->value, K(mode_word(e->mode)));

  list_add_action(&top, K("check"), K("Check: contrast, colour-vision, badges"));
  list_add(&top, ROLLTUI_MENU_SUBMENU, K("fixes"), K("Fixes (proposals; Enter applies one, undoable)"));

  memset(&gen, 0, sizeof gen);
  memset(&rulesets, 0, sizeof rulesets);
  for (i = 0; i < ROLLTUI_RULESET_COUNT; ++i) {
    size_t len = 0;
    const char* rn = rolltui_ruleset_name((unsigned char)i, &len);
    list_add_action(&rulesets, rn, len, rn, len);
  }
  it = list_add(&gen, ROLLTUI_MENU_CHOICE, K("gen.ruleset"), K("Ruleset"));
  rolltui_menu_list_release(&it->children);
  it->children = rulesets;
  memset(&rulesets, 0, sizeof rulesets);
  rolltui_str_set(&it->value, K("analogous"));
  it = list_add(&gen, ROLLTUI_MENU_INPUT, K("gen.seed"), K("Seed"));
  it->spec.type = ROLLTUI_INPUT_TYPE_INT;
  it->spec.min = 0;
  rolltui_str_set(&it->value, K("1"));
  it = list_add(&gen, ROLLTUI_MENU_INPUT, K("gen.chaos"), K("Chaos"));
  it->spec.type = ROLLTUI_INPUT_TYPE_FLOAT;
  it->spec.min = 0;
  it->spec.max = 1;
  it->spec.step = 0.1;
  it->spec.precision = 2;
  rolltui_str_set(&it->value, K("0"));
  list_add_action(&gen, K("gen.run"), K("Generate (replaces both variants, undoable)"));
  it = list_add(&top, ROLLTUI_MENU_SUBMENU, K("generate"), K("Generate a theme (seeded)"));
  rolltui_menu_list_release(&it->children);
  it->children = gen;
  memset(&gen, 0, sizeof gen);

  it = list_add(&top, ROLLTUI_MENU_ACTION, K("undo"), K("Undo"));
  rolltui_str_set(&it->shortcut, K("Ctrl-Z"));
  it = list_add(&top, ROLLTUI_MENU_ACTION, K("redo"), K("Redo"));
  rolltui_str_set(&it->shortcut, K("Ctrl-Y"));

  it = list_add(&top, ROLLTUI_MENU_CHOICE, K("load"), K("Load theme"));
  memset(&opts, 0, sizeof opts);
  names_to_options(&e->presets, &opts);
  rolltui_menu_list_release(&it->children);
  it->children = opts;
  memset(&opts, 0, sizeof opts);

  it = list_add(&top, ROLLTUI_MENU_INPUT, K("save"), K("Save theme as"));
  it->spec.type = ROLLTUI_INPUT_TYPE_NAME;

  it = list_add(&top, ROLLTUI_MENU_CHOICE, K("write_shipped"), K("Write a SHIPPED preset (the editor's privilege)"));
  memset(&opts, 0, sizeof opts);
  names_to_options(&e->shipped, &opts);
  rolltui_menu_list_release(&it->children);
  it->children = opts;
  memset(&opts, 0, sizeof opts);

  list_add_action(&top, K("reset_loaded"), K("Reset to the loaded preset\xE2\x80\xA6"));
  list_add_action(&top, K("reset_builtin"), K("Reset to the built-in default\xE2\x80\xA6"));

  rolltui_menu_item_init(&root);
  rolltui_menu_item_set(&root, ROLLTUI_MENU_SUBMENU, K("root"), K("theme editor"), NULL, 0);
  rolltui_menu_list_release(&root.children);
  root.children = top;
  memset(&top, 0, sizeof top);
  /* A rebuild (a load, a replace) starts at the top level; a committed custom colour only
   * refreshes the option lists in place, keeping the position. */
  rolltui_menu_set_root(e->menu, &root);
  rolltui_menu_item_release(&root);
  rolltui_menu_set_enabled(e->menu, K("write_shipped"), e->may_write_shipped && e->shipped.n != 0);
  sync_values(e);
  refresh_fixes(e);
}

static void sync_values(RolltuiThemeEditor* e) {
  const Edit* c = committed_of(e);
  const RolltuiStyle* t = e->mode == ROLLTUI_MODE_LIGHT ? c->light : c->dark;
  size_t i;
  char base[80], id[96];
  for (i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    size_t blen = 0, a;
    char buf[ROLLTUI_COLOR_STRING_MAX];
    size_t clen = 0;
    int n;
    role_base((unsigned char)i, base, sizeof base, &blen);
    n = snprintf(id, sizeof id, "%.*s.fg", (int)blen, base);
    color_id(t[i].fg, buf, sizeof buf, &clen);
    rolltui_menu_set_value(e->menu, id, (size_t)n, buf, clen);
    n = snprintf(id, sizeof id, "%.*s.bg", (int)blen, base);
    color_id(t[i].bg, buf, sizeof buf, &clen);
    rolltui_menu_set_value(e->menu, id, (size_t)n, buf, clen);
    for (a = 0; a < ATTR_COUNT; ++a) {
      RolltuiStyle copy = t[i];
      const unsigned char* v = attr_of(&copy, kAttrs[a], strlen(kAttrs[a]));
      n = snprintf(id, sizeof id, "%.*s.%s", (int)blen, base, kAttrs[a]);
      rolltui_menu_set_checked(e->menu, id, (size_t)n, v && *v);
    }
  }
  rolltui_menu_set_value(e->menu, K("mode"), K(mode_word(e->mode)));
}

static void refresh_fixes(RolltuiThemeEditor* e) {
  const Edit* c = committed_of(e);
  RolltuiMenuItemList items;
  size_t i;
  char id[32];
  rolltui_propose_fixes(e->mode == ROLLTUI_MODE_LIGHT ? c->light : c->dark, ROLLTUI_ROLE_COUNT,
                        rolltui_theme_default_vocab(), &e->fixes);
  memset(&items, 0, sizeof items);
  for (i = 0; i < e->fixes.n; ++i) {
    const int n = snprintf(id, sizeof id, "fix.%zu", i);
    list_add_action(&items, id, (size_t)n, e->fixes.v[i].what.p, e->fixes.v[i].what.n);
  }
  if (e->fixes.n == 0) {
    RolltuiMenuItem* it = list_add(&items, ROLLTUI_MENU_ACTION, K("fix.none"), K("(nothing to fix in this variant)"));
    it->enabled = 0;
  }
  set_options_list(e->menu, "fixes", &items);
}

size_t rolltui_theme_editor_fix_count(const RolltuiThemeEditor* e) { return e->fixes.n; }

const RolltuiFix* rolltui_theme_editor_fix_at(const RolltuiThemeEditor* e, size_t i) {
  return i < e->fixes.n ? &e->fixes.v[i] : NULL;
}

/* ---- the three lines a host draws beside the menu ------------------------------------------ */

void rolltui_theme_editor_badges_line(const RolltuiThemeEditor* e, RolltuiStr* out) {
  RolltuiRoleCheck roles[ROLLTUI_ROLE_COUNT];
  RolltuiPairCheck pairs[ROLLTUI_MUST_DIFFER_COUNT];
  RolltuiBadges badges;
  RolltuiStrArray names;
  size_t i;
  memset(&names, 0, sizeof names);
  memset(&badges, 0, sizeof badges);
  rolltui_theme_analyse(rolltui_theme_editor_styles(e, e->mode, 0), ROLLTUI_ROLE_COUNT, roles, pairs, &badges);
  rolltui_badge_names(&badges, &names);
  rolltui_str_append(out, K("badges:"));
  for (i = 0; i < names.n; ++i) {
    rolltui_str_append(out, K(" "));
    rolltui_str_append_str(out, &names.v[i]);
  }
  if (names.n == 0) rolltui_str_append(out, K(" (none)"));
  rolltui_str_array_release(&names);
}

void rolltui_theme_editor_report(const RolltuiThemeEditor* e, RolltuiStr* out) {
  RolltuiRoleCheck roles[ROLLTUI_ROLE_COUNT];
  RolltuiPairCheck pairs[ROLLTUI_MUST_DIFFER_COUNT];
  RolltuiBadges badges;
  memset(&badges, 0, sizeof badges);
  rolltui_theme_analyse(rolltui_theme_editor_styles(e, e->mode, 0), ROLLTUI_ROLE_COUNT, roles, pairs, &badges);
  rolltui_theme_report_text(roles, ROLLTUI_ROLE_COUNT, pairs, rolltui_must_differ_count(), &badges, NULL, 0,
                            rolltui_theme_default_vocab(), out);
}

static void append_count(RolltuiStr* out, size_t n) {
  char buf[24];
  const int len = snprintf(buf, sizeof buf, "%zu", n);
  rolltui_str_append(out, buf, len > 0 ? (size_t)len : 0);
}

void rolltui_theme_editor_status_line(const RolltuiThemeEditor* e, RolltuiStr* out) {
  size_t reason_len = 0;
  const char* reason = rolltui_menu_edit_reason(e->menu, &reason_len);
  const int editing = rolltui_menu_editing(e->menu) != 0;
  if (editing && reason_len > 0) {
    rolltui_str_append(out, K("refused: "));
    rolltui_str_append(out, reason, reason_len);
  } else if (e->previewing) {
    rolltui_str_append(out, K("previewing \xE2\x80\x94 Enter commits, Esc cancels"));
  } else if (e->status.n == 0) {
    rolltui_str_append(out, K("Enter commits, Esc cancels"));
  } else {
    rolltui_str_append_str(out, &e->status);
  }
  rolltui_str_append(out, K(" \xC2\xB7 undo "));
  append_count(out, rolltui_undo_undo_depth(e->undo));
  rolltui_str_append(out, K(" \xC2\xB7 redo "));
  append_count(out, rolltui_undo_redo_depth(e->undo));
  rolltui_str_append(out, K(" \xC2\xB7 "));
  rolltui_str_append(out, K(mode_word(e->mode)));
}

/* ---- editing ------------------------------------------------------------------------------- */

static void apply_field(RolltuiThemeEditor* e, const Field* f, RolltuiStyleColor c) {
  RolltuiStyle* s = &style_table(e, e->mode)[f->role];
  if (f->name_len == 2 && memcmp(f->name, "fg", 2) == 0) s->fg = c;
  else if (f->name_len == 2 && memcmp(f->name, "bg", 2) == 0) s->bg = c;
}

static void commit_current(RolltuiThemeEditor* e, RolltuiThemeEditorOutcome* out) {
  size_t before;
  e->previewing = 0;
  if (edit_equal(&e->current, committed_of(e))) {
    out->kind = ROLLTUI_THEME_EDIT_CHANGED;
    return;
  }
  rolltui_undo_commit(e->undo, edit_clone_owned(&e->current));
  before = e->palette_n;
  rebuild_palette(e);
  if (e->palette_n != before) {
    size_t i;
    char base[80], id[96];
    for (i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
      size_t blen = 0, a;
      role_base((unsigned char)i, base, sizeof base, &blen);
      for (a = 0; a < 2; ++a) {
        RolltuiMenuItemList opts;
        const int n = snprintf(id, sizeof id, "%.*s.%s", (int)blen, base, a == 0 ? "fg" : "bg");
        memset(&opts, 0, sizeof opts);
        palette_options(e, &opts);
        rolltui_menu_set_options(e->menu, id, (size_t)n, &opts);
        rolltui_menu_list_release(&opts);
      }
    }
  }
  sync_values(e);
  refresh_fixes(e);
  out->kind = ROLLTUI_THEME_EDIT_COMMITTED;
}

void rolltui_theme_editor_replace(RolltuiThemeEditor* e, const RolltuiStyle* dark, const RolltuiStyle* light,
                                  const char* dark_name, size_t dark_name_len, const char* light_name,
                                  size_t light_name_len, RolltuiJsonValue* dark_meta, RolltuiJsonValue* light_meta,
                                  RolltuiEffectMap* dark_effects) {
  e->previewing = 0;
  if (dark) memcpy(e->current.dark, dark, sizeof e->current.dark);
  if (light) memcpy(e->current.light, light, sizeof e->current.light);
  rolltui_str_set(&e->current.dark_name, dark_name, dark_name_len);
  rolltui_str_set(&e->current.light_name, light_name, light_name_len);
  rolltui_json_free(e->current.dark_meta);
  e->current.dark_meta = dark_meta;
  rolltui_json_free(e->current.light_meta);
  e->current.light_meta = light_meta;
  if (dark_effects) {
    rolltui_effect_map_free(e->dark_effects);
    e->dark_effects = dark_effects;
  }
  rolltui_undo_commit(e->undo, edit_clone_owned(&e->current));
  rebuild_palette(e);
  rebuild_menu(e);
}

static int restore_from_undo(RolltuiThemeEditor* e) {
  edit_copy(&e->current, committed_of(e));
  sync_values(e);
  refresh_fixes(e);
  return 1;
}

int rolltui_theme_editor_undo(RolltuiThemeEditor* e) {
  cancel_preview(e);
  if (!rolltui_undo_undo(e->undo)) return 0;
  restore_from_undo(e);
  rolltui_str_set(&e->status, K("undone"));
  return 1;
}

int rolltui_theme_editor_redo(RolltuiThemeEditor* e) {
  cancel_preview(e);
  if (!rolltui_undo_redo(e->undo)) return 0;
  restore_from_undo(e);
  rolltui_str_set(&e->status, K("redone"));
  return 1;
}

size_t rolltui_theme_editor_undo_depth(const RolltuiThemeEditor* e) { return rolltui_undo_undo_depth(e->undo); }
size_t rolltui_theme_editor_redo_depth(const RolltuiThemeEditor* e) { return rolltui_undo_redo_depth(e->undo); }

int rolltui_theme_editor_focused_role(const RolltuiThemeEditor* e, unsigned char* out) {
  const RolltuiMenuItem* level = rolltui_menu_level(e->menu);
  const RolltuiMenuItem* sel;
  if (level && role_in(level->id.p, level->id.n, out)) return 1;
  sel = rolltui_menu_selected_item(e->menu);
  if (sel && role_in(sel->id.p, sel->id.n, out)) return 1;
  return 0;
}

int rolltui_theme_editor_highlighted_color(const RolltuiThemeEditor* e, RolltuiStyleColor* out) {
  const RolltuiMenuItem* sel = rolltui_menu_selected_item(e->menu);
  const RolltuiMenuItem* level;
  Field f;
  if (!sel) return 0;
  if (rolltui_menu_editing(e->menu) && sel->id.n > 7 && memcmp(sel->id.p + sel->id.n - 7, ".custom", 7) == 0) {
    size_t len = 0;
    const char* p = rolltui_input_text(rolltui_menu_editor(e->menu), &len);
    return rolltui_color_parse(p, len, out);
  }
  level = rolltui_menu_level(e->menu);
  if (!level) return 0;
  f = field_of(level->id.p, level->id.n);
  if (!field_is_colour(&f)) return 0;
  return rolltui_color_parse(sel->id.p, sel->id.n, out);
}

/* ---- the generator, run from the menu ------------------------------------------------------ */

/* The roles and pairs a generated variant could not repair, as one sentence. */
static void broken_list(const RolltuiRoleCheck* roles, const RolltuiPairCheck* pairs, RolltuiStr* out) {
  size_t i;
  for (i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    char buf[96];
    size_t rn = 0;
    const char* name;
    if (!roles[i].text || roles[i].unknown || roles[i].readable) continue;
    name = rolltui_role_name(roles[i].role, &rn);
    if (out->n != 0) rolltui_str_append(out, K("; "));
    snprintf(buf, sizeof buf, "%.*s contrast %.3g", (int)rn, name ? name : "", roles[i].wcag);
    rolltui_str_append(out, buf, strlen(buf));
  }
  for (i = 0; i < rolltui_must_differ_count(); ++i) {
    char buf[128];
    size_t an = 0, bn = 0;
    const char *a, *b;
    if (pairs[i].unknown || (pairs[i].distinct && pairs[i].cvd_distinct) || pairs[i].attribute_redundant) continue;
    a = rolltui_role_name(pairs[i].a, &an);
    b = rolltui_role_name(pairs[i].b, &bn);
    if (out->n != 0) rolltui_str_append(out, K("; "));
    snprintf(buf, sizeof buf, "%.*s/%.*s confusable", (int)an, a ? a : "", (int)bn, b ? b : "");
    rolltui_str_append(out, buf, strlen(buf));
  }
}

static void run_generator(RolltuiThemeEditor* e, RolltuiThemeEditorOutcome* out) {
  RolltuiMenuItem* ruleset_item = rolltui_menu_find(e->menu, K("gen.ruleset"));
  RolltuiMenuItem* seed_item = rolltui_menu_find(e->menu, K("gen.seed"));
  RolltuiMenuItem* chaos_item = rolltui_menu_find(e->menu, K("gen.chaos"));
  RolltuiStyle dark[ROLLTUI_ROLE_COUNT], light[ROLLTUI_ROLE_COUNT];
  RolltuiRoleCheck roles[ROLLTUI_ROLE_COUNT];
  RolltuiPairCheck pairs[ROLLTUI_MUST_DIFFER_COUNT];
  RolltuiBadges dark_badges, light_badges;
  RolltuiJsonValue *dark_meta = NULL, *light_meta = NULL;
  RolltuiStr dark_name, light_name, dark_broken, light_broken;
  RolltuiStrArray badge_names;
  unsigned char rs = 0;
  uint64_t seed;
  double chaos;
  int repairs = 0;
  size_t i;
  int dark_mode;

  if (!ruleset_item || !rolltui_ruleset_from_name(ruleset_item->value.p, ruleset_item->value.n, &rs)) {
    rolltui_str_set(&e->status, K("pick a ruleset first"));
    out->kind = ROLLTUI_THEME_EDIT_CHANGED;
    return;
  }
  seed = seed_item && seed_item->value.p ? (uint64_t)strtoull(seed_item->value.p, NULL, 10) : 0;
  chaos = chaos_item && chaos_item->value.p ? strtod(chaos_item->value.p, NULL) : 0.0;

  memset(&dark_name, 0, sizeof dark_name);
  memset(&light_name, 0, sizeof light_name);
  memset(&dark_broken, 0, sizeof dark_broken);
  memset(&light_broken, 0, sizeof light_broken);
  memset(&dark_badges, 0, sizeof dark_badges);
  memset(&light_badges, 0, sizeof light_badges);

  rolltui_theme_generate(seed, rs, chaos, 1, 1, /*max_repair_passes=*/20, rolltui_theme_default_vocab(), dark,
                         ROLLTUI_ROLE_COUNT, &dark_name, &dark_meta, &repairs, roles, pairs, &dark_badges);
  broken_list(roles, pairs, &dark_broken);
  rolltui_theme_generate(seed, rs, chaos, 1, 0, /*max_repair_passes=*/20, rolltui_theme_default_vocab(), light,
                         ROLLTUI_ROLE_COUNT, &light_name, &light_meta, &repairs, roles, pairs, &light_badges);
  broken_list(roles, pairs, &light_broken);

  /* A generated theme has no motion of its own, so a fresh empty map (a still UI) is the honest
   * answer rather than keeping whatever the previous theme was moving. */
  dark_mode = e->mode != ROLLTUI_MODE_LIGHT;
  rolltui_theme_editor_replace(e, dark, light, dark_name.p, dark_name.n, light_name.p, light_name.n, dark_meta,
                               light_meta, rolltui_effect_map_new(ROLLTUI_EFFECT_STATE_COUNT,
                                                                  default_effect_fallback_role()));
  rolltui_str_free(&dark_name);
  rolltui_str_free(&light_name);

  rolltui_str_clear(&e->status);
  rolltui_str_append(&e->status, K("generated "));
  {
    size_t len = 0;
    const char* nm = rolltui_theme_editor_variant_name(e, e->mode, &len);
    rolltui_str_append(&e->status, nm, len);
  }
  rolltui_str_append(&e->status, K(" \xE2\x80\x94"));
  memset(&badge_names, 0, sizeof badge_names);
  rolltui_badge_names(dark_mode ? &dark_badges : &light_badges, &badge_names);
  if (badge_names.n == 0) rolltui_str_append(&e->status, K(" no badges"));
  for (i = 0; i < badge_names.n; ++i) {
    rolltui_str_append(&e->status, K(" "));
    rolltui_str_append_str(&e->status, &badge_names.v[i]);
  }
  rolltui_str_array_release(&badge_names);
  {
    const RolltuiStr* broken = dark_mode ? &dark_broken : &light_broken;
    if (broken->n != 0) {
      rolltui_str_append(&e->status, K("; broken: "));
      rolltui_str_append_str(&e->status, broken);
    }
  }
  rolltui_str_free(&dark_broken);
  rolltui_str_free(&light_broken);
  out->kind = ROLLTUI_THEME_EDIT_COMMITTED;
}

/* ---- the event ----------------------------------------------------------------------------- */

static int action_is(const RolltuiBindings* nav, const RolltuiChord* k, const char* want) {
  size_t len = 0;
  const char* a = rolltui_bindings_action_for(nav, k, K("editor"), &len);
  return a && len == strlen(want) && memcmp(a, want, len) == 0;
}

static void undo_outcome(RolltuiThemeEditor* e, int did, RolltuiThemeEditorOutcome* out, const char* nothing) {
  if (!did) rolltui_str_set(&e->status, nothing, strlen(nothing));
  out->kind = did ? ROLLTUI_THEME_EDIT_COMMITTED : ROLLTUI_THEME_EDIT_CHANGED;
}

void rolltui_theme_editor_handle(RolltuiThemeEditor* e, const RolltuiEvent* ev, const RolltuiBindings* nav,
                                 RolltuiThemeEditorOutcome* out) {
  RolltuiMenuEvent raw;
  unsigned char kind;
  RolltuiStr id, value;
  int checked;
  Field f;

  rolltui_theme_editor_outcome_release(out);

  if (ev->kind == ROLLTUI_EVENT_KEY) {
    /* An undo or redo changes the COMMITTED value: whoever mounted this writes it back. */
    if (action_is(nav, &ev->key, "editor.undo")) { undo_outcome(e, rolltui_theme_editor_undo(e), out, "nothing to undo"); return; }
    if (action_is(nav, &ev->key, "editor.redo")) { undo_outcome(e, rolltui_theme_editor_redo(e), out, "nothing to redo"); return; }
  }
  rolltui_str_clear(&e->status);

  memset(&raw, 0, sizeof raw);
  rolltui_menu_handle(e->menu, ev, nav, rolltui_menu_default_actions(), &raw);
  kind = raw.kind;
  memset(&id, 0, sizeof id);
  memset(&value, 0, sizeof value);
  rolltui_str_move(&id, &raw.id);
  rolltui_str_move(&value, &raw.value);
  checked = raw.checked != 0;
  rolltui_menu_event_release(&raw);

  if (kind == ROLLTUI_MENU_EVENT_CHOOSE) {
    f = field_of(id.p ? id.p : "", id.n);
    if (f.ok) {
      RolltuiStyleColor c;
      if (rolltui_color_parse(value.p ? value.p : "", value.n, &c)) { begin_preview(e); apply_field(e, &f, c); }
      commit_current(e, out);
    } else if (rolltui_str_eq(&id, K("mode"))) {
      rolltui_theme_editor_set_mode(e, rolltui_str_eq(&value, K("light")) ? ROLLTUI_MODE_LIGHT : ROLLTUI_MODE_DARK);
      out->kind = ROLLTUI_THEME_EDIT_CHANGED;
    } else if (rolltui_str_eq(&id, K("load"))) {
      out->kind = ROLLTUI_THEME_EDIT_LOAD_PRESET;
      rolltui_str_move(&out->value, &value);
    } else if (rolltui_str_eq(&id, K("write_shipped"))) {
      out->kind = ROLLTUI_THEME_EDIT_WRITE_SHIPPED;
      rolltui_str_move(&out->value, &value);
    }
    goto done;
  }
  if (kind == ROLLTUI_MENU_EVENT_TOGGLE) {
    f = field_of(id.p ? id.p : "", id.n);
    if (f.ok) {
      unsigned char* a = attr_of(&style_table(e, e->mode)[f.role], f.name, f.name_len);
      begin_preview(e);
      /* `begin_preview` copied the current value; the pointer above is still into `current`. */
      a = attr_of(&style_table(e, e->mode)[f.role], f.name, f.name_len);
      if (a) *a = (unsigned char)checked;
      commit_current(e, out);
    }
    goto done;
  }
  if (kind == ROLLTUI_MENU_EVENT_INPUT) {
    if (rolltui_str_eq(&id, K("save"))) {
      out->kind = ROLLTUI_THEME_EDIT_SAVE_AS;
      rolltui_str_move(&out->value, &value);
      goto done;
    }
    f = field_of(id.p ? id.p : "", id.n);
    if (f.ok) {
      RolltuiStyleColor c;
      if (rolltui_color_parse(value.p ? value.p : "", value.n, &c)) {
        begin_preview(e);
        apply_field(e, &f, c);
        commit_current(e, out);
      } else {
        char buf[96];
        cancel_preview(e);
        snprintf(buf, sizeof buf, "'%.*s' is not a colour (#rrggbb, 0-255, none)", (int)value.n, value.p ? value.p : "");
        rolltui_str_set(&e->status, buf, strlen(buf));
        out->kind = ROLLTUI_THEME_EDIT_CHANGED;
      }
    }
    goto done;
  }
  if (kind == ROLLTUI_MENU_EVENT_ACTIVATE) {
    if (rolltui_str_eq(&id, K("undo"))) { undo_outcome(e, rolltui_theme_editor_undo(e), out, "nothing to undo"); goto done; }
    if (rolltui_str_eq(&id, K("redo"))) { undo_outcome(e, rolltui_theme_editor_redo(e), out, "nothing to redo"); goto done; }
    if (rolltui_str_eq(&id, K("reset_loaded"))) { out->kind = ROLLTUI_THEME_EDIT_RESET_LOADED; goto done; }
    if (rolltui_str_eq(&id, K("reset_builtin"))) { out->kind = ROLLTUI_THEME_EDIT_RESET_BUILTIN; goto done; }
    if (rolltui_str_eq(&id, K("check"))) { out->kind = ROLLTUI_THEME_EDIT_CHECK; goto done; }
    if (rolltui_str_eq(&id, K("gen.run"))) { run_generator(e, out); goto done; }
    if (id.n > 4 && memcmp(id.p, "fix.", 4) == 0 && !rolltui_str_eq(&id, K("fix.none"))) {
      const size_t i = (size_t)atoi(id.p + 4);
      if (i < e->fixes.n) {
        RolltuiChord left;
        RolltuiEvent left_ev;
        RolltuiMenuEvent discard;
        begin_preview(e);
        rolltui_apply_fix(style_table(e, e->mode), ROLLTUI_ROLE_COUNT, e->fixes.v[i].role, &e->fixes.v[i].after);
        rolltui_str_set(&e->status, K("applied: "));
        rolltui_str_append_str(&e->status, &e->fixes.v[i].what);
        commit_current(e, out);
        memset(&left, 0, sizeof left);
        left.key = ROLLTUI_KEY_LEFT;
        memset(&left_ev, 0, sizeof left_ev);
        left_ev.kind = ROLLTUI_EVENT_KEY;
        left_ev.key = left;
        memset(&discard, 0, sizeof discard);
        /* back to the (refreshed) Fixes level's parent */
        rolltui_menu_handle(e->menu, &left_ev, nav, rolltui_menu_default_actions(), &discard);
        rolltui_menu_event_release(&discard);
      }
    }
    goto done;
  }
  if (kind == ROLLTUI_MENU_EVENT_CLOSED) {
    out->kind = ROLLTUI_THEME_EDIT_CLOSED;
    goto done;
  }

  /* ---- live preview: the highlighted palette entry, or a custom colour being typed ---- */
  {
    const RolltuiMenuItem* sel = rolltui_menu_selected_item(e->menu);
    const RolltuiMenuItem* level = rolltui_menu_level(e->menu);
    const int editing = rolltui_menu_editing(e->menu) != 0;
    Field level_field = level ? field_of(level->id.p ? level->id.p : "", level->id.n) : (Field){0, NULL, 0, 0};
    if (field_is_colour(&level_field) && sel && !editing) {
      RolltuiStyleColor c;
      if (rolltui_color_parse(sel->id.p ? sel->id.p : "", sel->id.n, &c)) {
        begin_preview(e);
        apply_field(e, &level_field, c);
        out->kind = ROLLTUI_THEME_EDIT_CHANGED;
        goto done;
      }
    }
    if (editing && sel) {
      Field sf = field_of(sel->id.p ? sel->id.p : "", sel->id.n);
      if (sf.ok) {
        size_t len = 0;
        const char* p = rolltui_input_text(rolltui_menu_editor(e->menu), &len);
        RolltuiStyleColor c;
        begin_preview(e);
        /* The editing TEXT, never the item's value: that is the committed colour. */
        if (rolltui_color_parse(p, len, &c)) {
          apply_field(e, &sf, c);
        } else {
          /* not (yet) a colour: show the committed value while typing continues */
          const RolltuiStyle* pt = e->mode == ROLLTUI_MODE_LIGHT ? e->preview.light : e->preview.dark;
          apply_field(e, &sf, sf.name_len == 2 && memcmp(sf.name, "fg", 2) == 0 ? pt[sf.role].fg : pt[sf.role].bg);
        }
        out->kind = ROLLTUI_THEME_EDIT_CHANGED;
        goto done;
      }
    }
  }
  /* Anywhere else with a preview showing: the focused change was abandoned (Escape, Left, a
   * move away) — the field returns to its committed value. */
  if (e->previewing) {
    cancel_preview(e);
    out->kind = ROLLTUI_THEME_EDIT_CHANGED;
  }

done:
  rolltui_str_free(&id);
  rolltui_str_free(&value);
}
