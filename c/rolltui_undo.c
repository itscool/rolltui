/* rolltui/c/rolltui_undo.c — see rolltui_undo.h. */
#include "rolltui/c/rolltui_undo.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"

struct RolltuiUndoStack {
  void** v;      /* GROWING, AMORTISED (rolltui_grow): owned snapshots, appended a commit
                   * at a time with the final count never known in advance. */
  size_t n, cap; /* history length and capacity, in ELEMENTS */
  size_t at;     /* the current position: v[at] is current() */
  size_t limit;  /* commit() drops the oldest past this many entries */
  RolltuiUndoFreeFn free_fn;
};

RolltuiUndoStack* rolltui_undo_new(size_t limit, RolltuiUndoFreeFn free_fn) {
  RolltuiUndoStack* u = (RolltuiUndoStack*)rolltui_mem_alloc(sizeof *u);
  u->v = NULL;
  u->n = 0;
  u->cap = 0;
  u->at = 0;
  u->limit = limit;
  u->free_fn = free_fn;
  return u;
}

void rolltui_undo_free(RolltuiUndoStack* u) {
  size_t i;
  if (!u) return;
  for (i = 0; i < u->n; ++i) u->free_fn(u->v[i]);
  rolltui_mem_free(u->v);
  rolltui_mem_free(u);
}

void rolltui_undo_reset(RolltuiUndoStack* u, void* baseline) {
  size_t i;
  for (i = 0; i < u->n; ++i) u->free_fn(u->v[i]);
  u->n = 0;
  u->v = (void**)rolltui_grow(u->v, &u->cap, 1, sizeof *u->v);
  u->v[u->n++] = baseline;
  u->at = 0;
}

void rolltui_undo_commit(RolltuiUndoStack* u, void* value) {
  size_t i;
  if (u->n == 0) {
    u->v = (void**)rolltui_grow(u->v, &u->cap, 1, sizeof *u->v);
    u->v[u->n++] = value;
    u->at = 0;
    return;
  }
  /* Drop the redo branch: every snapshot past the current position is freed, the same
   * as `history_.resize(at_ + 1)` freeing the elements std::vector truncates away. */
  for (i = u->at + 1; i < u->n; ++i) u->free_fn(u->v[i]);
  u->n = u->at + 1;
  u->v = (void**)rolltui_grow(u->v, &u->cap, u->n + 1, sizeof *u->v);
  u->v[u->n++] = value;
  if (u->n > u->limit) {
    /* Drop the OLDEST rather than refuse the newest — the bound is on history KEPT, not
     * on commits allowed. */
    u->free_fn(u->v[0]);
    memmove(u->v, u->v + 1, (u->n - 1) * sizeof *u->v);
    --u->n;
  }
  u->at = u->n - 1;
}

void rolltui_undo_replace_current(RolltuiUndoStack* u, void* value) {
  if (u->n == 0) {
    u->v = (void**)rolltui_grow(u->v, &u->cap, 1, sizeof *u->v);
    u->v[u->n++] = value;
    u->at = 0;
    return;
  }
  u->free_fn(u->v[u->at]);
  u->v[u->at] = value;
}

int rolltui_undo_undo(RolltuiUndoStack* u) {
  if (u->at == 0) return 0;
  --u->at;
  return 1;
}

int rolltui_undo_redo(RolltuiUndoStack* u) {
  if (u->at + 1 >= u->n) return 0;
  ++u->at;
  return 1;
}

int rolltui_undo_can_undo(const RolltuiUndoStack* u) { return u->at > 0; }
int rolltui_undo_can_redo(const RolltuiUndoStack* u) { return u->at + 1 < u->n; }
size_t rolltui_undo_undo_depth(const RolltuiUndoStack* u) { return u->at; }
size_t rolltui_undo_redo_depth(const RolltuiUndoStack* u) { return u->n == 0 ? 0 : u->n - 1 - u->at; }
int rolltui_undo_empty(const RolltuiUndoStack* u) { return u->n == 0; }
const void* rolltui_undo_current(const RolltuiUndoStack* u) { return u->n == 0 ? NULL : u->v[u->at]; }
