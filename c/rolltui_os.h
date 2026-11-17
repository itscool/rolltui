#ifndef ROLLTUI_OS_H
#define ROLLTUI_OS_H
/*
 * rolltui/c/rolltui_os.h — the operating system, behind one interface. INTERNAL.
 *
 * c/rolltui_os_posix.c implements it on macOS, Linux and the BSDs; c/rolltui_os_win32.c on
 * Windows. Every path is UTF-8; the Windows implementation converts to and from UTF-16.
 */
#include "rolltui/rolltui.h"

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#if !defined(_WIN32)
#include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- processes (rolltui_process_posix.c, rolltui_process_win32.c) --------------------------
 * A program another of this one's parts started: its output read without waiting, its input written, its end asked about without
 * waiting. PIPES: stdin a pipe, stdout and stderr one pipe. PTY: a pseudo-terminal `cols` x `rows` (on Windows, pipes with COLUMNS and
 * LINES). TERMINAL: the three streams are the terminal `tty_in` / `tty_out` (an editor). QUIET: the null device, its own session or
 * process group, let go when freed. */
typedef struct RolltuiOsChild RolltuiOsChild;
#define ROLLTUI_OS_CHILD_PIPES 0
#define ROLLTUI_OS_CHILD_PTY 1
#define ROLLTUI_OS_CHILD_QUIET 2
#define ROLLTUI_OS_CHILD_TERMINAL 3
RolltuiOsChild* rolltui_os_child_spawn(const char* const* argv, const char* cwd, const char* const* env, int mode, int cols,
                                       int rows, int tty_in, int tty_out, RolltuiStr* err);
/* Bytes now (> 0), nothing yet (0), or the end (-1). */
long rolltui_os_child_read(RolltuiOsChild* c, char* buf, size_t cap);
int rolltui_os_child_write(RolltuiOsChild* c, const char* bytes, size_t len);
void rolltui_os_child_close_input(RolltuiOsChild* c);
void rolltui_os_child_resize(RolltuiOsChild* c, int cols, int rows);
int rolltui_os_child_done(RolltuiOsChild* c, int wait, int* status);
void rolltui_os_child_stop(RolltuiOsChild* c);
void rolltui_os_child_free(RolltuiOsChild* c);
/* Runs `argv` on the terminal and waits; 1 when it started. This process ignores the terminal's Ctrl-C while it waits. */
int rolltui_os_child_wait_terminal(const char* const* argv, const char* cwd, int tty_in, int tty_out, int* status,
                                   RolltuiStr* err);
int rolltui_os_open_with_system(const char* target, RolltuiStr* err);
/* The first native clipboard tool that takes `text` (or `$ROLLTUI_CLIPBOARD`, a command fed it on stdin). */
int rolltui_os_clipboard_native(const char* text, size_t len);
int rolltui_os_open_terminal(int* in_fd, int* out_fd);
void rolltui_os_close_terminal(int in_fd, int out_fd);

/* ---- time ---------------------------------------------------------------------------------- */

/* Milliseconds on a steady clock: never steps back, unrelated to the wall clock. */
unsigned long long rolltui_os_monotonic_ms(void);
unsigned long long rolltui_os_monotonic_us(void); /* the same clock in microseconds */
/* Local time for a wall-clock second count; 0 when it cannot be converted. */
int rolltui_os_localtime(long long secs, struct tm* out);

/* ---- locks --------------------------------------------------------------------------------- */

#if defined(_WIN32)
typedef struct RolltuiMutex { void* lock; } RolltuiMutex; /* an SRWLOCK, which is one pointer */
#else
typedef struct RolltuiMutex { pthread_mutex_t lock; } RolltuiMutex;
#endif
void rolltui_mutex_init(RolltuiMutex* m);
void rolltui_mutex_destroy(RolltuiMutex* m);
void rolltui_mutex_lock(RolltuiMutex* m);
void rolltui_mutex_unlock(RolltuiMutex* m);

/* ---- threads, for the task pool (rolltui_tasks.c) -------------------------------------------- */
#if defined(_WIN32)
typedef struct RolltuiCond { void* cv; } RolltuiCond; /* a CONDITION_VARIABLE, which is one pointer */
#else
typedef struct RolltuiCond { pthread_cond_t cv; } RolltuiCond;
#endif
void rolltui_cond_init(RolltuiCond* c);
void rolltui_cond_destroy(RolltuiCond* c);
/* Waits for a signal with `m` held, as the platform's condition variable does: `m` is released while waiting. */
void rolltui_cond_wait(RolltuiCond* c, RolltuiMutex* m);
void rolltui_cond_signal(RolltuiCond* c);
void rolltui_cond_broadcast(RolltuiCond* c);
typedef struct RolltuiThread RolltuiThread;
/* A thread running `fn(arg)`; NULL when one cannot be made. `rolltui_thread_join` waits for it and frees the handle. */
RolltuiThread* rolltui_thread_start(void (*fn)(void* arg), void* arg);
void rolltui_thread_join(RolltuiThread* t);
void rolltui_os_sleep_ms(int ms);
/* The calling thread, as a number no other running thread has. */
unsigned long long rolltui_os_thread_self(void);

/* ---- files --------------------------------------------------------------------------------- */

/* File types and permissions in the POSIX layout on every platform: `RolltuiDirEntry.mode` is
 * public and keeps one meaning. Windows synthesises them from the file's attributes. */
#define ROLLTUI_OS_TYPE_MASK 0170000u
#define ROLLTUI_OS_TYPE_DIR 0040000u
#define ROLLTUI_OS_TYPE_FILE 0100000u
#define ROLLTUI_OS_TYPE_LINK 0120000u
#define ROLLTUI_OS_IS_DIR(m) (((m) & ROLLTUI_OS_TYPE_MASK) == ROLLTUI_OS_TYPE_DIR)
#define ROLLTUI_OS_IS_FILE(m) (((m) & ROLLTUI_OS_TYPE_MASK) == ROLLTUI_OS_TYPE_FILE)
#define ROLLTUI_OS_IS_LINK(m) (((m) & ROLLTUI_OS_TYPE_MASK) == ROLLTUI_OS_TYPE_LINK)

typedef struct RolltuiOsStat {
  unsigned int mode;   /* type and permission bits, POSIX layout */
  long long size;      /* bytes */
  long long mtime;     /* seconds since the epoch */
  long long mtime_ns;  /* the modification time in nanoseconds since the epoch */
  long long ctime_ns;  /* the status-change time in nanoseconds (Windows: the creation time, new when a file is replaced) */
  unsigned long long dev, ino; /* the file's identity (Windows: 0) */
} RolltuiOsStat;

/* 0 on success. `follow_links` 0 describes a link itself rather than what it points at. */
int rolltui_os_stat(const char* path, int follow_links, RolltuiOsStat* out);
/* 0 when the directory exists afterwards, whether or not it was made by this call. */
int rolltui_os_mkdir(const char* path);
/* Renames `from` over `to`, replacing it; 0 on success. */
int rolltui_os_rename_replace(const char* from, const char* to);
FILE* rolltui_os_fopen(const char* path, const char* mode);
/* The working directory into `buf`; 0 on success. */
int rolltui_os_getcwd(char* buf, size_t cap);
/* This program's own executable, as a full path, into `buf`; 0 on success. */
int rolltui_os_exe_path(char* buf, size_t cap);
int rolltui_os_pid(void);
/* Where a person's configuration lives when no variable names it: `$HOME/.config` on macOS and
 * Linux, `%APPDATA%` on Windows. 0 on success. */
int rolltui_os_config_home(char* buf, size_t cap);
/* The operating system's release ("24.1.0", "10.0.22631") and this machine's name; 0 on success. */
int rolltui_os_release(char* buf, size_t cap);
int rolltui_os_host_name(char* buf, size_t cap);

/* Reading a file without blocking on a pipe or a device: open, describe, read, close. */
int rolltui_os_open_read(const char* path);  /* a descriptor, or -1 */
int rolltui_os_fstat(int fd, RolltuiOsStat* out);
long rolltui_os_read(int fd, char* buf, size_t cap); /* bytes read, 0 at the end, -1 on error */
long rolltui_os_pread(int fd, char* buf, size_t cap, long long offset); /* the same, at `offset` */
void rolltui_os_close(int fd);

/* A directory's names, `.` and `..` included, in the order the system gives them. */
typedef struct RolltuiOsDir RolltuiOsDir;
RolltuiOsDir* rolltui_os_dir_open(const char* path);
const char* rolltui_os_dir_next(RolltuiOsDir* d); /* NULL at the end; valid until the next call */
/* The entry `next` last returned, described as `rolltui_os_stat` describes a path; 0 on success.
 * Windows answers from the listing itself and touches the file only to follow a link. */
int rolltui_os_dir_stat(RolltuiOsDir* d, int follow_links, RolltuiOsStat* out);
/* 1 when the system marks that entry hidden: Windows' hidden attribute. 0 on macOS and Linux,
 * where a leading dot is the only mark. */
int rolltui_os_dir_hidden(const RolltuiOsDir* d);
void rolltui_os_dir_close(RolltuiOsDir* d);

/* ---- the terminal -------------------------------------------------------------------------- */

int rolltui_os_isatty(int fd);

/* One terminal: the input and output it was opened on, its saved mode, and a wake channel that
 * another thread (or the resize signal) can trip. */
typedef struct RolltuiOsTty RolltuiOsTty;
RolltuiOsTty* rolltui_os_tty_open(int in_fd, int out_fd);
void rolltui_os_tty_close(RolltuiOsTty* t);
int rolltui_os_tty_is_tty(const RolltuiOsTty* t);
/* The tty is a Linux virtual console: its name is /dev/ttyN or /dev/vc/N. */
int rolltui_os_tty_is_console(const RolltuiOsTty* t);
/* Saves the mode and switches to raw input (and, on Windows, VT input and output); 1 when a mode
 * was saved and will be restored. */
int rolltui_os_tty_raw(RolltuiOsTty* t);
void rolltui_os_tty_restore(RolltuiOsTty* t);
/* Columns and rows; 0 when the size cannot be read. */
int rolltui_os_tty_size(const RolltuiOsTty* t, int* w, int* h);
/* The window's size in pixels, where the terminal says (TIOCGWINSZ's xpixel and ypixel); 0 where it does not. */
int rolltui_os_tty_pixels(const RolltuiOsTty* t, int* xpix, int* ypix);
void rolltui_os_tty_write(RolltuiOsTty* t, const char* bytes, size_t len);

/* Waits up to `timeout_ms` (-1: forever). Returns 0 on a timeout, a set of ROLLTUI_OS_TTY_* bits
 * when something happened, ROLLTUI_OS_TTY_AGAIN when interrupted (call again) and
 * ROLLTUI_OS_TTY_ERROR on an error. With `with_wake` 0 only input is waited for. */
#define ROLLTUI_OS_TTY_INPUT 1
#define ROLLTUI_OS_TTY_WOKEN 2
#define ROLLTUI_OS_TTY_RESIZED 4
#define ROLLTUI_OS_TTY_AGAIN (-1)
#define ROLLTUI_OS_TTY_ERROR (-2)
int rolltui_os_tty_wait(RolltuiOsTty* t, int timeout_ms, int with_wake);
/* Reads input that `wait` reported: bytes read, or 0 or less when there is nothing more. */
long rolltui_os_tty_read(RolltuiOsTty* t, char* buf, size_t cap);
/* Wakes a `wait` on another thread. Safe from any thread. */
void rolltui_os_tty_wake(RolltuiOsTty* t);
/* Drops what has been typed and not yet read: a key held down over a slow action does not run on after it. */
void rolltui_os_tty_flush_input(RolltuiOsTty* t);

/* THE PROCESS-WIDE RESTORE. `publish` names the terminal and the bytes that leave it, so a fatal
 * signal, a crash or an `exit` (Windows: also a console close or Ctrl-C) can put the terminal back; `withdraw` clears it.
 * `restore_now` writes those bytes and restores the mode, and is safe in a signal handler.
 * `install_handlers` installs the fatal handlers once per process; `listen_resize` routes the
 * resize notification (SIGWINCH) to this terminal's wake channel. */
void rolltui_os_tty_publish(RolltuiOsTty* t, const char* leave, size_t len);
void rolltui_os_tty_withdraw(void);
void rolltui_os_tty_restore_now(void);
void rolltui_os_tty_install_handlers(void);
/* 1 once after the process was stopped from outside and continued (the stop's handler left the terminal as it was found); clears it. */
int rolltui_os_tty_take_resumed(void);
/* Stops the process as Ctrl-Z in a shell would, and returns when it is continued: 1; 0 where there is no job control. */
int rolltui_os_stop_process(void);
void rolltui_os_tty_listen_resize(RolltuiOsTty* t);

/* A FATAL ERROR, SAID WHERE IT CAN BE READ: the terminal is put back (the alternate screen left, the mode restored) before the
 * message goes to stderr with a "rolltui: " prefix and a newline, and then the process aborts. Every stop in the library goes
 * through here: a message printed first would be drawn on the screen that leaving discards. */
#if defined(__cplusplus)
#define ROLLTUI_NORETURN [[noreturn]]
#else
#define ROLLTUI_NORETURN _Noreturn
#endif
#if defined(__MINGW32__) /* the UCRT's printf, which has %zu; GCC's `printf` there means Microsoft's older one */
#define ROLLTUI_PRINTF_LIKE __attribute__((format(gnu_printf, 1, 2)))
#elif defined(__GNUC__)
#define ROLLTUI_PRINTF_LIKE __attribute__((format(printf, 1, 2)))
#else
#define ROLLTUI_PRINTF_LIKE
#endif
ROLLTUI_NORETURN void rolltui_fatal(const char* fmt, ...) ROLLTUI_PRINTF_LIKE;

#ifdef __cplusplus
}
#endif
#endif
