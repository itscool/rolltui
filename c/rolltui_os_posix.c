/* rolltui/c/rolltui_os_posix.c — rolltui_os.h on macOS, Linux and the BSDs. */
#if !defined(_WIN32)

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "rolltui/c/rolltui_os.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "rolltui/c/rolltui_alloc.h"

/* ---- time ---------------------------------------------------------------------------------- */

unsigned long long rolltui_os_monotonic_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (unsigned long long)now.tv_sec * 1000ULL + (unsigned long long)now.tv_nsec / 1000000ULL;
}

unsigned long long rolltui_os_monotonic_us(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (unsigned long long)now.tv_sec * 1000000ULL + (unsigned long long)now.tv_nsec / 1000ULL;
}

int rolltui_os_localtime(long long secs, struct tm* out) {
  const time_t t = (time_t)secs;
  return localtime_r(&t, out) != NULL;
}

/* ---- locks --------------------------------------------------------------------------------- */

void rolltui_mutex_init(RolltuiMutex* m) { pthread_mutex_init(&m->lock, NULL); }
void rolltui_mutex_destroy(RolltuiMutex* m) { pthread_mutex_destroy(&m->lock); }
void rolltui_mutex_lock(RolltuiMutex* m) { pthread_mutex_lock(&m->lock); }
void rolltui_mutex_unlock(RolltuiMutex* m) { pthread_mutex_unlock(&m->lock); }

/* ---- files --------------------------------------------------------------------------------- */

#if defined(__APPLE__)
#define MTIME_NS(st) ((long long)(st).st_mtimespec.tv_sec * 1000000000LL + (long long)(st).st_mtimespec.tv_nsec)
#define CTIME_NS(st) ((long long)(st).st_ctimespec.tv_sec * 1000000000LL + (long long)(st).st_ctimespec.tv_nsec)
#else
#define MTIME_NS(st) ((long long)(st).st_mtim.tv_sec * 1000000000LL + (long long)(st).st_mtim.tv_nsec)
#define CTIME_NS(st) ((long long)(st).st_ctim.tv_sec * 1000000000LL + (long long)(st).st_ctim.tv_nsec)
#endif

static void describe(const struct stat* st, RolltuiOsStat* out) {
  out->mode = (unsigned int)st->st_mode;
  out->size = (long long)st->st_size;
  out->mtime = (long long)st->st_mtime;
  out->mtime_ns = MTIME_NS(*st);
  out->ctime_ns = CTIME_NS(*st);
  out->dev = (unsigned long long)st->st_dev;
  out->ino = (unsigned long long)st->st_ino;
}

int rolltui_os_stat(const char* path, int follow_links, RolltuiOsStat* out) {
  struct stat st;
  if ((follow_links ? stat(path, &st) : lstat(path, &st)) != 0) return -1;
  describe(&st, out);
  return 0;
}

int rolltui_os_mkdir(const char* path) {
  struct stat st;
  if (mkdir(path, 0777) == 0) return 0;
  return errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

int rolltui_os_rename_replace(const char* from, const char* to) { return rename(from, to) == 0 ? 0 : -1; }

FILE* rolltui_os_fopen(const char* path, const char* mode) { return fopen(path, mode); }

int rolltui_os_getcwd(char* buf, size_t cap) { return getcwd(buf, cap) ? 0 : -1; }

int rolltui_os_exe_path(char* buf, size_t cap) {
  if (!cap) return -1;
#if defined(__APPLE__)
  {
    uint32_t size = (uint32_t)cap;
    return _NSGetExecutablePath(buf, &size) == 0 ? 0 : -1;
  }
#else
  {
    const ssize_t k = readlink("/proc/self/exe", buf, cap - 1);
    if (k <= 0) return -1;
    buf[k] = '\0';
    return 0;
  }
#endif
}

int rolltui_os_pid(void) { return (int)getpid(); }

int rolltui_os_config_home(char* buf, size_t cap) {
  const char* home = getenv("HOME");
  const int n = snprintf(buf, cap, "%s/.config", home && *home ? home : ".");
  return n > 0 && (size_t)n < cap ? 0 : -1;
}

int rolltui_os_release(char* buf, size_t cap) {
  struct utsname u;
  if (!cap || uname(&u) != 0) return -1;
  snprintf(buf, cap, "%s", u.release);
  return 0;
}

int rolltui_os_host_name(char* buf, size_t cap) {
  if (!cap || gethostname(buf, cap - 1) != 0) return -1;
  buf[cap - 1] = '\0';
  return 0;
}

int rolltui_os_open_read(const char* path) {
  return open(path, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
}

int rolltui_os_fstat(int fd, RolltuiOsStat* out) {
  struct stat st;
  if (fstat(fd, &st) != 0) return -1;
  describe(&st, out);
  return 0;
}

long rolltui_os_read(int fd, char* buf, size_t cap) {
  for (;;) {
    const ssize_t k = read(fd, buf, cap);
    if (k < 0 && errno == EINTR) continue;
    return (long)k;
  }
}

long rolltui_os_pread(int fd, char* buf, size_t cap, long long offset) {
  for (;;) {
    const ssize_t k = pread(fd, buf, cap, (off_t)offset);
    if (k < 0 && errno == EINTR) continue;
    return (long)k;
  }
}

void rolltui_os_close(int fd) {
  if (fd >= 0) close(fd);
}

struct RolltuiOsDir {
  DIR* d;
  const char* name; /* the entry `next` last returned */
};

RolltuiOsDir* rolltui_os_dir_open(const char* path) {
  DIR* d = opendir(path);
  RolltuiOsDir* out;
  if (!d) return NULL;
  out = (RolltuiOsDir*)rolltui_mem_alloc(sizeof *out);
  out->d = d;
  out->name = NULL;
  return out;
}

const char* rolltui_os_dir_next(RolltuiOsDir* d) {
  struct dirent* e = readdir(d->d);
  d->name = e ? e->d_name : NULL;
  return d->name;
}

int rolltui_os_dir_stat(RolltuiOsDir* d, int follow_links, RolltuiOsStat* out) {
  struct stat st;
  if (!d->name || fstatat(dirfd(d->d), d->name, &st, follow_links ? 0 : AT_SYMLINK_NOFOLLOW) != 0) return -1;
  describe(&st, out);
  return 0;
}

int rolltui_os_dir_hidden(const RolltuiOsDir* d) {
  (void)d;
  return 0;
}

void rolltui_os_dir_close(RolltuiOsDir* d) {
  if (!d) return;
  closedir(d->d);
  rolltui_mem_free(d);
}

/* ---- the terminal -------------------------------------------------------------------------- */

int rolltui_os_isatty(int fd) { return isatty(fd) != 0; }

struct RolltuiOsTty {
  int in_fd, out_fd;
  int owned_fd; /* a reopened terminal (see reopen_if_unpollable), closed at close; else -1 */
  int tty;
  int wake_r, wake_w; /* a self-pipe: the resize handler and `wake` write, `wait` reads */
  int have_tio;
  struct termios saved_tio;
};

/* THE PROCESS-WIDE RESTORE STATE, read from a signal handler. The scalars are lock-free atomics,
 * which C11 allows a signal handler to touch, and `g_have_tio`'s store orders the plain write to
 * `g_saved_tio` before any reader that sees it true. `g_leave` is overwritten whole before
 * `g_leave_len` publishes its length, so a handler never reads a torn sequence. */
static _Atomic int g_out_fd = -1;
static _Atomic int g_wake_fd = -1;
static char g_leave[256];
static _Atomic size_t g_leave_len = 0;
static struct termios g_saved_tio;
static _Atomic int g_have_tio = 0;
static _Atomic int g_handlers_installed = 0;

static void write_all_fd(int fd, const char* p, size_t n) {
  while (n > 0) {
    const ssize_t k = write(fd, p, n);
    if (k < 0) {
      if (errno == EINTR) continue;
      return;
    }
    p += k;
    n -= (size_t)k;
  }
}

/* A descriptor from the `/dev/tty` alias cannot be polled on Darwin: poll(2) answers POLLNVAL
 * while read(2) works. The same device opened by its own name (what ttyname(3) gives for a
 * standard descriptor) polls normally, so such a descriptor is reopened by that name. */
static int reopen_if_unpollable(int fd) {
  struct pollfd p;
  int std_fd;
  p.fd = fd;
  p.events = POLLIN;
  p.revents = 0;
  if (poll(&p, 1, 0) < 0 || !(p.revents & POLLNVAL)) return -1;
  if (!isatty(fd)) return -1;
  for (std_fd = 0; std_fd < 3; ++std_fd) {
    const char* name = isatty(std_fd) ? ttyname(std_fd) : NULL;
    if (name && strcmp(name, "/dev/tty") != 0) return open(name, O_RDWR | O_CLOEXEC);
  }
  return -1;
}

RolltuiOsTty* rolltui_os_tty_open(int in_fd, int out_fd) {
  RolltuiOsTty* t = (RolltuiOsTty*)rolltui_mem_alloc(sizeof *t);
  int wake[2];
  memset(t, 0, sizeof *t);
  t->in_fd = in_fd;
  t->out_fd = out_fd;
  t->owned_fd = reopen_if_unpollable(in_fd);
  if (t->owned_fd >= 0) {
    t->in_fd = t->owned_fd;
    if (out_fd == in_fd) t->out_fd = t->owned_fd;
  }
  t->tty = isatty(out_fd) != 0;
  t->wake_r = t->wake_w = -1;
  if (pipe(wake) == 0) {
    fcntl(wake[0], F_SETFL, O_NONBLOCK);
    fcntl(wake[1], F_SETFL, O_NONBLOCK);
    t->wake_r = wake[0];
    t->wake_w = wake[1];
  }
  return t;
}

void rolltui_os_tty_close(RolltuiOsTty* t) {
  if (!t) return;
  if (t->owned_fd >= 0) close(t->owned_fd);
  if (t->wake_r >= 0) close(t->wake_r);
  if (t->wake_w >= 0) close(t->wake_w);
  rolltui_mem_free(t);
}

int rolltui_os_tty_is_tty(const RolltuiOsTty* t) { return t->tty; }

int rolltui_os_tty_raw(RolltuiOsTty* t) {
  struct termios raw;
  if (!t->tty || tcgetattr(t->out_fd, &t->saved_tio) != 0) return 0;
  t->have_tio = 1;
  raw = t->saved_tio;
  cfmakeraw(&raw);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  tcsetattr(t->out_fd, TCSANOW, &raw);
  g_saved_tio = t->saved_tio; /* a plain copy, ordered before the flag below */
  g_have_tio = 1;
  return 1;
}

void rolltui_os_tty_restore(RolltuiOsTty* t) {
  if (t->have_tio) tcsetattr(t->out_fd, TCSANOW, &t->saved_tio);
}

int rolltui_os_tty_size(const RolltuiOsTty* t, int* w, int* h) {
  struct winsize ws;
  memset(&ws, 0, sizeof ws);
  if (ioctl(t->out_fd, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0 || ws.ws_row == 0) return 0;
  *w = ws.ws_col;
  *h = ws.ws_row;
  return 1;
}

void rolltui_os_tty_write(RolltuiOsTty* t, const char* bytes, size_t len) { write_all_fd(t->out_fd, bytes, len); }

int rolltui_os_tty_wait(RolltuiOsTty* t, int timeout_ms, int with_wake) {
  struct pollfd fds[2];
  const int watch_wake = with_wake && t->wake_r >= 0;
  int n, out = 0;
  fds[0].fd = t->in_fd;
  fds[0].events = POLLIN;
  fds[0].revents = 0;
  fds[1].fd = t->wake_r;
  fds[1].events = POLLIN;
  fds[1].revents = 0;
  n = poll(fds, watch_wake ? 2 : 1, timeout_ms);
  if (n < 0) return errno == EINTR ? ROLLTUI_OS_TTY_AGAIN : ROLLTUI_OS_TTY_ERROR;
  if (n == 0) return 0;
  if (watch_wake && (fds[1].revents & POLLIN)) {
    char buf[64];
    ssize_t k;
    while ((k = read(t->wake_r, buf, sizeof buf)) > 0) {
      ssize_t i;
      for (i = 0; i < k; ++i) out |= buf[i] == 'w' ? ROLLTUI_OS_TTY_RESIZED : ROLLTUI_OS_TTY_WOKEN;
    }
  }
  if (fds[0].revents & (POLLIN | POLLHUP)) out |= ROLLTUI_OS_TTY_INPUT;
  return out;
}

long rolltui_os_tty_read(RolltuiOsTty* t, char* buf, size_t cap) { return rolltui_os_read(t->in_fd, buf, cap); }

void rolltui_os_tty_wake(RolltuiOsTty* t) {
  char c = 'k';
  ssize_t r;
  if (t->wake_w < 0) return;
  r = write(t->wake_w, &c, 1); /* non-blocking; a full pipe already wakes */
  (void)r;
}

void rolltui_os_tty_publish(RolltuiOsTty* t, const char* leave, size_t len) {
  const size_t n = len < sizeof g_leave ? len : sizeof g_leave;
  memcpy(g_leave, leave, n);
  g_leave_len = n;
  g_out_fd = t->out_fd;
}

void rolltui_os_tty_withdraw(void) {
  g_out_fd = -1;
  g_wake_fd = -1;
  g_have_tio = 0;
}

void rolltui_os_tty_restore_now(void) {
  const int fd = g_out_fd;
  if (fd < 0) return;
  write_all_fd(fd, g_leave, g_leave_len);
  if (g_have_tio) tcsetattr(fd, TCSANOW, &g_saved_tio);
  g_out_fd = -1;
}

static void on_fatal_signal(int sig) {
  rolltui_os_tty_restore_now();
  signal(sig, SIG_DFL);
  raise(sig);
}

static void on_winch(int sig) {
  const int fd = g_wake_fd;
  (void)sig;
  if (fd >= 0) {
    char c = 'w';
    ssize_t r = write(fd, &c, 1);
    (void)r;
  }
}

/* A CRASH (an abort, an assert, a bad access) puts the terminal back too, then goes on to whatever handled the signal before, so a
 * host's crash reporter still runs, and the process ends as that signal ends it. */
static const int kCrashSignals[] = {SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL};
#define CRASH_SIGNALS (sizeof kCrashSignals / sizeof kCrashSignals[0])
static struct sigaction g_crash_prev[CRASH_SIGNALS];

static void on_crash_signal(int sig) {
  size_t i;
  rolltui_os_tty_restore_now();
  for (i = 0; i < CRASH_SIGNALS; ++i)
    if (kCrashSignals[i] == sig) sigaction(sig, &g_crash_prev[i], NULL);
  raise(sig); /* blocked while this runs: delivered to the previous action as it returns */
}

void rolltui_os_tty_install_handlers(void) {
  size_t i;
  struct sigaction sa;
  if (atomic_exchange(&g_handlers_installed, 1)) return;
  signal(SIGINT, on_fatal_signal);
  signal(SIGTERM, on_fatal_signal);
  signal(SIGHUP, on_fatal_signal);
  signal(SIGQUIT, on_fatal_signal);
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_crash_signal;
  sigemptyset(&sa.sa_mask);
  for (i = 0; i < CRASH_SIGNALS; ++i) sigaction(kCrashSignals[i], &sa, &g_crash_prev[i]);
  atexit(rolltui_os_tty_restore_now); /* a host's `exit` with the terminal still entered: nothing once it is freed */
}

void rolltui_os_tty_listen_resize(RolltuiOsTty* t) {
  struct sigaction sa;
  g_wake_fd = t->wake_w;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_winch;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGWINCH, &sa, NULL);
}

#endif /* !_WIN32 */
