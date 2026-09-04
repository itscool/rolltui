/* rolltui/c/rolltui_document.c — see rolltui_document.h. Compiled into BOTH configurations:
 * this is the DATA a HOST fills and both implementations of the transcript walk. */
#include "rolltui/c/rolltui_document.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"

void rolltui_doc_entry_init(RolltuiDocEntry* e) {
  memset(e, 0, sizeof *e);
  /* The fields whose zero is NOT the default — plus `role` and `state`, which ARE zero and
   * are named anyway so a reader is never left inferring which of the two it is. */
  e->markdown = 1;
  e->role = ROLLTUI_ROLE_DEFAULT_TEXT;
  e->prefix_role = ROLLTUI_ROLE_DEFAULT_PROMPT;
  e->folded = 1;
}

void rolltui_doc_entry_release(RolltuiDocEntry* e) {
  if (!e) return;
  rolltui_str_free(&e->id);
  rolltui_str_free(&e->text);
  rolltui_str_free(&e->prefix);
  rolltui_str_free(&e->summary);
  rolltui_doc_entry_init(e);
}

void rolltui_doc_entry_copy(RolltuiDocEntry* to, const RolltuiDocEntry* from) {
  if (to == from) return;
  rolltui_str_set(&to->id, from->id.p, from->id.n);
  rolltui_str_set(&to->text, from->text.p, from->text.n);
  rolltui_str_set(&to->prefix, from->prefix.p, from->prefix.n);
  rolltui_str_set(&to->summary, from->summary.p, from->summary.n);
  to->version = from->version;
  to->markdown = from->markdown;
  to->role = from->role;
  to->prefix_role = from->prefix_role;
  to->foldable = from->foldable;
  to->folded = from->folded;
  to->state = from->state;
  to->progress = from->progress;
  to->state_since_ms = from->state_since_ms;
}

size_t rolltui_document_count(const RolltuiDocument* d) { return d->n; }

RolltuiDocEntry* rolltui_document_at(const RolltuiDocument* d, size_t i) {
  return i < d->n ? d->v[i] : NULL;
}

RolltuiDocEntry* rolltui_document_add(RolltuiDocument* d) {
  /* GROWING, AMORTISED AND **ZEROED** — the one place this list does not use
   * `rolltui_ptrvec_push`, and the reason is the reuse below: a slot past `n` may still hold
   * an entry a `clear` released and kept, and telling that from garbage requires the unwritten
   * slots to be NULL. `rolltui_alloc.h` names exactly this case ("a slot holding an owned
   * pointer, where garbage would be freed as if it were a buffer"); ASan named it too, on the
   * first run, as a use-after-poison in `release`. */
  RolltuiDocEntry* e;
  d->v = (RolltuiDocEntry**)rolltui_grow_zeroed(d->v, &d->cap, d->n + 1, sizeof *d->v);
  if (d->v[d->n]) return d->v[d->n++]; /* released and kept by a previous clear */
  e = (RolltuiDocEntry*)rolltui_mem_alloc(sizeof *e);
  rolltui_doc_entry_init(e);
  d->v[d->n++] = e;
  return e;
}

void rolltui_document_clear(RolltuiDocument* d) {
  size_t i;
  for (i = 0; i < d->n; ++i) rolltui_doc_entry_release(d->v[i]);
  d->n = 0; /* the entries and the array both stay, for the next fill */
}

void rolltui_document_release(RolltuiDocument* d) {
  size_t i;
  rolltui_document_clear(d);
  /* Every slot up to `cap`, because `clear` keeps its entries past `n` — and every one of
   * them is either an entry or NULL, which is what the zeroed growth above guarantees. */
  for (i = 0; i < d->cap; ++i) {
    if (!d->v[i]) continue;
    rolltui_doc_entry_release(d->v[i]);
    rolltui_mem_free(d->v[i]);
    d->v[i] = NULL;
  }
  rolltui_ptrvec_free((RolltuiPtrVec*)d);
}

void rolltui_document_copy(RolltuiDocument* to, const RolltuiDocument* from) {
  size_t i;
  if (to == from) return;
  rolltui_document_clear(to);
  for (i = 0; i < from->n; ++i) rolltui_doc_entry_copy(rolltui_document_add(to), from->v[i]);
}
