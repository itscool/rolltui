/* rolltui/c/rolltui_process_win32.c — other programs, on Windows: rolltui_os.h's processes, the shell's opener, the clipboard and the
 * console by name.
 *
 * A PROGRAM IS FOUND as the shell finds it: a path is itself, a bare name the first folder on PATH that has it with one of PATHEXT's
 * extensions. An .exe or a .com runs itself; anything else (a batch file, a script with an association) runs through cmd.exe, which
 * reads `%` and `"` even inside quotes, so an argument with either is refused rather than handed over changed. The program inherits
 * the handles it is given and no others.
 *
 * THE PSEUDO-TERMINAL MODE IS PIPES with COLUMNS and LINES set: ConPTY reorders a program's OSC records against its cells and turns
 * its line feeds into cursor moves, so what it gives back is not the stream the program wrote. */
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_str.h"

struct RolltuiOsChild {
  HANDLE process;
  HANDLE out; /* what the program writes, read here; NULL once it closed */
  HANDLE in;  /* what the program reads; NULL once closed */
  int mode;
  int exited, status;
};

/* ---- UTF-8 and UTF-16 ---------------------------------------------------------------------- */

static wchar_t* to_wide(const char* s, size_t n) {
  const int k = n ? MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0) : 0;
  wchar_t* w = (wchar_t*)rolltui_mem_alloc(((size_t)k + 1) * sizeof *w);
  if (k) MultiByteToWideChar(CP_UTF8, 0, s, (int)n, w, k);
  w[k] = 0;
  return w;
}

static void from_wide(const wchar_t* w, RolltuiStr* out) {
  const int k = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  rolltui_str_clear(out);
  if (k <= 1) return;
  {
    char* b = (char*)rolltui_mem_alloc((size_t)k);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, b, k, NULL, NULL);
    rolltui_str_set(out, b, (size_t)k - 1);
    rolltui_mem_free(b);
  }
}

static int ends_with_ci(const RolltuiStr* s, const char* tail) {
  const size_t n = strlen(tail);
  size_t i;
  if (s->n < n) return 0;
  for (i = 0; i < n; ++i) {
    char a = s->p[s->n - n + i], b = tail[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return 0;
  }
  return 1;
}

static int is_file(const RolltuiStr* p) {
  wchar_t* w = to_wide(p->p, p->n);
  const DWORD a = GetFileAttributesW(w);
  rolltui_mem_free(w);
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* `stem` as a program: itself when it has an extension PATHEXT lists and exists, else `stem` + the first of PATHEXT's that does. */
static int program_file(const RolltuiStr* stem, RolltuiStr* out) {
  const char* exts = getenv("PATHEXT");
  const char* at;
  if (!exts || !*exts) exts = ".COM;.EXE;.BAT;.CMD";
  for (at = exts;;) {
    const char* end = strchr(at, ';');
    const size_t n = end ? (size_t)(end - at) : strlen(at);
    if (n) {
      char ext[32];
      if (n < sizeof ext) {
        memcpy(ext, at, n);
        ext[n] = 0;
        if (ends_with_ci(stem, ext) && is_file(stem)) {
          rolltui_str_set(out, stem->p, stem->n);
          return 1;
        }
      }
    }
    if (!end) break;
    at = end + 1;
  }
  for (at = exts;;) {
    const char* end = strchr(at, ';');
    const size_t n = end ? (size_t)(end - at) : strlen(at);
    if (n) {
      rolltui_str_set(out, stem->p, stem->n);
      rolltui_str_append(out, at, n);
      if (is_file(out)) return 1;
    }
    if (!end) break;
    at = end + 1;
  }
  return 0;
}

static int resolve(const char* name, RolltuiStr* out) {
  RolltuiStr stem;
  const char* path = getenv("PATH");
  const char* at;
  int found = 0;
  memset(&stem, 0, sizeof stem);
  if (strchr(name, '/') || strchr(name, '\\') || (name[0] && name[1] == ':')) {
    rolltui_str_set(&stem, name, strlen(name));
    found = program_file(&stem, out);
    rolltui_str_free(&stem);
    return found;
  }
  for (at = path ? path : ""; !found;) {
    const char* end = strchr(at, ';');
    const size_t n = end ? (size_t)(end - at) : strlen(at);
    if (n) {
      rolltui_str_set(&stem, at, n);
      if (stem.p[stem.n - 1] != '\\' && stem.p[stem.n - 1] != '/') rolltui_str_append(&stem, "\\", 1);
      rolltui_str_append(&stem, name, strlen(name));
      found = program_file(&stem, out);
    }
    if (!end) break;
    at = end + 1;
  }
  rolltui_str_free(&stem);
  return found;
}

/* `arg` added to a command line the way the C runtime's parser reads it back. */
static void append_quoted(RolltuiStr* line, const char* arg, int always) {
  size_t slashes = 0, i, k;
  const size_t n = strlen(arg);
  int plain = !always && n > 0;
  if (line->n) rolltui_str_append(line, " ", 1);
  for (i = 0; i < n; ++i)
    if (arg[i] == ' ' || arg[i] == '\t' || arg[i] == '\n' || arg[i] == '\v' || arg[i] == '"') plain = 0;
  if (plain) {
    rolltui_str_append(line, arg, n);
    return;
  }
  rolltui_str_append(line, "\"", 1);
  for (i = 0; i < n; ++i) {
    if (arg[i] == '\\') {
      ++slashes;
      continue;
    }
    for (k = 0; k < (arg[i] == '"' ? slashes * 2 + 1 : slashes); ++k) rolltui_str_append(line, "\\", 1);
    slashes = 0;
    rolltui_str_append(line, &arg[i], 1);
  }
  for (k = 0; k < slashes * 2; ++k) rolltui_str_append(line, "\\", 1);
  rolltui_str_append(line, "\"", 1);
}

/* The application and the command line `argv` runs as; 0 with the reason when it cannot be. */
static int command_line(const char* const* argv, RolltuiStr* app, RolltuiStr* line, RolltuiStr* err) {
  RolltuiStr program;
  size_t i;
  memset(&program, 0, sizeof program);
  if (!resolve(argv[0], &program)) {
    if (err) {
      rolltui_str_set(err, "not found: ", 11);
      rolltui_str_append(err, argv[0], strlen(argv[0]));
    }
    rolltui_str_free(&program);
    return 0;
  }
  for (i = 0; i < program.n; ++i)
    if (program.p[i] == '/') program.p[i] = '\\';
  rolltui_str_clear(line);
  if (ends_with_ci(&program, ".exe") || ends_with_ci(&program, ".com")) {
    rolltui_str_set(app, program.p, program.n);
    append_quoted(line, program.p, 0);
    for (i = 1; argv[i]; ++i) append_quoted(line, argv[i], 0);
  } else {
    RolltuiStr inner;
    wchar_t sys[MAX_PATH + 1];
    const UINT sn = GetSystemDirectoryW(sys, MAX_PATH + 1);
    memset(&inner, 0, sizeof inner);
    append_quoted(&inner, program.p, 1);
    for (i = 1; argv[i]; ++i) {
      if (strchr(argv[i], '%') || strchr(argv[i], '"')) {
        if (err) rolltui_str_set(err, "an argument with % or \" cannot go through cmd.exe", 50);
        rolltui_str_free(&inner);
        rolltui_str_free(&program);
        return 0;
      }
      append_quoted(&inner, argv[i], 1);
    }
    if (sn > 0 && sn <= MAX_PATH) from_wide(sys, app);
    else rolltui_str_set(app, "C:\\Windows\\System32", 19);
    rolltui_str_append(app, "\\cmd.exe", 8);
    rolltui_str_append(line, "\"", 1);
    rolltui_str_append(line, app->p, app->n);
    rolltui_str_append(line, "\" /d /v:off /s /c \"", 19);
    rolltui_str_append(line, inner.p, inner.n);
    rolltui_str_append(line, "\"", 1);
    rolltui_str_free(&inner);
  }
  rolltui_str_free(&program);
  return 1;
}

/* This process's environment with `extra`'s "NAME=value" entries over it, as a block CreateProcessW takes. NULL: inherit. */
static wchar_t* env_block(const char* const* extra) {
  wchar_t* cur;
  const wchar_t* e;
  RolltuiStr block, item;
  size_t i, k = 0;
  wchar_t* w;
  if (!extra || !extra[0]) return NULL;
  while (extra[k]) ++k;
  memset(&block, 0, sizeof block);
  memset(&item, 0, sizeof item);
  cur = GetEnvironmentStringsW();
  for (e = cur; e && *e; e += wcslen(e) + 1) {
    int replaced = 0;
    from_wide(e, &item);
    for (i = 0; i < k && !replaced; ++i) {
      const char* eq = strchr(extra[i], '=');
      const size_t nn = eq ? (size_t)(eq - extra[i]) : strlen(extra[i]);
      if (item.n > nn && item.p[nn] == '=' && _strnicmp(item.p, extra[i], nn) == 0) replaced = 1;
    }
    if (!replaced) {
      rolltui_str_append(&block, item.p, item.n);
      rolltui_str_append(&block, "\0", 1);
    }
  }
  if (cur) FreeEnvironmentStringsW(cur);
  for (i = 0; i < k; ++i) {
    rolltui_str_append(&block, extra[i], strlen(extra[i]));
    rolltui_str_append(&block, "\0", 1);
  }
  rolltui_str_append(&block, "\0", 1);
  w = to_wide(block.p, block.n);
  rolltui_str_free(&block);
  rolltui_str_free(&item);
  return w;
}

static void say_last_error(RolltuiStr* err, const char* what) {
  char buf[96];
  if (!err) return;
  snprintf(buf, sizeof buf, "%s (Windows error %lu)", what, (unsigned long)GetLastError());
  rolltui_str_set(err, buf, strlen(buf));
}

void rolltui_os_child_refuse_pty(int refuse) { (void)refuse; } /* a PTY program has pipes here already */
RolltuiOsChild* rolltui_os_child_spawn(const char* const* argv, const char* cwd, const char* const* env, int mode, int cols,
                                       int rows, int tty_in, int tty_out, RolltuiStr* err) {
  RolltuiStr app, line;
  SECURITY_ATTRIBUTES sa;
  HANDLE child_in = INVALID_HANDLE_VALUE, child_out = INVALID_HANDLE_VALUE, mine_in = NULL, mine_out = NULL;
  PROCESS_INFORMATION pi;
  RolltuiOsChild* c = NULL;
  const char* extra[64];
  char size_cols[32], size_rows[32];
  wchar_t* block = NULL;
  int started = 0;
  (void)tty_in;
  (void)tty_out;
  if (err) rolltui_str_clear(err);
  if (!argv || !argv[0]) {
    if (err) rolltui_str_set(err, "nothing to run", 14);
    return NULL;
  }
  memset(&app, 0, sizeof app);
  memset(&line, 0, sizeof line);
  if (!command_line(argv, &app, &line, err)) {
    rolltui_str_free(&app);
    rolltui_str_free(&line);
    return NULL;
  }
  memset(&sa, 0, sizeof sa);
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;
  if (mode == ROLLTUI_OS_CHILD_PIPES || mode == ROLLTUI_OS_CHILD_PTY) {
    HANDLE r1 = NULL, w1 = NULL, r2 = NULL, w2 = NULL;
    if (!CreatePipe(&r1, &w1, &sa, 0) || !CreatePipe(&r2, &w2, &sa, 0)) {
      say_last_error(err, "cannot make a pipe");
      if (r1) CloseHandle(r1), CloseHandle(w1);
      rolltui_str_free(&app);
      rolltui_str_free(&line);
      return NULL;
    }
    SetHandleInformation(w1, HANDLE_FLAG_INHERIT, 0); /* this end is this process's */
    SetHandleInformation(r2, HANDLE_FLAG_INHERIT, 0);
    child_in = r1;
    mine_in = w1;
    mine_out = r2;
    child_out = w2;
  } else {
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    if (mode == ROLLTUI_OS_CHILD_TERMINAL) {
      child_in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, share, &sa, OPEN_EXISTING, 0, NULL);
      child_out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, share, &sa, OPEN_EXISTING, 0, NULL);
    } else {
      child_in = child_out = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, share, &sa, OPEN_EXISTING, 0, NULL);
    }
  }
  {
    size_t k = 0, i;
    if (env)
      for (i = 0; env[i] && k < 60; ++i) extra[k++] = env[i];
    if (mode == ROLLTUI_OS_CHILD_PTY) {
      snprintf(size_cols, sizeof size_cols, "COLUMNS=%d", cols > 0 ? cols : 80);
      snprintf(size_rows, sizeof size_rows, "LINES=%d", rows > 0 ? rows : 24);
      extra[k++] = size_cols;
      extra[k++] = size_rows;
    }
    extra[k] = NULL;
    block = env_block(extra);
  }
  memset(&pi, 0, sizeof pi);
  if (child_in != INVALID_HANDLE_VALUE && child_out != INVALID_HANDLE_VALUE) {
    HANDLE inherit[2];
    SIZE_T size = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST list;
    const DWORD inherit_n = child_in == child_out ? 1 : 2;
    inherit[0] = child_in;
    inherit[1] = child_out;
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    list = (LPPROC_THREAD_ATTRIBUTE_LIST)rolltui_mem_alloc(size ? size : 1);
    if (size && InitializeProcThreadAttributeList(list, 1, 0, &size)) {
      if (UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, inherit_n * sizeof(HANDLE), NULL, NULL)) {
        STARTUPINFOEXW si;
        wchar_t* wapp = to_wide(app.p, app.n);
        wchar_t* wline = to_wide(line.p, line.n);
        wchar_t* wcwd = cwd && *cwd ? to_wide(cwd, strlen(cwd)) : NULL;
        DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
        if (mode != ROLLTUI_OS_CHILD_TERMINAL) flags |= CREATE_NO_WINDOW; /* a program that does not take the console gets no window */
        if (mode == ROLLTUI_OS_CHILD_QUIET) flags |= CREATE_NEW_PROCESS_GROUP; /* Ctrl-C here never reaches it */
        memset(&si, 0, sizeof si);
        si.StartupInfo.cb = sizeof si;
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = child_in;
        si.StartupInfo.hStdOutput = child_out;
        si.StartupInfo.hStdError = child_out;
        si.lpAttributeList = list;
        started = CreateProcessW(wapp, wline, NULL, NULL, TRUE, flags, block, wcwd, &si.StartupInfo, &pi) != 0;
        if (!started) say_last_error(err, "cannot start the program");
        rolltui_mem_free(wapp);
        rolltui_mem_free(wline);
        rolltui_mem_free(wcwd);
      }
      DeleteProcThreadAttributeList(list);
    }
    rolltui_mem_free(list);
  } else {
    say_last_error(err, "cannot open the program's streams");
  }
  rolltui_mem_free(block);
  if (child_in != INVALID_HANDLE_VALUE) CloseHandle(child_in);
  if (child_out != INVALID_HANDLE_VALUE && child_out != child_in) CloseHandle(child_out);
  rolltui_str_free(&app);
  rolltui_str_free(&line);
  if (!started) {
    if (mine_in) CloseHandle(mine_in);
    if (mine_out) CloseHandle(mine_out);
    return NULL;
  }
  CloseHandle(pi.hThread);
  c = (RolltuiOsChild*)rolltui_mem_alloc(sizeof *c);
  memset(c, 0, sizeof *c);
  c->process = pi.hProcess;
  c->in = mine_in;
  c->out = mine_out;
  c->mode = mode;
  return c;
}

long rolltui_os_child_read(RolltuiOsChild* c, char* buf, size_t cap) {
  DWORD avail = 0, got = 0;
  if (!c || !c->out) return -1;
  if (cap == 0) return 0;
  if (!PeekNamedPipe(c->out, NULL, 0, NULL, &avail, NULL)) { /* the program closed its end */
    CloseHandle(c->out);
    c->out = NULL;
    return -1;
  }
  if (avail == 0) return 0;
  if (!ReadFile(c->out, buf, (DWORD)(cap < avail ? cap : avail), &got, NULL)) return -1;
  return (long)got;
}

int rolltui_os_child_write(RolltuiOsChild* c, const char* bytes, size_t len) {
  size_t done = 0;
  if (!c || !c->in) return 0;
  while (done < len) {
    DWORD w = 0;
    if (!WriteFile(c->in, bytes + done, (DWORD)(len - done), &w, NULL) || w == 0) break;
    done += w;
  }
  return done == len;
}

void rolltui_os_child_close_input(RolltuiOsChild* c) {
  if (!c || !c->in) return;
  CloseHandle(c->in);
  c->in = NULL;
}

void rolltui_os_child_resize(RolltuiOsChild* c, int cols, int rows) {
  /* COLUMNS and LINES were the program's at its start: a pipe has no size to change */
  (void)c;
  (void)cols;
  (void)rows;
}

int rolltui_os_child_done(RolltuiOsChild* c, int wait, int* status) {
  if (!c) return 1;
  if (!c->exited && WaitForSingleObject(c->process, wait ? INFINITE : 0) == WAIT_OBJECT_0) {
    DWORD code = 0;
    c->exited = 1;
    c->status = GetExitCodeProcess(c->process, &code) ? (int)code : -1;
  }
  if (c->exited && status) *status = c->status;
  return c->exited;
}

void rolltui_os_child_stop(RolltuiOsChild* c) {
  if (!c || c->exited) return;
  TerminateProcess(c->process, 1);
  rolltui_os_child_done(c, 1, NULL);
}

void rolltui_os_child_free(RolltuiOsChild* c) {
  if (!c) return;
  if (c->mode != ROLLTUI_OS_CHILD_QUIET) rolltui_os_child_stop(c);
  if (c->in) CloseHandle(c->in);
  if (c->out) CloseHandle(c->out);
  CloseHandle(c->process);
  rolltui_mem_free(c);
}

int rolltui_os_child_wait_terminal(const char* const* argv, const char* cwd, int tty_in, int tty_out, int* status,
                                   RolltuiStr* err) {
  RolltuiOsChild* c = rolltui_os_child_spawn(argv, cwd, NULL, ROLLTUI_OS_CHILD_TERMINAL, 0, 0, tty_in, tty_out, err);
  int st = -1;
  if (c) rolltui_os_child_done(c, 1, &st);
  if (status) *status = st;
  rolltui_os_child_free(c);
  return c != NULL;
}

/* ---- the shell's opener --------------------------------------------------------------------- */

int rolltui_os_open_with_system(const char* target, RolltuiStr* err) {
  size_t n = strlen(target), i;
  char* native = (char*)rolltui_mem_alloc(n + 1);
  wchar_t* w;
  HRESULT com;
  HINSTANCE r;
  memcpy(native, target, n + 1);
  if (strstr(native, "://") == NULL)
    for (i = 0; i < n; ++i)
      if (native[i] == '/') native[i] = '\\';
  w = to_wide(native, n);
  /* the shell may hand the file to a COM extension, so COM is set up for the call */
  com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  r = ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
  if (SUCCEEDED(com)) CoUninitialize();
  rolltui_mem_free(w);
  rolltui_mem_free(native);
  if ((INT_PTR)r > 32) return 1;
  if (err) {
    char buf[64];
    snprintf(buf, sizeof buf, "the shell could not open it (%d)", (int)(INT_PTR)r);
    rolltui_str_set(err, buf, strlen(buf));
  }
  return 0;
}

/* ---- the clipboard -------------------------------------------------------------------------- */

int rolltui_os_clipboard_read(RolltuiStr* out) {
  HANDLE h;
  int ok = 0;
  if (!OpenClipboard(NULL)) return 0;
  h = GetClipboardData(CF_UNICODETEXT);
  if (h) {
    const wchar_t* w = (const wchar_t*)GlobalLock(h);
    if (w) {
      const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
      if (n > 1) {
        char* b = (char*)rolltui_mem_alloc((size_t)n);
        size_t i, k = 0;
        WideCharToMultiByte(CP_UTF8, 0, w, -1, b, n, NULL, NULL);
        rolltui_str_clear(out);
        for (i = 0; i + 1 < (size_t)n; ++i) /* a line ends in \n here, as everywhere else in the library */
          if (!(b[i] == '\r' && b[i + 1] == '\n')) b[k++] = b[i];
        rolltui_str_set(out, b, k);
        rolltui_mem_free(b);
        ok = 1;
      }
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return ok;
}

int rolltui_os_clipboard_native(const char* text, size_t len) {
  const char* override = getenv("ROLLTUI_CLIPBOARD");
  wchar_t* w;
  size_t wn;
  HGLOBAL mem;
  void* dst;
  HWND owner;
  int open, tries, ok = 0;
  if (override && *override) {
    FILE* p = _popen(override, "wb");
    if (!p) return 0;
    fwrite(text, 1, len, p);
    return _pclose(p) == 0;
  }
  w = to_wide(text, len);
  wn = wcslen(w) + 1;
  mem = GlobalAlloc(GMEM_MOVEABLE, wn * sizeof(wchar_t));
  if (!mem) {
    rolltui_mem_free(w);
    return 0;
  }
  dst = GlobalLock(mem);
  if (!dst) {
    GlobalFree(mem);
    rolltui_mem_free(w);
    return 0;
  }
  memcpy(dst, w, wn * sizeof(wchar_t));
  GlobalUnlock(mem);
  rolltui_mem_free(w);
  /* UTF-16 text (CF_UNICODETEXT), owned by a message-only window made for the copy */
  owner = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
  open = OpenClipboard(owner) != 0;
  for (tries = 0; !open && tries < 10; ++tries) { /* another program may hold it for a moment */
    Sleep(10);
    open = OpenClipboard(owner) != 0;
  }
  if (open) {
    EmptyClipboard();
    ok = SetClipboardData(CF_UNICODETEXT, mem) != NULL;
    CloseClipboard();
  }
  if (!ok) GlobalFree(mem); /* once set, the clipboard owns it */
  if (owner) DestroyWindow(owner);
  return ok;
}

/* ---- the console ---------------------------------------------------------------------------- */

int rolltui_os_open_terminal(int* in_fd, int* out_fd) {
  const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
  const HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, share, NULL, OPEN_EXISTING, 0, NULL);
  const HANDLE out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, share, NULL, OPEN_EXISTING, 0, NULL);
  const int i = in == INVALID_HANDLE_VALUE ? -1 : _open_osfhandle((intptr_t)in, _O_RDONLY);
  const int o = out == INVALID_HANDLE_VALUE ? -1 : _open_osfhandle((intptr_t)out, 0);
  if (i >= 0 && o >= 0) {
    *in_fd = i;
    *out_fd = o;
    return 1;
  }
  if (i >= 0) _close(i);
  else if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
  if (o >= 0) _close(o);
  else if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
  return 0;
}

void rolltui_os_close_terminal(int in_fd, int out_fd) {
  if (in_fd >= 0) _close(in_fd);
  if (out_fd >= 0 && out_fd != in_fd) _close(out_fd);
}

#endif /* _WIN32 */
