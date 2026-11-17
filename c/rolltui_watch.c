/* rolltui/c/rolltui_watch.c — FILES AND FOLDERS WATCHED: what changed, told once, with the loop woken to look.
 *
 * WHAT DECIDES A CHANGE IS A SIGNATURE: each path's device, inode, size and times (`rolltui_filesig_read`), compared with what it
 * was at the last look. The system's own notice (kqueue, inotify, a change notification) only says WHEN to look, so a save that
 * writes a new file and renames it over the old one, a path that does not exist yet, and a file on a share the system cannot watch
 * all come out the same way. Without a notice the thread looks every LOOK_MS; with no thread at all (a browser) a call to
 * `rolltui_watch_changed` looks.
 */
#include "rolltui/rolltui.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_preview.h"
#include "rolltui/c/rolltui_str.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define WATCH_WIN32 1
#elif defined(__wasi__)
#define WATCH_NONE 1
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <fcntl.h>
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>
#define WATCH_KQUEUE 1
#elif defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#define WATCH_INOTIFY 1
#else
#define WATCH_NONE 1
#endif

#define LOOK_MS 500

typedef struct WatchPath {
  unsigned int id;
  RolltuiStr path;
  RolltuiFileSig sig; /* at the last look */
  int changed;        /* since the last `rolltui_watch_changed` */
#if WATCH_KQUEUE
  int fd, dir_fd; /* the path and its folder, opened to be told of; -1 none */
#elif WATCH_WIN32
  HANDLE h; /* a change notification on its folder (the path itself, if a folder); NULL none */
#endif
} WatchPath;

struct RolltuiWatch {
  RolltuiMutex lock;
  WatchPath* v;
  size_t n, cap;
  unsigned int next_id;
  void (*wake)(void*);
  void* wake_ctx;
  RolltuiThread* thread;
  int stopping;
  int rearm; /* paths came or went, or one changed: the handles are made again */
  unsigned long long next_look_ms;
#if WATCH_KQUEUE
  int kq, ctl[2];
#elif WATCH_INOTIFY
  int in_fd, ctl[2];
#elif WATCH_WIN32
  HANDLE ctl; /* set when paths came or went, or the watch is stopping */
#endif
};

/* ---- looking -------------------------------------------------------------------------------- */

/* EVERY PATH LOOKED AT, under the lock: the ones whose signature moved marked. 1 when one did. */
static int look_locked(RolltuiWatch* w) {
  size_t i;
  int any = 0;
  for (i = 0; i < w->n; ++i) {
    RolltuiFileSig now;
    rolltui_filesig_read(w->v[i].path.p, &now);
    if (!rolltui_filesig_same(&w->v[i].sig, &now)) {
      w->v[i].sig = now;
      w->v[i].changed = 1;
      any = 1;
    }
  }
  if (any) w->rearm = 1; /* a file replaced is another file: its handles are made again */
  return any;
}

/* `path`'s folder, into `out`: what is before its last separator ("." when it has none). */
static void parent_of(const RolltuiStr* path, RolltuiStr* out) {
  size_t k = path->n;
  while (k > 0 && path->p[k - 1] != '/'
#if WATCH_WIN32
         && path->p[k - 1] != '\\'
#endif
  )
    --k;
  if (k <= 1) rolltui_str_set(out, k ? "/" : ".", 1);
  else rolltui_str_set(out, path->p, k - 1);
}

/* ---- the system's notices ------------------------------------------------------------------- */

#if WATCH_KQUEUE
#if defined(O_EVTONLY)
#define WATCH_OPEN_FLAGS (O_EVTONLY | O_CLOEXEC)
#else
#define WATCH_OPEN_FLAGS (O_RDONLY | O_CLOEXEC)
#endif
static void close_handles(RolltuiWatch* w) {
  size_t i;
  for (i = 0; i < w->n; ++i) {
    if (w->v[i].fd >= 0) close(w->v[i].fd);
    if (w->v[i].dir_fd >= 0) close(w->v[i].dir_fd);
    w->v[i].fd = w->v[i].dir_fd = -1;
  }
}
static void arm(RolltuiWatch* w, int fd) {
  struct kevent ev;
  EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME | NOTE_LINK, 0, NULL);
  kevent(w->kq, &ev, 1, NULL, 0, NULL);
}
static void rearm_locked(RolltuiWatch* w) {
  size_t i;
  RolltuiStr dir;
  memset(&dir, 0, sizeof dir);
  close_handles(w); /* closing a descriptor takes its events with it */
  for (i = 0; i < w->n; ++i) {
    WatchPath* p = &w->v[i];
    p->fd = open(p->path.p, WATCH_OPEN_FLAGS);
    parent_of(&p->path, &dir);
    p->dir_fd = open(dir.p, WATCH_OPEN_FLAGS); /* a rename over the file, or the file arriving, is the folder's */
    if (p->fd >= 0) arm(w, p->fd);
    if (p->dir_fd >= 0) arm(w, p->dir_fd);
  }
  rolltui_str_free(&dir);
  w->rearm = 0;
}
static int os_open(RolltuiWatch* w) {
  w->kq = kqueue();
  if (w->kq < 0 || pipe(w->ctl) != 0) return 0;
  fcntl(w->ctl[0], F_SETFL, O_NONBLOCK);
  fcntl(w->ctl[1], F_SETFL, O_NONBLOCK);
  {
    struct kevent ev;
    EV_SET(&ev, w->ctl[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
    kevent(w->kq, &ev, 1, NULL, 0, NULL);
  }
  return 1;
}
static void os_close(RolltuiWatch* w) {
  close_handles(w);
  if (w->kq >= 0) close(w->kq);
  if (w->ctl[0] >= 0) close(w->ctl[0]), close(w->ctl[1]);
}
static void os_poke(RolltuiWatch* w) {
  const char b = 1;
  if (w->ctl[1] >= 0) (void)!write(w->ctl[1], &b, 1);
}
static void os_wait(RolltuiWatch* w, int ms) { /* until told, poked, or `ms` */
  struct kevent got[16];
  struct timespec ts;
  char buf[64];
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  kevent(w->kq, NULL, 0, got, 16, &ts);
  while (read(w->ctl[0], buf, sizeof buf) > 0) {
  }
}
static const char* os_name(void) { return "kqueue"; }

#elif WATCH_INOTIFY
static void rearm_locked(RolltuiWatch* w) {
  size_t i;
  RolltuiStr dir;
  const unsigned int mask = IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_CLOSE_WRITE | IN_MODIFY | IN_ATTRIB | IN_DELETE_SELF |
                            IN_MOVE_SELF;
  memset(&dir, 0, sizeof dir);
  if (w->in_fd >= 0) close(w->in_fd); /* every watch goes with it */
  w->in_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  for (i = 0; i < w->n && w->in_fd >= 0; ++i) {
    parent_of(&w->v[i].path, &dir);
    inotify_add_watch(w->in_fd, dir.p, mask); /* a file's arrival, a rename over it, a write to it: the folder is told */
    inotify_add_watch(w->in_fd, w->v[i].path.p, mask); /* a folder's own entries (fails harmlessly for a missing path) */
  }
  rolltui_str_free(&dir);
  w->rearm = 0;
}
static int os_open(RolltuiWatch* w) {
  w->in_fd = -1;
  if (pipe(w->ctl) != 0) return 0;
  fcntl(w->ctl[0], F_SETFL, O_NONBLOCK);
  fcntl(w->ctl[1], F_SETFL, O_NONBLOCK);
  w->in_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  return w->in_fd >= 0;
}
static void os_close(RolltuiWatch* w) {
  if (w->in_fd >= 0) close(w->in_fd);
  if (w->ctl[0] >= 0) close(w->ctl[0]), close(w->ctl[1]);
}
static void os_poke(RolltuiWatch* w) {
  const char b = 1;
  if (w->ctl[1] >= 0) (void)!write(w->ctl[1], &b, 1);
}
static void os_wait(RolltuiWatch* w, int ms) {
  struct pollfd p[2];
  char buf[4096];
  p[0].fd = w->in_fd;
  p[0].events = POLLIN;
  p[1].fd = w->ctl[0];
  p[1].events = POLLIN;
  poll(p, 2, ms);
  while (w->in_fd >= 0 && read(w->in_fd, buf, sizeof buf) > 0) {
  }
  while (read(w->ctl[0], buf, sizeof buf) > 0) {
  }
}
static const char* os_name(void) { return "inotify"; }

#elif WATCH_WIN32
static wchar_t* watch_wide(const char* s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t* out = (wchar_t*)rolltui_mem_alloc((size_t)(n > 0 ? n : 1) * sizeof *out);
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n);
  else out[0] = 0;
  return out;
}
static void close_handles(RolltuiWatch* w) {
  size_t i;
  for (i = 0; i < w->n; ++i) {
    if (w->v[i].h && w->v[i].h != INVALID_HANDLE_VALUE) FindCloseChangeNotification(w->v[i].h);
    w->v[i].h = NULL;
  }
}
static void rearm_locked(RolltuiWatch* w) {
  size_t i;
  RolltuiStr dir;
  memset(&dir, 0, sizeof dir);
  close_handles(w);
  for (i = 0; i < w->n; ++i) {
    wchar_t* wp = watch_wide(w->v[i].path.p);
    const DWORD pa = GetFileAttributesW(wp);
    wchar_t* wd;
    if (pa != INVALID_FILE_ATTRIBUTES && (pa & FILE_ATTRIBUTE_DIRECTORY)) rolltui_str_set(&dir, w->v[i].path.p, w->v[i].path.n);
    else parent_of(&w->v[i].path, &dir);
    wd = watch_wide(dir.p);
    w->v[i].h = FindFirstChangeNotificationW(wd, FALSE,
                                             FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                                 FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_ATTRIBUTES);
    if (w->v[i].h == INVALID_HANDLE_VALUE) w->v[i].h = NULL;
    rolltui_mem_free(wp);
    rolltui_mem_free(wd);
  }
  rolltui_str_free(&dir);
  w->rearm = 0;
}
static int os_open(RolltuiWatch* w) {
  w->ctl = CreateEventW(NULL, FALSE, FALSE, NULL);
  return w->ctl != NULL;
}
static void os_close(RolltuiWatch* w) {
  close_handles(w);
  if (w->ctl) CloseHandle(w->ctl);
}
static void os_poke(RolltuiWatch* w) {
  if (w->ctl) SetEvent(w->ctl);
}
static void os_wait(RolltuiWatch* w, int ms) {
  HANDLE hs[MAXIMUM_WAIT_OBJECTS];
  size_t idx[MAXIMUM_WAIT_OBJECTS];
  DWORD n = 0, r;
  size_t i;
  hs[n++] = w->ctl;
  rolltui_mutex_lock(&w->lock);
  for (i = 0; i < w->n && n < MAXIMUM_WAIT_OBJECTS; ++i) /* past the system's limit, a path is looked at on the clock */
    if (w->v[i].h) idx[n] = i, hs[n++] = w->v[i].h;
  rolltui_mutex_unlock(&w->lock);
  r = WaitForMultipleObjects(n, hs, FALSE, (DWORD)ms);
  if (r > WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + n) {
    rolltui_mutex_lock(&w->lock);
    if (idx[r - WAIT_OBJECT_0] < w->n && w->v[idx[r - WAIT_OBJECT_0]].h == hs[r - WAIT_OBJECT_0]) FindNextChangeNotification(hs[r - WAIT_OBJECT_0]);
    rolltui_mutex_unlock(&w->lock);
  }
}
static const char* os_name(void) { return "change notifications"; }

#else /* no notices: the clock */
static void rearm_locked(RolltuiWatch* w) { w->rearm = 0; }
static int os_open(RolltuiWatch* w) {
  (void)w;
  return 0;
}
static void os_close(RolltuiWatch* w) { (void)w; }
static void os_poke(RolltuiWatch* w) { (void)w; }
static void os_wait(RolltuiWatch* w, int ms) {
  (void)w;
  rolltui_os_sleep_ms(ms);
}
static const char* os_name(void) { return "a look on the clock"; }
#endif

/* ---- the watcher's thread ------------------------------------------------------------------- */

static void watch_main(void* arg) {
  RolltuiWatch* w = (RolltuiWatch*)arg;
  for (;;) {
    int any;
    rolltui_mutex_lock(&w->lock);
    if (w->stopping) {
      rolltui_mutex_unlock(&w->lock);
      return;
    }
    if (w->rearm) rearm_locked(w);
    rolltui_mutex_unlock(&w->lock);
    os_wait(w, LOOK_MS);
    rolltui_mutex_lock(&w->lock);
    any = !w->stopping && look_locked(w);
    rolltui_mutex_unlock(&w->lock);
    if (any && w->wake) w->wake(w->wake_ctx);
  }
}

/* ---- the calls ------------------------------------------------------------------------------ */

static int os_ok_of(const RolltuiWatch* w) {
#if WATCH_KQUEUE
  return w->kq >= 0;
#elif WATCH_INOTIFY
  return w->in_fd >= 0;
#elif WATCH_WIN32
  return w->ctl != NULL;
#else
  (void)w;
  return 0;
#endif
}

RolltuiWatch* rolltui_watch_new(void (*wake)(void* ctx), void* wake_ctx) {
  RolltuiWatch* w = (RolltuiWatch*)rolltui_mem_alloc(sizeof *w);
  memset(w, 0, sizeof *w);
  rolltui_mutex_init(&w->lock);
  w->wake = wake;
  w->wake_ctx = wake_ctx;
#if WATCH_KQUEUE
  w->kq = -1;
  w->ctl[0] = w->ctl[1] = -1;
#elif WATCH_INOTIFY
  w->in_fd = -1;
  w->ctl[0] = w->ctl[1] = -1;
#endif
  os_open(w);
  w->thread = rolltui_thread_start(watch_main, w); /* none in a browser: `rolltui_watch_changed` looks */
  return w;
}

void rolltui_watch_free(RolltuiWatch* w) {
  size_t i;
  if (!w) return;
  rolltui_mutex_lock(&w->lock);
  w->stopping = 1;
  rolltui_mutex_unlock(&w->lock);
  os_poke(w);
  if (w->thread) rolltui_thread_join(w->thread);
  os_close(w);
  for (i = 0; i < w->n; ++i) rolltui_str_free(&w->v[i].path);
  rolltui_mem_free(w->v);
  rolltui_mutex_destroy(&w->lock);
  rolltui_mem_free(w);
}

unsigned int rolltui_watch_add(RolltuiWatch* w, const char* path, size_t len) {
  WatchPath* p;
  unsigned int id;
  if (!w || !path || len == 0 || memchr(path, 0, len)) return 0;
  rolltui_mutex_lock(&w->lock);
  w->v = (WatchPath*)rolltui_grow_zeroed(w->v, &w->cap, w->n + 1, sizeof *w->v);
  p = &w->v[w->n++];
  memset(p, 0, sizeof *p);
  w->next_id = w->next_id == 0xFFFFFFFFu ? 1u : w->next_id + 1u;
  id = p->id = w->next_id;
  rolltui_str_set(&p->path, path, len);
  rolltui_filesig_read(p->path.p, &p->sig); /* as it is now: what changes from here is told */
#if WATCH_KQUEUE
  p->fd = p->dir_fd = -1;
#endif
  w->rearm = 1;
  rolltui_mutex_unlock(&w->lock);
  os_poke(w);
  return id;
}

void rolltui_watch_remove(RolltuiWatch* w, unsigned int id) {
  size_t i;
  if (!w || id == 0) return;
  rolltui_mutex_lock(&w->lock);
  for (i = 0; i < w->n; ++i)
    if (w->v[i].id == id) {
#if WATCH_KQUEUE
      if (w->v[i].fd >= 0) close(w->v[i].fd);
      if (w->v[i].dir_fd >= 0) close(w->v[i].dir_fd);
#elif WATCH_WIN32
      if (w->v[i].h) FindCloseChangeNotification(w->v[i].h);
#endif
      rolltui_str_free(&w->v[i].path);
      w->v[i] = w->v[--w->n];
      w->rearm = 1;
      break;
    }
  rolltui_mutex_unlock(&w->lock);
  os_poke(w);
}

size_t rolltui_watch_changed(RolltuiWatch* w, unsigned int* out, size_t cap) {
  size_t i, got = 0;
  if (!w) return 0;
  rolltui_mutex_lock(&w->lock);
  if (!w->thread) { /* no thread to look: this call does, on the clock */
    const unsigned long long now = rolltui_os_monotonic_ms();
    if (now >= w->next_look_ms) {
      look_locked(w);
      w->next_look_ms = now + LOOK_MS;
    }
  }
  for (i = 0; i < w->n; ++i)
    if (w->v[i].changed) {
      if (out && got >= cap) break; /* the rest are told at the next call */
      w->v[i].changed = 0;
      if (out) out[got] = w->v[i].id;
      ++got;
    }
  rolltui_mutex_unlock(&w->lock);
  return got;
}

const char* rolltui_watch_how(const RolltuiWatch* w) { return w && w->thread && os_ok_of(w) ? os_name() : "a look on the clock"; }
