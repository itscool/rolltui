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

/* GROWING HEAP, short-lived: one path and one file body per call, freed before return. */
static void path_join(char* out, size_t cap, const char* a, const char* b) {
  snprintf(out, cap, "%s%s%s", a, (a[0] && a[strlen(a) - 1] != '/') ? "/" : "", b);
}

static int read_whole(const char* path, RolltuiStr* out) {
  FILE* f = fopen(path, "rb");
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

static void config_root(char* out, size_t cap) {
  const char* d = getenv("ROLL_CONFIG_DIR");
  const char* x;
  const char* home;
  if (d && *d) { snprintf(out, cap, "%s", d); return; }
  x = getenv("XDG_CONFIG_HOME");
  if (x && *x) { snprintf(out, cap, "%s/roll", x); return; }
  home = getenv("HOME");
  snprintf(out, cap, "%s/.config/roll", (home && *home) ? home : ".");
}

int rolltui_app_file(const char* argv0, const char* app, const char* kind,
                     const RolltuiEmbeddedFile* embedded, size_t embedded_n,
                     RolltuiStr* out, RolltuiStr* tried) {
  char path[2048], dir[1024], leaf[512];
  int found = 0;
  size_t i;
  if (!app || !kind || !out) return 0;
  if (tried) tried->n = 0;

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

  snprintf(leaf, sizeof leaf, "%s.%s.json", app, kind);

  /* Rung 2: beside the binary. No directory convention to learn — the file is named after the
   * program and sits next to it. */
  if (argv0 && *argv0) {
    const char* slash = strrchr(argv0, '/');
    size_t len = slash ? (size_t)(slash - argv0) : 0;
    if (len >= sizeof dir) len = sizeof dir - 1;
    memcpy(dir, argv0, len);
    dir[len] = '\0';
    path_join(path, sizeof path, len ? dir : ".", leaf);
    if (read_whole(path, out)) { note_tried(tried, path, 1); found = 1; }
    else note_tried(tried, path, 0);
  }

  /* Rung 3: the known folder. Last, so it overrides both. */
  config_root(dir, sizeof dir);
  {
    char sub[1600];
    snprintf(sub, sizeof sub, "%s/rolltui/%s", dir, app);
    path_join(path, sizeof path, sub, kind);
    strncat(path, ".json", sizeof path - strlen(path) - 1);
    if (read_whole(path, out)) { note_tried(tried, path, 1); found = 1; }
    else note_tried(tried, path, 0);
  }
  return found;
}
