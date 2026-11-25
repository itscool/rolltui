/* rolltui_widget_dialog.c — A QUESTION THE APP ASKS: `rolltui_windows_ask` opens it as a popup, the `dialog` kind draws and answers
 * it, and `rolltui_windows_answer` hands the answer back and closes it. The dialog lives in the window table under its id, so the
 * widget a sync makes for its window finds the same one, chosen button and typed phrase included, whatever is remade around it. */
#include "rolltui/c/rolltui_widget_dialog.h"

#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_feedback.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_texts.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_widget_kinds.h"

typedef struct DialogButton {
  RolltuiStr label;
  unsigned flags;
  char key;      /* the lowercase letter that answers it; 0: none */
  size_t key_at; /* where that letter is in the label, to underline it */
} DialogButton;

struct RolltuiDialog {
  int level;
  unsigned flags;
  RolltuiStr title, body, phrase, typed;
  RolltuiStr* items;
  size_t item_n, item_cap;
  DialogButton* buttons;
  size_t button_n, button_cap;
  size_t chosen, top; /* the button chosen; the first item in view */
  int answered;       /* the button answered; -1 while open */
  unsigned char copy_asked;
};

/* ---- the dialog --------------------------------------------------------------------------------- */

RolltuiDialog* rolltui_dialog_new(int level, unsigned flags, const char* title, size_t title_len, const char* body, size_t body_len) {
  RolltuiDialog* d = (RolltuiDialog*)rolltui_mem_alloc(sizeof *d);
  if (!d) return NULL;
  memset(d, 0, sizeof *d);
  d->level = level >= ROLLTUI_MESSAGE_INFO && level <= ROLLTUI_MESSAGE_ERROR ? level : ROLLTUI_MESSAGE_INFO;
  d->flags = flags;
  d->answered = -1;
  if (title) rolltui_str_set(&d->title, title, title_len);
  if (body) rolltui_str_set(&d->body, body, body_len);
  return d;
}
void rolltui_dialog_add_item(RolltuiDialog* d, const char* text, size_t len) {
  if (!d) return;
  d->items = (RolltuiStr*)rolltui_grow_zeroed(d->items, &d->item_cap, d->item_n + 1, sizeof *d->items);
  rolltui_str_set(&d->items[d->item_n++], text ? text : "", text ? len : 0);
}
void rolltui_dialog_add_button(RolltuiDialog* d, const char* label, size_t len, unsigned flags) {
  DialogButton* b;
  if (!d) return;
  d->buttons = (DialogButton*)rolltui_grow_zeroed(d->buttons, &d->button_cap, d->button_n + 1, sizeof *d->buttons);
  b = &d->buttons[d->button_n++];
  rolltui_str_set(&b->label, label ? label : "", label ? len : 0);
  b->flags = flags;
}
void rolltui_dialog_set_phrase(RolltuiDialog* d, const char* phrase, size_t len) {
  if (!d) return;
  rolltui_str_set(&d->phrase, phrase ? phrase : "", phrase ? len : 0);
  rolltui_str_clear(&d->typed);
}
void rolltui_dialog_free(RolltuiDialog* d) {
  size_t i;
  if (!d) return;
  rolltui_str_free(&d->title);
  rolltui_str_free(&d->body);
  rolltui_str_free(&d->phrase);
  rolltui_str_free(&d->typed);
  for (i = 0; i < d->item_n; ++i) rolltui_str_free(&d->items[i]);
  for (i = 0; i < d->button_n; ++i) rolltui_str_free(&d->buttons[i].label);
  rolltui_mem_free(d->items);
  rolltui_mem_free(d->buttons);
  rolltui_mem_free(d);
}

static size_t cancel_of(const RolltuiDialog* d) {
  size_t i;
  for (i = 0; i < d->button_n; ++i)
    if (d->buttons[i].flags & ROLLTUI_DIALOG_CANCEL) return i;
  return d->button_n ? d->button_n - 1 : 0;
}
/* A button answers only once the phrase is typed, the cancel button always. */
static int inert(const RolltuiDialog* d, size_t i) {
  return d->phrase.n && i != cancel_of(d) && !(d->typed.n == d->phrase.n && memcmp(d->typed.p, d->phrase.p, d->phrase.n) == 0);
}
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static int letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
/* Each button's letter: the first in its label that no button before it has. */
static void assign_keys(RolltuiDialog* d) {
  size_t i, j, k;
  for (i = 0; i < d->button_n; ++i) {
    DialogButton* b = &d->buttons[i];
    b->key = 0;
    for (j = 0; j < b->label.n && !b->key; ++j) {
      const char c = lower(b->label.p[j]);
      int taken = 0;
      if (!letter(c)) continue;
      for (k = 0; k < i; ++k) taken |= d->buttons[k].key == c;
      if (!taken) b->key = c, b->key_at = j;
    }
  }
}
static void answer(RolltuiDialog* d, size_t i) {
  if (i >= d->button_n || inert(d, i)) return;
  d->answered = (int)i;
}

/* ---- the widget ---------------------------------------------------------------------------------- */

typedef struct DialogCtx {
  RolltuiWindows* w; /* BORROWED */
  RolltuiStr id;
  RolltuiDrawScratch* draw;
  RolltuiWrapLines* wrap;
  RolltuiUnicodeScratch* measure;
  int item_rows;      /* the items' rows last frame, a page */
  RolltuiRect* hits;  /* each button's cells last frame, for a press */
  size_t hit_cap;
} DialogCtx;

static RolltuiDialog* dialog_of(const DialogCtx* c) { return rolltui_windows_dialog(c->w, c->id.p ? c->id.p : "", c->id.n); }

static int body_lines(DialogCtx* c, const RolltuiDialog* d, int width) {
  RolltuiWrapOptions wo;
  if (!d->body.n || width <= 0) return 0;
  memset(&wo, 0, sizeof wo);
  wo.ambiguous_wide = rolltui_windows_env(c->w)->ambiguous_wide;
  rolltui_wrap(c->wrap, d->body.p, d->body.n, width, wo);
  return (int)rolltui_wrap_line_count(c->wrap);
}
/* The rows the dialog holds at `width`, every item shown: the body, a gap, the items, a gap, the phrase's prompt and field, a gap and
 * the buttons. */
static int rows_for(DialogCtx* c, const RolltuiDialog* d, int width) {
  int rows = body_lines(c, d, width);
  if (d->item_n) rows += (rows ? 1 : 0) + (int)d->item_n;
  if (d->phrase.n) rows += (rows ? 1 : 0) + 2;
  return rows + (rows ? 1 : 0) + 1;
}

static void dialog_ctx_destroy(void* ctx) {
  DialogCtx* c = (DialogCtx*)ctx;
  rolltui_str_free(&c->id);
  rolltui_draw_scratch_free(c->draw);
  rolltui_wrap_free(c->wrap);
  rolltui_u_scratch_free(c->measure);
  rolltui_mem_free(c->hits);
  rolltui_mem_free(c);
}
/* A SCREEN THAT NAMES A DIALOG NO HOST ASKED says so, as an unbound source does: a window is never blank for want of what it shows. */
static int dialog_ctx_problem(void* ctx, RolltuiStr* out) {
  DialogCtx* c = (DialogCtx*)ctx;
  if (dialog_of(c)) return 0;
  rolltui_str_clear(out);
  rolltui_text_format(out, ROLLTUI_TEXT_NO_DIALOG, c->id.p, c->id.n, NULL, 0);
  return 1;
}
static void dialog_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  (void)ctx;
  (void)rn;
}
/* AS TALL AS WHAT IT HOLDS, at most nine tenths of the screen: the items scroll in what is left. */
static int dialog_ctx_desired_outer(void* ctx, int inner_w, int parent_extent, int border, int* out) {
  DialogCtx* c = (DialogCtx*)ctx;
  const RolltuiDialog* d = dialog_of(c);
  int want, most;
  if (!d) return 0;
  want = rows_for(c, d, inner_w) + border;
  most = parent_extent * 9 / 10;
  if (most < border + 3) most = border + 3;
  *out = want < most ? want : most;
  if (d->item_n) { /* the rows the items get at that height, for the footer asked before the draw */
    const long cut = (long)d->item_n - (want - *out);
    c->item_rows = cut < 1 ? 1 : (int)cut;
  }
  return 1;
}

static RolltuiStyle on_ground(RolltuiStyle s, const RolltuiFrame* f, int x, int y) {
  RolltuiCellRaw cell;
  memset(&cell, 0, sizeof cell);
  rolltui_frame_cell(f, x, y, &cell);
  s.bg = cell.style.bg;
  return s;
}

static void dialog_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  DialogCtx* c = (DialogCtx*)ctx;
  RolltuiDialog* d = dialog_of(c);
  const RolltuiStyle* styles = rolltui_windows_styles(c->w);
  const RolltuiBuiltinRoles* roles = rolltui_windows_builtin_roles(c->w);
  const int amb = rolltui_windows_env(c->w)->ambiguous_wide;
  RolltuiRect r;
  int y, lines, shown_lines, below, item_rows = 0, i, x;
  size_t b;
  rolltui_content_rect(rn, &r);
  if (!d || r.w <= 0 || r.h <= 0) return;
  y = r.y;
  /* what goes under the body, from the bottom up: the buttons, the phrase's two rows, at least one item, the gaps */
  below = 1 + (d->phrase.n ? 3 : 0) + (d->item_n ? 2 : 0);
  lines = body_lines(c, d, r.w);
  shown_lines = lines < r.h - below ? lines : r.h - below;
  if (shown_lines < 0) shown_lines = 0;
  for (i = 0; i < shown_lines; ++i, ++y) {
    const char* tp = NULL;
    size_t tn = 0, gn = 0;
    const RolltuiWrapGrapheme* gs = NULL;
    int width = 0, indent = 0, hard = 0;
    rolltui_wrap_line(c->wrap, (size_t)i, &tp, &tn, &gs, &gn, &width, &indent, &hard);
    rolltui_frame_put_text(f, c->draw, r.x + indent, y, tp, tn, on_ground(styles[roles->text], f, r.x, y), r.w - indent, amb, 0);
  }
  if (d->item_n) { /* THE ITEMS IT IS ABOUT, one line each, cut with an ellipsis; they scroll in what the dialog has left */
    if (y > r.y) ++y;
    item_rows = r.y + r.h - y - 2 - (d->phrase.n ? 3 : 0);
    if (item_rows < 1) item_rows = 1;
    if (item_rows > (int)d->item_n) item_rows = (int)d->item_n;
    if (d->top + (size_t)item_rows > d->item_n) d->top = d->item_n - (size_t)item_rows;
    for (i = 0; i < item_rows && y < r.y + r.h; ++i, ++y) {
      const RolltuiStr* it = &d->items[d->top + (size_t)i];
      rolltui_frame_put_text_cut(f, c->draw, r.x + 2, y, it->p ? it->p : "", it->n, on_ground(styles[roles->value], f, r.x, y),
                                 r.w - 2, ROLLTUI_CUT_END, amb, 0);
    }
  }
  c->item_rows = item_rows;
  if (d->phrase.n && y + 2 < r.y + r.h) { /* THE PHRASE: what to type, and the field it is typed in */
    size_t tn = 0;
    const char* say = rolltui_text(ROLLTUI_TEXT_TYPE_TO_CONFIRM, &tn); /* "Type {1} to confirm", the phrase in bold where {1} is */
    const char* at = NULL;
    size_t k;
    RolltuiStyle prompt, field;
    for (k = 0; k + 2 < tn && !at; ++k)
      if (say[k] == '{' && say[k + 1] == '1' && say[k + 2] == '}') at = say + k;
    if (y > r.y) ++y;
    prompt = on_ground(styles[roles->prompt], f, r.x, y);
    x = r.x + rolltui_frame_put_text(f, c->draw, r.x, y, say, at ? (size_t)(at - say) : tn, prompt, r.w, amb, 0);
    if (at) {
      prompt.bold = 1;
      x += rolltui_frame_put_text(f, c->draw, x, y, d->phrase.p, d->phrase.n, prompt, r.x + r.w - x, amb, 0);
      prompt.bold = 0;
      rolltui_frame_put_text(f, c->draw, x, y, at + 3, tn - (size_t)(at + 3 - say), prompt, r.x + r.w - x, amb, 0);
    }
    ++y;
    field = styles[roles->input_text];
    field.underline = 1; /* the field's extent, in every theme */
    for (x = r.x; x < r.x + r.w; ++x) rolltui_frame_put(f, x, y, " ", 1, 1, field, 0);
    x = r.x + rolltui_frame_put_text_cut(f, c->draw, r.x, y, d->typed.p ? d->typed.p : "", d->typed.n, field, r.w - 1,
                                         ROLLTUI_CUT_START, amb, 0);
    if (rn->focused) rolltui_frame_set_cursor(f, x < r.x + r.w ? x : r.x + r.w - 1, y, 1);
    ++y;
  }
  { /* THE BUTTONS on the last row, centred, each framed; the chosen one in the menu's selection, an inert one muted */
    const int by = r.y + r.h - 1;
    int total = 0;
    int* widths = NULL;
    size_t cap = 0;
    widths = (int*)rolltui_grow_zeroed(widths, &cap, d->button_n + 1, sizeof *widths);
    c->hits = (RolltuiRect*)rolltui_grow_zeroed(c->hits, &c->hit_cap, d->button_n + 1, sizeof *c->hits);
    for (b = 0; b < d->button_n; ++b) {
      const RolltuiStr* l = &d->buttons[b].label;
      widths[b] = rolltui_u_display_width(c->measure, l->p ? l->p : "", l->n, amb) + 4;
      total += widths[b] + (b ? 2 : 0);
    }
    x = r.x + (r.w > total ? (r.w - total) / 2 : 0);
    for (b = 0; b < d->button_n; ++b) {
      const DialogButton* bt = &d->buttons[b];
      const int chosen = b == d->chosen, dead = inert(d, b);
      RolltuiStyle frame = on_ground(styles[chosen ? ROLLTUI_ROLE_BORDER_ACTIVE : roles->border], f, x, by);
      RolltuiStyle label = styles[chosen && !dead ? ROLLTUI_ROLE_MENU_SELECTED : dead ? roles->text_muted : ROLLTUI_ROLE_MENU_ITEM];
      int at = x;
      if (!chosen || dead) label = on_ground(label, f, x, by);
      if (dead) label.dim = 1;
      if (chosen) label.bold = 1;
      c->hits[b].x = x, c->hits[b].y = by, c->hits[b].w = widths[b], c->hits[b].h = 1;
      at += rolltui_frame_put_text(f, c->draw, at, by, "[", 1, frame, r.x + r.w - at, amb, 0);
      at += rolltui_frame_put_text(f, c->draw, at, by, " ", 1, label, r.x + r.w - at, amb, 0);
      if (bt->key && !d->phrase.n) { /* its letter underlined: what answers it */
        RolltuiStyle key = label;
        key.underline = 1;
        key.bold = 1;
        at += rolltui_frame_put_text(f, c->draw, at, by, bt->label.p, bt->key_at, label, r.x + r.w - at, amb, 0);
        at += rolltui_frame_put_text(f, c->draw, at, by, bt->label.p + bt->key_at, 1, key, r.x + r.w - at, amb, 0);
        at += rolltui_frame_put_text(f, c->draw, at, by, bt->label.p + bt->key_at + 1, bt->label.n - bt->key_at - 1, label,
                                     r.x + r.w - at, amb, 0);
      } else {
        at += rolltui_frame_put_text(f, c->draw, at, by, bt->label.p ? bt->label.p : "", bt->label.n, label, r.x + r.w - at, amb, 0);
      }
      at += rolltui_frame_put_text(f, c->draw, at, by, " ", 1, label, r.x + r.w - at, amb, 0);
      rolltui_frame_put_text(f, c->draw, at, by, "]", 1, frame, r.x + r.w - at, amb, 0);
      x += widths[b] + 2;
    }
    rolltui_mem_free(widths);
  }
}

static void scroll_items(DialogCtx* c, RolltuiDialog* d, long by) {
  const size_t page = c->item_rows > 0 ? (size_t)c->item_rows : 1;
  const size_t most = d->item_n > page ? d->item_n - page : 0;
  if (by < 0) d->top = (size_t)(-by) > d->top ? 0 : d->top - (size_t)(-by);
  else d->top = d->top + (size_t)by > most ? most : d->top + (size_t)by;
}

static int dialog_ctx_perform(void* ctx, const char* action, size_t len) {
  DialogCtx* c = (DialogCtx*)ctx;
  RolltuiDialog* d = dialog_of(c);
  const long page = c->item_rows > 1 ? c->item_rows - 1 : 1;
#define IS(name) (len == sizeof name - 1 && memcmp(action, name, len) == 0)
  if (!d || !action) return 0;
  if (IS("dialog.next")) d->chosen = d->button_n ? (d->chosen + 1) % d->button_n : 0;
  else if (IS("dialog.prev")) d->chosen = d->button_n ? (d->chosen + d->button_n - 1) % d->button_n : 0;
  else if (IS("dialog.answer")) answer(d, d->chosen);
  else if (IS("dialog.up")) scroll_items(c, d, -1);
  else if (IS("dialog.down")) scroll_items(c, d, 1);
  else if (IS("dialog.page_up")) scroll_items(c, d, -page);
  else if (IS("dialog.page_down")) scroll_items(c, d, page);
  else if (IS("dialog.copy")) d->copy_asked = 1;
  else return 0;
#undef IS
  return 1;
}

static void type_into(RolltuiDialog* d, const char* s, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i)
    if ((unsigned char)s[i] >= 0x20 && s[i] != 0x7f) rolltui_str_append(&d->typed, s + i, 1); /* a paste's newlines and controls go */
}
static void type_char(RolltuiDialog* d, unsigned long cp) {
  char b[4];
  size_t n;
  if (cp < 0x20 || cp == 0x7f || cp > 0x10FFFF) return;
  if (cp < 0x80) b[0] = (char)cp, n = 1;
  else if (cp < 0x800) b[0] = (char)(0xC0 | (cp >> 6)), b[1] = (char)(0x80 | (cp & 0x3F)), n = 2;
  else if (cp < 0x10000)
    b[0] = (char)(0xE0 | (cp >> 12)), b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)), b[2] = (char)(0x80 | (cp & 0x3F)), n = 3;
  else
    b[0] = (char)(0xF0 | (cp >> 18)), b[1] = (char)(0x80 | ((cp >> 12) & 0x3F)), b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)),
    b[3] = (char)(0x80 | (cp & 0x3F)), n = 4;
  rolltui_str_append(&d->typed, b, n);
}
static void erase_char(RolltuiDialog* d) {
  while (d->typed.n && ((unsigned char)d->typed.p[d->typed.n - 1] & 0xC0) == 0x80) --d->typed.n; /* the code point's tail */
  if (d->typed.n) --d->typed.n;
  if (d->typed.p) d->typed.p[d->typed.n] = 0;
}
static int plain(const RolltuiChord* k) { return k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt && !k->super; }
static int button_for(const RolltuiDialog* d, const RolltuiChord* k) {
  size_t i;
  if (!plain(k) || k->ch > 0x7f) return -1;
  for (i = 0; i < d->button_n; ++i)
    if (d->buttons[i].key && d->buttons[i].key == lower((char)k->ch)) return (int)i;
  return -1;
}

/* A MODAL QUESTION TAKES EVERYTHING but the close key, which the stack has already turned into the cancel button's answer. */
static int dialog_ctx_handle(void* ctx, const RolltuiEvent* e) {
  DialogCtx* c = (DialogCtx*)ctx;
  RolltuiDialog* d = dialog_of(c);
  if (!d) return 0;
  if (e->kind == ROLLTUI_EVENT_PASTE) {
    if (d->phrase.n && e->text) type_into(d, e->text, e->text_len);
    return 1;
  }
  if (e->kind == ROLLTUI_EVENT_MOUSE) {
    size_t b;
    if (e->mouse.kind == 4) scroll_items(c, d, -1);
    else if (e->mouse.kind == 5) scroll_items(c, d, 1);
    else if (e->mouse.kind == 0 && e->mouse.button == 1)
      for (b = 0; b < d->button_n && b < c->hit_cap; ++b) {
        const RolltuiRect h = c->hits[b];
        if (e->mouse.y == h.y && e->mouse.x >= h.x && e->mouse.x < h.x + h.w) {
          d->chosen = b;
          answer(d, b);
        }
      }
    return 1;
  }
  if (e->kind == ROLLTUI_EVENT_KEY) {
    size_t n = 0;
    const char* a = rolltui_bindings_action_for(rolltui_windows_bindings(c->w), &e->key, "dialog", 6, &n);
    int b;
    if (a && dialog_ctx_perform(ctx, a, n)) return 1;
    if (d->phrase.n) {
      if (e->key.key == ROLLTUI_KEY_BACKSPACE) erase_char(d);
      else if (plain(&e->key)) type_char(d, (unsigned long)e->key.ch);
      return 1;
    }
    b = button_for(d, &e->key);
    if (b >= 0) {
      d->chosen = (size_t)b;
      answer(d, (size_t)b);
    }
    return 1;
  }
  return 0;
}
static int dialog_ctx_keys(void* ctx, const char** scope, size_t* len) {
  (void)ctx;
  *scope = "dialog", *len = 6;
  return 0; /* no counts: a digit is nothing here */
}
static int dialog_ctx_takes(void* ctx, const RolltuiChord* k) {
  DialogCtx* c = (DialogCtx*)ctx;
  const RolltuiDialog* d = dialog_of(c);
  size_t n = 0;
  if (!d) return 0;
  if (rolltui_bindings_action_for(rolltui_windows_bindings(c->w), k, "dialog", 6, &n)) return 1;
  if (d->phrase.n) return plain(k) || k->key == ROLLTUI_KEY_BACKSPACE;
  return button_for(d, k) >= 0;
}
/* "3–8 of 40" in the bottom border while the items do not all fit */
static int dialog_ctx_footer(void* ctx, RolltuiStr* out) {
  DialogCtx* c = (DialogCtx*)ctx;
  const RolltuiDialog* d = dialog_of(c);
  char b[64];
  int n;
  if (!d || c->item_rows <= 0 || (size_t)c->item_rows >= d->item_n) return 0;
  n = snprintf(b, sizeof b, "%zu\xE2\x80\x93%zu of %zu", d->top + 1, d->top + (size_t)c->item_rows, d->item_n);
  if (n <= 0) return 0;
  rolltui_str_set(out, b, (size_t)n);
  return 1;
}

static const RolltuiWidgetPlugin kDialogPlugin = {
    dialog_ctx_destroy, dialog_ctx_layout, dialog_ctx_draw, dialog_ctx_problem, NULL /* note_at */, dialog_ctx_desired_outer,
    dialog_ctx_handle, NULL /* scroll_extent: the items scroll inside */, NULL, NULL /* title: the layout's */, NULL /* back: no levels */,
    dialog_ctx_keys, dialog_ctx_perform, dialog_ctx_footer, dialog_ctx_takes};

RolltuiWidget rolltui_dialog_widget_factory(void* ctx, RolltuiWindows* w, const char* content, size_t len) {
  RolltuiWidget out;
  DialogCtx* c = (DialogCtx*)rolltui_mem_alloc(sizeof *c);
  const char* colon = content ? (const char*)memchr(content, ':', len) : NULL;
  (void)ctx;
  memset(&out, 0, sizeof out);
  if (!c) return out;
  memset(c, 0, sizeof *c);
  c->w = w;
  if (colon) rolltui_str_set(&c->id, colon + 1, len - (size_t)(colon + 1 - content));
  c->draw = rolltui_draw_scratch_new();
  c->wrap = rolltui_wrap_new();
  c->measure = rolltui_u_scratch_new();
  out.vt = &kDialogPlugin;
  out.ctx = c;
  return out;
}

/* ---- asking and answering ------------------------------------------------------------------------ */

static void window_id(const char* id, size_t len, RolltuiStr* out) {
  rolltui_str_set(out, "dialog:", 7);
  rolltui_str_append(out, id, len);
}

int rolltui_windows_ask(RolltuiWindows* w, RolltuiWindowStack* s, const char* id, size_t len, RolltuiDialog* d) {
  RolltuiLayer l;
  RolltuiStr wid;
  size_t gl = 0;
  const char* glyph;
  if (!w || !s || !d || !id || !len) {
    rolltui_dialog_free(d);
    return 0;
  }
  if (!d->button_n) rolltui_dialog_add_button(d, "OK", 2, ROLLTUI_DIALOG_DEFAULT | ROLLTUI_DIALOG_CANCEL);
  assign_keys(d);
  {
    size_t i;
    d->chosen = 0;
    for (i = 0; i < d->button_n; ++i)
      if (d->buttons[i].flags & ROLLTUI_DIALOG_DEFAULT) {
        d->chosen = i;
        break;
      }
  }
  memset(&wid, 0, sizeof wid);
  window_id(id, len, &wid);
  rolltui_window_stack_remove_layer(s, wid.p, wid.n); /* one of that id already open is replaced */
  rolltui_windows_put_dialog(w, id, len, d);
  rolltui_layer_init(&l);
  rolltui_str_set(&l.id, wid.p, wid.n);
  l.modal = 1;
  if (d->flags & ROLLTUI_DIALOG_FULL_SCREEN) {
    l.placement.w.fraction = 1, l.placement.h.fraction = 1;
  } else {
    l.placement.x.fraction = 0.5, l.placement.y.fraction = 0.5;
    l.placement.w.fraction = 0.6;
    l.placement.w.cells = 0;
    l.placement.h.fraction = 0, l.placement.h.cells = 5; /* until the window table sizes it to what it holds */
    l.placement.anchor = 4;   /* the centre */
    l.placement.clamp = 1;
    l.placement.edge_margin = 2;
    l.placement.min_w.present = 1, l.placement.min_w.d.cells = 36;
    l.placement.max_w.present = 1, l.placement.max_w.d.cells = 76;
  }
  l.root.kind = ROLLTUI_NODE_WINDOW;
  rolltui_str_set(&l.root.id, wid.p, wid.n);
  rolltui_str_set(&l.root.content, wid.p, wid.n);
  l.root.border = ROLLTUI_BORDER_ROUNDED;
  l.root.focusable = 1;
  l.root.fit = (unsigned char)!(d->flags & ROLLTUI_DIALOG_FULL_SCREEN);
  l.root.background = ROLLTUI_ROLE_PANEL_BACKGROUND;
  l.root.border_role = rolltui_message_role(rolltui_windows_builtin_roles(w), d->level);
  glyph = rolltui_message_glyph(d->level, &gl); /* the level reads without colour */
  rolltui_str_set(&l.root.title, glyph, gl);
  if (d->title.n) {
    rolltui_str_append(&l.root.title, " ", 1);
    rolltui_str_append_str(&l.root.title, &d->title);
  }
  rolltui_window_stack_push(s, &l);
  rolltui_layer_release(&l);
  rolltui_window_stack_focus(s, wid.p, wid.n);
  rolltui_str_free(&wid);
  return 1;
}

int rolltui_windows_answer(RolltuiWindows* w, RolltuiWindowStack* s, const char* id, size_t len, int* button, RolltuiStr* copy) {
  RolltuiDialog* d = w && id ? rolltui_windows_dialog(w, id, len) : NULL;
  RolltuiStr wid;
  int open, at;
  if (!d || !s) return 0;
  if (d->copy_asked) { /* THE TEXT, for the host's clipboard: the title, the body, each item on its own line */
    size_t i;
    d->copy_asked = 0;
    if (copy) {
      rolltui_str_clear(copy);
      rolltui_str_append_str(copy, &d->title);
      if (d->body.n) rolltui_str_append(copy, "\n\n", d->title.n ? 2 : 0), rolltui_str_append_str(copy, &d->body);
      if (d->item_n) rolltui_str_append(copy, "\n", 1);
      for (i = 0; i < d->item_n; ++i) rolltui_str_append(copy, "\n", 1), rolltui_str_append_str(copy, &d->items[i]);
    }
    return ROLLTUI_DIALOG_COPY;
  }
  memset(&wid, 0, sizeof wid);
  window_id(id, len, &wid);
  open = rolltui_window_stack_has_popup(s, wid.p, wid.n);
  if (open && d->answered < 0) {
    rolltui_str_free(&wid);
    return 0;
  }
  at = d->answered >= 0 ? d->answered : (int)cancel_of(d); /* closed without an answer: the close key, a press outside, the host */
  if (open) rolltui_window_stack_remove_layer(s, wid.p, wid.n);
  rolltui_str_free(&wid);
  rolltui_windows_put_dialog(w, id, len, NULL);
  if (button) *button = at;
  return ROLLTUI_DIALOG_ANSWERED;
}
