/*
 * testctl.c — the one table. Compiled ONLY into the control-enabled build of testkit; the
 * shipped build has no translation unit for it at all, which is what makes "no control name
 * survives" a link-time fact rather than an optimisation.
 */
#include "testkit/testctl.h"

#ifdef TESTKIT_CONTROL

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* A control name is a short literal and there are tens of them, so an interned array with a
 * linear scan is the whole implementation. A hash table here would be a data structure nobody
 * can check by reading. */
enum { kMax = 256 };

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static char* g_name[kMax];
static unsigned char g_on[kMax];       /* currently flipped on */
static unsigned char g_was_on[kMax];   /* a test has turned it on at some point */
static unsigned char g_asked[kMax];    /* some code path has asked about it */
static size_t g_count = 0;

/* Interns `name` and returns its slot, or kMax if the table is full — in which case every
 * operation degrades to "off", which is the safe direction: a control that cannot be turned on
 * makes its check FAIL loudly at the middle assertion rather than pass quietly. */
static size_t slot(const char* name) {
  size_t i;
  if (!name) return kMax;
  for (i = 0; i < g_count; ++i)
    if (strcmp(g_name[i], name) == 0) return i;
  if (g_count == kMax) return kMax;
  g_name[g_count] = strdup(name);
  if (!g_name[g_count]) return kMax;
  g_on[g_count] = 0;
  g_was_on[g_count] = 0;
  g_asked[g_count] = 0;
  return g_count++;
}

int testkit_ctl_on(const char* name) {
  size_t i;
  int v = 0;
  pthread_mutex_lock(&g_mu);
  i = slot(name);
  if (i < kMax) {
    g_asked[i] = 1;
    v = g_on[i] ? 1 : 0;
  }
  pthread_mutex_unlock(&g_mu);
  return v;
}

void testkit_ctl_set(const char* name, int value) {
  size_t i;
  pthread_mutex_lock(&g_mu);
  i = slot(name);
  if (i < kMax) {
    g_on[i] = value ? 1 : 0;
    if (value) g_was_on[i] = 1;
  }
  pthread_mutex_unlock(&g_mu);
}

void testkit_ctl_reset(void) {
  size_t i;
  pthread_mutex_lock(&g_mu);
  for (i = 0; i < g_count; ++i) g_on[i] = 0;
  pthread_mutex_unlock(&g_mu);
}

static size_t collect(const unsigned char* flag, const char** out, size_t cap) {
  size_t i, n = 0;
  pthread_mutex_lock(&g_mu);
  for (i = 0; i < g_count; ++i) {
    if (!flag[i]) continue;
    if (n < cap && out) out[n] = g_name[i];
    ++n;
  }
  pthread_mutex_unlock(&g_mu);
  return n;
}

size_t testkit_ctl_known(const char** out, size_t cap) { return collect(g_asked, out, cap); }
size_t testkit_ctl_flipped(const char** out, size_t cap) { return collect(g_was_on, out, cap); }

int testkit_ctl_set_from_env(const char* var) {
  const char* v = getenv(var ? var : "ROLL_TEST_CONTROLS");
  const char* p;
  int n = 0;
  if (!v || !*v) return 0;
  p = v;
  while (*p) {
    const char* comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    char buf[256];
    while (len > 0 && *p == ' ') { ++p; --len; }
    while (len > 0 && p[len - 1] == ' ') --len;
    if (len > 0 && len < sizeof buf) {
      memcpy(buf, p, len);
      buf[len] = '\0';
      testkit_ctl_set(buf, 1);
      ++n;
    }
    if (!comma) break;
    p = comma + 1;
  }
  return n;
}

#endif /* TESTKIT_CONTROL */
