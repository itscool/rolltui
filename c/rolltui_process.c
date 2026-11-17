/* rolltui/c/rolltui_process.c — other programs, run from here: the public half of rolltui_os.h's processes, the clipboard (OSC 52
 * and a native tool) and the controlling terminal. What differs by platform is in rolltui_process_posix.c and _win32.c. */
#include "rolltui/rolltui.h"

#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_str.h"

struct RolltuiChild {
  RolltuiOsChild* os;
  int closed; /* the program closed its output and all of it was read */
  int done, status;
};

/* How long a render waits while a program writes: what it wrote is drawn within this. */
#define CHILD_WAIT_MS 30

RolltuiChild* rolltui_child_spawn(const RolltuiChildOptions* o, RolltuiStr* err) {
  RolltuiOsChild* os;
  RolltuiChild* c;
  int mode;
  if (!o) return NULL;
  mode = o->mode == ROLLTUI_CHILD_PTY ? ROLLTUI_OS_CHILD_PTY : o->mode == ROLLTUI_CHILD_QUIET ? ROLLTUI_OS_CHILD_QUIET : ROLLTUI_OS_CHILD_PIPES;
  os = rolltui_os_child_spawn(o->argv, o->cwd, o->env, mode, o->cols, o->rows, -1, -1, err);
  if (!os) return NULL;
  c = (RolltuiChild*)rolltui_mem_alloc(sizeof *c);
  memset(c, 0, sizeof *c);
  c->os = os;
  c->closed = mode == ROLLTUI_OS_CHILD_QUIET; /* nothing to read from the null device */
  return c;
}

void rolltui_child_free(RolltuiChild* c) {
  if (!c) return;
  rolltui_os_child_free(c->os);
  rolltui_mem_free(c);
}

size_t rolltui_child_read(RolltuiChild* c, char* buf, size_t cap) {
  long n;
  if (!c || c->closed || !buf) return 0;
  n = rolltui_os_child_read(c->os, buf, cap);
  if (n < 0) {
    c->closed = 1;
    return 0;
  }
  return (size_t)n;
}

size_t rolltui_child_pump(RolltuiChild* c, RolltuiAnsiText* t, size_t max_lines) {
  char buf[4096];
  size_t total = 0;
  if (!c || !t) return 0;
  while (!c->closed && (max_lines == 0 || rolltui_ansi_line_count(t) < max_lines)) {
    const size_t n = rolltui_child_read(c, buf, sizeof buf);
    if (n == 0) break;
    rolltui_ansi_feed(t, buf, n);
    total += n;
  }
  return total;
}

int rolltui_child_drained(const RolltuiChild* c) { return !c || c->closed; }

int rolltui_child_write(RolltuiChild* c, const char* bytes, size_t len) {
  return c && bytes ? rolltui_os_child_write(c->os, bytes, len) : 0;
}

void rolltui_child_close_input(RolltuiChild* c) {
  if (c) rolltui_os_child_close_input(c->os);
}

void rolltui_child_resize(RolltuiChild* c, int cols, int rows) {
  if (c) rolltui_os_child_resize(c->os, cols, rows);
}

int rolltui_child_done(RolltuiChild* c, int* status) {
  int st = -1;
  if (!c) return 1;
  if (!c->done && rolltui_os_child_done(c->os, 0, &st)) {
    c->done = 1;
    c->status = st;
  }
  if (c->done && status) *status = c->status;
  return c->done;
}

int rolltui_child_wait(RolltuiChild* c, int* status) {
  int st = -1;
  if (!c) return 0;
  if (!c->done && rolltui_os_child_done(c->os, 1, &st)) {
    c->done = 1;
    c->status = st;
  }
  if (status) *status = c->status;
  return c->done;
}

void rolltui_child_stop(RolltuiChild* c) {
  if (c) rolltui_os_child_stop(c->os);
}

int rolltui_child_wait_ms(const RolltuiChild* c) {
  if (!c) return -1;
  if (c->closed && c->done) return -1;
  return CHILD_WAIT_MS;
}

int rolltui_open_with_system(const char* target, size_t len, RolltuiStr* err) {
  char* z;
  int ok;
  if (err) rolltui_str_clear(err);
  if (!target || len == 0) {
    if (err) rolltui_str_set(err, "nothing to open", sizeof "nothing to open" - 1);
    return 0;
  }
  z = (char*)rolltui_mem_alloc(len + 1);
  memcpy(z, target, len);
  z[len] = 0;
  ok = rolltui_os_open_with_system(z, err);
  rolltui_mem_free(z);
  return ok;
}

/* ---- the clipboard -------------------------------------------------------------------------- */

/* The most an OSC 52 payload may be: what xterm, kitty and the multiplexers take before they drop the sequence. */
#define OSC52_MAX 74994

static void base64(const char* text, size_t len, RolltuiStr* out) {
  static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i;
  for (i = 0; i + 2 < len; i += 3) {
    const unsigned v = ((unsigned)(unsigned char)text[i] << 16) | ((unsigned)(unsigned char)text[i + 1] << 8) | (unsigned char)text[i + 2];
    const char q[4] = {k[(v >> 18) & 63], k[(v >> 12) & 63], k[(v >> 6) & 63], k[v & 63]};
    rolltui_str_append(out, q, 4);
  }
  if (len - i == 1) {
    const unsigned v = (unsigned)(unsigned char)text[i] << 16;
    const char q[4] = {k[(v >> 18) & 63], k[(v >> 12) & 63], '=', '='};
    rolltui_str_append(out, q, 4);
  } else if (len - i == 2) {
    const unsigned v = ((unsigned)(unsigned char)text[i] << 16) | ((unsigned)(unsigned char)text[i + 1] << 8);
    const char q[4] = {k[(v >> 18) & 63], k[(v >> 12) & 63], k[(v >> 6) & 63], '='};
    rolltui_str_append(out, q, 4);
  }
}

#define SCREEN_CHUNK 512 /* GNU screen passes a DCS through a piece at a time; it drops one longer than its buffer */

int rolltui_clipboard_copy(RolltuiTerminal* t, const char* text, size_t len) {
  int sent = 0, native;
  const char* tmux = getenv("TMUX");
  const char* sty = getenv("STY");
  const int in_tmux = tmux && *tmux, in_screen = !in_tmux && sty && *sty;
  const char* kitty = getenv("KITTY_WINDOW_ID");
  /* kitty takes OSC 52 of any size (up to its own clipboard_max_size): drawn on directly, no cap of ours */
  const int uncapped = kitty && *kitty && !in_tmux && !in_screen;
  if (!text) len = 0;
  if (t) rolltui_terminal_remember_copy(t, text ? text : "", len);
  if (t && rolltui_terminal_is_tty(t) && (uncapped || ((len + 2) / 3) * 4 <= OSC52_MAX)) {
    RolltuiStr seq, b64;
    memset(&seq, 0, sizeof seq);
    memset(&b64, 0, sizeof b64);
    base64(text ? text : "", len, &b64);
    if (in_screen) { /* through GNU screen: in pieces, each its own DCS, which screen passes on as it comes */
      size_t at = 0;
      rolltui_str_append(&seq, "\x1bP\x1b]52;c;", sizeof "\x1bP\x1b]52;c;" - 1);
      while (at < b64.n) {
        const size_t n = b64.n - at < SCREEN_CHUNK ? b64.n - at : SCREEN_CHUNK;
        rolltui_str_append(&seq, b64.p + at, n);
        at += n;
        if (at < b64.n) rolltui_str_append(&seq, "\x1b\\\x1bP", sizeof "\x1b\\\x1bP" - 1);
      }
      rolltui_str_append(&seq, "\x07\x1b\\", sizeof "\x07\x1b\\" - 1);
    } else {
      /* inside tmux, passed through to the terminal tmux is drawn on: the DCS wrapper, its ESCs doubled */
      if (in_tmux) rolltui_str_append(&seq, "\x1bPtmux;\x1b", sizeof "\x1bPtmux;\x1b" - 1);
      rolltui_str_append(&seq, "\x1b]52;c;", sizeof "\x1b]52;c;" - 1);
      rolltui_str_append(&seq, b64.p, b64.n);
      rolltui_str_append(&seq, "\x07", 1);
      if (in_tmux) rolltui_str_append(&seq, "\x1b\\", sizeof "\x1b\\" - 1);
    }
    rolltui_terminal_write(t, seq.p, seq.n);
    rolltui_str_free(&seq);
    rolltui_str_free(&b64);
    sent = 1;
  }
  native = rolltui_os_clipboard_native(text ? text : "", len);
  return sent || native;
}

int rolltui_clipboard_paste(RolltuiTerminal* t, RolltuiStr* out) {
  if (!out) return 0;
  rolltui_str_clear(out);
  if (rolltui_os_clipboard_read(out)) return 1;
  return rolltui_terminal_last_copy(t, out); /* no tool could: what this program last copied */
}

/* ---- the controlling terminal --------------------------------------------------------------- */

int rolltui_open_terminal(int* in_fd, int* out_fd) {
  int i = -1, o = -1;
  if (!in_fd || !out_fd) return 0;
  if (!rolltui_os_open_terminal(&i, &o)) return 0;
  *in_fd = i;
  *out_fd = o;
  return 1;
}

void rolltui_close_terminal(int in_fd, int out_fd) { rolltui_os_close_terminal(in_fd, out_fd); }
