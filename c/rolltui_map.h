#ifndef ROLLTUI_C_MAP_H
#define ROLLTUI_C_MAP_H
/*
 * rolltui/c/rolltui_map.h — A STRING-KEYED TABLE, ONCE (Phase 15 m5).
 *
 * The last container the port needs, and the one the C++ used most invisibly: SEVEN
 * `std::unordered_map<std::string, T>`s across two modules. The transcript alone has five —
 * the parse cache, the layout cache, the find-text cache, the fold overrides and the
 * code-fold states — and `Windows` has four more, one of which owns every widget in the
 * program.
 *
 * ---- WHY A LINEAR TABLE AND NOT A HASH MAP ---------------------------------------------
 *
 * Because the sizes are known and small, and saying so is worth more than a hash: a
 * transcript's caches are one entry per DOCUMENT ENTRY (forty in the budget's worst case),
 * and `Windows` holds one widget per window (five). A linear scan over forty short keys is
 * faster than hashing them, and it is a decision with a reason rather than a container
 * picked because it was the one with `map` in the name. **The lookup is O(n) and the header
 * says so** — if a caller ever needs thousands of keys, that is the moment to add a hash,
 * with a measurement behind it.
 *
 * ---- OWNERSHIP -------------------------------------------------------------------------
 *
 * The table owns its KEYS and nothing else: a value is a `void*` the CALLER owns, because
 * every user of this holds a different type and the freeing of that type must stay visible
 * at the owner (the same reason `RolltuiPtrVec` takes no deleter). `rolltui_map_release`
 * frees the keys and the array; the caller walks the values first.
 *
 * ---- THE SWEEP, WHICH IS WHY THIS IS NOT JUST A LIST ------------------------------------
 *
 * Four of the seven maps are CACHES swept per frame: mark what this frame touched, then drop
 * what it did not. That is `rolltui_map_unmark_all` / `rolltui_map_mark` / a walk over the
 * unmarked, and it is here rather than in each caller because doing it by hand four times is
 * how the two implementations would come to disagree about when an entry dies.
 */
#include <stddef.h>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RolltuiMapEntry {
  RolltuiStr key;       /* OWNED by the table */
  void* value;          /* the CALLER's; the table never frees one */
  unsigned char marked; /* the per-frame sweep's flag */
} RolltuiMapEntry;

typedef struct RolltuiMap {
  RolltuiMapEntry* e;
  size_t n, cap;
} RolltuiMap;

/* The value for `key`, or NULL. O(n) — see the header note. */
void* rolltui_map_get(const RolltuiMap* m, const char* key, size_t len);
/* Sets `key`'s value, adding the entry when there is none. Returns the PREVIOUS value (NULL
 * when the entry is new), so a caller can free what it displaced without a second lookup. */
void* rolltui_map_put(RolltuiMap* m, const char* key, size_t len, void* value);
/* Removes `key` and returns its value; NULL when absent. */
void* rolltui_map_take(RolltuiMap* m, const char* key, size_t len);

size_t rolltui_map_count(const RolltuiMap* m);
void* rolltui_map_value_at(const RolltuiMap* m, size_t i);
const char* rolltui_map_key_at(const RolltuiMap* m, size_t i, size_t* len);

/* ---- the per-frame sweep ----------------------------------------------------------------- */
void rolltui_map_unmark_all(RolltuiMap* m);
void rolltui_map_mark(RolltuiMap* m, const char* key, size_t len);
int rolltui_map_marked_at(const RolltuiMap* m, size_t i);
/* Removes entry `i`, returning its value — for a caller walking the unmarked backwards. */
void* rolltui_map_remove_at(RolltuiMap* m, size_t i);

/* Empties, KEEPING the key buffers and the array (the reuse a `clear()` must not discard).
 * The caller has already dealt with the values. */
void rolltui_map_clear(RolltuiMap* m);
/* …and this hands the storage back. */
void rolltui_map_release(RolltuiMap* m);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_MAP_H */
