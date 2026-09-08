#ifndef ROLLTUI_C_FILES_H
#define ROLLTUI_C_FILES_H

/* rolltui/c/rolltui_files.h — READING A DIRECTORY, which is a mechanism rather than a look.
 *
 * A file browser and a file picker want the same four things: the entries, sorted, with dotfiles
 * shown or hidden, and a NAMED reason when the directory cannot be opened. What differs between
 * them is presentation and outcome — Miller columns against a single list, browsing against
 * choosing — and neither of those is here.
 *
 * This is the library's because every app that opens anything needs it, not because a browser is
 * a nice example. It is the same test the theme and keys editors passed.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROLLTUI_SORT_NAME 0
#define ROLLTUI_SORT_SIZE 1
#define ROLLTUI_SORT_MODIFIED 2

typedef struct RolltuiDirEntry {
  RolltuiStr name;      /* OWNED by the list */
  int is_dir;
  long long size;       /* bytes; 0 for a directory */
  long long modified;   /* seconds, for sorting and for a host to format */
} RolltuiDirEntry;

/* GROWING AMORTISED. Zero-initialise before first use; `_release` frees every name and the array
 * and zeroes it, and is a no-op on a zeroed list and on NULL. REUSED across reads rather than
 * rebuilt, so walking a tree does not allocate per directory. */
typedef struct RolltuiDirList {
  RolltuiDirEntry* v;
  size_t n, cap;
} RolltuiDirList;

void rolltui_dir_list_release(RolltuiDirList* l);

/* Fills `out` with `path`'s entries, sorted by `sort`, `.`-prefixed names included only when
 * `hidden` is non-zero. Directories sort before files at every sort, because a person walking a
 * tree is looking for the next directory far more often than for the largest file.
 *
 * Returns 1 on success. On failure `out` is left EMPTY and `err` (may be NULL) is given the
 * reason — a directory that cannot be read and one that is empty are different answers, and a
 * caller that cannot tell them apart draws "(empty)" over a permission error. */
int rolltui_dir_read(const char* path, size_t len, int sort, int hidden, RolltuiDirList* out,
                     RolltuiStr* err);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_FILES_H */
