#include "rolltui/rolltui.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "rolltui/c/rolltui_alloc.h"

void rolltui_dir_list_release(RolltuiDirList* l) {
  size_t i;
  if (!l) return;
  for (i = 0; i < l->n; ++i) rolltui_str_free(&l->v[i].name);
  rolltui_mem_free(l->v);
  l->v = NULL;
  l->n = 0;
  l->cap = 0;
}

/* Directories first at every sort. A person walking a tree is looking for the next directory far
 * more often than for the largest file, so mixing them by size or date buries the way onward. */
static int cmp_name(const void* a, const void* b) {
  const RolltuiDirEntry* x = (const RolltuiDirEntry*)a;
  const RolltuiDirEntry* y = (const RolltuiDirEntry*)b;
  if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
  return strcmp(x->name.p ? x->name.p : "", y->name.p ? y->name.p : "");
}

static int cmp_size(const void* a, const void* b) {
  const RolltuiDirEntry* x = (const RolltuiDirEntry*)a;
  const RolltuiDirEntry* y = (const RolltuiDirEntry*)b;
  if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
  if (x->size != y->size) return x->size < y->size ? 1 : -1; /* largest first */
  return cmp_name(a, b);
}

static int cmp_modified(const void* a, const void* b) {
  const RolltuiDirEntry* x = (const RolltuiDirEntry*)a;
  const RolltuiDirEntry* y = (const RolltuiDirEntry*)b;
  if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
  if (x->modified != y->modified) return x->modified < y->modified ? 1 : -1; /* newest first */
  return cmp_name(a, b);
}

int rolltui_dir_read(const char* path, size_t len, int sort, int flags, RolltuiDirList* out,
                     RolltuiStr* err) {
  const int hidden = (flags & ROLLTUI_DIR_HIDDEN) != 0;
  const int links = (flags & ROLLTUI_DIR_LINKS) != 0;
  char* dir;
  DIR* d;
  struct dirent* e;
  if (!out) return 0;
  for (size_t i = 0; i < out->n; ++i) rolltui_str_free(&out->v[i].name);
  out->n = 0;
  out->hidden_n = 0;
  if (err) err->n = 0;
  /* OWNED, short-lived: `opendir` needs a NUL-terminated path and the caller's is a slice. */
  dir = (char*)rolltui_mem_alloc(len + 1);
  memcpy(dir, path ? path : "", len);
  dir[len] = 0;
  d = opendir(dir);
  if (!d) {
    if (err) {
      rolltui_str_set(err, "cannot open ", 12);
      rolltui_str_append(err, dir, len);
    }
    rolltui_mem_free(dir);
    return 0;
  }
  while ((e = readdir(d)) != NULL) {
    const size_t nlen = strlen(e->d_name);
    struct stat st;
    char* full;
    RolltuiDirEntry* slot;
    if (nlen == 0) continue;
    /* `.` and `..` are never entries: they are the same directory and its parent, and a browser
     * walks out by its own means. A dotfile is hidden unless asked for. */
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    if (!hidden && e->d_name[0] == '.') { ++out->hidden_n; continue; }
    out->v = (RolltuiDirEntry*)rolltui_grow_zeroed(out->v, &out->cap, out->n + 1, sizeof *out->v);
    slot = &out->v[out->n];
    rolltui_str_set(&slot->name, e->d_name, nlen);
    /* Never `d_type`: it says LNK for a symlink and answers neither question. `stat` follows the
     * link, `lstat` describes it, and which one a caller wants is the whole reason for the flag. */
    full = (char*)rolltui_mem_alloc(len + 1 + nlen + 1);
    memcpy(full, dir, len);
    full[len] = '/';
    memcpy(full + len + 1, e->d_name, nlen);
    full[len + 1 + nlen] = 0;
    if ((links ? lstat(full, &st) : stat(full, &st)) == 0) {
      slot->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
      slot->size = slot->is_dir ? 0 : (long long)st.st_size;
      slot->modified = (long long)st.st_mtime;
      slot->mode = (unsigned int)st.st_mode;
      slot->unreadable = 0;
    } else {
      /* THE ENTRY IS STILL THERE. A broken link is a thing on disk that cannot be described, and
       * dropping it would make a browser show fewer files than `ls` does. */
      slot->is_dir = 0;
      slot->size = 0;
      slot->modified = 0;
      slot->mode = 0;
      slot->unreadable = 1;
    }
    rolltui_mem_free(full);
    ++out->n;
  }
  closedir(d);
  rolltui_mem_free(dir);
  if (out->n > 1)
    qsort(out->v, out->n, sizeof *out->v,
          sort == ROLLTUI_SORT_SIZE ? cmp_size : sort == ROLLTUI_SORT_MODIFIED ? cmp_modified : cmp_name);
  return 1;
}
