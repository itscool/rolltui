/* rolltui/c/rolltui_process_posix.c — other programs, on macOS, Linux and the BSDs: rolltui_os.h's processes, the system's opener,
 * the native clipboard and the controlling terminal by name.
 *
 * A PROGRAM IS STARTED WITH fork AND execve, its path found on PATH and its environment built BEFORE the fork, so the child runs
 * only calls that are safe between a fork and an exec in a process that has threads (a host's worker may hold the allocator's lock).
 * Whether the exec happened comes back through a close-on-exec pipe: nothing read means it ran, an errno read means it did not. */
#if !defined(_WIN32) && !defined(__wasi__)
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "rolltui/c/rolltui_os.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_str.h"

extern char** environ;

struct RolltuiOsChild {
  pid_t pid;
  int mode;
  int out_fd; /* what the program writes, read here; -1 once it closed */
  int in_fd;  /* what the program reads; -1 once closed */
  int exited, status;
};

/* ---- before the fork ----------------------------------------------------------------------- */

/* The file `name` names: itself when it has a slash, else the first folder on PATH that has it, runnable. */
static int resolve(const char* name, RolltuiStr* out) {
  const char* path = getenv("PATH");
  const char* at;
  if (!name || !*name) return 0;
  if (strchr(name, '/')) {
    rolltui_str_set(out, name, strlen(name));
    return access(name, X_OK) == 0;
  }
  if (!path || !*path) path = "/usr/bin:/bin";
  at = path;
  for (;;) {
    const char* end = strchr(at, ':');
    const size_t n = end ? (size_t)(end - at) : strlen(at);
    rolltui_str_set(out, n ? at : ".", n ? n : 1);
    rolltui_str_append(out, "/", 1);
    rolltui_str_append(out, name, strlen(name));
    if (access(out->p, X_OK) == 0) {
      struct stat st;
      if (stat(out->p, &st) == 0 && S_ISREG(st.st_mode)) return 1;
    }
    if (!end) break;
    at = end + 1;
  }
  return 0;
}

/* `environ` with `extra`'s "NAME=value" entries over it: one array of pointers into this process's strings and `extra`'s. */
static char** build_env(const char* const* extra) {
  size_t n = 0, k = 0, i, j;
  char** v;
  if (!extra || !extra[0]) return environ;
  while (environ[n]) ++n;
  while (extra[k]) ++k;
  v = (char**)rolltui_mem_alloc((n + k + 1) * sizeof *v);
  {
    size_t out = 0;
    for (i = 0; i < n; ++i) {
      const char* eq = strchr(environ[i], '=');
      const size_t name_n = eq ? (size_t)(eq - environ[i]) : strlen(environ[i]);
      int replaced = 0;
      for (j = 0; j < k && !replaced; ++j)
        if (strncmp(extra[j], environ[i], name_n) == 0 && extra[j][name_n] == '=') replaced = 1;
      if (!replaced) v[out++] = environ[i];
    }
    for (j = 0; j < k; ++j) v[out++] = (char*)extra[j];
    v[out] = NULL;
  }
  return v;
}

static void say(RolltuiStr* err, const char* a, const char* b) {
  if (!err) return;
  rolltui_str_set(err, a, strlen(a));
  if (b) rolltui_str_append(err, b, strlen(b));
}

/* The program could not be started: a reason from errno, named. */
static void say_errno(RolltuiStr* err, const char* what, int e) {
  if (!err) return;
  rolltui_str_set(err, what, strlen(what));
  rolltui_str_append(err, ": ", 2);
  rolltui_str_append(err, strerror(e), strlen(strerror(e)));
}

static void nonblocking(int fd) {
  const int fl = fcntl(fd, F_GETFL, 0);
  if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* ---- the child, after the fork: only async-signal-safe calls ----------------------------------- */

static void child_fail(int report) {
  const int e = errno;
  ssize_t w;
  do {
    w = write(report, &e, sizeof e);
  } while (w < 0 && errno == EINTR);
  _exit(127);
}

static void dup_to(int from, int to, int report) {
  if (from == to) {
    const int fl = fcntl(from, F_GETFD, 0);
    if (fl >= 0) fcntl(from, F_SETFD, fl & ~FD_CLOEXEC);
    return;
  }
  if (dup2(from, to) < 0) child_fail(report);
}

RolltuiOsChild* rolltui_os_child_spawn(const char* const* argv, const char* cwd, const char* const* env, int mode, int cols,
                                       int rows, int tty_in, int tty_out, RolltuiStr* err) {
  RolltuiStr path;
  char** envp;
  int report[2] = {-1, -1}, out_pipe[2] = {-1, -1}, in_pipe[2] = {-1, -1};
  int master = -1, devnull = -1;
  const char* slave_name = NULL;
  char slave_buf[128];
  struct winsize ws;
  pid_t pid;
  RolltuiOsChild* c;
  int e = 0;
  ssize_t got;
  if (err) rolltui_str_clear(err);
  if (!argv || !argv[0]) {
    say(err, "nothing to run", NULL);
    return NULL;
  }
  memset(&path, 0, sizeof path);
  if (!resolve(argv[0], &path)) {
    say(err, "not found: ", argv[0]);
    rolltui_str_free(&path);
    return NULL;
  }
  if (pipe(report) < 0) {
    say_errno(err, "cannot start", errno);
    rolltui_str_free(&path);
    return NULL;
  }
  fcntl(report[0], F_SETFD, FD_CLOEXEC);
  fcntl(report[1], F_SETFD, FD_CLOEXEC);
  if (mode == ROLLTUI_OS_CHILD_PIPES) {
    if (pipe(out_pipe) < 0 || pipe(in_pipe) < 0) {
      e = errno;
    } else { /* every end close-on-exec: the program gets only the copies made on 0, 1 and 2, so it sees its input end */
      fcntl(out_pipe[0], F_SETFD, FD_CLOEXEC);
      fcntl(out_pipe[1], F_SETFD, FD_CLOEXEC);
      fcntl(in_pipe[0], F_SETFD, FD_CLOEXEC);
      fcntl(in_pipe[1], F_SETFD, FD_CLOEXEC);
    }
  } else if (mode == ROLLTUI_OS_CHILD_PTY) {
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) < 0 || unlockpt(master) < 0) {
      e = errno;
    } else {
      const char* n = ptsname(master);
      if (!n || strlen(n) >= sizeof slave_buf) {
        e = ENXIO;
      } else {
        strcpy(slave_buf, n);
        slave_name = slave_buf;
        memset(&ws, 0, sizeof ws);
        ws.ws_col = (unsigned short)(cols > 0 ? cols : 80);
        ws.ws_row = (unsigned short)(rows > 0 ? rows : 24);
        ioctl(master, TIOCSWINSZ, &ws);
        fcntl(master, F_SETFD, FD_CLOEXEC);
      }
    }
  } else if (mode == ROLLTUI_OS_CHILD_QUIET) {
    devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (devnull < 0) e = errno;
  }
  if (e) {
    say_errno(err, "cannot start", e);
    if (master >= 0) close(master);
    if (out_pipe[0] >= 0) close(out_pipe[0]), close(out_pipe[1]);
    if (in_pipe[0] >= 0) close(in_pipe[0]), close(in_pipe[1]);
    close(report[0]);
    close(report[1]);
    rolltui_str_free(&path);
    return NULL;
  }
  envp = build_env(env);
  pid = fork();
  if (pid == 0) { /* ---- the child ---- */
    struct sigaction dfl;
    memset(&dfl, 0, sizeof dfl);
    dfl.sa_handler = SIG_DFL;
    sigaction(SIGINT, &dfl, NULL); /* what this process ignores while it waits, the program has to itself */
    sigaction(SIGQUIT, &dfl, NULL);
    sigaction(SIGPIPE, &dfl, NULL);
    sigaction(SIGTSTP, &dfl, NULL);
    sigaction(SIGTTIN, &dfl, NULL);
    sigaction(SIGTTOU, &dfl, NULL);
    if (mode == ROLLTUI_OS_CHILD_PTY) {
      int slave;
      setsid();
      slave = open(slave_name, O_RDWR);
      if (slave < 0) child_fail(report[1]);
#if defined(TIOCSCTTY)
      ioctl(slave, TIOCSCTTY, 0);
#endif
      ioctl(slave, TIOCSWINSZ, &ws); /* the size again, on the open slave: macOS does not keep one set before it was opened */
      dup_to(slave, 0, report[1]);
      dup_to(slave, 1, report[1]);
      dup_to(slave, 2, report[1]);
      if (slave > 2) close(slave);
    } else if (mode == ROLLTUI_OS_CHILD_PIPES) {
      setpgid(0, 0); /* its own group: stopping it stops what it started */
      dup_to(in_pipe[0], 0, report[1]);
      dup_to(out_pipe[1], 1, report[1]);
      dup_to(out_pipe[1], 2, report[1]);
    } else if (mode == ROLLTUI_OS_CHILD_QUIET) {
      setsid(); /* its own session: Ctrl-C here never reaches it, and it outlives this program */
      dup_to(devnull, 0, report[1]);
      dup_to(devnull, 1, report[1]);
      dup_to(devnull, 2, report[1]);
    } else { /* ROLLTUI_OS_CHILD_TERMINAL: the terminal itself, as `system` gives it */
      dup_to(tty_in, 0, report[1]);
      dup_to(tty_out, 1, report[1]);
      dup_to(tty_out, 2, report[1]);
    }
    if (cwd && *cwd && chdir(cwd) < 0) child_fail(report[1]);
    { /* the program gets its three streams and nothing else this process had open (a terminal, a wake pipe) */
      int fd;
      for (fd = 3; fd < 256; ++fd)
        if (fd != report[1]) close(fd);
    }
    execve(path.p, (char* const*)argv, envp);
    child_fail(report[1]);
  }
  /* ---- the parent ---- */
  e = pid < 0 ? errno : 0;
  close(report[1]);
  if (envp != environ) rolltui_mem_free(envp);
  if (pid > 0) {
    do {
      got = read(report[0], &e, sizeof e);
    } while (got < 0 && errno == EINTR);
    if (got != (ssize_t)sizeof e) e = 0;
  }
  close(report[0]);
  if (out_pipe[1] >= 0) close(out_pipe[1]);
  if (in_pipe[0] >= 0) close(in_pipe[0]);
  if (devnull >= 0) close(devnull);
  if (e) { /* it did not start: the reason, and nothing left behind */
    int st;
    if (pid > 0)
      while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
      }
    say_errno(err, argv[0], e);
    if (master >= 0) close(master);
    if (out_pipe[0] >= 0) close(out_pipe[0]);
    if (in_pipe[1] >= 0) close(in_pipe[1]);
    rolltui_str_free(&path);
    return NULL;
  }
  rolltui_str_free(&path);
  c = (RolltuiOsChild*)rolltui_mem_alloc(sizeof *c);
  memset(c, 0, sizeof *c);
  c->pid = pid;
  c->mode = mode;
  c->out_fd = c->in_fd = -1;
  if (mode == ROLLTUI_OS_CHILD_PIPES) {
    c->out_fd = out_pipe[0];
    c->in_fd = in_pipe[1];
    fcntl(c->out_fd, F_SETFD, FD_CLOEXEC);
    fcntl(c->in_fd, F_SETFD, FD_CLOEXEC);
  } else if (mode == ROLLTUI_OS_CHILD_PTY) {
    c->out_fd = master;
    c->in_fd = master;
  }
  if (c->out_fd >= 0) nonblocking(c->out_fd);
#if defined(F_SETNOSIGPIPE)
  if (c->in_fd >= 0) fcntl(c->in_fd, F_SETNOSIGPIPE, 1); /* a write after the program left is an error, not a signal */
#endif
  return c;
}

long rolltui_os_child_read(RolltuiOsChild* c, char* buf, size_t cap) {
  ssize_t n;
  if (!c || c->out_fd < 0) return -1;
  if (cap == 0) return 0;
  do {
    n = read(c->out_fd, buf, cap);
  } while (n < 0 && errno == EINTR);
  if (n > 0) return (long)n;
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
  /* the end (a pipe closed, or a pty whose program left: EIO on Linux) */
  if (c->mode == ROLLTUI_OS_CHILD_PIPES) close(c->out_fd);
  c->out_fd = -1;
  return -1;
}

int rolltui_os_child_write(RolltuiOsChild* c, const char* bytes, size_t len) {
  size_t done = 0;
#if !defined(F_SETNOSIGPIPE)
  sigset_t block, old;
#endif
  if (!c || c->in_fd < 0) return 0;
#if !defined(F_SETNOSIGPIPE)
  /* NO SIGPIPE FROM A PROGRAM THAT LEFT: blocked for the write, and one it raised taken back before the mask is restored */
  sigemptyset(&block);
  sigaddset(&block, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &block, &old);
#endif
  while (done < len) {
    const ssize_t n = write(c->in_fd, bytes + done, len - done);
    if (n > 0) {
      done += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    break;
  }
#if !defined(F_SETNOSIGPIPE)
  {
    sigset_t pending;
    sigpending(&pending);
    if (sigismember(&pending, SIGPIPE)) {
      int sig;
      sigwait(&block, &sig);
    }
    pthread_sigmask(SIG_SETMASK, &old, NULL);
  }
#endif
  return done == len;
}

void rolltui_os_child_close_input(RolltuiOsChild* c) {
  if (!c || c->in_fd < 0) return;
  if (c->mode == ROLLTUI_OS_CHILD_PTY) {
    const char eot = 4; /* a pty has one end: end of input is the terminal's ^D */
    ssize_t w = write(c->in_fd, &eot, 1);
    (void)w;
    return;
  }
  close(c->in_fd);
  c->in_fd = -1;
}

void rolltui_os_child_resize(RolltuiOsChild* c, int cols, int rows) {
  struct winsize ws;
  if (!c || c->mode != ROLLTUI_OS_CHILD_PTY || c->out_fd < 0) return;
  memset(&ws, 0, sizeof ws);
  ws.ws_col = (unsigned short)(cols > 0 ? cols : 1);
  ws.ws_row = (unsigned short)(rows > 0 ? rows : 1);
  ioctl(c->out_fd, TIOCSWINSZ, &ws); /* the program is told with a SIGWINCH */
}

int rolltui_os_child_done(RolltuiOsChild* c, int wait, int* status) {
  int st = 0;
  pid_t r;
  if (!c) return 1;
  if (!c->exited) {
    do {
      r = waitpid(c->pid, &st, wait ? 0 : WNOHANG);
    } while (r < 0 && errno == EINTR);
    if (r == c->pid) {
      c->exited = 1;
      c->status = WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1;
    } else if (r < 0) { /* reaped elsewhere: nothing to say about how it ended */
      c->exited = 1;
      c->status = -1;
    }
  }
  if (c->exited && status) *status = c->status;
  return c->exited;
}

void rolltui_os_child_stop(RolltuiOsChild* c) {
  int i, st;
  if (!c || c->exited) return;
  /* ASKED, then TOLD: SIGTERM to its group, and SIGKILL if it is still there 200 ms later */
  kill(c->mode == ROLLTUI_OS_CHILD_TERMINAL ? c->pid : -c->pid, SIGTERM);
  for (i = 0; i < 20; ++i) {
    struct timespec ts = {0, 10 * 1000 * 1000};
    if (rolltui_os_child_done(c, 0, &st)) return;
    nanosleep(&ts, NULL);
  }
  kill(c->mode == ROLLTUI_OS_CHILD_TERMINAL ? c->pid : -c->pid, SIGKILL);
  rolltui_os_child_done(c, 1, &st);
}

void rolltui_os_child_free(RolltuiOsChild* c) {
  if (!c) return;
  if (c->mode != ROLLTUI_OS_CHILD_QUIET) rolltui_os_child_stop(c); /* a quiet program is let go: it is meant to outlive this */
  if (c->out_fd >= 0) close(c->out_fd);
  if (c->in_fd >= 0 && c->in_fd != c->out_fd) close(c->in_fd);
  rolltui_mem_free(c);
}

/* A SIGTERM WHILE A PROGRAM HAS THE TERMINAL is passed on to it (async-signal-safe: a kill and a note), and once it has ended it is
 * raised here again, so this program ends as it was asked to. */
static volatile pid_t g_term_to = 0;
static volatile sig_atomic_t g_term_got = 0;
static void on_term_while_away(int sig) {
  g_term_got = sig;
  if (g_term_to > 0) kill(g_term_to, sig);
}

int rolltui_os_child_wait_terminal(const char* const* argv, const char* cwd, int tty_in, int tty_out, int* status,
                                   RolltuiStr* err) {
  struct sigaction ignore, old_int, old_quit, pass, old_term;
  RolltuiOsChild* c;
  int st = -1;
  memset(&pass, 0, sizeof pass);
  pass.sa_handler = on_term_while_away;
  sigemptyset(&pass.sa_mask);
  g_term_to = 0;
  g_term_got = 0;
  sigaction(SIGTERM, &pass, &old_term);
  /* THE PROGRAM HAS ITS CTRL-C TO ITSELF: the terminal is in its ordinary mode, so the key signals the whole foreground group, this
   * process included, which ignores SIGINT and SIGQUIT while it waits. */
  memset(&ignore, 0, sizeof ignore);
  ignore.sa_handler = SIG_IGN;
  sigemptyset(&ignore.sa_mask);
  sigaction(SIGINT, &ignore, &old_int);
  sigaction(SIGQUIT, &ignore, &old_quit);
  c = rolltui_os_child_spawn(argv, cwd, NULL, ROLLTUI_OS_CHILD_TERMINAL, 0, 0, tty_in, tty_out, err);
  if (c) {
    g_term_to = c->pid;
    if (g_term_got) kill(c->pid, (int)g_term_got); /* asked to end as it started */
    rolltui_os_child_done(c, 1, &st);
  }
  g_term_to = 0;
  sigaction(SIGINT, &old_int, NULL);
  sigaction(SIGQUIT, &old_quit, NULL);
  sigaction(SIGTERM, &old_term, NULL);
  if (status) *status = st;
  if (c) {
    c->exited = 1;
    rolltui_os_child_free(c);
  }
  if (g_term_got) raise((int)g_term_got); /* this program was asked to end too: as it would have */
  return c != NULL;
}

/* ---- the system's opener ------------------------------------------------------------------- */

int rolltui_os_open_with_system(const char* target, RolltuiStr* err) {
#if defined(__APPLE__)
  const char* argv[] = {"open", target, NULL};
#else
  const char* argv[] = {"xdg-open", target, NULL};
#endif
  RolltuiOsChild* c = rolltui_os_child_spawn(argv, NULL, NULL, ROLLTUI_OS_CHILD_QUIET, 0, 0, -1, -1, err);
  int st = -1;
  if (!c) return 0;
  /* the opener hands the file over and returns at once; its answer says whether it could */
  rolltui_os_child_done(c, 1, &st);
  rolltui_os_child_free(c);
  if (st != 0 && err) {
    char buf[64];
    snprintf(buf, sizeof buf, "%s exited with %d", argv[0], st);
    rolltui_str_set(err, buf, strlen(buf));
  }
  return st == 0;
}

/* ---- the native clipboard ------------------------------------------------------------------- */

/* `text` on the standard input of `argv`; whether it ran and exited 0. */
static int pipe_into(const char* const* argv, const char* text, size_t len) {
  RolltuiOsChild* c = rolltui_os_child_spawn(argv, NULL, NULL, ROLLTUI_OS_CHILD_PIPES, 0, 0, -1, -1, NULL);
  int st = -1;
  if (!c) return 0;
  {
    /* the tool's own output is not read: it is waited for, so a pipe it fills cannot hold it */
    const int fl = fcntl(c->in_fd, F_GETFL, 0);
    if (fl >= 0) fcntl(c->in_fd, F_SETFL, fl & ~O_NONBLOCK);
  }
  rolltui_os_child_write(c, text, len);
  rolltui_os_child_close_input(c);
  for (;;) {
    char sink[512];
    const long n = rolltui_os_child_read(c, sink, sizeof sink);
    if (n < 0) break;
    if (n == 0) {
      if (rolltui_os_child_done(c, 0, &st)) break;
      {
        struct timespec ts = {0, 2 * 1000 * 1000};
        nanosleep(&ts, NULL);
      }
    }
  }
  rolltui_os_child_done(c, 1, &st);
  c->exited = 1;
  rolltui_os_child_free(c);
  return st == 0;
}

int rolltui_os_clipboard_native(const char* text, size_t len) {
  const char* override = getenv("ROLLTUI_CLIPBOARD");
  if (override && *override) { /* a command named for the clipboard: a test's recorder, or a person's own tool */
    const char* argv[] = {"/bin/sh", "-c", override, NULL};
    return pipe_into(argv, text, len);
  }
#if defined(__APPLE__)
  {
    const char* argv[] = {"pbcopy", NULL};
    return pipe_into(argv, text, len);
  }
#else
  if (getenv("WAYLAND_DISPLAY")) {
    const char* argv[] = {"wl-copy", NULL};
    if (pipe_into(argv, text, len)) return 1;
  }
  if (getenv("DISPLAY")) {
    const char* xclip[] = {"xclip", "-selection", "clipboard", NULL};
    const char* xsel[] = {"xsel", "--clipboard", "--input", NULL};
    if (pipe_into(xclip, text, len)) return 1;
    if (pipe_into(xsel, text, len)) return 1;
  }
  return 0;
#endif
}

/* ---- the controlling terminal --------------------------------------------------------------- */

int rolltui_os_open_terminal(int* in_fd, int* out_fd) {
  const int fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
  if (fd < 0) return 0;
  *in_fd = fd;
  *out_fd = fd;
  return 1;
}

void rolltui_os_close_terminal(int in_fd, int out_fd) {
  if (in_fd >= 0) close(in_fd);
  if (out_fd >= 0 && out_fd != in_fd) close(out_fd);
}

#endif /* !_WIN32 && !__wasi__ */
