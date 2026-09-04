/* rolltui/c/rolltui_keys.c — the C side of the input decoder and the deliverability model.
 * See rolltui_keys.h for the boundary's rules and rolltui/Keys.hpp for the decoding and
 * protocol rules themselves; `KeysCpp.cpp` is the other implementation of the same
 * functions, and the byte tables in `rolltui/tests/keys_test.cpp` and the enumerated
 * round trip in `rolltui/tests/deliverability_test.cpp` are the oracle for both.
 *
 * Everything allocates through the closed set in `rolltui_alloc.h`. The only process-wide
 * state is the active protocol, which is a scalar and retains nothing. */
#include "rolltui/c/rolltui_keys.h"

#include <stdatomic.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"

/* ---- the decoder's own storage --------------------------------------------------------- */

/* GROWING, AMORTISED (rolltui_alloc.h strategy 2), one buffer per ROLE: `buf` is what has
 * arrived and not yet been decoded, `paste` is what a bracketed paste has accumulated. Two
 * roles, two buffers, so a paste in progress cannot be clobbered by the bytes that end it.
 * A decoder is OWNED, LONG-LIVED — `rolltui::KeyDecoder` holds exactly one and frees it. */
struct RolltuiKeyDecoder {
  char* buf;
  size_t buf_len, buf_cap;
  char* paste;
  size_t paste_len, paste_cap;
  int in_paste;
};

static void buf_append(char** p, size_t* len, size_t* cap, const char* bytes, size_t n) {
  if (n == 0) return;
  *p = (char*)rolltui_grow(*p, cap, *len + n, sizeof **p);
  memcpy(*p + *len, bytes, n);
  *len += n;
}

/* Drops the first `n` bytes. The capacity is KEPT — a decoder is fed a few bytes at a time
 * forever, and freeing the storage being reused is exactly what Phase 13 shipped four
 * times over. */
static void buf_drop_front(char* p, size_t* len, size_t n) {
  if (n >= *len) {
    *len = 0;
    return;
  }
  memmove(p, p + n, *len - n);
  *len -= n;
}

RolltuiKeyDecoder* rolltui_key_decoder_new(void) {
  RolltuiKeyDecoder* d = (RolltuiKeyDecoder*)rolltui_mem_alloc(sizeof(RolltuiKeyDecoder));
  memset(d, 0, sizeof *d);
  return d;
}

void rolltui_key_decoder_free(RolltuiKeyDecoder* d) {
  if (!d) return;
  rolltui_mem_free(d->buf);
  rolltui_mem_free(d->paste);
  rolltui_mem_free(d);
}

int rolltui_key_decoder_pending(const RolltuiKeyDecoder* d) { return d && d->buf_len > 0; }
int rolltui_key_decoder_in_paste(const RolltuiKeyDecoder* d) { return d && d->in_paste; }

/* ---- the events, built and handed over ------------------------------------------------- */

static RolltuiChord chord_key(unsigned char k, int ctrl, int alt, int shift) {
  RolltuiChord c;
  c.key = k;
  c.ch = 0;
  c.ctrl = (unsigned char)(ctrl != 0);
  c.alt = (unsigned char)(alt != 0);
  c.shift = (unsigned char)(shift != 0);
  return c;
}

static RolltuiChord chord_char(RolltuiCodepoint cp, int ctrl, int alt) {
  RolltuiChord c = chord_key(ROLLTUI_KEY_CHAR, ctrl, alt, 0);
  c.ch = cp;
  return c;
}

static void emit_key(RolltuiEventFn emit, void* ctx, RolltuiChord k, const char* text, size_t text_len) {
  RolltuiEvent e;
  memset(&e, 0, sizeof e);
  e.kind = ROLLTUI_EVENT_KEY;
  e.key = k;
  e.text = text;
  e.text_len = text_len;
  emit(ctx, &e);
}

static void emit_mouse(RolltuiEventFn emit, void* ctx, const RolltuiMouseEvent* m) {
  RolltuiEvent e;
  memset(&e, 0, sizeof e);
  e.kind = ROLLTUI_EVENT_MOUSE;
  e.mouse = *m;
  emit(ctx, &e);
}

static void emit_paste(RolltuiEventFn emit, void* ctx, const char* text, size_t len) {
  RolltuiEvent e;
  memset(&e, 0, sizeof e);
  e.kind = ROLLTUI_EVENT_PASTE;
  e.text = text;
  e.text_len = len;
  emit(ctx, &e);
}

/* ---- the decoding rules ----------------------------------------------------------------- */

/* True when the bytes at `pos` are the start of a UTF-8 sequence that is not yet complete
 * but could still become valid with more input. Anything else is decided now. */
static int could_complete(const char* s, size_t len, size_t pos) {
  const unsigned char lead = (unsigned char)s[pos];
  const size_t need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : (lead >= 0xC0) ? 2 : 1;
  size_t i;
  if (lead < 0xC2 || lead > 0xF4) return 0;
  if (len - pos >= need) return 0;
  for (i = pos + 1; i < len; ++i)
    if (((unsigned char)s[i] & 0xC0) != 0x80) return 0;
  return 1;
}

static void apply_modifier(RolltuiChord* e, int mod) { /* xterm: mod = 1 + (shift 1 | alt 2 | ctrl 4) */
  int bits;
  if (mod < 2) return;
  bits = mod - 1;
  e->shift = (unsigned char)((bits & 1) != 0);
  e->alt = (unsigned char)((bits & 2) != 0);
  e->ctrl = (unsigned char)((bits & 4) != 0);
}

/* One control byte (< 0x20 or 0x7F) as a key. 0x0A is Ctrl-J and not Enter — see Keys.hpp. */
static RolltuiChord control(unsigned char c) {
  switch (c) {
    case 0x0D: return chord_key(ROLLTUI_KEY_ENTER, 0, 0, 0);
    case 0x09: return chord_key(ROLLTUI_KEY_TAB, 0, 0, 0);
    case 0x7F:
    case 0x08: return chord_key(ROLLTUI_KEY_BACKSPACE, 0, 0, 0);
    case 0x1B: return chord_key(ROLLTUI_KEY_ESCAPE, 0, 0, 0);
    case 0x00: return chord_char(' ', 1, 0);
    case 0x1C: return chord_char('\\', 1, 0);
    case 0x1D: return chord_char(']', 1, 0);
    case 0x1E: return chord_char('^', 1, 0);
    case 0x1F: return chord_char('_', 1, 0);
    default: return chord_char((RolltuiCodepoint)('a' + c - 1), 1, 0); /* 0x01..0x1A */
  }
}

/* Parses "ESC [ params final"; returns the length consumed, 0 if incomplete, -1 if
 * malformed. */
static long csi_length(const char* s, size_t len) {
  size_t i = 2;
  while (i < len && s[i] >= 0x30 && s[i] <= 0x3F) ++i;
  while (i < len && s[i] >= 0x20 && s[i] <= 0x2F) ++i;
  if (i >= len) return 0;
  if (s[i] >= 0x40 && s[i] <= 0x7E) return (long)(i + 1);
  return -1;
}

/* CSI parameters, into a caller-sized array. A well-formed report never has more than
 * three that anything reads; anything past the cap is a sequence nothing decodes, and it
 * becomes an Unknown event carrying its own bytes rather than being mis-read.
 *
 * `atoi` stops at ':', which is exactly right for kitty's sub-parameters — see Keys.hpp. */
#define ROLLTUI_KEYS_MAX_PARAMS 8
static size_t csi_params(const char* p, size_t len, int* out) {
  size_t n = 0, start = 0;
  for (;;) {
    size_t i = start, end;
    int v = 0;
    while (i < len && p[i] != ';') ++i;
    end = i;
    /* Decimal digits only, stopping at anything else (':' above all). */
    for (i = start; i < end; ++i) {
      if (p[i] < '0' || p[i] > '9') break;
      v = v * 10 + (p[i] - '0');
    }
    if (n < ROLLTUI_KEYS_MAX_PARAMS) out[n] = v;
    ++n;
    if (end >= len) break;
    start = end + 1;
  }
  return n > ROLLTUI_KEYS_MAX_PARAMS ? ROLLTUI_KEYS_MAX_PARAMS : n;
}

/* One key of an ENHANCED report as a chord; 0 for a code we have no key for. */
static int enhanced_key(int code, int mod, RolltuiChord* out) {
  RolltuiChord e = chord_key(ROLLTUI_KEY_CHAR, 0, 0, 0);
  if (code <= 0) return 0;
  switch (code) {
    case 13: e.key = ROLLTUI_KEY_ENTER; break;
    case 9: e.key = ROLLTUI_KEY_TAB; break;
    case 127: e.key = ROLLTUI_KEY_BACKSPACE; break;
    case 27: e.key = ROLLTUI_KEY_ESCAPE; break;
    default:
      if (code >= 0xE000 || code > 0x10FFFF) return 0;
      e.key = ROLLTUI_KEY_CHAR;
      e.ch = (RolltuiCodepoint)code;
      break;
  }
  apply_modifier(&e, mod);
  *out = e;
  return 1;
}

static int decode_csi(const char* seq, size_t len, RolltuiEventFn emit, void* ctx) {
  const char final = seq[len - 1];
  const char* body = seq + 2;
  const size_t body_len = len - 3;
  int p[ROLLTUI_KEYS_MAX_PARAMS];
  size_t np;
  int mod;
  RolltuiChord e;

  if (body_len > 0 && body[0] == '<' && (final == 'M' || final == 'm')) { /* SGR mouse */
    RolltuiMouseEvent m;
    int b, low, motion;
    np = csi_params(body + 1, body_len - 1, p);
    if (np < 3) return 0;
    memset(&m, 0, sizeof m);
    b = p[0];
    m.x = p[1] - 1;
    m.y = p[2] - 1;
    m.shift = (unsigned char)((b & 4) != 0);
    m.alt = (unsigned char)((b & 8) != 0);
    m.ctrl = (unsigned char)((b & 16) != 0);
    low = b & 3;
    motion = b & 32;
    if (b & 64) {
      /* xterm buttons 4-7: 64 up, 65 down, 66 left, 67 right. Left/right are a trackpad's
       * sideways ticks and must never become vertical scrolling. */
      static const unsigned char wheel[4] = {4, 5, 6, 7};
      m.kind = wheel[low];
      m.button = 0;
    } else if (motion) {
      m.kind = (unsigned char)((low == 3) ? 3 : 2);
      m.button = (low == 3) ? 0 : low + 1;
    } else if (final == 'm' || low == 3) {
      m.kind = 1;
      m.button = (low == 3) ? 0 : low + 1;
    } else {
      m.kind = 0;
      m.button = low + 1;
    }
    emit_mouse(emit, ctx, &m);
    return 1;
  }

  np = csi_params(body, body_len, p);
  mod = np >= 2 ? p[1] : 1;
  /* The two ENHANCED forms (Phase 12 m3) — see Keys.hpp for both specs. */
  if (final == 'u' && np >= 1) {
    if (enhanced_key(p[0], mod, &e)) {
      emit_key(emit, ctx, e, NULL, 0);
      return 1;
    }
    return 0;
  }
  if (final == '~' && np >= 3 && p[0] == 27) {
    if (enhanced_key(p[2], p[1], &e)) {
      emit_key(emit, ctx, e, NULL, 0);
      return 1;
    }
    return 0;
  }

  e = chord_key(ROLLTUI_KEY_CHAR, 0, 0, 0);
  switch (final) {
    case 'A': e = chord_key(ROLLTUI_KEY_UP, 0, 0, 0); break;
    case 'B': e = chord_key(ROLLTUI_KEY_DOWN, 0, 0, 0); break;
    case 'C': e = chord_key(ROLLTUI_KEY_RIGHT, 0, 0, 0); break;
    case 'D': e = chord_key(ROLLTUI_KEY_LEFT, 0, 0, 0); break;
    case 'H': e = chord_key(ROLLTUI_KEY_HOME, 0, 0, 0); break;
    case 'F': e = chord_key(ROLLTUI_KEY_END, 0, 0, 0); break;
    case 'P': e = chord_key(ROLLTUI_KEY_F1, 0, 0, 0); break;
    case 'Q': e = chord_key(ROLLTUI_KEY_F1 + 1, 0, 0, 0); break;
    case 'R': e = chord_key(ROLLTUI_KEY_F1 + 2, 0, 0, 0); break;
    case 'S': e = chord_key(ROLLTUI_KEY_F1 + 3, 0, 0, 0); break;
    case 'Z': e = chord_key(ROLLTUI_KEY_TAB, 0, 0, 1); break;
    case '~': {
      const int n = np == 0 ? 0 : p[0];
      switch (n) {
        case 1:
        case 7: e = chord_key(ROLLTUI_KEY_HOME, 0, 0, 0); break;
        case 2: e = chord_key(ROLLTUI_KEY_INSERT, 0, 0, 0); break;
        case 3: e = chord_key(ROLLTUI_KEY_DELETE, 0, 0, 0); break;
        case 4:
        case 8: e = chord_key(ROLLTUI_KEY_END, 0, 0, 0); break;
        case 5: e = chord_key(ROLLTUI_KEY_PAGEUP, 0, 0, 0); break;
        case 6: e = chord_key(ROLLTUI_KEY_PAGEDOWN, 0, 0, 0); break;
        case 11: e = chord_key(ROLLTUI_KEY_F1, 0, 0, 0); break;
        case 12: e = chord_key(ROLLTUI_KEY_F1 + 1, 0, 0, 0); break;
        case 13: e = chord_key(ROLLTUI_KEY_F1 + 2, 0, 0, 0); break;
        case 14: e = chord_key(ROLLTUI_KEY_F1 + 3, 0, 0, 0); break;
        case 15: e = chord_key(ROLLTUI_KEY_F1 + 4, 0, 0, 0); break;
        case 17: e = chord_key(ROLLTUI_KEY_F1 + 5, 0, 0, 0); break;
        case 18: e = chord_key(ROLLTUI_KEY_F1 + 6, 0, 0, 0); break;
        case 19: e = chord_key(ROLLTUI_KEY_F1 + 7, 0, 0, 0); break;
        case 20: e = chord_key(ROLLTUI_KEY_F1 + 8, 0, 0, 0); break;
        case 21: e = chord_key(ROLLTUI_KEY_F1 + 9, 0, 0, 0); break;
        case 23: e = chord_key(ROLLTUI_KEY_F1 + 10, 0, 0, 0); break;
        case 24: e = chord_key(ROLLTUI_KEY_F12, 0, 0, 0); break;
        default: return 0;
      }
      break;
    }
    default: return 0;
  }
  apply_modifier(&e, mod);
  emit_key(emit, ctx, e, NULL, 0);
  return 1;
}

void rolltui_key_decoder_feed(RolltuiKeyDecoder* d, const char* bytes, size_t len, RolltuiEventFn emit, void* ctx) {
  buf_append(&d->buf, &d->buf_len, &d->buf_cap, bytes, len);
  for (;;) {
    unsigned char c0;
    char c1;
    if (d->buf_len == 0) break;
    if (d->in_paste) {
      /* The terminator, found by hand: the buffer is not NUL-terminated, so there is no
       * `strstr` to reach for and the search is the six bytes it is. */
      size_t end = (size_t)-1, i, k, keep = 0;
      for (i = 0; i + 6 <= d->buf_len; ++i)
        if (memcmp(d->buf + i, "\x1b[201~", 6) == 0) {
          end = i;
          break;
        }
      if (end == (size_t)-1) {
        /* Keep everything that could still be the start of the terminator. */
        for (k = 1; k < 6 && k <= d->buf_len; ++k)
          if (memcmp(d->buf + d->buf_len - k, "\x1b[201~", k) == 0) keep = k;
        buf_append(&d->paste, &d->paste_len, &d->paste_cap, d->buf, d->buf_len - keep);
        buf_drop_front(d->buf, &d->buf_len, d->buf_len - keep);
        break;
      }
      buf_append(&d->paste, &d->paste_len, &d->paste_cap, d->buf, end);
      buf_drop_front(d->buf, &d->buf_len, end + 6);
      emit_paste(emit, ctx, d->paste, d->paste_len);
      d->paste_len = 0;
      d->in_paste = 0;
      continue;
    }
    c0 = (unsigned char)d->buf[0];
    if (c0 != 0x1B) {
      RolltuiDecodedChar dc;
      if (c0 < 0x20 || c0 == 0x7F) {
        emit_key(emit, ctx, control(c0), NULL, 0);
        buf_drop_front(d->buf, &d->buf_len, 1);
        continue;
      }
      rolltui_u_decode_one(d->buf, d->buf_len, 0, &dc);
      /* A truncated multi-byte sequence at the end waits for more — but only if more could
       * ever complete it; a stray 0xFF is U+FFFD right now. */
      if (!dc.valid && could_complete(d->buf, d->buf_len, 0)) break;
      emit_key(emit, ctx, chord_char(dc.cp, 0, 0), NULL, 0);
      buf_drop_front(d->buf, &d->buf_len, dc.length);
      continue;
    }
    /* ESC ... */
    if (d->buf_len == 1) break; /* lone ESC: wait for more or flush() */
    c1 = d->buf[1];
    if (c1 == '[') {
      long n = csi_length(d->buf, d->buf_len);
      size_t seq_len;
      if (n == 0) break; /* incomplete */
      if (n < 0) {       /* malformed: report ESC as Escape and move on */
        emit_key(emit, ctx, chord_key(ROLLTUI_KEY_ESCAPE, 0, 0, 0), NULL, 0);
        buf_drop_front(d->buf, &d->buf_len, 1);
        continue;
      }
      seq_len = (size_t)n;
      if (seq_len == 6 && memcmp(d->buf, "\x1b[200~", 6) == 0) {
        buf_drop_front(d->buf, &d->buf_len, seq_len);
        d->in_paste = 1;
        continue;
      }
      if (seq_len == 6 && memcmp(d->buf, "\x1b[201~", 6) == 0) { /* stray terminator */
        buf_drop_front(d->buf, &d->buf_len, seq_len);
        continue;
      }
      if (!decode_csi(d->buf, seq_len, emit, ctx))
        emit_key(emit, ctx, chord_key(ROLLTUI_KEY_UNKNOWN, 0, 0, 0), d->buf, seq_len);
      /* The Unknown event's bytes were a BORROW of `buf` for exactly that emit call; the
       * window closes here, which is why the drop comes after it. */
      buf_drop_front(d->buf, &d->buf_len, seq_len);
      continue;
    }
    if (c1 == 'O') {
      RolltuiChord e;
      int known = 1;
      if (d->buf_len < 3) break;
      switch (d->buf[2]) {
        case 'A': e = chord_key(ROLLTUI_KEY_UP, 0, 0, 0); break;
        case 'B': e = chord_key(ROLLTUI_KEY_DOWN, 0, 0, 0); break;
        case 'C': e = chord_key(ROLLTUI_KEY_RIGHT, 0, 0, 0); break;
        case 'D': e = chord_key(ROLLTUI_KEY_LEFT, 0, 0, 0); break;
        case 'H': e = chord_key(ROLLTUI_KEY_HOME, 0, 0, 0); break;
        case 'F': e = chord_key(ROLLTUI_KEY_END, 0, 0, 0); break;
        case 'P': e = chord_key(ROLLTUI_KEY_F1, 0, 0, 0); break;
        case 'Q': e = chord_key(ROLLTUI_KEY_F1 + 1, 0, 0, 0); break;
        case 'R': e = chord_key(ROLLTUI_KEY_F1 + 2, 0, 0, 0); break;
        case 'S': e = chord_key(ROLLTUI_KEY_F1 + 3, 0, 0, 0); break;
        default:
          e = chord_key(ROLLTUI_KEY_UNKNOWN, 0, 0, 0);
          known = 0;
          break;
      }
      emit_key(emit, ctx, e, known ? NULL : d->buf, known ? 0 : 3);
      buf_drop_front(d->buf, &d->buf_len, 3);
      continue;
    }
    if ((unsigned char)c1 == 0x1B) { /* ESC ESC: an Escape, then decide the rest */
      emit_key(emit, ctx, chord_key(ROLLTUI_KEY_ESCAPE, 0, 0, 0), NULL, 0);
      buf_drop_front(d->buf, &d->buf_len, 1);
      continue;
    }
    /* Alt + key: ESC followed by one character (a control byte becomes Alt+Ctrl+x) */
    {
      const unsigned char c = (unsigned char)c1;
      RolltuiDecodedChar dc;
      RolltuiChord alt_key;
      if (c < 0x20 || c == 0x7F) {
        RolltuiChord e = control(c);
        e.alt = 1;
        emit_key(emit, ctx, e, NULL, 0);
        buf_drop_front(d->buf, &d->buf_len, 2);
        continue;
      }
      rolltui_u_decode_one(d->buf, d->buf_len, 1, &dc);
      if (!dc.valid && could_complete(d->buf, d->buf_len, 1)) break;
      /* ESC + an UPPERCASE letter is alt+shift+<letter> — see Keys.hpp for why it folds. */
      alt_key = chord_char(dc.cp, 0, 1);
      if (dc.cp >= 'A' && dc.cp <= 'Z') {
        alt_key.ch = dc.cp - 'A' + 'a';
        alt_key.shift = 1;
      }
      emit_key(emit, ctx, alt_key, NULL, 0);
      buf_drop_front(d->buf, &d->buf_len, 1 + dc.length);
    }
  }
}

void rolltui_key_decoder_flush(RolltuiKeyDecoder* d, RolltuiEventFn emit, void* ctx) {
  if (d->in_paste) { /* an unterminated paste: deliver what arrived */
    buf_append(&d->paste, &d->paste_len, &d->paste_cap, d->buf, d->buf_len);
    d->buf_len = 0;
    emit_paste(emit, ctx, d->paste, d->paste_len);
    d->paste_len = 0;
    d->in_paste = 0;
    return;
  }
  while (d->buf_len > 0) {
    if ((unsigned char)d->buf[0] == 0x1B) {
      emit_key(emit, ctx, chord_key(ROLLTUI_KEY_ESCAPE, 0, 0, 0), NULL, 0);
      buf_drop_front(d->buf, &d->buf_len, 1);
      /* What is left is re-fed, because it may hold whole events (ESC 'a' ESC 'b') and only
       * a feed knows that. Nothing is appended, and the ESC above is already gone, so every
       * turn of this loop consumes at least one byte and it terminates. */
      rolltui_key_decoder_feed(d, NULL, 0, emit, ctx);
      continue;
    }
    emit_key(emit, ctx, chord_char(0xFFFD, 0, 0), NULL, 0);
    buf_drop_front(d->buf, &d->buf_len, 1);
  }
}

/* ---- deliverability ---------------------------------------------------------------------- */

/* The active protocol. A plain scalar written once at startup and read from the UI thread;
 * `_Atomic` where the compiler has it, which every toolchain this builds on does. */
static _Atomic unsigned char g_protocol = ROLLTUI_PROTOCOL_LEGACY;

unsigned char rolltui_key_active_protocol(void) { return g_protocol; }
void rolltui_key_set_active_protocol(unsigned char p) { g_protocol = p; }

static int mod_bits(const RolltuiChord* k) { return (k->shift ? 1 : 0) | (k->alt ? 2 : 0) | (k->ctrl ? 4 : 0); }

/* The keys legacy already parameterises. */
static int is_functional(unsigned char k) {
  switch (k) {
    case ROLLTUI_KEY_CHAR:
    case ROLLTUI_KEY_ENTER:
    case ROLLTUI_KEY_TAB:
    case ROLLTUI_KEY_BACKSPACE:
    case ROLLTUI_KEY_ESCAPE:
    case ROLLTUI_KEY_UNKNOWN: return 0;
    default: return 1;
  }
}

/* A small unsigned decimal, appended. Every number these encodings carry is a modifier
 * (2..8) or a code point, so there is no negative case to handle. */
static size_t put_uint(char* out, unsigned v) {
  char tmp[12];
  size_t n = 0, i;
  do {
    tmp[n++] = (char)('0' + (v % 10));
    v /= 10;
  } while (v);
  for (i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
  return n;
}

/* nullopt (-1) for a key that is not functional. */
static long legacy_functional(const RolltuiChord* k, int bits, char* out) {
  static const struct {
    unsigned char key;
    char final;
  } kLetters[] = {{ROLLTUI_KEY_UP, 'A'},   {ROLLTUI_KEY_DOWN, 'B'},   {ROLLTUI_KEY_RIGHT, 'C'},
                  {ROLLTUI_KEY_LEFT, 'D'}, {ROLLTUI_KEY_HOME, 'H'},   {ROLLTUI_KEY_END, 'F'},
                  {ROLLTUI_KEY_F1, 'P'},   {ROLLTUI_KEY_F1 + 1, 'Q'}, {ROLLTUI_KEY_F1 + 2, 'R'},
                  {ROLLTUI_KEY_F1 + 3, 'S'}};
  static const struct {
    unsigned char key;
    int n;
  } kTildes[] = {{ROLLTUI_KEY_INSERT, 2},   {ROLLTUI_KEY_DELETE, 3},   {ROLLTUI_KEY_PAGEUP, 5},
                 {ROLLTUI_KEY_PAGEDOWN, 6}, {ROLLTUI_KEY_F1 + 4, 15},  {ROLLTUI_KEY_F1 + 5, 17},
                 {ROLLTUI_KEY_F1 + 6, 18},  {ROLLTUI_KEY_F1 + 7, 19},  {ROLLTUI_KEY_F1 + 8, 20},
                 {ROLLTUI_KEY_F1 + 9, 21},  {ROLLTUI_KEY_F1 + 10, 23}, {ROLLTUI_KEY_F12, 24}};
  size_t i, n = 0;
  for (i = 0; i < sizeof kLetters / sizeof *kLetters; ++i) {
    if (kLetters[i].key != k->key) continue;
    out[n++] = '\x1b';
    out[n++] = '[';
    if (bits != 0) {
      out[n++] = '1';
      out[n++] = ';';
      n += put_uint(out + n, (unsigned)(1 + bits));
    }
    out[n++] = kLetters[i].final;
    return (long)n;
  }
  for (i = 0; i < sizeof kTildes / sizeof *kTildes; ++i) {
    if (kTildes[i].key != k->key) continue;
    out[n++] = '\x1b';
    out[n++] = '[';
    n += put_uint(out + n, (unsigned)kTildes[i].n);
    if (bits != 0) {
      out[n++] = ';';
      n += put_uint(out + n, (unsigned)(1 + bits));
    }
    out[n++] = '~';
    return (long)n;
  }
  return -1;
}

/* The ASCII control code a terminal sends for ctrl+<this character>; -1 when there is none. */
static int ascii_control(RolltuiCodepoint ch) {
  if (ch >= 'a' && ch <= 'z') return (int)(ch - 'a' + 1);
  if (ch >= 'A' && ch <= 'Z') return (int)(ch - 'A' + 1);
  switch (ch) {
    case ' ':
    case '@': return 0x00;
    case '[': return 0x1b;
    case '\\': return 0x1c;
    case ']': return 0x1d;
    case '^': return 0x1e;
    case '_': return 0x1f;
    case '?': return 0x7f;
    default: return -1;
  }
}

/* The control codes that ALREADY belong to another key — see Keys.hpp for the six and why
 * ctrl+j is not among them. */
static int control_code_is_its_own(RolltuiCodepoint ch) {
  const RolltuiCodepoint lower = (ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch;
  switch (lower) {
    case 'h':
    case 'i':
    case 'm':
    case '[':
    case '?':
    case '@': return 0;
    default: return ascii_control(ch) >= 0;
  }
}

/* The unshifted code an enhanced report carries for a key. */
static int base_code(const RolltuiChord* k) {
  switch (k->key) {
    case ROLLTUI_KEY_ENTER: return 13;
    case ROLLTUI_KEY_TAB: return 9;
    case ROLLTUI_KEY_BACKSPACE: return 127;
    case ROLLTUI_KEY_ESCAPE: return 27;
    default: return (int)k->ch;
  }
}

static int is_letter(RolltuiCodepoint c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static RolltuiCodepoint transmitted_char(const RolltuiChord* k) {
  return (k->shift && k->ch >= 'a' && k->ch <= 'z') ? k->ch - 'a' + 'A' : k->ch;
}

static int starts_a_sequence(RolltuiCodepoint c) { return c == '[' || c == 'O'; }

/* Deliverable under LEGACY, for the keys legacy did not parameterise — the floor all three
 * protocols stand on. */
static int legacy_ok(const RolltuiChord* k) {
  const int bits = mod_bits(k);
  if (bits == 0) return 1;
  if (k->key == ROLLTUI_KEY_CHAR) {
    if (!k->ctrl && !k->alt) return 0; /* shift alone is the shifted character */
    if (k->ctrl) {
      if (k->shift) return 0; /* ctrl folds the letter to a control code; the shift is gone */
      return control_code_is_its_own(k->ch);
    }
    if (k->shift && !is_letter(k->ch)) return 0;
    return !starts_a_sequence(transmitted_char(k));
  }
  if (k->shift) return k->key == ROLLTUI_KEY_TAB && bits == 1; /* back-tab, and nothing else */
  switch (k->key) {
    case ROLLTUI_KEY_ENTER:
    case ROLLTUI_KEY_BACKSPACE: return bits == 2; /* alt only, as an ESC prefix */
    default: return 0;                            /* Tab and Escape carry nothing else */
  }
}

/* Legacy, for the keys legacy did NOT parameterise; also what the enhanced protocols send
 * for an unmodified key. -1 when no bytes exist to name. */
static long legacy_other(const RolltuiChord* k, int bits, char* out) {
  size_t n = 0;
  switch (k->key) {
    case ROLLTUI_KEY_TAB:
      if (bits == 0) { out[0] = '\t'; return 1; }
      if (bits == 1) { memcpy(out, "\x1b[Z", 3); return 3; } /* back-tab (CBT) */
      return -1;
    case ROLLTUI_KEY_ENTER:
      if (bits == 0) { out[0] = '\r'; return 1; }
      if (bits == 2) { memcpy(out, "\x1b\r", 2); return 2; }
      return -1;
    case ROLLTUI_KEY_BACKSPACE:
      if (bits == 0) { out[0] = '\x7f'; return 1; }
      if (bits == 2) { memcpy(out, "\x1b\x7f", 2); return 2; }
      return -1;
    case ROLLTUI_KEY_ESCAPE:
      if (bits == 0) { out[0] = '\x1b'; return 1; }
      return -1;
    case ROLLTUI_KEY_CHAR:
      /* The bytes as they really are, AMBIGUOUS ONES INCLUDED — see Keys.hpp. */
      if (k->shift && !is_letter(k->ch) && !k->ctrl) return -1;
      if (k->alt) out[n++] = '\x1b';
      if (k->ctrl) {
        const int c0 = ascii_control(k->ch);
        if (c0 < 0) return -1;
        out[n++] = (char)c0;
      } else {
        n += rolltui_u_append_utf8(transmitted_char(k), out + n);
      }
      return (long)n;
    default: return -1;
  }
}

long rolltui_key_encode(const RolltuiChord* k, unsigned char p, char* out, size_t cap) {
  long n;
  int bits;
  size_t m = 0;
  if (cap < ROLLTUI_KEY_ENCODE_MAX) return -1;
  if (k->key == ROLLTUI_KEY_UNKNOWN) return -1;
  bits = mod_bits(k);
  /* Functional keys are identical in all three protocols. */
  n = legacy_functional(k, bits, out);
  if (n >= 0) return n;

  if (p == ROLLTUI_PROTOCOL_KITTY) {
    if (k->key == ROLLTUI_KEY_ESCAPE) {
      memcpy(out, "\x1b[27;", 5);
      m = 5;
      m += put_uint(out + m, (unsigned)(1 + bits));
      out[m++] = 'u';
      return (long)m;
    }
    if (bits == 0) return legacy_other(k, bits, out);
    if (k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt) return -1; /* 'P' is text, not a chord */
    out[m++] = '\x1b';
    out[m++] = '[';
    m += put_uint(out + m, (unsigned)base_code(k));
    out[m++] = ';';
    m += put_uint(out + m, (unsigned)(1 + bits));
    out[m++] = 'u';
    return (long)m;
  }
  /* Legacy first in both remaining protocols — see Keys.hpp for xterm's mode 1. */
  if (legacy_ok(k)) return legacy_other(k, bits, out);
  if (p == ROLLTUI_PROTOCOL_LEGACY) return legacy_other(k, bits, out);
  if (k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt) return -1;
  if (k->shift) return -1; /* unstated by xterm's own docs — see Keys.hpp */
  out[m++] = '\x1b';
  out[m++] = '[';
  out[m++] = '2';
  out[m++] = '7';
  out[m++] = ';';
  m += put_uint(out + m, (unsigned)(1 + bits));
  out[m++] = ';';
  m += put_uint(out + m, (unsigned)base_code(k));
  out[m++] = '~';
  return (long)m;
}

int rolltui_key_deliverable(const RolltuiChord* k, unsigned char p) {
  if (k->key == ROLLTUI_KEY_UNKNOWN) return 0;
  if (is_functional(k->key)) return 1; /* legacy already parameterises these */
  if (legacy_ok(k)) return 1;          /* the floor all three protocols stand on */
  if (k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt) return 0;
  switch (p) {
    case ROLLTUI_PROTOCOL_KITTY: return 1;
    case ROLLTUI_PROTOCOL_MODIFY_OTHER_KEYS: return !k->shift;
    default: return 0;
  }
}

int rolltui_key_undeliverable_reason(const RolltuiChord* k, unsigned char p) {
  if (rolltui_key_deliverable(k, p)) return ROLLTUI_UNDELIVERABLE_NONE;
  if (k->key == ROLLTUI_KEY_UNKNOWN) return ROLLTUI_UNDELIVERABLE_NOT_A_KEY;
  if (k->key == ROLLTUI_KEY_CHAR && k->shift && !k->ctrl && !k->alt) return ROLLTUI_UNDELIVERABLE_SHIFT_ON_CHAR;
  if (p == ROLLTUI_PROTOCOL_LEGACY && rolltui_key_deliverable(k, ROLLTUI_PROTOCOL_MODIFY_OTHER_KEYS))
    return ROLLTUI_UNDELIVERABLE_NEEDS_ENHANCED;
  if (rolltui_key_deliverable(k, ROLLTUI_PROTOCOL_KITTY)) return ROLLTUI_UNDELIVERABLE_NEEDS_KITTY;
  return ROLLTUI_UNDELIVERABLE_NEVER;
}
