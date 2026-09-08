/* rolltui/c/rolltui_keys_editor.c — see rolltui_keys_editor.h for the contract and for what this
 * model deliberately does not do. The rules it implements (capture, the chord that moves, the
 * Enter rule refused by name, every change a commit) are stated there and asserted in
 * `rolltui/tests/keys_editor_test.cpp`; none of it is repeated here. */
#include "rolltui/c/rolltui_keys_editor.h"

#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_menu_tree.h"
#include "rolltui/c/rolltui_undo.h"

#define K(s) (s), strlen(s)

/* ---- the undo-tracked value ---------------------------------------------------------------
 * A WHOLE TABLE. There is no partial state to snapshot: an edit here is one chord landing or
 * leaving, and it lands committed. The stack's entries are `RolltuiBindings*` clones, freed
 * through the one callback the stack takes. */
static void bindings_free_owned(void* p) { rolltui_bindings_free((RolltuiBindings*)p); }

struct RolltuiKeysEditor {
  RolltuiMenu* menu;         /* OWNED */
  RolltuiBindings* current;  /* OWNED — the table being edited */
  RolltuiUndoStack* undo;    /* snapshots of RolltuiBindings*, GROWING HEAP: an editing session
                              * has no bound on how many it makes */
  int capturing;             /* whether `capture` names an action awaiting its chord. EXPLICIT
                              * rather than inferred from `capture.n`: an empty action name is
                              * not a state this editor can be in, so a flag that can be read
                              * beats a length that has to be interpreted */
  RolltuiStr capture;
  RolltuiStrList presets, shipped;
  int may_write_shipped;
  RolltuiStr status;
  /* CALLER-FILLED working strings the editor owns and REFILLS (rolltui_alloc.h strategy 3),
   * one per ROLE so a builder cannot alias its own caller's buffer: ids, labels, and the
   * chord text a label ends with. */
  RolltuiStr id_buf, label_buf, chords_buf;
  RolltuiStrList scopes; /* the scopes present in `current`, first-appearance order */
};

static const RolltuiBindings* committed_of(const RolltuiKeysEditor* e) {
  return (const RolltuiBindings*)rolltui_undo_current(e->undo);
}

/* ---- menu glue ---------------------------------------------------------------------------- */

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

static void adopt_children(RolltuiMenuItem* it, RolltuiMenuItemList* children) {
  rolltui_menu_list_release(&it->children);
  it->children = *children;
  memset(children, 0, sizeof *children);
}

static void set_options_list(RolltuiMenu* m, const char* id, size_t len, RolltuiMenuItemList* list) {
  rolltui_menu_set_options(m, id, len, list);
  rolltui_menu_list_release(list);
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

/* ---- chord and action text ----------------------------------------------------------------- */

/* Both bounded writers, into the caller's own stack buffer (rolltui_bindings.h rule 1). */
static size_t chord_id(const RolltuiChord* k, char* buf, size_t cap) { return rolltui_chord_to_string(k, buf, cap); }
static size_t chord_disp(const RolltuiChord* k, char* buf, size_t cap) { return rolltui_chord_display(k, buf, cap); }

/* What an action is called with its scope removed: "word_left" of "input.word_left". A name with
 * no dot is its own verb. The scope is in the breadcrumb already, and the popup this is drawn in
 * is 48 columns wide. */
static const char* verb_of(const char* action, size_t len, size_t* out_len) {
  size_t i;
  for (i = 0; i < len; ++i)
    if (action[i] == '.') {
      *out_len = len - i - 1;
      return action + i + 1;
    }
  *out_len = len;
  return action;
}

static void append_count(RolltuiStr* out, size_t n) {
  char buf[24];
  const int len = snprintf(buf, sizeof buf, "%zu", n);
  rolltui_str_append(out, buf, len > 0 ? (size_t)len : 0);
}

/* ---- construction --------------------------------------------------------------------------*/

static void rebuild_menu(RolltuiKeysEditor* e);

RolltuiKeysEditor* rolltui_keys_editor_new(const RolltuiBindings* baseline) {
  RolltuiKeysEditor* e = (RolltuiKeysEditor*)rolltui_mem_alloc(sizeof *e);
  memset(e, 0, sizeof *e);
  e->menu = rolltui_menu_new();
  e->undo = rolltui_undo_new((size_t)-1, bindings_free_owned);
  e->current = baseline ? rolltui_bindings_clone(baseline) : rolltui_bindings_new_seeded();
  rolltui_undo_reset(e->undo, rolltui_bindings_clone(e->current));
  rebuild_menu(e);
  return e;
}

void rolltui_keys_editor_free(RolltuiKeysEditor* e) {
  if (!e) return;
  rolltui_menu_free(e->menu);
  rolltui_undo_free(e->undo);
  rolltui_bindings_free(e->current);
  rolltui_str_free(&e->capture);
  rolltui_str_list_release(&e->presets);
  rolltui_str_list_release(&e->shipped);
  rolltui_str_free(&e->status);
  rolltui_str_free(&e->id_buf);
  rolltui_str_free(&e->label_buf);
  rolltui_str_free(&e->chords_buf);
  rolltui_str_list_release(&e->scopes);
  rolltui_mem_free(e);
}

void rolltui_keys_editor_outcome_release(RolltuiKeysEditorOutcome* o) {
  if (!o) return;
  rolltui_str_free(&o->value);
  memset(o, 0, sizeof *o);
}

void rolltui_keys_editor_load(RolltuiKeysEditor* e, const RolltuiBindings* b) {
  rolltui_bindings_free(e->current);
  e->current = rolltui_bindings_clone(b);
  rolltui_undo_reset(e->undo, rolltui_bindings_clone(e->current));
  e->capturing = 0;
  rebuild_menu(e);
  rolltui_str_set(&e->status, K("loaded"));
}

void rolltui_keys_editor_set_presets(RolltuiKeysEditor* e, const RolltuiStrList* names) {
  RolltuiMenuItemList opts;
  memset(&opts, 0, sizeof opts);
  copy_str_list(&e->presets, names);
  names_to_options(&e->presets, &opts);
  set_options_list(e->menu, K("load"), &opts);
}

void rolltui_keys_editor_set_shipped(RolltuiKeysEditor* e, const RolltuiStrList* names, int may_write) {
  RolltuiMenuItemList opts;
  memset(&opts, 0, sizeof opts);
  copy_str_list(&e->shipped, names);
  e->may_write_shipped = may_write;
  names_to_options(&e->shipped, &opts);
  set_options_list(e->menu, K("write_shipped"), &opts);
  rolltui_menu_set_enabled(e->menu, K("write_shipped"), may_write && e->shipped.n != 0);
}

const RolltuiBindings* rolltui_keys_editor_current(const RolltuiKeysEditor* e) { return e->current; }
const RolltuiBindings* rolltui_keys_editor_committed(const RolltuiKeysEditor* e) { return committed_of(e); }
int rolltui_keys_editor_capturing(const RolltuiKeysEditor* e) { return e->capturing; }

const char* rolltui_keys_editor_capturing_action(const RolltuiKeysEditor* e, size_t* len) {
  if (!e->capturing) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = e->capture.n;
  return e->capture.p ? e->capture.p : "";
}

RolltuiMenu* rolltui_keys_editor_menu(RolltuiKeysEditor* e) { return e->menu; }

size_t rolltui_keys_editor_undo_depth(const RolltuiKeysEditor* e) { return rolltui_undo_undo_depth(e->undo); }
size_t rolltui_keys_editor_redo_depth(const RolltuiKeysEditor* e) { return rolltui_undo_redo_depth(e->undo); }

/* ---- the menu ------------------------------------------------------------------------------ */

/* THE SCOPES ARE READ OFF THE TABLE, never listed here. A scope exists because something
 * DECLARED an action in it, so the library's five come first (it declares them itself, in its
 * own table's order) and whatever a screen or a mounted tool declared follows in the order it
 * was declared. A written-down list would be this library carrying one application's vocabulary
 * — and an app whose screen declares `browser.open` would find it uneditable. */
static void collect_scopes(RolltuiKeysEditor* e) {
  const size_t n = rolltui_bindings_action_count(e->current);
  size_t i, j;
  rolltui_str_list_clear(&e->scopes);
  for (i = 0; i < n; ++i) {
    size_t alen = 0, slen = 0;
    const char* a = rolltui_bindings_action_at(e->current, i, &alen);
    const char* s = rolltui_bindings_scope_of(a, alen, &slen);
    int seen = 0;
    for (j = 0; j < e->scopes.n && !seen; ++j) seen = rolltui_str_eq(&e->scopes.v[j], s, slen);
    if (!seen) rolltui_str_list_add(&e->scopes, s, slen);
  }
}

/* "<action>  Ctrl-Left, Alt-Left", or "(unbound)". The chords are the HELP spelling, so a chord
 * this terminal cannot deliver is not offered as if it worked. */
static void action_label(RolltuiKeysEditor* e, const char* action, size_t alen) {
  size_t vlen = 0;
  const char* v = verb_of(action, alen, &vlen);
  rolltui_bindings_chords_text(e->current, action, alen, &e->chords_buf);
  rolltui_str_clear(&e->label_buf);
  rolltui_str_append(&e->label_buf, v, vlen);
  rolltui_str_append(&e->label_buf, K("  "));
  if (e->chords_buf.n == 0) rolltui_str_append(&e->label_buf, K("(unbound)"));
  else rolltui_str_append_str(&e->label_buf, &e->chords_buf);
}

/* One action's items: add, one remove per chord, clear. */
static void action_items(RolltuiKeysEditor* e, const char* action, size_t alen, RolltuiMenuItemList* out) {
  const size_t chords = rolltui_bindings_chord_count(e->current, action, alen);
  size_t i;
  rolltui_str_clear(&e->id_buf);
  rolltui_str_append(&e->id_buf, K("bind."));
  rolltui_str_append(&e->id_buf, action, alen);
  list_add_action(out, e->id_buf.p, e->id_buf.n, K("add a chord (press it)\xE2\x80\xA6"));
  for (i = 0; i < chords; ++i) {
    RolltuiChord k;
    char id[ROLLTUI_CHORD_STRING_MAX], disp[ROLLTUI_CHORD_STRING_MAX];
    size_t id_len, disp_len;
    memset(&k, 0, sizeof k);
    if (!rolltui_bindings_chord_at(e->current, action, alen, i, &k)) continue;
    id_len = chord_id(&k, id, sizeof id);
    disp_len = chord_disp(&k, disp, sizeof disp);
    rolltui_str_clear(&e->id_buf);
    rolltui_str_append(&e->id_buf, K("unbind."));
    rolltui_str_append(&e->id_buf, action, alen);
    rolltui_str_append(&e->id_buf, K("."));
    rolltui_str_append(&e->id_buf, id, id_len);
    /* The label buffer is free here: `action_label` runs after this list is built. */
    rolltui_str_clear(&e->label_buf);
    rolltui_str_append(&e->label_buf, K("remove "));
    rolltui_str_append(&e->label_buf, disp, disp_len);
    list_add_action(out, e->id_buf.p, e->id_buf.n, e->label_buf.p, e->label_buf.n);
  }
  rolltui_str_clear(&e->id_buf);
  rolltui_str_append(&e->id_buf, K("clear."));
  rolltui_str_append(&e->id_buf, action, alen);
  list_add_action(out, e->id_buf.p, e->id_buf.n, K("clear every chord"));
}

static void rebuild_menu(RolltuiKeysEditor* e) {
  RolltuiMenuItemList top, scopes, actions, items, opts;
  RolltuiMenuItem root, *it;
  size_t s, i;
  const size_t action_n = rolltui_bindings_action_count(e->current);

  collect_scopes(e);
  memset(&top, 0, sizeof top);
  memset(&scopes, 0, sizeof scopes);

  for (s = 0; s < e->scopes.n; ++s) {
    const char* scope = e->scopes.v[s].p ? e->scopes.v[s].p : "";
    const size_t scope_len = e->scopes.v[s].n;
    memset(&actions, 0, sizeof actions);
    for (i = 0; i < action_n; ++i) {
      size_t alen = 0, slen = 0;
      const char* a = rolltui_bindings_action_at(e->current, i, &alen);
      const char* sc = rolltui_bindings_scope_of(a, alen, &slen);
      if (slen != scope_len || memcmp(sc, scope, slen) != 0) continue;
      memset(&items, 0, sizeof items);
      action_items(e, a, alen, &items);
      action_label(e, a, alen);
      /* `a` is a BORROW into the table and outlives this loop body; `id_buf` is not, so the
       * level id is built after `action_items` has finished with it. */
      rolltui_str_clear(&e->id_buf);
      rolltui_str_append(&e->id_buf, K("action."));
      rolltui_str_append(&e->id_buf, a, alen);
      it = list_add(&actions, ROLLTUI_MENU_SUBMENU, e->id_buf.p, e->id_buf.n, e->label_buf.p, e->label_buf.n);
      adopt_children(it, &items);
    }
    rolltui_str_clear(&e->id_buf);
    rolltui_str_append(&e->id_buf, K("scope."));
    rolltui_str_append(&e->id_buf, scope, scope_len);
    it = list_add(&scopes, ROLLTUI_MENU_SUBMENU, e->id_buf.p, e->id_buf.n, scope, scope_len);
    adopt_children(it, &actions);
  }

  it = list_add(&top, ROLLTUI_MENU_SUBMENU, K("scopes"), K("Actions by scope"));
  adopt_children(it, &scopes);

  it = list_add(&top, ROLLTUI_MENU_ACTION, K("undo"), K("Undo"));
  rolltui_str_set(&it->shortcut, K("Ctrl-Z"));
  it = list_add(&top, ROLLTUI_MENU_ACTION, K("redo"), K("Redo"));
  rolltui_str_set(&it->shortcut, K("Ctrl-Y"));

  it = list_add(&top, ROLLTUI_MENU_CHOICE, K("load"), K("Load keys"));
  memset(&opts, 0, sizeof opts);
  names_to_options(&e->presets, &opts);
  adopt_children(it, &opts);

  it = list_add(&top, ROLLTUI_MENU_INPUT, K("save"), K("Save keys as"));
  it->spec.type = ROLLTUI_INPUT_TYPE_NAME;

  it = list_add(&top, ROLLTUI_MENU_CHOICE, K("write_shipped"), K("Write a SHIPPED preset (the editor's privilege)"));
  memset(&opts, 0, sizeof opts);
  names_to_options(&e->shipped, &opts);
  adopt_children(it, &opts);

  list_add_action(&top, K("reset_loaded"), K("Reset to the loaded preset\xE2\x80\xA6"));

  rolltui_menu_item_init(&root);
  rolltui_menu_item_set(&root, ROLLTUI_MENU_SUBMENU, K("root"), K("keys editor"), NULL, 0);
  adopt_children(&root, &top);
  rolltui_menu_set_root(e->menu, &root);
  rolltui_menu_item_release(&root);
  rolltui_menu_set_enabled(e->menu, K("write_shipped"), e->may_write_shipped && e->shipped.n != 0);
}

/* Refreshes ONE action's level in place — its label and its remove items — so the navigation
 * stays where it is. A whole rebuild would drop the person back at the top level after every
 * chord they bound. */
static void rebuild_action(RolltuiKeysEditor* e, const char* action, size_t alen) {
  RolltuiMenuItemList items;
  RolltuiMenuItem* it;
  memset(&items, 0, sizeof items);
  action_items(e, action, alen, &items);
  rolltui_str_clear(&e->id_buf);
  rolltui_str_append(&e->id_buf, K("action."));
  rolltui_str_append(&e->id_buf, action, alen);
  rolltui_menu_set_options(e->menu, e->id_buf.p, e->id_buf.n, &items);
  rolltui_menu_list_release(&items);
  it = rolltui_menu_find(e->menu, e->id_buf.p, e->id_buf.n);
  if (it) {
    action_label(e, action, alen);
    rolltui_str_set(&it->label, e->label_buf.p, e->label_buf.n);
  }
}

/* ---- editing -------------------------------------------------------------------------------*/

static void commit_current(RolltuiKeysEditor* e, RolltuiKeysEditorOutcome* out) {
  if (rolltui_bindings_equal(e->current, committed_of(e))) {
    out->kind = ROLLTUI_KEYS_EDIT_CHANGED;
    return;
  }
  rolltui_undo_commit(e->undo, rolltui_bindings_clone(e->current));
  out->kind = ROLLTUI_KEYS_EDIT_COMMITTED;
}

/* An undo or a redo replaces the whole table, so the menu is rebuilt rather than patched. */
static int walk_history(RolltuiKeysEditor* e, int moved, const char* what) {
  e->capturing = 0;
  if (!moved) return 0;
  rolltui_bindings_free(e->current);
  e->current = rolltui_bindings_clone(committed_of(e));
  rebuild_menu(e);
  rolltui_str_set(&e->status, what, strlen(what));
  return 1;
}

int rolltui_keys_editor_undo(RolltuiKeysEditor* e) { return walk_history(e, rolltui_undo_undo(e->undo), "undone"); }
int rolltui_keys_editor_redo(RolltuiKeysEditor* e) { return walk_history(e, rolltui_undo_redo(e->undo), "redone"); }

void rolltui_keys_editor_replace(RolltuiKeysEditor* e, RolltuiBindings* b) {
  e->capturing = 0;
  rolltui_bindings_free(e->current);
  e->current = b; /* ADOPTED */
  rolltui_undo_commit(e->undo, rolltui_bindings_clone(e->current));
  rebuild_menu(e);
}

void rolltui_keys_editor_status_line(const RolltuiKeysEditor* e, RolltuiStr* out) {
  /* While capturing, a refusal (an unnameable key) is shown WITH the prompt, so the reason and
   * what to do next are both on the line. */
  if (e->capturing) {
    if (e->status.n != 0) {
      rolltui_str_append_str(out, &e->status);
      rolltui_str_append(out, K(" \xE2\x80\x94 "));
    }
    rolltui_str_append(out, K("press the chord for "));
    rolltui_str_append_str(out, &e->capture);
    rolltui_str_append(out, K(" (Esc cancels)"));
  } else if (e->status.n == 0) {
    rolltui_str_append(out, K("Enter on an action: add, remove or clear its chords"));
  } else {
    rolltui_str_append_str(out, &e->status);
  }
  rolltui_str_append(out, K(" \xC2\xB7 undo "));
  append_count(out, rolltui_undo_undo_depth(e->undo));
  rolltui_str_append(out, K(" \xC2\xB7 redo "));
  append_count(out, rolltui_undo_redo_depth(e->undo));
}

/* ---- handling ------------------------------------------------------------------------------*/

static int action_is(const RolltuiBindings* nav, const RolltuiChord* k, const char* want) {
  size_t len = 0;
  const char* a = rolltui_bindings_action_for(nav, k, K("editor"), &len);
  return a && len == strlen(want) && memcmp(a, want, len) == 0;
}

static void undo_outcome(RolltuiKeysEditor* e, int did, RolltuiKeysEditorOutcome* out, const char* nothing) {
  if (!did) rolltui_str_set(&e->status, nothing, strlen(nothing));
  out->kind = did ? ROLLTUI_KEYS_EDIT_COMMITTED : ROLLTUI_KEYS_EDIT_CHANGED;
}

/* The key pressed while capturing IS the chord. Everything this function can say ends the
 * capture except an unnameable key, which leaves it open for another try. */
static void take_capture(RolltuiKeysEditor* e, const RolltuiChord* k, RolltuiKeysEditorOutcome* out) {
  RolltuiStr action;
  const char* moved_from = NULL;
  size_t moved_len = 0;
  char disp[ROLLTUI_CHORD_STRING_MAX];
  size_t disp_len;

  if (k->key == ROLLTUI_KEY_ESCAPE && !k->ctrl && !k->alt && !k->shift) {
    e->capturing = 0;
    rolltui_str_set(&e->status, K("capture cancelled"));
    out->kind = ROLLTUI_KEYS_EDIT_CHANGED;
    return;
  }
  if (k->key == ROLLTUI_KEY_UNKNOWN) {
    rolltui_str_set(&e->status, K("that key has no chord name; try another"));
    out->kind = ROLLTUI_KEYS_EDIT_CHANGED;
    return;
  }
  /* The action name is the editor's own storage and `rebuild_action` refills every scratch
   * buffer, so it is moved out before anything below can touch `capture`. */
  memset(&action, 0, sizeof action);
  rolltui_str_move(&action, &e->capture);
  e->capturing = 0;
  disp_len = chord_disp(k, disp, sizeof disp);

  if (!rolltui_bindings_bind(e->current, action.p ? action.p : "", action.n, k, &moved_from, &moved_len)) {
    rolltui_str_set(&e->status, K("refused: "));
    rolltui_str_append(&e->status, disp, disp_len);
    rolltui_str_append(&e->status, K(" cannot be bound to "));
    rolltui_str_append_str(&e->status, &action);
    if (k->key == ROLLTUI_KEY_ENTER) rolltui_str_append(&e->status, K(" (Enter is always input.submit)"));
    out->kind = ROLLTUI_KEYS_EDIT_CHANGED;
    rolltui_str_free(&action);
    return;
  }
  {
    /* `moved_from` BORROWS into the table and the rebuilds below mutate it, so the loser's name
     * is copied out before either. */
    RolltuiStr moved;
    size_t vlen = 0;
    const char* v;
    memset(&moved, 0, sizeof moved);
    if (moved_from) rolltui_str_set(&moved, moved_from, moved_len);
    rolltui_str_set(&e->status, K("bound "));
    rolltui_str_append(&e->status, disp, disp_len);
    rolltui_str_append(&e->status, K(" \xE2\x86\x92 "));
    v = verb_of(action.p ? action.p : "", action.n, &vlen);
    rolltui_str_append(&e->status, v, vlen);
    if (moved.n != 0) {
      rolltui_str_append(&e->status, K(" (was "));
      v = verb_of(moved.p, moved.n, &vlen);
      rolltui_str_append(&e->status, v, vlen);
      rolltui_str_append(&e->status, K(")"));
    }
    rebuild_action(e, action.p ? action.p : "", action.n);
    if (moved.n != 0) rebuild_action(e, moved.p, moved.n);
    rolltui_str_free(&moved);
  }
  commit_current(e, out);
  rolltui_str_free(&action);
}

/* "unbind.<action>.<chord>": the action carries exactly one dot, so the chord is what follows
 * the second one. */
static int split_unbind(const char* rest, size_t len, size_t* action_len, const char** chord, size_t* chord_len) {
  size_t i, first = len, second = len;
  for (i = 0; i < len; ++i)
    if (rest[i] == '.') { first = i; break; }
  if (first == len) return 0;
  for (i = first + 1; i < len; ++i)
    if (rest[i] == '.') { second = i; break; }
  if (second == len) return 0;
  *action_len = second;
  *chord = rest + second + 1;
  *chord_len = len - second - 1;
  return 1;
}

static int id_starts(const RolltuiStr* id, const char* prefix) {
  const size_t n = strlen(prefix);
  return id->n >= n && id->p && memcmp(id->p, prefix, n) == 0;
}

void rolltui_keys_editor_handle(RolltuiKeysEditor* e, const RolltuiEvent* ev, const RolltuiBindings* nav,
                                RolltuiKeysEditorOutcome* out) {
  RolltuiMenuEvent raw;
  unsigned char kind;
  RolltuiStr id, value;

  rolltui_keys_editor_outcome_release(out);

  if (e->capturing) {
    /* EVERY key is the chord while capturing — including the editor's own undo key, which is
     * why this stands before the lookup below rather than after it. */
    if (ev->kind != ROLLTUI_EVENT_KEY) return;
    take_capture(e, &ev->key, out);
    return;
  }
  if (ev->kind == ROLLTUI_EVENT_KEY) {
    if (action_is(nav, &ev->key, "editor.undo")) { undo_outcome(e, rolltui_keys_editor_undo(e), out, "nothing to undo"); return; }
    if (action_is(nav, &ev->key, "editor.redo")) { undo_outcome(e, rolltui_keys_editor_redo(e), out, "nothing to redo"); return; }
  }
  rolltui_str_clear(&e->status);

  memset(&raw, 0, sizeof raw);
  rolltui_menu_handle(e->menu, ev, nav, rolltui_menu_default_actions(), &raw);
  kind = raw.kind;
  memset(&id, 0, sizeof id);
  memset(&value, 0, sizeof value);
  rolltui_str_move(&id, &raw.id);
  rolltui_str_move(&value, &raw.value);
  rolltui_menu_event_release(&raw);

  if (kind == ROLLTUI_MENU_EVENT_ACTIVATE) {
    if (id_starts(&id, "bind.")) {
      rolltui_str_set(&e->capture, id.p + 5, id.n - 5);
      e->capturing = 1;
      rolltui_str_clear(&e->status);
      out->kind = ROLLTUI_KEYS_EDIT_CHANGED;
      goto done;
    }
    if (id_starts(&id, "unbind.")) {
      const char* rest = id.p + 7;
      const size_t rest_len = id.n - 7;
      size_t alen = 0, clen = 0;
      const char* chord = NULL;
      RolltuiChord k;
      char disp[ROLLTUI_CHORD_STRING_MAX];
      size_t disp_len;
      if (!split_unbind(rest, rest_len, &alen, &chord, &clen)) goto done;
      memset(&k, 0, sizeof k);
      if (!rolltui_chord_parse(chord, clen, &k)) goto done;
      disp_len = chord_disp(&k, disp, sizeof disp);
      if (!rolltui_bindings_unbind(e->current, rest, alen, &k)) {
        rolltui_str_set(&e->status, K("refused: "));
        rolltui_str_append(&e->status, disp, disp_len);
        rolltui_str_append(&e->status, K(" stays on "));
        rolltui_str_append(&e->status, rest, alen);
        out->kind = ROLLTUI_KEYS_EDIT_CHANGED;
        goto done;
      }
      rolltui_str_set(&e->status, K("removed "));
      rolltui_str_append(&e->status, disp, disp_len);
      rolltui_str_append(&e->status, K(" from "));
      rolltui_str_append(&e->status, rest, alen);
      rebuild_action(e, rest, alen);
      commit_current(e, out);
      goto done;
    }
    if (id_starts(&id, "clear.")) {
      const char* action = id.p + 6;
      const size_t alen = id.n - 6;
      rolltui_bindings_clear(e->current, action, alen);
      /* The Enter rule keeps input.submit's chord whatever this asks for, so the line says what
       * happened rather than what was requested. */
      if (alen == 12 && memcmp(action, "input.submit", 12) == 0) {
        rolltui_str_set(&e->status, K("input.submit keeps Enter"));
      } else {
        rolltui_str_set(&e->status, K("cleared "));
        rolltui_str_append(&e->status, action, alen);
      }
      rebuild_action(e, action, alen);
      commit_current(e, out);
      goto done;
    }
    if (rolltui_str_eq(&id, K("undo"))) { undo_outcome(e, rolltui_keys_editor_undo(e), out, "nothing to undo"); goto done; }
    if (rolltui_str_eq(&id, K("redo"))) { undo_outcome(e, rolltui_keys_editor_redo(e), out, "nothing to redo"); goto done; }
    if (rolltui_str_eq(&id, K("reset_loaded"))) { out->kind = ROLLTUI_KEYS_EDIT_RESET_LOADED; goto done; }
    goto done;
  }
  if (kind == ROLLTUI_MENU_EVENT_CHOOSE) {
    if (rolltui_str_eq(&id, K("load"))) {
      out->kind = ROLLTUI_KEYS_EDIT_LOAD_PRESET;
      rolltui_str_move(&out->value, &value);
    } else if (rolltui_str_eq(&id, K("write_shipped"))) {
      out->kind = ROLLTUI_KEYS_EDIT_WRITE_SHIPPED;
      rolltui_str_move(&out->value, &value);
    }
    goto done;
  }
  if (kind == ROLLTUI_MENU_EVENT_INPUT && rolltui_str_eq(&id, K("save"))) {
    out->kind = ROLLTUI_KEYS_EDIT_SAVE_AS;
    rolltui_str_move(&out->value, &value);
    goto done;
  }
  if (kind == ROLLTUI_MENU_EVENT_CLOSED) out->kind = ROLLTUI_KEYS_EDIT_CLOSED;

done:
  rolltui_str_free(&id);
  rolltui_str_free(&value);
}
