/* rolltui/c/rolltui_os_web.c — rolltui_os.h in a web page: a wasm32-wasi build whose terminal is xterm.js.
 *
 * THE PAGE IS THE TERMINAL. What the library writes goes out on descriptor 1 (WASI's `fd_write`, which the page hands to
 * xterm.js); what the person types, and the terminal's replies, the page hands in with `rolltui_web_input`; a new size with
 * `rolltui_web_resize`. Nothing here blocks: a page cannot wait, so a wait answers at once with what has arrived, and the page
 * drives the loop with `rolltui_run_step` (rolltui.h, the run loop).
 *
 * WHAT A PAGE HAS NOT: no other programs, no threads (a task pool runs each job on the thread that drains), no files (every
 * preset is the library's embedded copy, and nothing a person changes is kept), no job control and no signals. */
#if defined(__wasi__)

#include "rolltui/c/rolltui_os.h"

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rolltui/c/rolltui_alloc.h"

#define WEB_EXPORT(name) __attribute__((export_name(name), used))

/* ---- what the page hands in ---------------------------------------------------------------- */

/* THE PAGE'S SIDE OF THE TERMINAL, one per page: its bytes waiting to be read, its size, and whether it was resized or the loop
 * woken since the last wait. */
typedef struct WebTerminal {
  char* in;
  size_t in_n, in_cap;
  char* stage; /* where the page writes the bytes it hands in */
  size_t stage_cap;
  int w, h, xpix, ypix;
  int resized, woken;
} WebTerminal;
static WebTerminal g_web = {NULL, 0, 0, NULL, 0, 80, 24, 0, 0, 0, 0};

/* Room for `n` bytes the page is about to hand in; it writes them there and calls `rolltui_web_input` with the count. */
WEB_EXPORT("rolltui_web_input_area") char* rolltui_web_input_area(size_t n) {
  g_web.stage = (char*)rolltui_grow(g_web.stage, &g_web.stage_cap, n ? n : 1, 1);
  return g_web.stage;
}

/* The `n` bytes at the input area: typing, a paste, the terminal's replies, read by the next step. */
WEB_EXPORT("rolltui_web_input") void rolltui_web_input(size_t n) {
  if (n > g_web.stage_cap) n = g_web.stage_cap;
  g_web.in = (char*)rolltui_grow(g_web.in, &g_web.in_cap, g_web.in_n + n, 1);
  memcpy(g_web.in + g_web.in_n, g_web.stage, n);
  g_web.in_n += n;
}

/* The terminal is now `cols` x `rows` cells, `xpix` x `ypix` pixels (0 where unknown). */
WEB_EXPORT("rolltui_web_resize") void rolltui_web_resize(int cols, int rows, int xpix, int ypix) {
  if (cols < 1) cols = 1;
  if (rows < 1) rows = 1;
  g_web.resized = g_web.resized || cols != g_web.w || rows != g_web.h;
  g_web.w = cols;
  g_web.h = rows;
  g_web.xpix = xpix > 0 ? xpix : 0;
  g_web.ypix = ypix > 0 ? ypix : 0;
}

/* ---- other programs: a page runs none -------------------------------------------------------- */

static void no_programs(RolltuiStr* err) {
  if (err) rolltui_str_set(err, "a web page runs no other programs", 33);
}

void rolltui_os_child_refuse_pty(int refuse) { (void)refuse; }
RolltuiOsChild* rolltui_os_child_spawn(const char* const* argv, const char* cwd, const char* const* env, int mode, int cols,
                                       int rows, int tty_in, int tty_out, RolltuiStr* err) {
  (void)argv, (void)cwd, (void)env, (void)mode, (void)cols, (void)rows, (void)tty_in, (void)tty_out;
  no_programs(err);
  return NULL;
}
long rolltui_os_child_read(RolltuiOsChild* c, char* buf, size_t cap) {
  (void)c, (void)buf, (void)cap;
  return -1;
}
int rolltui_os_child_write(RolltuiOsChild* c, const char* bytes, size_t len) {
  (void)c, (void)bytes, (void)len;
  return -1;
}
void rolltui_os_child_close_input(RolltuiOsChild* c) { (void)c; }
void rolltui_os_child_resize(RolltuiOsChild* c, int cols, int rows) { (void)c, (void)cols, (void)rows; }
int rolltui_os_child_done(RolltuiOsChild* c, int wait, int* status) {
  (void)c, (void)wait;
  if (status) *status = -1;
  return 1;
}
void rolltui_os_child_stop(RolltuiOsChild* c) { (void)c; }
void rolltui_os_child_free(RolltuiOsChild* c) { (void)c; }
int rolltui_os_child_wait_terminal(const char* const* argv, const char* cwd, int tty_in, int tty_out, int* status,
                                   RolltuiStr* err) {
  (void)argv, (void)cwd, (void)tty_in, (void)tty_out;
  if (status) *status = -1;
  no_programs(err);
  return 0;
}
int rolltui_os_open_with_system(const char* target, RolltuiStr* err) {
  (void)target;
  no_programs(err);
  return 0;
}
int rolltui_os_clipboard_read(RolltuiStr* out) { /* a page reads nothing of the person's clipboard: what the program copied stands in */
  (void)out;
  return 0;
}
int rolltui_os_clipboard_native(const char* text, size_t len) { /* OSC 52, which xterm.js's clipboard addon takes, is the way */
  (void)text, (void)len;
  return 0;
}
int rolltui_os_open_terminal(int* in_fd, int* out_fd) {
  (void)in_fd, (void)out_fd;
  return 0;
}
void rolltui_os_close_terminal(int in_fd, int out_fd) { (void)in_fd, (void)out_fd; }

/* ---- time ------------------------------------------------------------------------------------ */

unsigned long long rolltui_os_monotonic_us(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (unsigned long long)ts.tv_sec * 1000000ULL + (unsigned long long)ts.tv_nsec / 1000ULL;
}
unsigned long long rolltui_os_monotonic_ms(void) { return rolltui_os_monotonic_us() / 1000ULL; }
int rolltui_os_localtime(long long secs, struct tm* out) { /* a page has no time zone the library can read: UTC */
  const time_t t = (time_t)secs;
  return gmtime_r(&t, out) != NULL;
}

/* ---- locks and threads: one thread, so a lock is never contended and no thread starts --------------- */

void rolltui_mutex_init(RolltuiMutex* m) { (void)m; }
void rolltui_mutex_destroy(RolltuiMutex* m) { (void)m; }
void rolltui_mutex_lock(RolltuiMutex* m) { (void)m; }
void rolltui_mutex_unlock(RolltuiMutex* m) { (void)m; }
void rolltui_cond_init(RolltuiCond* c) { (void)c; }
void rolltui_cond_destroy(RolltuiCond* c) { (void)c; }
void rolltui_cond_wait(RolltuiCond* c, RolltuiMutex* m) { (void)c, (void)m; }
void rolltui_cond_signal(RolltuiCond* c) { (void)c; }
void rolltui_cond_broadcast(RolltuiCond* c) { (void)c; }
int rolltui_os_temp_file(const char* stem, const void* bytes, size_t n, RolltuiStr* out) { /* no terminal here reads one */
  (void)stem, (void)bytes, (void)n, (void)out;
  return 0;
}
void rolltui_os_listen_reload(int on) { (void)on; } /* no SIGUSR1: the files' own changes are what is watched */
int rolltui_os_take_reload(void) { return 0; }
unsigned long long rolltui_os_thread_self(void) { return 1; } /* one thread */
RolltuiThread* rolltui_thread_start(void (*fn)(void* arg), void* arg) {
  (void)fn, (void)arg;
  return NULL;
}
void rolltui_thread_join(RolltuiThread* t) { (void)t; }
void rolltui_os_sleep_ms(int ms) { (void)ms; } /* a page cannot sleep */

/* ---- files: a page has none ---------------------------------------------------------------- */

int rolltui_os_stat(const char* path, int follow_links, RolltuiOsStat* out) {
  (void)path, (void)follow_links, (void)out;
  return -1;
}
int rolltui_os_mkdir(const char* path) {
  (void)path;
  return -1;
}
int rolltui_os_rename_replace(const char* from, const char* to) {
  (void)from, (void)to;
  return -1;
}
FILE* rolltui_os_fopen(const char* path, const char* mode) {
  (void)path, (void)mode;
  return NULL;
}
int rolltui_os_getcwd(char* buf, size_t cap) {
  if (cap < 2) return -1;
  memcpy(buf, "/", 2);
  return 0;
}
int rolltui_os_exe_path(char* buf, size_t cap) {
  (void)buf, (void)cap;
  return -1;
}
int rolltui_os_pid(void) { return 1; }
int rolltui_os_config_home(char* buf, size_t cap) {
  (void)buf, (void)cap;
  return -1;
}
int rolltui_os_state_home(char* buf, size_t cap) {
  (void)buf, (void)cap;
  return -1;
}
static int put_name(char* buf, size_t cap, const char* name) {
  const size_t n = strlen(name);
  if (n + 1 > cap) return -1;
  memcpy(buf, name, n + 1);
  return 0;
}
int rolltui_os_release(char* buf, size_t cap) { return put_name(buf, cap, "web"); }
int rolltui_os_host_name(char* buf, size_t cap) { return put_name(buf, cap, "browser"); }
int rolltui_os_open_read(const char* path) {
  (void)path;
  return -1;
}
int rolltui_os_fstat(int fd, RolltuiOsStat* out) {
  (void)fd, (void)out;
  return -1;
}
long rolltui_os_read(int fd, char* buf, size_t cap) {
  (void)fd, (void)buf, (void)cap;
  return -1;
}
long rolltui_os_pread(int fd, char* buf, size_t cap, long long offset) {
  (void)fd, (void)buf, (void)cap, (void)offset;
  return -1;
}
void rolltui_os_close(int fd) { (void)fd; }
RolltuiOsDir* rolltui_os_dir_open(const char* path) {
  (void)path;
  return NULL;
}
const char* rolltui_os_dir_next(RolltuiOsDir* d) {
  (void)d;
  return NULL;
}
int rolltui_os_dir_stat(RolltuiOsDir* d, int follow_links, RolltuiOsStat* out) {
  (void)d, (void)follow_links, (void)out;
  return -1;
}
int rolltui_os_dir_hidden(const RolltuiOsDir* d) {
  (void)d;
  return 0;
}
void rolltui_os_dir_close(RolltuiOsDir* d) { (void)d; }

/* ---- the terminal: the page's ------------------------------------------------------------------- */

struct RolltuiOsTty {
  int in_fd, out_fd;
};

int rolltui_os_tty_is_console(const RolltuiOsTty* t) {
  (void)t;
  return 0; /* no Linux console here */
}

int rolltui_os_isatty(int fd) { return fd == 0 || fd == 1; }

RolltuiOsTty* rolltui_os_tty_open(int in_fd, int out_fd) {
  RolltuiOsTty* t = (RolltuiOsTty*)rolltui_mem_alloc(sizeof *t);
  t->in_fd = in_fd;
  t->out_fd = out_fd;
  return t;
}
void rolltui_os_tty_close(RolltuiOsTty* t) { rolltui_mem_free(t); }
int rolltui_os_tty_is_tty(const RolltuiOsTty* t) { return t != NULL; }
int rolltui_os_tty_raw(RolltuiOsTty* t) { /* xterm.js is raw: every key reaches the page */
  (void)t;
  return 0;
}
void rolltui_os_tty_restore(RolltuiOsTty* t) { (void)t; }
int rolltui_os_tty_size(const RolltuiOsTty* t, int* w, int* h) {
  (void)t;
  *w = g_web.w;
  *h = g_web.h;
  return 1;
}
int rolltui_os_tty_pixels(const RolltuiOsTty* t, int* xpix, int* ypix) {
  (void)t;
  *xpix = g_web.xpix;
  *ypix = g_web.ypix;
  return g_web.xpix > 0 && g_web.ypix > 0;
}
void rolltui_os_tty_write(RolltuiOsTty* t, const char* bytes, size_t len) {
  while (len > 0) {
    const ssize_t n = write(t->out_fd, bytes, len);
    if (n <= 0) return;
    bytes += n;
    len -= (size_t)n;
  }
}
int rolltui_os_tty_wait(RolltuiOsTty* t, int timeout_ms, int with_wake) { /* never waits: what has arrived, now */
  int bits = 0;
  (void)t, (void)timeout_ms;
  if (g_web.in_n) bits |= ROLLTUI_OS_TTY_INPUT;
  if (g_web.resized) bits |= ROLLTUI_OS_TTY_RESIZED;
  if (with_wake && g_web.woken) bits |= ROLLTUI_OS_TTY_WOKEN;
  g_web.resized = 0;
  if (with_wake) g_web.woken = 0;
  return bits;
}
long rolltui_os_tty_read(RolltuiOsTty* t, char* buf, size_t cap) {
  size_t n = g_web.in_n < cap ? g_web.in_n : cap;
  (void)t;
  if (n == 0) return 0;
  memcpy(buf, g_web.in, n);
  memmove(g_web.in, g_web.in + n, g_web.in_n - n);
  g_web.in_n -= n;
  return (long)n;
}
void rolltui_os_tty_wake(RolltuiOsTty* t) {
  (void)t;
  g_web.woken = 1;
}
void rolltui_os_tty_flush_input(RolltuiOsTty* t) {
  (void)t;
  g_web.in_n = 0;
}
void rolltui_os_tty_publish(RolltuiOsTty* t, const char* leave, size_t len) { (void)t, (void)leave, (void)len; }
void rolltui_os_tty_withdraw(void) {}
void rolltui_os_tty_restore_now(void) {}
void rolltui_os_tty_install_handlers(void) {}
int rolltui_os_tty_take_resumed(void) { return 0; }
int rolltui_os_stop_process(void) { return 0; }
void rolltui_os_tty_listen_resize(RolltuiOsTty* t) { (void)t; }

#endif /* __wasi__ */
