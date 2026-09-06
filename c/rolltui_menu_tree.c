/* rolltui/c/rolltui_menu_tree.c — see rolltui_menu_tree.h. Compiled into BOTH
 * configurations: this is the DATA both implementations of the widget walk. */
#include "rolltui/c/rolltui_menu_tree.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"

/* ---- the spec ---------------------------------------------------------------------------- */

void rolltui_input_spec_init(RolltuiInputSpec* s) {
  memset(s, 0, sizeof *s);
  /* The four fields whose zero is NOT the default. Named here rather than left to a `= {0}`
   * at each call site — a spec with min == max == 0 accepts nothing at all. */
  s->min = -1e15;
  s->max = 1e15;
  s->step = 1;
  s->precision = -1;
}

void rolltui_input_spec_release(RolltuiInputSpec* s) {
  rolltui_str_free(&s->validator);
  rolltui_str_free(&s->hint);
}

void rolltui_input_spec_copy(RolltuiInputSpec* to, const RolltuiInputSpec* from) {
  if (to == from) return;
  rolltui_str_set(&to->validator, from->validator.p, from->validator.n);
  rolltui_str_set(&to->hint, from->hint.p, from->hint.n);
  to->type = from->type;
  to->min = from->min;
  to->max = from->max;
  to->step = from->step;
  to->precision = from->precision;
  to->max_len = from->max_len;
  to->min_len = from->min_len;
  to->optional = from->optional;
}

int rolltui_input_spec_equal(const RolltuiInputSpec* a, const RolltuiInputSpec* b) {
  return a->type == b->type && a->min == b->min && a->max == b->max && a->step == b->step &&
         a->precision == b->precision && a->max_len == b->max_len && a->min_len == b->min_len &&
         a->optional == b->optional && rolltui_str_eq(&a->validator, b->validator.p, b->validator.n) &&
         rolltui_str_eq(&a->hint, b->hint.p, b->hint.n);
}

/* ---- the child list ------------------------------------------------------------------------ */
/* THE MECHANICS ARE `RolltuiPtrVec`'s, not a second copy of them: the two lists have the same
 * layout by construction and the cast is what says so. A C++ template would have made them
 * one type; in C the honest answer is one mechanism with two typed faces. */

size_t rolltui_menu_list_count(const RolltuiMenuItemList* l) { return l->n; }

RolltuiMenuItem* rolltui_menu_list_at(const RolltuiMenuItemList* l, size_t i) {
  return i < l->n ? l->v[i] : NULL;
}

RolltuiMenuItem* rolltui_menu_list_add(RolltuiMenuItemList* l) {
  RolltuiMenuItem* it = rolltui_menu_item_new();
  rolltui_ptrvec_push((RolltuiPtrVec*)l, it);
  return it;
}

void rolltui_menu_list_clear(RolltuiMenuItemList* l) {
  size_t i;
  for (i = 0; i < l->n; ++i) rolltui_menu_item_free(l->v[i]);
  l->n = 0; /* the ARRAY stays — the reuse a `clear()` must not throw away */
}

void rolltui_menu_list_release(RolltuiMenuItemList* l) {
  rolltui_menu_list_clear(l);
  rolltui_ptrvec_free((RolltuiPtrVec*)l);
}

void rolltui_menu_list_copy(RolltuiMenuItemList* to, const RolltuiMenuItemList* from) {
  size_t i;
  if (to == from) return;
  rolltui_menu_list_clear(to);
  for (i = 0; i < from->n; ++i) rolltui_menu_item_copy(rolltui_menu_list_add(to), from->v[i]);
}

/* ---- the item -------------------------------------------------------------------------------- */

void rolltui_menu_item_set(RolltuiMenuItem* it, unsigned char kind, const char* id, size_t id_len, const char* label,
                           size_t label_len, const char* shortcut, size_t shortcut_len) {
  it->kind = kind;
  rolltui_str_set(&it->id, id, id_len);
  rolltui_str_set(&it->label, label, label_len);
  rolltui_str_set(&it->shortcut, shortcut, shortcut_len);
}

void rolltui_menu_item_init(RolltuiMenuItem* it) {
  memset(it, 0, sizeof *it);
  it->enabled = 1; /* the one field whose zero is not the default */
  rolltui_input_spec_init(&it->spec);
}

RolltuiMenuItem* rolltui_menu_item_new(void) {
  RolltuiMenuItem* it = (RolltuiMenuItem*)rolltui_mem_alloc(sizeof *it);
  rolltui_menu_item_init(it);
  return it;
}

void rolltui_menu_item_release(RolltuiMenuItem* it) {
  if (!it) return;
  rolltui_str_free(&it->id);
  rolltui_str_free(&it->label);
  rolltui_str_free(&it->action_name);
  rolltui_str_free(&it->shortcut);
  rolltui_str_free(&it->value);
  rolltui_input_spec_release(&it->spec);
  rolltui_menu_list_release(&it->children);
  rolltui_menu_item_init(it);
}

void rolltui_menu_item_free(RolltuiMenuItem* it) {
  if (!it) return;
  rolltui_menu_item_release(it);
  rolltui_mem_free(it);
}

void rolltui_menu_item_copy(RolltuiMenuItem* to, const RolltuiMenuItem* from) {
  if (to == from) return;
  to->kind = from->kind;
  rolltui_str_set(&to->id, from->id.p, from->id.n);
  rolltui_str_set(&to->label, from->label.p, from->label.n);
  rolltui_str_set(&to->action_name, from->action_name.p, from->action_name.n);
  rolltui_str_set(&to->shortcut, from->shortcut.p, from->shortcut.n);
  rolltui_str_set(&to->value, from->value.p, from->value.n);
  to->enabled = from->enabled;
  to->checked = from->checked;
  rolltui_input_spec_copy(&to->spec, &from->spec);
  rolltui_menu_list_copy(&to->children, &from->children);
}

int rolltui_menu_item_equal(const RolltuiMenuItem* a, const RolltuiMenuItem* b) {
  size_t i;
  if (a == b) return 1;
  if (a->kind != b->kind || a->enabled != b->enabled || a->checked != b->checked) return 0;
  if (!rolltui_str_eq(&a->id, b->id.p, b->id.n) || !rolltui_str_eq(&a->label, b->label.p, b->label.n) ||
      !rolltui_str_eq(&a->action_name, b->action_name.p, b->action_name.n) ||
      !rolltui_str_eq(&a->shortcut, b->shortcut.p, b->shortcut.n) ||
      !rolltui_str_eq(&a->value, b->value.p, b->value.n))
    return 0;
  if (!rolltui_input_spec_equal(&a->spec, &b->spec)) return 0;
  if (a->children.n != b->children.n) return 0;
  for (i = 0; i < a->children.n; ++i)
    if (!rolltui_menu_item_equal(a->children.v[i], b->children.v[i])) return 0;
  return 1;
}
