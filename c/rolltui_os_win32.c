/* rolltui/c/rolltui_os_win32.c — rolltui_os.h on Windows. Paths are UTF-8 at the interface and
 * UTF-16 at the system; the console is driven in its VT modes, so the terminal protocol above is
 * the same one every other platform speaks. */
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_alloc.h"

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

/* ---- text ---------------------------------------------------------------------------------- */

/* UTF-8 to UTF-16, allocated; NULL when `s` is not valid UTF-8. Freed with rolltui_mem_free. */
static wchar_t* wide(const char* s) {
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
  wchar_t* w;
  if (n <= 0) return NULL;
  w = (wchar_t*)rolltui_mem_alloc((size_t)n * sizeof *w);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
  return w;
}

static int narrow(const wchar_t* w, char* out, size_t cap) {
  if (!cap) return -1;
  return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL) > 0 ? 0 : -1;
}

static void errno_from_last(void) {
  const DWORD e = GetLastError();
  if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND || e == ERROR_INVALID_NAME) errno = ENOENT;
  else if (e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION) errno = EACCES;
  else if (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS) errno = EEXIST;
  else if (e == ERROR_DIRECTORY) errno = ENOTDIR;
  else errno = EIO;
}

/* ---- time ---------------------------------------------------------------------------------- */

unsigned long long rolltui_os_monotonic_ms(void) { return (unsigned long long)GetTickCount64(); }

unsigned long long rolltui_os_monotonic_us(void) {
  static LARGE_INTEGER freq;
  LARGE_INTEGER now;
  if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&now);
  return (unsigned long long)(now.QuadPart / freq.QuadPart) * 1000000ULL +
         (unsigned long long)(now.QuadPart % freq.QuadPart) * 1000000ULL / (unsigned long long)freq.QuadPart;
}

int rolltui_os_localtime(long long secs, struct tm* out) {
  const time_t t = (time_t)secs;
  return localtime_s(out, &t) == 0;
}

/* ---- locks --------------------------------------------------------------------------------- */

typedef char rolltui_srwlock_fits[sizeof(SRWLOCK) == sizeof(void*) ? 1 : -1];

void rolltui_mutex_init(RolltuiMutex* m) { InitializeSRWLock((PSRWLOCK)&m->lock); }
void rolltui_mutex_destroy(RolltuiMutex* m) { (void)m; }
void rolltui_mutex_lock(RolltuiMutex* m) { AcquireSRWLockExclusive((PSRWLOCK)&m->lock); }
void rolltui_mutex_unlock(RolltuiMutex* m) { ReleaseSRWLockExclusive((PSRWLOCK)&m->lock); }

void rolltui_cond_init(RolltuiCond* c) { InitializeConditionVariable((PCONDITION_VARIABLE)&c->cv); }
void rolltui_cond_destroy(RolltuiCond* c) { (void)c; }
void rolltui_cond_wait(RolltuiCond* c, RolltuiMutex* m) {
  SleepConditionVariableSRW((PCONDITION_VARIABLE)&c->cv, (PSRWLOCK)&m->lock, INFINITE, 0);
}
void rolltui_cond_signal(RolltuiCond* c) { WakeConditionVariable((PCONDITION_VARIABLE)&c->cv); }
void rolltui_cond_broadcast(RolltuiCond* c) { WakeAllConditionVariable((PCONDITION_VARIABLE)&c->cv); }
int rolltui_os_temp_file(const char* stem, const void* bytes, size_t n, RolltuiStr* out) { /* no terminal here reads one */
  (void)stem, (void)bytes, (void)n, (void)out;
  return 0;
}
void rolltui_os_listen_reload(int on) { (void)on; } /* no SIGUSR1: the files' own changes are what is watched */
int rolltui_os_take_reload(void) { return 0; }
unsigned long long rolltui_os_thread_self(void) { return (unsigned long long)GetCurrentThreadId(); }

struct RolltuiThread {
  HANDLE h;
  void (*fn)(void*);
  void* arg;
};
static DWORD WINAPI thread_main(LPVOID p) {
  RolltuiThread* t = (RolltuiThread*)p;
  t->fn(t->arg);
  return 0;
}
RolltuiThread* rolltui_thread_start(void (*fn)(void* arg), void* arg) {
  RolltuiThread* t = (RolltuiThread*)rolltui_mem_alloc(sizeof *t);
  t->fn = fn;
  t->arg = arg;
  t->h = CreateThread(NULL, 0, thread_main, t, 0, NULL);
  if (!t->h) {
    rolltui_mem_free(t);
    return NULL;
  }
  return t;
}
void rolltui_thread_join(RolltuiThread* t) {
  if (!t) return;
  WaitForSingleObject(t->h, INFINITE);
  CloseHandle(t->h);
  rolltui_mem_free(t);
}
void rolltui_os_sleep_ms(int ms) {
  if (ms > 0) Sleep((DWORD)ms);
}

/* ---- files --------------------------------------------------------------------------------- */

/* 100-nanosecond intervals since 1601 to nanoseconds since 1970. */
static long long filetime_ns(FILETIME ft) {
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return ((long long)u.QuadPart - 116444736000000000LL) * 100LL;
}

/* Only a symbolic link and a junction are links. A cloud placeholder (OneDrive) or a
 * deduplicated file is a reparse point too, and is a plain file or folder to a person. */
static int is_link(DWORD attrs, DWORD tag) {
  return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) && (tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT);
}

static void describe(DWORD attrs, FILETIME created, FILETIME written, DWORD size_high, DWORD size_low, int as_link,
                     RolltuiOsStat* out) {
  unsigned int m;
  if (as_link) m = ROLLTUI_OS_TYPE_LINK | 0777u;
  else if (attrs & FILE_ATTRIBUTE_DIRECTORY) m = ROLLTUI_OS_TYPE_DIR | 0755u;
  else m = ROLLTUI_OS_TYPE_FILE | 0644u;
  if (!as_link && (attrs & FILE_ATTRIBUTE_READONLY)) m &= ~0222u;
  out->mode = m;
  out->size = ROLLTUI_OS_IS_FILE(m) ? (long long)(((unsigned long long)size_high << 32) | size_low) : 0;
  out->mtime_ns = filetime_ns(written);
  out->mtime = out->mtime_ns / 1000000000LL;
  out->ctime_ns = filetime_ns(created);
  out->dev = 0;
  out->ino = 0;
}

/* What a link points at, through a handle that follows it. */
static int stat_target(const wchar_t* w, RolltuiOsStat* out) {
  BY_HANDLE_FILE_INFORMATION fi;
  int ok;
  const HANDLE h = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    errno_from_last();
    return -1;
  }
  ok = GetFileInformationByHandle(h, &fi) != 0;
  if (!ok) errno_from_last();
  CloseHandle(h);
  if (!ok) return -1;
  describe(fi.dwFileAttributes, fi.ftCreationTime, fi.ftLastWriteTime, fi.nFileSizeHigh, fi.nFileSizeLow, 0, out);
  return 0;
}

/* The reparse tag of `w` itself, from its directory entry; 0 when it has none. */
static DWORD reparse_tag(const wchar_t* w) {
  WIN32_FIND_DATAW fd;
  const HANDLE h = FindFirstFileW(w, &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  FindClose(h);
  return (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? fd.dwReserved0 : 0;
}

/* One attribute read answers for everything but a link; a link is opened only to follow it. */
int rolltui_os_stat(const char* path, int follow_links, RolltuiOsStat* out) {
  wchar_t* w = wide(path);
  WIN32_FILE_ATTRIBUTE_DATA a;
  int rc = 0, link;
  if (!w) {
    errno = ENOENT;
    return -1;
  }
  if (!GetFileAttributesExW(w, GetFileExInfoStandard, &a)) {
    errno_from_last();
    rolltui_mem_free(w);
    return -1;
  }
  link = (a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && is_link(a.dwFileAttributes, reparse_tag(w));
  if (link && follow_links) rc = stat_target(w, out);
  else describe(a.dwFileAttributes, a.ftCreationTime, a.ftLastWriteTime, a.nFileSizeHigh, a.nFileSizeLow, link, out);
  rolltui_mem_free(w);
  return rc;
}

int rolltui_os_mkdir(const char* path) {
  wchar_t* w = wide(path);
  DWORD attrs;
  int ok;
  if (!w) return -1;
  ok = CreateDirectoryW(w, NULL) != 0;
  if (!ok && GetLastError() == ERROR_ALREADY_EXISTS) {
    attrs = GetFileAttributesW(w);
    ok = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
  } else if (!ok) {
    errno_from_last();
  }
  rolltui_mem_free(w);
  return ok ? 0 : -1;
}

int rolltui_os_rename_replace(const char* from, const char* to) {
  wchar_t* a = wide(from);
  wchar_t* b = wide(to);
  int ok = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  if (!ok) errno_from_last();
  rolltui_mem_free(a);
  rolltui_mem_free(b);
  return ok ? 0 : -1;
}

FILE* rolltui_os_fopen(const char* path, const char* mode) {
  wchar_t* w = wide(path);
  wchar_t* m = wide(mode);
  FILE* f = w && m ? _wfopen(w, m) : NULL;
  rolltui_mem_free(w);
  rolltui_mem_free(m);
  return f;
}

int rolltui_os_getcwd(char* buf, size_t cap) {
  wchar_t w[MAX_PATH * 4];
  const DWORD n = GetCurrentDirectoryW((DWORD)(sizeof w / sizeof *w), w);
  if (n == 0 || n >= sizeof w / sizeof *w) return -1;
  return narrow(w, buf, cap);
}

int rolltui_os_exe_path(char* buf, size_t cap) {
  wchar_t w[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(NULL, w, (DWORD)(sizeof w / sizeof *w));
  if (n == 0 || n >= sizeof w / sizeof *w) return -1;
  return narrow(w, buf, cap);
}

int rolltui_os_pid(void) { return (int)GetCurrentProcessId(); }

int rolltui_os_config_home(char* buf, size_t cap) {
  wchar_t w[MAX_PATH * 4];
  DWORD n = GetEnvironmentVariableW(L"APPDATA", w, (DWORD)(sizeof w / sizeof *w));
  if (n > 0 && n < sizeof w / sizeof *w) return narrow(w, buf, cap);
  n = GetEnvironmentVariableW(L"USERPROFILE", w, (DWORD)(sizeof w / sizeof *w));
  if (n > 0 && n + 16 < sizeof w / sizeof *w) {
    wcscat(w, L"\\AppData\\Roaming");
    return narrow(w, buf, cap);
  }
  return -1;
}

typedef LONG(WINAPI* RtlGetVersionFn)(OSVERSIONINFOW*);

int rolltui_os_release(char* buf, size_t cap) {
  OSVERSIONINFOW v;
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  RtlGetVersionFn get = ntdll ? (RtlGetVersionFn)(void (*)(void))GetProcAddress(ntdll, "RtlGetVersion") : NULL;
  if (!cap || !get) return -1;
  memset(&v, 0, sizeof v);
  v.dwOSVersionInfoSize = sizeof v;
  if (get(&v) != 0) return -1;
  snprintf(buf, cap, "%lu.%lu.%lu", (unsigned long)v.dwMajorVersion, (unsigned long)v.dwMinorVersion,
           (unsigned long)v.dwBuildNumber);
  return 0;
}

int rolltui_os_host_name(char* buf, size_t cap) {
  DWORD n = (DWORD)cap;
  if (!cap) return -1;
  return GetComputerNameExA(ComputerNameDnsHostname, buf, &n) ? 0 : -1;
}

int rolltui_os_open_read(const char* path) {
  wchar_t* w = wide(path);
  int fd;
  if (!w) return -1;
  fd = _wopen(w, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
  rolltui_mem_free(w);
  return fd;
}

int rolltui_os_fstat(int fd, RolltuiOsStat* out) {
  const HANDLE h = (HANDLE)_get_osfhandle(fd);
  BY_HANDLE_FILE_INFORMATION fi;
  if (h == INVALID_HANDLE_VALUE) return -1;
  if (GetFileType(h) != FILE_TYPE_DISK) { /* a pipe or a device: not a regular file */
    memset(out, 0, sizeof *out);
    return 0;
  }
  if (!GetFileInformationByHandle(h, &fi)) {
    errno_from_last();
    return -1;
  }
  describe(fi.dwFileAttributes, fi.ftCreationTime, fi.ftLastWriteTime, fi.nFileSizeHigh, fi.nFileSizeLow, 0, out);
  return 0;
}

long rolltui_os_read(int fd, char* buf, size_t cap) {
  const unsigned int n = cap > 0x7fffffffu ? 0x7fffffffu : (unsigned int)cap;
  return (long)_read(fd, buf, n);
}

long rolltui_os_pread(int fd, char* buf, size_t cap, long long offset) {
  if (_lseeki64(fd, offset, SEEK_SET) < 0) return -1;
  return rolltui_os_read(fd, buf, cap);
}

void rolltui_os_close(int fd) {
  if (fd >= 0) _close(fd);
}

struct RolltuiOsDir {
  HANDLE h;
  WIN32_FIND_DATAW data; /* the entry `next` last returned, as the listing described it */
  int first;
  char* path;            /* the directory, UTF-8, for following a link */
  char name[MAX_PATH * 4];
};

RolltuiOsDir* rolltui_os_dir_open(const char* path) {
  const size_t n = strlen(path);
  char* pattern = (char*)rolltui_mem_alloc(n + 3);
  wchar_t* w;
  RolltuiOsDir* d;
  memcpy(pattern, path, n);
  if (n && path[n - 1] != '/' && path[n - 1] != '\\') pattern[n] = '/', pattern[n + 1] = '*', pattern[n + 2] = '\0';
  else pattern[n] = '*', pattern[n + 1] = '\0';
  w = wide(pattern);
  rolltui_mem_free(pattern);
  if (!w) return NULL;
  d = (RolltuiOsDir*)rolltui_mem_alloc(sizeof *d);
  d->h = FindFirstFileW(w, &d->data);
  rolltui_mem_free(w);
  if (d->h == INVALID_HANDLE_VALUE) {
    errno_from_last();
    rolltui_mem_free(d);
    return NULL;
  }
  d->first = 1;
  d->path = (char*)rolltui_mem_alloc(n + 1);
  memcpy(d->path, path, n + 1);
  return d;
}

const char* rolltui_os_dir_next(RolltuiOsDir* d) {
  if (!d->first && !FindNextFileW(d->h, &d->data)) return NULL;
  d->first = 0;
  if (narrow(d->data.cFileName, d->name, sizeof d->name) != 0) d->name[0] = '\0';
  return d->name;
}

int rolltui_os_dir_stat(RolltuiOsDir* d, int follow_links, RolltuiOsStat* out) {
  const WIN32_FIND_DATAW* f = &d->data;
  const int link = is_link(f->dwFileAttributes, f->dwReserved0);
  if (link && follow_links) {
    const size_t pn = strlen(d->path), nn = strlen(d->name);
    const size_t sep = pn && d->path[pn - 1] != '/' && d->path[pn - 1] != '\\';
    char* full = (char*)rolltui_mem_alloc(pn + sep + nn + 1);
    wchar_t* w;
    int rc;
    memcpy(full, d->path, pn);
    if (sep) full[pn] = '/';
    memcpy(full + pn + sep, d->name, nn + 1);
    w = wide(full);
    rolltui_mem_free(full);
    if (!w) return -1;
    rc = stat_target(w, out);
    rolltui_mem_free(w);
    return rc;
  }
  describe(f->dwFileAttributes, f->ftCreationTime, f->ftLastWriteTime, f->nFileSizeHigh, f->nFileSizeLow, link, out);
  return 0;
}

int rolltui_os_dir_hidden(const RolltuiOsDir* d) { return (d->data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0; }

void rolltui_os_dir_close(RolltuiOsDir* d) {
  if (!d) return;
  FindClose(d->h);
  rolltui_mem_free(d->path);
  rolltui_mem_free(d);
}

/* ---- the terminal -------------------------------------------------------------------------- */

int rolltui_os_tty_is_console(const RolltuiOsTty* t) {
  (void)t;
  return 0; /* no Linux console here */
}

int rolltui_os_isatty(int fd) {
  DWORD mode;
  const HANDLE h = (HANDLE)_get_osfhandle(fd);
  return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode) != 0;
}

struct RolltuiOsTty {
  HANDLE in, out;
  int tty;
  int have_mode;
  DWORD saved_in, saved_out;
  UINT saved_in_cp, saved_out_cp;
  HANDLE wake;       /* an auto-reset event: `wake` sets it, `wait` sees it */
  WCHAR high;        /* the first half of a surrogate pair, waiting for the second */
  int resized;       /* a size change read from the input queue, not yet reported */
  char pending[4096]; /* input already taken from the console, as UTF-8, not yet read */
  size_t pending_n;
};

RolltuiOsTty* rolltui_os_tty_open(int in_fd, int out_fd) {
  RolltuiOsTty* t = (RolltuiOsTty*)rolltui_mem_alloc(sizeof *t);
  DWORD mode;
  memset(t, 0, sizeof *t);
  t->in = (HANDLE)_get_osfhandle(in_fd);
  t->out = (HANDLE)_get_osfhandle(out_fd);
  t->tty = t->out != INVALID_HANDLE_VALUE && GetConsoleMode(t->out, &mode) != 0 && t->in != INVALID_HANDLE_VALUE &&
           GetConsoleMode(t->in, &mode) != 0;
  t->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
  return t;
}

void rolltui_os_tty_close(RolltuiOsTty* t) {
  if (!t) return;
  if (t->wake) CloseHandle(t->wake);
  rolltui_mem_free(t);
}

int rolltui_os_tty_is_tty(const RolltuiOsTty* t) { return t->tty; }

/* The process-wide restore, which the console control handler reaches from another thread. */
static HANDLE volatile g_in = INVALID_HANDLE_VALUE;
static HANDLE volatile g_out = INVALID_HANDLE_VALUE;
static char g_leave[256];
static volatile LONG g_leave_len = 0;
static DWORD g_saved_in, g_saved_out;
static UINT g_saved_in_cp, g_saved_out_cp;
static volatile LONG g_have_mode = 0;
static volatile LONG g_handlers_installed = 0;

int rolltui_os_tty_raw(RolltuiOsTty* t) {
  DWORD in_mode, out_mode;
  if (!t->tty || !GetConsoleMode(t->in, &t->saved_in) || !GetConsoleMode(t->out, &t->saved_out)) return 0;
  t->saved_in_cp = GetConsoleCP();
  t->saved_out_cp = GetConsoleOutputCP();
  /* Raw, VT input: keys arrive as the same escape sequences every other terminal sends; Ctrl-C is a
   * byte, not a signal; a size change is a record in the queue. Quick edit is off, or a click would
   * select text instead of reaching the app. */
  in_mode = ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
  out_mode = t->saved_out | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
  SetConsoleMode(t->in, in_mode);
  SetConsoleMode(t->out, out_mode);
  SetConsoleCP(CP_UTF8);
  SetConsoleOutputCP(CP_UTF8);
  t->have_mode = 1;
  g_saved_in = t->saved_in;
  g_saved_out = t->saved_out;
  g_saved_in_cp = t->saved_in_cp;
  g_saved_out_cp = t->saved_out_cp;
  g_in = t->in;
  MemoryBarrier();
  InterlockedExchange(&g_have_mode, 1);
  return 1;
}

void rolltui_os_tty_restore(RolltuiOsTty* t) {
  if (!t->have_mode) return;
  SetConsoleMode(t->in, t->saved_in);
  SetConsoleMode(t->out, t->saved_out);
  SetConsoleCP(t->saved_in_cp);
  SetConsoleOutputCP(t->saved_out_cp);
}

int rolltui_os_tty_pixels(const RolltuiOsTty* t, int* xpix, int* ypix) {
  /* the console says nothing of its pixels */
  (void)t;
  (void)xpix;
  (void)ypix;
  return 0;
}

int rolltui_os_tty_size(const RolltuiOsTty* t, int* w, int* h) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!GetConsoleScreenBufferInfo(t->out, &info)) return 0;
  *w = info.srWindow.Right - info.srWindow.Left + 1;
  *h = info.srWindow.Bottom - info.srWindow.Top + 1;
  return *w > 0 && *h > 0;
}

static void write_all(HANDLE h, const char* p, size_t n) {
  while (n > 0) {
    DWORD done = 0;
    const DWORD chunk = n > 0x10000 ? 0x10000 : (DWORD)n;
    if (!WriteFile(h, p, chunk, &done, NULL) || done == 0) return;
    p += done;
    n -= done;
  }
}

void rolltui_os_tty_write(RolltuiOsTty* t, const char* bytes, size_t len) { write_all(t->out, bytes, len); }

/* One UTF-16 unit from a key record, as UTF-8 into `pending`. */
static void take_unit(RolltuiOsTty* t, WCHAR c) {
  WCHAR pair[2];
  int units = 1, n;
  char utf8[8];
  if (c >= 0xD800 && c <= 0xDBFF) {
    t->high = c;
    return;
  }
  if (c >= 0xDC00 && c <= 0xDFFF) {
    if (!t->high) return;
    pair[0] = t->high;
    pair[1] = c;
    units = 2;
    t->high = 0;
  } else {
    pair[0] = c;
  }
  n = WideCharToMultiByte(CP_UTF8, 0, pair, units, utf8, (int)sizeof utf8, NULL, NULL);
  if (n > 0 && t->pending_n + (size_t)n <= sizeof t->pending) {
    memcpy(t->pending + t->pending_n, utf8, (size_t)n);
    t->pending_n += (size_t)n;
  }
}

/* Everything in the console's input queue, taken: key text into `pending`, a size change into
 * `resized`. Never blocks. */
static void drain_console(RolltuiOsTty* t) {
  INPUT_RECORD recs[64];
  DWORD avail = 0, got = 0, i;
  while (GetNumberOfConsoleInputEvents(t->in, &avail) && avail > 0 && t->pending_n + 64 * 4 <= sizeof t->pending) {
    if (!ReadConsoleInputW(t->in, recs, avail < 64 ? avail : 64, &got) || got == 0) return;
    for (i = 0; i < got; ++i) {
      if (recs[i].EventType == KEY_EVENT) {
        const KEY_EVENT_RECORD* k = &recs[i].Event.KeyEvent;
        WORD r;
        if (!k->bKeyDown || k->uChar.UnicodeChar == 0) continue;
        for (r = 0; r < (k->wRepeatCount ? k->wRepeatCount : 1); ++r) take_unit(t, k->uChar.UnicodeChar);
      } else if (recs[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
        t->resized = 1;
      }
    }
  }
}

int rolltui_os_tty_wait(RolltuiOsTty* t, int timeout_ms, int with_wake) {
  const unsigned long long deadline = timeout_ms < 0 ? 0 : rolltui_os_monotonic_ms() + (unsigned long long)timeout_ms;
  for (;;) {
    HANDLE handles[2];
    DWORD count = 0, r, left;
    int out = 0;
    if (t->tty) drain_console(t);
    if (t->pending_n) out |= ROLLTUI_OS_TTY_INPUT;
    if (t->resized) {
      t->resized = 0;
      out |= ROLLTUI_OS_TTY_RESIZED;
    }
    if (out) return out;
    if (timeout_ms < 0) left = INFINITE;
    else {
      const unsigned long long now = rolltui_os_monotonic_ms();
      if (now >= deadline) return 0;
      left = (DWORD)(deadline - now);
    }
    if (!t->tty) { /* a pipe cannot be waited on: look for bytes every few milliseconds */
      DWORD bytes = 0;
      if (GetFileType(t->in) == FILE_TYPE_DISK) return ROLLTUI_OS_TTY_INPUT; /* a file is always ready */
      if (PeekNamedPipe(t->in, NULL, 0, NULL, &bytes, NULL) && bytes > 0) return ROLLTUI_OS_TTY_INPUT;
      if (with_wake && t->wake && WaitForSingleObject(t->wake, 0) == WAIT_OBJECT_0) return ROLLTUI_OS_TTY_WOKEN;
      Sleep(left == INFINITE || left > 10 ? 10 : left);
      continue;
    }
    handles[count++] = t->in;
    if (with_wake && t->wake) handles[count++] = t->wake;
    r = WaitForMultipleObjects(count, handles, FALSE, left);
    if (r == WAIT_TIMEOUT) return 0;
    if (r == WAIT_OBJECT_0 + 1) return ROLLTUI_OS_TTY_WOKEN;
    if (r != WAIT_OBJECT_0) return ROLLTUI_OS_TTY_ERROR;
    /* the console was signalled: drain it at the top; a record with no text waits on */
  }
}

long rolltui_os_tty_read(RolltuiOsTty* t, char* buf, size_t cap) {
  if (!t->tty) {
    DWORD got = 0;
    if (!ReadFile(t->in, buf, (DWORD)(cap > 0x10000 ? 0x10000 : cap), &got, NULL)) return -1;
    return (long)got;
  }
  if (t->pending_n == 0) drain_console(t);
  {
    const size_t n = t->pending_n < cap ? t->pending_n : cap;
    memcpy(buf, t->pending, n);
    memmove(t->pending, t->pending + n, t->pending_n - n);
    t->pending_n -= n;
    return (long)n;
  }
}

void rolltui_os_tty_wake(RolltuiOsTty* t) {
  if (t->wake) SetEvent(t->wake);
}

void rolltui_os_tty_flush_input(RolltuiOsTty* t) {
  t->pending_n = 0;
  if (t->in && t->in != INVALID_HANDLE_VALUE) FlushConsoleInputBuffer(t->in);
}

void rolltui_os_tty_publish(RolltuiOsTty* t, const char* leave, size_t len) {
  const size_t n = len < sizeof g_leave ? len : sizeof g_leave;
  memcpy(g_leave, leave, n);
  MemoryBarrier();
  InterlockedExchange(&g_leave_len, (LONG)n);
  g_out = t->out;
}

void rolltui_os_tty_withdraw(void) {
  g_out = INVALID_HANDLE_VALUE;
  InterlockedExchange(&g_have_mode, 0);
}

void rolltui_os_tty_restore_now(void) {
  const HANDLE out = g_out;
  if (out == INVALID_HANDLE_VALUE) return;
  write_all(out, g_leave, (size_t)g_leave_len);
  if (g_have_mode) {
    SetConsoleMode(g_in, g_saved_in);
    SetConsoleMode(out, g_saved_out);
    SetConsoleCP(g_saved_in_cp);
    SetConsoleOutputCP(g_saved_out_cp);
  }
  g_out = INVALID_HANDLE_VALUE;
}

static BOOL WINAPI on_console_event(DWORD kind) {
  (void)kind;
  rolltui_os_tty_restore_now();
  return FALSE; /* the default handler then ends the process */
}

/* A CRASH (an abort, an assert, an access violation) puts the console back too, then goes on to whatever handled it before. */
typedef void(__cdecl* AbortHandler)(int);
static AbortHandler g_abort_prev;
static LPTOP_LEVEL_EXCEPTION_FILTER g_filter_prev;

static void __cdecl on_abort(int sig) {
  rolltui_os_tty_restore_now();
  signal(sig, g_abort_prev ? g_abort_prev : SIG_DFL);
  raise(sig);
}

static LONG WINAPI on_unhandled(EXCEPTION_POINTERS* info) {
  rolltui_os_tty_restore_now();
  return g_filter_prev ? g_filter_prev(info) : EXCEPTION_CONTINUE_SEARCH;
}

int rolltui_os_tty_take_resumed(void) { return 0; } /* no job control */
int rolltui_os_stop_process(void) { return 0; }

void rolltui_os_tty_install_handlers(void) {
  if (InterlockedExchange(&g_handlers_installed, 1)) return;
  SetConsoleCtrlHandler(on_console_event, TRUE);
  g_abort_prev = signal(SIGABRT, on_abort);
  if (g_abort_prev == SIG_ERR) g_abort_prev = NULL;
  g_filter_prev = SetUnhandledExceptionFilter(on_unhandled);
  atexit(rolltui_os_tty_restore_now); /* a host's `exit` with the console still entered: nothing once it is freed */
}

/* A size change arrives in the input queue (ENABLE_WINDOW_INPUT), which `wait` already reads. */
void rolltui_os_tty_listen_resize(RolltuiOsTty* t) { (void)t; }

#endif /* _WIN32 */
