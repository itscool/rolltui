/* rolltui_app_files.c — where an app's OWN default files live.
 *
 * This answers "where is this app's default screen", which is a different question from "may a
 * user change it". The preset store answers the second through its own rungs; this one is asked
 * first, and what it returns is what a user's preset directory then shadows.
 *
 * An app built the strict way keeps its screen in files rather than in its source, so it cannot
 * start until it knows where those files are. Building the path in each host is what produced
 * `rolltui-explorer: no layout ()` — an empty directory string, a path of `/layouts/x.json`, and
 * a message naming neither what was wanted nor where it was sought.
 *
 * THREE RUNGS, LATER OVERRIDING EARLIER. Not first-found-wins: under that reading an app with an
 * embedded default would never look at a file beside it, which makes the file useless for exactly
 * the apps that ship a default. Overriding makes the embedded copy a FLOOR that guarantees the app
 * runs, and a file on disk a customisation.
 *   1. embedded  — compiled in from the app's own files by cmake/embed_presets.cmake
 *   2. beside the binary — <dir of argv0>/<app>.<kind>.json
 *   3. a known folder    — <config>/rolltui/<app>/<kind>.json
 *
 * `tried` collects every path examined whether it hit or missed, so a host that finds nothing can
 * say what it looked for. A miss that cannot name its candidates is the failure this file exists
 * to remove.
 */
#include "rolltui/rolltui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_os.h"

/* PATHS ARE RolltuiStr: no length cuts one short, because a cut path names another file. */
static const char* str_c(const RolltuiStr* s) { return s->p ? s->p : ""; }

static int is_sep(char c) {
#if defined(_WIN32)
  if (c == '\\') return 1;
#endif
  return c == '/';
}

/* Appends `part` as the next component of `p`. */
static void path_add(RolltuiStr* p, const char* part) {
  if (p->n && !is_sep(p->p[p->n - 1])) rolltui_str_append(p, "/", 1);
  rolltui_str_append(p, part, strlen(part));
}

static int read_whole(const char* path, RolltuiStr* out) {
  FILE* f = rolltui_os_fopen(path, "rb");
  long n;
  char* buf;
  if (!f) return 0;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
  n = ftell(f);
  if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
  buf = (char*)rolltui_mem_alloc((size_t)n + 1);
  if (!buf) { fclose(f); return 0; }
  if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) { rolltui_mem_free(buf); fclose(f); return 0; }
  fclose(f);
  buf[n] = '\0';
  rolltui_str_set(out, buf, (size_t)n);
  rolltui_mem_free(buf);
  return 1;
}

static void note_tried(RolltuiStr* tried, const char* path, int hit) {
  if (!tried) return;
  if (tried->n) rolltui_str_append(tried, "\n", 1);
  rolltui_str_append(tried, hit ? "  found   " : "  missing ", 10);
  rolltui_str_append(tried, path, strlen(path));
}

static void config_root(RolltuiStr* out) {
  const char* d = getenv("ROLL_CONFIG_DIR");
  const char* x = getenv("XDG_CONFIG_HOME");
  char home[4096];
  if (d && *d) {
    rolltui_str_set(out, d, strlen(d));
    return;
  }
  if (x && *x) rolltui_str_set(out, x, strlen(x));
  else if (rolltui_os_config_home(home, sizeof home) == 0) rolltui_str_set(out, home, strlen(home));
  else rolltui_str_set(out, ".", 1);
  path_add(out, "roll");
}

static int try_path(const RolltuiStr* path, RolltuiStr* out, RolltuiStr* tried) {
  const int hit = read_whole(str_c(path), out);
  note_tried(tried, str_c(path), hit);
  return hit;
}

int rolltui_app_file(const char* argv0, const char* app, const char* kind,
                     const RolltuiEmbeddedFile* embedded, size_t embedded_n,
                     RolltuiStr* out, RolltuiStr* tried) {
  RolltuiStr path, leaf;
  int found = 0;
  size_t i;
  if (!app || !kind || !out) return 0;
  if (tried) tried->n = 0;
  memset(&path, 0, sizeof path);
  memset(&leaf, 0, sizeof leaf);

  /* Rung 1: the app's own compiled-in copy. Named by KIND, because that is the stem the
   * generator writes and the name a host thinks in ("layout", "bindings"). */
  for (i = 0; i < embedded_n; ++i) {
    if (embedded[i].name && strcmp(embedded[i].name, kind) == 0) {
      rolltui_str_set(out, embedded[i].text, strlen(embedded[i].text));
      note_tried(tried, "(embedded)", 1);
      found = 1;
      break;
    }
  }
  if (!found) note_tried(tried, "(embedded)", 0);

  /* Rung 2: beside the binary. No directory convention to learn — the file is named after the
   * program and sits next to it. argv0 with no directory in it is looked for in the working one. */
  if (argv0 && *argv0) {
    size_t at = strlen(argv0);
    while (at > 0 && !is_sep(argv0[at - 1])) --at;
    if (at == 0) rolltui_str_set(&path, ".", 1);
    else rolltui_str_set(&path, argv0, at > 1 ? at - 1 : 1); /* "/x" is in "/", not "" */
    rolltui_str_set(&leaf, app, strlen(app));
    rolltui_str_append(&leaf, ".", 1);
    rolltui_str_append(&leaf, kind, strlen(kind));
    rolltui_str_append(&leaf, ".json", 5);
    path_add(&path, str_c(&leaf));
    found |= try_path(&path, out, tried);
  }

  /* Rung 3: the known folder. Last, so it overrides both. */
  config_root(&path);
  path_add(&path, "rolltui");
  path_add(&path, app);
  path_add(&path, kind);
  rolltui_str_append(&path, ".json", 5);
  found |= try_path(&path, out, tried);

  rolltui_str_free(&path);
  rolltui_str_free(&leaf);
  return found;
}
