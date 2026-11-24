/* rolltui_log.c — THE LOG: what the library and a host say for whoever is looking, in a file, because the terminal is the
 * screen's while a program runs. One per process, as stderr is. */
#include "rolltui/rolltui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rolltui/c/rolltui_os.h"

/* The file a host named or ROLLTUI_LOG names; what was asked for (0 not yet looked at, 1 a file, 2 none); the level let through;
 * when the first line was written, on the steady clock; and the lock that keeps a worker's line out of the middle of another's. */
static RolltuiMutex g_log_lock = ROLLTUI_MUTEX_STATIC_INIT;
static FILE* g_log_file;
static int g_log_state;
static int g_log_level = -1;
static unsigned long long g_log_t0;

static int level_named(const char* s) {
  if (!s || !*s) return ROLLTUI_LOG_INFO;
  if (!strcmp(s, "error")) return ROLLTUI_LOG_ERROR;
  if (!strcmp(s, "warn") || !strcmp(s, "warning")) return ROLLTUI_LOG_WARN;
  if (!strcmp(s, "debug")) return ROLLTUI_LOG_DEBUG;
  return ROLLTUI_LOG_INFO;
}
/* Under the lock: the file from the environment, the first time anything is said. */
static void look(void) {
  if (g_log_level < 0) g_log_level = level_named(getenv("ROLLTUI_LOG_LEVEL"));
  if (g_log_state) return;
  {
    const char* path = getenv("ROLLTUI_LOG");
    g_log_file = path && *path ? fopen(path, "a") : NULL;
    g_log_state = g_log_file ? 1 : 2;
  }
}

int rolltui_log_to(const char* path, size_t len) {
  int ok = 0;
  rolltui_mutex_lock(&g_log_lock);
  if (g_log_file) fclose(g_log_file);
  g_log_file = NULL;
  g_log_state = 0;
  if (path && len) {
    char* p = (char*)rolltui_mem_alloc(len + 1);
    if (p) {
      memcpy(p, path, len);
      p[len] = 0;
      g_log_file = fopen(p, "a");
      rolltui_mem_free(p);
    }
    g_log_state = g_log_file ? 1 : 2;
    ok = g_log_file != NULL;
  }
  rolltui_mutex_unlock(&g_log_lock);
  return ok;
}

void rolltui_log_set_level(int level) {
  rolltui_mutex_lock(&g_log_lock);
  g_log_level = level < ROLLTUI_LOG_ERROR ? ROLLTUI_LOG_ERROR : level > ROLLTUI_LOG_DEBUG ? ROLLTUI_LOG_DEBUG : level;
  rolltui_mutex_unlock(&g_log_lock);
}

int rolltui_log_wants(int level) {
  int r;
  rolltui_mutex_lock(&g_log_lock);
  look();
  r = g_log_file && level <= g_log_level;
  rolltui_mutex_unlock(&g_log_lock);
  return r;
}

void rolltui_log(int level, const char* text, size_t len) {
  static const char* const kNames[] = {"ERROR", "WARN ", "INFO ", "DEBUG"};
  rolltui_mutex_lock(&g_log_lock);
  look();
  if (g_log_file && level <= g_log_level && level >= ROLLTUI_LOG_ERROR) {
    const unsigned long long now = rolltui_os_monotonic_ms();
    struct tm tm;
    char when[32] = "";
    size_t i;
    if (!g_log_t0) g_log_t0 = now;
    if (rolltui_os_localtime((long long)time(NULL), &tm)) strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(g_log_file, "%s %8.3fs %s ", when, (double)(now - g_log_t0) / 1000.0, kNames[level > 3 ? 3 : level]);
    for (i = 0; i < len; ++i) fputc(text[i] == '\n' || text[i] == '\r' ? ' ' : text[i], g_log_file); /* one line a call */
    fputc('\n', g_log_file);
    fflush(g_log_file);
  }
  rolltui_mutex_unlock(&g_log_lock);
}

void rolltui_logf(int level, const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  int n;
  if (!fmt || !rolltui_log_wants(level)) return;
  va_start(ap, fmt);
  n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  rolltui_log(level, buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
}
