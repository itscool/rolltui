#ifndef ROLLTUI_C_WIDGET_LIST_H
#define ROLLTUI_C_WIDGET_LIST_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h` (a `list:` window, its rows, columns and options, and
 * the window table's calls for it). What is below is the widget itself, which the window table owns by name.
 *
 * rolltui/c/rolltui_widget_list.h — A LIST, A TABLE AND A TREE, fed by index.
 */
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/* THE ACTION NAMES the list's keys are resolved against, in the `list` scope: this file knows what each does and none of the words. */
typedef struct RolltuiListActions {
  const char *up, *down, *page_up, *page_down, *first, *last, *activate, *back;
  const char *mark, *mark_up, *mark_down, *mark_all, *unmark_all, *invert_marks;
  const char *open, *close, *open_all, *close_all, *sort, *reverse, *filter;
} RolltuiListActions;
const RolltuiListActions* rolltui_list_default_actions(void);

/* The roles a draw needs, handed in as bytes: this file names no role. */
typedef struct RolltuiListRoles {
  unsigned char text, text_muted, header, cursor, cursor_away, mark, hover, prompt;
  unsigned char heading; /* a section heading's name (`headings`), as a menu names its sections; 0: `header` */
} RolltuiListRoles;

typedef struct RolltuiList RolltuiList;

RolltuiList* rolltui_list_new(void);
void rolltui_list_free(RolltuiList* l); /* a no-op on NULL; frees the source's ctx through its free_ctx */
void rolltui_list_set_source(RolltuiList* l, RolltuiListCountFn count, RolltuiListRowFn row, void* ctx, void (*free_ctx)(void*));
int rolltui_list_has_source(const RolltuiList* l);
void rolltui_list_set_options(RolltuiList* l, const RolltuiListOptions* o);
void rolltui_list_set_columns(RolltuiList* l, const RolltuiListColumn* cols, size_t n);
void rolltui_list_refresh(RolltuiList* l);
void rolltui_list_set_filter(RolltuiList* l, const char* q, size_t n);
void rolltui_list_set_cursor(RolltuiList* l, const char* key, size_t n);
int rolltui_list_cursor(RolltuiList* l, RolltuiStr* key);
size_t rolltui_list_marked(const RolltuiList* l, RolltuiStrListRaw* keys);
int rolltui_list_event(RolltuiList* l, RolltuiListEvent* out);
/* One command by its action name; 1 when it did something. */
int rolltui_list_perform(RolltuiList* l, const RolltuiListActions* a, const char* action, size_t n);
/* Closes one level of its own: the filter, then the marks; 0 when neither is there. */
int rolltui_list_back(RolltuiList* l);
/* Whether `back` would close something: a filter, or marks. */
int rolltui_list_has_back(const RolltuiList* l);
/* 1 while the filter is being typed: a printable key is the filter's. */
int rolltui_list_typing(const RolltuiList* l);
/* What the list says in its window's footer, into `out` (replaced): "12 of 340" while it shows more rows than fit or a filter
 * narrows it, and "5 marked". 0 when it has nothing to say. */
int rolltui_list_footer(RolltuiList* l, RolltuiStr* out);
/* The cells column `k` was given when the list was last laid out, and where it starts; 0 and -1 for a column that did not fit. */
int rolltui_list_column_width(const RolltuiList* l, size_t k);
int rolltui_list_column_x(const RolltuiList* l, size_t k);
/* The screen row the cursor's row is drawn on, or -1 while it is scrolled out of the window: where a sheet edits a row in place. */
int rolltui_list_cursor_y(const RolltuiList* l);
void rolltui_list_layout(RolltuiList* l, RolltuiRect area, int ambiguous_wide);
/* Draws what the last layout placed. `hover_y` is the row under the pointer, -1 for none. */
void rolltui_list_draw(RolltuiList* l, RolltuiFrame* f, const RolltuiStyle* styles, const RolltuiListRoles* roles, int focused,
                       int hover_y, int ambiguous_wide);
int rolltui_list_handle(RolltuiList* l, const RolltuiEvent* e, const RolltuiBindings* b, const RolltuiListActions* a);
int rolltui_list_scroll_extent(RolltuiList* l, RolltuiScrollExtent* out);
int rolltui_list_scroll_to(RolltuiList* l, size_t first);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_WIDGET_LIST_H */
