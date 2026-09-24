/* rolltui/c/rolltui_termfacts.c — see rolltui_termfacts.h. Pure: bytes and strings in, facts out,
 * plus one small file. The tty is `rolltui_terminal.c`'s. */
#include "rolltui/c/rolltui_termfacts.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_theme.h" /* rolltui_parse_osc11_reply */

/* ---- the environment ------------------------------------------------------------------------ */

static const char* nz(const char* s) { return s && *s ? s : NULL; }
static int eq(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }

void rolltui_termenv_read(RolltuiTermEnv* e) {
  struct utsname u;
  memset(e, 0, sizeof *e);
  e->colorterm = nz(getenv("COLORTERM"));
  e->term = nz(getenv("TERM"));
  e->term_program = nz(getenv("TERM_PROGRAM"));
  e->term_program_version = nz(getenv("TERM_PROGRAM_VERSION"));
  e->lc_terminal = nz(getenv("LC_TERMINAL"));
  e->lc_terminal_version = nz(getenv("LC_TERMINAL_VERSION"));
  e->colorfgbg = nz(getenv("COLORFGBG"));
  e->force_depth = nz(getenv("ROLL_COLOR_DEPTH"));
  e->force_wide = nz(getenv("ROLL_AMBIGUOUS_WIDE"));
  e->no_probe = nz(getenv("ROLL_TERM_PROBE")) && strcmp(getenv("ROLL_TERM_PROBE"), "0") == 0;
  e->tmux = nz(getenv("TMUX"));
  e->sty = nz(getenv("STY"));
  e->ssh_connection = nz(getenv("SSH_CONNECTION"));
  e->locale = nz(getenv("LC_ALL"));
  if (!e->locale) e->locale = nz(getenv("LC_CTYPE"));
  if (!e->locale) e->locale = nz(getenv("LANG"));
  if (uname(&u) == 0) snprintf(e->os_release, sizeof e->os_release, "%s", u.release);
  if (gethostname(e->host, sizeof e->host - 1) != 0) e->host[0] = '\0';
}

int rolltui_termenv_multiplexed(const RolltuiTermEnv* e) { return e->tmux != NULL || e->sty != NULL; }

/* Terminals that do 24-bit colour and name themselves in a way `TERM=...256color` does not
 * catch. Without this table `TERM=xterm-ghostty` with no COLORTERM — which is exactly what
 * ssh delivers, since it forwards TERM and not COLORTERM — lands on SIXTEEN colours, the wrong
 * answer in the safe direction and still a needless downgrade. Every entry is a terminal whose
 * own documentation says it draws 24-bit colour; a terminal that merely might is not here,
 * because over-detecting is the direction that paints nonsense. */
static int known_truecolor(const RolltuiTermEnv* e) {
  static const char* const kTerms[] = {"xterm-ghostty", "ghostty", "xterm-kitty", "wezterm", "alacritty",
                                       "alacritty-direct", "foot", "foot-direct", "foot-extra", "contour",
                                       "rio", "xterm-direct"};
  static const char* const kPrograms[] = {"ghostty", "WezTerm", "iTerm.app", "vscode", "WarpTerminal"};
  size_t i;
  for (i = 0; i < sizeof kTerms / sizeof kTerms[0]; ++i)
    if (eq(e->term, kTerms[i])) return 1;
  for (i = 0; i < sizeof kPrograms / sizeof kPrograms[0]; ++i)
    if (eq(e->term_program, kPrograms[i])) return 1;
  return eq(e->lc_terminal, "iTerm2");
}

/* THE ONE TERMINAL WE KNOW A VERSION BOUNDARY FOR. Apple's Terminal did not draw 24-bit colour
 * until macOS 26 (Terminal 2.15, build 470); before that it drew a parsed `38;2;r;g;b` as stray
 * attributes and basic colours — nonsense that "did not make sense" to the person who saw it. It
 * announces itself and its build in TERM_PROGRAM(_VERSION), and a COLORTERM that says otherwise is
 * not to be believed there: shell configuration exports `COLORTERM=truecolor` unconditionally
 * (it is common advice), and it follows the person into a terminal that cannot honour it. An
 * absent version is treated as an old one — the safe direction. */
#define ROLLTUI_APPLE_TERMINAL_24BIT_BUILD 470
int rolltui_termenv_no_24bit_known(const RolltuiTermEnv* e) {
  long build;
  if (!eq(e->term_program, "Apple_Terminal")) return 0;
  if (!e->term_program_version) return 1;
  build = strtol(e->term_program_version, NULL, 10);
  return build < ROLLTUI_APPLE_TERMINAL_24BIT_BUILD;
}

static int force_depth_of(const char* s) {
  int d;
  if (!s) return -1;
  d = rolltui_color_depth_from_name(s, strlen(s));
  if (d >= 0) return d;
  return strcmp(s, "24bit") == 0 ? ROLLTUI_DEPTH_TRUECOLOR : -1;
}

/* COLORFGBG is "fg;bg" or "fg;default;bg" and the LAST field is the background's palette index:
 * 0-6 and 8 are dark colours, 7 and 9-15 are light ones. -1 for anything else. */
static int mode_from_colorfgbg(const char* s) {
  const char* last = strrchr(s, ';');
  char* end;
  long bg;
  if (!last || !last[1]) return -1;
  bg = strtol(last + 1, &end, 10);
  if (end == last + 1 || bg < 0 || bg > 15) return -1;
  return (bg <= 6 || bg == 8) ? ROLLTUI_MODE_DARK : ROLLTUI_MODE_LIGHT;
}

void rolltui_termfacts_from_env(const RolltuiTermEnv* e, RolltuiTermFacts* f) {
  int forced, m;
  memset(f, 0, sizeof *f);
  f->keyboard = ROLLTUI_PROTOCOL_LEGACY;

  /* DEPTH */
  f->depth = rolltui_detect_color_depth(e->colorterm, e->term, NULL);
  f->depth_source = (e->colorterm || e->term) ? ROLLTUI_FACT_ENV : ROLLTUI_FACT_DEFAULT;
  if (f->depth < ROLLTUI_DEPTH_TRUECOLOR && known_truecolor(e)) f->depth = ROLLTUI_DEPTH_TRUECOLOR;
  if (rolltui_termenv_no_24bit_known(e) && f->depth > ROLLTUI_DEPTH_ANSI256) f->depth = ROLLTUI_DEPTH_ANSI256;
  forced = force_depth_of(e->force_depth);
  if (forced >= 0) {
    f->depth = (unsigned char)forced;
    f->depth_source = ROLLTUI_FACT_FORCED;
  }

  /* MODE: dark unless something says otherwise, because a light guess that is wrong is the one
   * a person notices. The terminal's own answer replaces it. */
  f->mode = ROLLTUI_MODE_DARK;
  f->mode_source = ROLLTUI_FACT_DEFAULT;
  if (e->colorfgbg && (m = mode_from_colorfgbg(e->colorfgbg)) >= 0) {
    f->mode = (unsigned char)m;
    f->mode_source = ROLLTUI_FACT_ENV;
  }

  /* AMBIGUOUS WIDTH: one cell unless the terminal says two. */
  if (e->force_wide) {
    f->ambiguous_wide = (unsigned char)(eq(e->force_wide, "1") || eq(e->force_wide, "true") || eq(e->force_wide, "yes") ||
                                        eq(e->force_wide, "wide"));
    f->ambiguous_source = ROLLTUI_FACT_FORCED;
  }

  /* NAME, for a person reading `dirktui check-terminal`. */
  if (e->term_program) {
    if (e->term_program_version) snprintf(f->name, sizeof f->name, "%s %s", e->term_program, e->term_program_version);
    else snprintf(f->name, sizeof f->name, "%s", e->term_program);
  } else if (e->term) {
    snprintf(f->name, sizeof f->name, "%s", e->term);
  }
}

size_t rolltui_termfacts_fingerprint(const RolltuiTermEnv* e, const char* app_key, char* out, size_t cap) {
  /* The ssh CLIENT's address, not the whole variable: the ports change every connection and would
   * make every session a stranger. */
  char client[64];
  size_t i = 0;
  const char* mux = e->tmux ? "tmux" : (e->sty ? "screen" : "");
  int n;
  client[0] = '\0';
  if (e->ssh_connection) {
    while (e->ssh_connection[i] && e->ssh_connection[i] != ' ' && i + 1 < sizeof client) {
      client[i] = e->ssh_connection[i];
      ++i;
    }
    client[i] = '\0';
  }
  /* "rt1" is the SCHEMA of what is asked and remembered: a release that asks a new question
   * bumps it and every earlier answer is a stranger. */
  n = snprintf(out, cap, "rt1|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s", app_key ? app_key : "", e->os_release,
               e->term ? e->term : "", e->term_program ? e->term_program : "",
               e->term_program_version ? e->term_program_version : "", e->colorterm ? e->colorterm : "",
               e->lc_terminal ? e->lc_terminal : "", e->lc_terminal_version ? e->lc_terminal_version : "", client, mux,
               e->locale ? e->locale : "", e->host);
  if (n < 0) {
    if (cap) out[0] = '\0';
    return 0;
  }
  return (size_t)n < cap ? (size_t)n : (cap ? cap - 1 : 0);
}

size_t rolltui_termfacts_exe_identity(char* out, size_t cap) {
  char path[1024];
  struct stat st;
  const char* base;
  int n;
  if (!cap) return 0;
  out[0] = '\0';
#ifdef __APPLE__
  {
    uint32_t size = (uint32_t)sizeof path;
    if (_NSGetExecutablePath(path, &size) != 0) return 0;
  }
#else
  {
    const ssize_t k = readlink("/proc/self/exe", path, sizeof path - 1);
    if (k <= 0) return 0;
    path[k] = '\0';
  }
#endif
  if (stat(path, &st) != 0) return 0; /* follows a symlink: it is the FILE that was rebuilt */
  base = strrchr(path, '/');
  base = base ? base + 1 : path;
  n = snprintf(out, cap, "%s:%lld:%lld", base, (long long)st.st_mtime, (long long)st.st_size);
  if (n < 0) {
    out[0] = '\0';
    return 0;
  }
  return (size_t)n < cap ? (size_t)n : cap - 1;
}

/* ---- the ceiling ----------------------------------------------------------------------------- */

/* One process, one terminal drawn to at a time — the same scalar-per-process shape as the active
 * key protocol in rolltui_keys.c, for the same reason: a `present` deep in a host's loop has no
 * terminal to be handed, and a signal or a library-internal caller cannot be given one. */
static _Atomic int g_active_depth = -1;
static _Atomic int g_active_forced = 0;

void rolltui_termfacts_set_active(int depth, int forced) {
  g_active_forced = forced ? 1 : 0;
  g_active_depth = depth;
}

static _Atomic int g_active_wide = -1;
static _Atomic int g_active_mode = -1;

void rolltui_termfacts_set_active_wide(int wide) { g_active_wide = wide < 0 ? -1 : (wide ? 1 : 0); }
int rolltui_termfacts_active_wide(void) { return g_active_wide == 1; }
void rolltui_termfacts_set_active_mode(int mode) { g_active_mode = (mode >= 0 && mode < ROLLTUI_MODE_COUNT) ? mode : -1; }
int rolltui_termfacts_active_mode(void) { return g_active_mode; }

unsigned char rolltui_termfacts_clamp_depth(unsigned char requested) {
  const int active = g_active_depth;
  if (active < 0 || active >= ROLLTUI_DEPTH_COUNT) return requested;
  if (g_active_forced) return (unsigned char)active;
  return requested < (unsigned char)active ? requested : (unsigned char)active;
}

/* ---- the questions -------------------------------------------------------------------------- */

size_t rolltui_termprobe_queries(unsigned want, char* out, size_t cap) {
  size_t n = 0;
#define ROLLTUI_TERMQ_ADD(lit)                                  \
  do {                                                          \
    const size_t k_ = sizeof(lit) - 1;                          \
    if (n + k_ + 1 <= cap) {                                    \
      memcpy(out + n, lit, k_);                                 \
      n += k_;                                                  \
    }                                                           \
  } while (0)
  if (want & ROLLTUI_TERMQ_KEYBOARD) ROLLTUI_TERMQ_ADD("\x1b[?u"  /* kitty: which enhancement flags are set? */
                                                       "\x1b[?4m"); /* xterm: what is modifyOtherKeys? */
  if (want & ROLLTUI_TERMQ_BACKGROUND) ROLLTUI_TERMQ_ADD("\x1b]11;?\x1b\\"); /* OSC 11 */
  /* A 24-bit background, ASKED BACK (DECRQSS SGR), and reset. A terminal that took the colour as
   * 24-bit says so in its answer; one that reduced it says a palette index; one that does not
   * implement DECRQSS says nothing, which is "unknown" and never "no". */
  if (want & ROLLTUI_TERMQ_SGR) {
    /* The colour is built by the colour engine like every other one, and is arbitrary: all that
     * matters is that it is a 24-bit colour and not a palette one. */
    RolltuiStyle st;
    char sgr[ROLLTUI_SGR_MAX];
    size_t k;
    memset(&st, 0, sizeof st);
    st.bg.kind = 2; /* Rgb */
    st.bg.r = st.bg.g = st.bg.b = 1;
    k = rolltui_sgr(&st, ROLLTUI_DEPTH_TRUECOLOR, sgr, sizeof sgr);
    if (n + k + 1 <= cap) {
      memcpy(out + n, sgr, k);
      n += k;
    }
    ROLLTUI_TERMQ_ADD("\x1bP$qm\x1b\\" "\x1b[0m");
  }
  if (want & ROLLTUI_TERMQ_VERSION) ROLLTUI_TERMQ_ADD("\x1b[>0q"); /* XTVERSION */
  /* DECSC, home, one ambiguous glyph (U+2588), CPR, home, erase the line, DECRC — the same
   * exchange `rolltui_terminal_query_ambiguous_wide` makes, now in the same round trip. */
  if (want & ROLLTUI_TERMQ_WIDTH)
    ROLLTUI_TERMQ_ADD("\x1b" "7" "\x1b[1;1H" "\xE2\x96\x88" "\x1b[6n" "\x1b[1;1H" "\x1b[K" "\x1b" "8");
  ROLLTUI_TERMQ_ADD("\x1b[c"); /* Primary DA: the terminator every terminal answers */
#undef ROLLTUI_TERMQ_ADD
  if (n < cap) out[n] = '\0';
  return n;
}

/* ---- the answers ---------------------------------------------------------------------------- */

static int is_param(unsigned char c) { return c >= 0x30 && c <= 0x3F; }
static int is_inter(unsigned char c) { return c >= 0x20 && c <= 0x2F; }

/* A number in `s[from, to)`, or -1 when it is not all digits. */
static int digits_at(const char* s, size_t from, size_t to) {
  int v = 0;
  size_t i;
  if (from >= to) return -1;
  for (i = from; i < to; ++i) {
    if (s[i] < '0' || s[i] > '9') return -1;
    v = v * 10 + (s[i] - '0');
    if (v > 100000) return -1;
  }
  return v;
}

/* One CSI reply at p[0..n), or 0. Sets *incomplete when the bytes end before the sequence does. */
static size_t take_csi(const char* p, size_t n, int expect_cpr, RolltuiTermReplies* r, int* incomplete) {
  size_t i = 2, params_end, total;
  unsigned char fin;
  char lead = 0;
  int has_dollar = 0;
  while (i < n && is_param((unsigned char)p[i])) ++i;
  params_end = i;
  while (i < n && is_inter((unsigned char)p[i])) {
    if (p[i] == '$') has_dollar = 1;
    ++i;
  }
  if (i >= n) {
    *incomplete = 1;
    return 0;
  }
  fin = (unsigned char)p[i];
  if (fin < 0x40 || fin > 0x7E) return 0;
  total = i + 1;
  if (params_end > 2 && (p[2] == '?' || p[2] == '>' || p[2] == '<' || p[2] == '=')) lead = p[2];

  if (lead == '?' && fin == 'c') { /* Primary DA — the terminator */
    r->da1 = 1;
    return total;
  }
  if (lead == '>' && fin == 'c') return total; /* Secondary DA: swallowed, never a key */
  if (lead == '?' && fin == 'u') {              /* kitty: CSI ? flags u */
    r->kitty = 1;
    r->key_last = 1;
    return total;
  }
  if (lead == '>' && fin == 'm') { /* xterm: CSI > 4 ; v m */
    r->modkeys = 1;
    r->key_last = 2;
    return total;
  }
  if (lead == '?' && fin == 'y' && has_dollar) return total; /* DECRPM: swallowed */
  if (lead == 0 && fin == 'R' && expect_cpr && !has_dollar) { /* CSI row ; col R */
    size_t semi = 0, k;
    for (k = 2; k < params_end; ++k)
      if (p[k] == ';') {
        semi = k;
        break;
      }
    if (semi) {
      const int row = digits_at(p, 2, semi), col = digits_at(p, semi + 1, params_end);
      if (row >= 1 && col >= 1) {
        r->cpr = 1;
        r->cpr_row = row;
        r->cpr_col = col;
        return total;
      }
    }
  }
  return 0;
}

static int sgr_reply_kind(const char* s, size_t n) {
  size_t i;
  int palette = 0;
  for (i = 0; i + 4 <= n; ++i) {
    if (s[i] == '4' && s[i + 1] == '8' && (s[i + 2] == ':' || s[i + 2] == ';')) {
      if (s[i + 3] == '2') return ROLLTUI_TERMR_SGR_TRUE;
      if (s[i + 3] == '5') palette = 1;
    }
  }
  return palette ? ROLLTUI_TERMR_SGR_256 : ROLLTUI_TERMR_SGR_NONE;
}

/* One OSC or DCS string reply at p[0..n), or 0. Its terminator is ST (ESC \), or BEL for an OSC.
 * Refuses (returns 0) an `ESC ]` not followed by a digit and an `ESC P` not followed by a
 * parameter byte: those are a key — Alt-] and Alt-Shift-P — and nothing a terminal ever sends. */
static size_t take_string(const char* p, size_t n, int osc, RolltuiTermReplies* r, int* incomplete) {
  enum { kMax = 4096 };
  size_t i, end = 0, term_len = 0, limit = n < kMax ? n : (size_t)kMax;
  if (n < 3) {
    *incomplete = 1;
    return 0;
  }
  if (osc) {
    if (!isdigit((unsigned char)p[2])) return 0;
  } else if (!p[2] || !strchr("0123456789>+=<$", p[2])) {
    return 0;
  }
  for (i = 2; i < limit; ++i) {
    if (osc && p[i] == '\a') {
      end = i + 1;
      term_len = 1;
      break;
    }
    if (p[i] == '\x1b') {
      if (i + 1 >= n) {
        *incomplete = 1;
        return 0;
      }
      if (p[i + 1] == '\\') {
        end = i + 2;
        term_len = 2;
        break;
      }
    }
  }
  if (!end) {
    if (n >= kMax) return (size_t)kMax; /* longer than any reply: drop that much rather than wait for ever */
    *incomplete = 1;
    return 0;
  }
  {
    const char* payload = p + 2;
    const size_t plen = end - 2 - term_len;
    if (osc) {
      if (plen >= 3 && memcmp(payload, "11;", 3) == 0) {
        RolltuiStyleColorRaw c;
        if (rolltui_parse_osc11_reply(p, end, &c)) {
          r->has_bg = 1;
          r->bg = c;
        }
      }
    } else if (plen >= 3 && memcmp(payload, "1$r", 3) == 0) {
      r->sgr = sgr_reply_kind(payload + 3, plen - 3);
    } else if (plen >= 2 && payload[0] == '>' && payload[1] == '|') {
      size_t k, m = 0;
      for (k = 2; k < plen && m + 1 < sizeof r->version; ++k)
        if ((unsigned char)payload[k] >= 0x20 && (unsigned char)payload[k] < 0x7F) r->version[m++] = payload[k];
      r->version[m] = '\0';
    }
  }
  return end;
}

size_t rolltui_termreplies_take(char* buf, size_t len, int expect_cpr, RolltuiTermReplies* out) {
  size_t in = 0, kept = 0;
  while (in < len) {
    size_t n = 0;
    int incomplete = 0;
    if (buf[in] == '\x1b' && in + 1 < len) {
      const char c1 = buf[in + 1];
      if (c1 == '[') n = take_csi(buf + in, len - in, expect_cpr, out, &incomplete);
      else if (c1 == ']') n = take_string(buf + in, len - in, 1, out, &incomplete);
      else if (c1 == 'P') n = take_string(buf + in, len - in, 0, out, &incomplete);
    }
    if (n) {
      in += n;
      continue;
    }
    if (incomplete) { /* keep the unfinished reply, and everything after it, for the next scan */
      memmove(buf + kept, buf + in, len - in);
      return kept + (len - in);
    }
    buf[kept++] = buf[in++];
  }
  return kept;
}

void rolltui_termfacts_apply_replies(RolltuiTermFacts* f, const RolltuiTermReplies* r, unsigned asked) {
  if (r->da1) f->responsive = 1;
  if ((asked & ROLLTUI_TERMQ_BACKGROUND) && r->has_bg) {
    f->background = r->bg;
    f->has_background = 1;
    f->mode = rolltui_mode_for_background(r->bg);
    f->mode_source = ROLLTUI_FACT_PROBE;
  }
  if ((asked & ROLLTUI_TERMQ_WIDTH) && r->cpr && r->cpr_col >= 2 && f->ambiguous_source != ROLLTUI_FACT_FORCED) {
    f->ambiguous_wide = r->cpr_col >= 3;
    f->ambiguous_source = ROLLTUI_FACT_PROBE;
  }
  if ((asked & ROLLTUI_TERMQ_SGR) && f->depth_source != ROLLTUI_FACT_FORCED) {
    if (r->sgr == ROLLTUI_TERMR_SGR_TRUE) {
      f->depth = ROLLTUI_DEPTH_TRUECOLOR;
      f->depth_source = ROLLTUI_FACT_PROBE;
    } else if (r->sgr == ROLLTUI_TERMR_SGR_256) {
      /* It took our 24-bit colour and answered with a palette index: it parses the sequence, so
       * it is at least a 256-colour terminal, and it is not a 24-bit one whatever the environment
       * said. Reduce a lie; raise a sixteen. */
      if (f->depth == ROLLTUI_DEPTH_TRUECOLOR || f->depth == ROLLTUI_DEPTH_ANSI16) f->depth = ROLLTUI_DEPTH_ANSI256;
      f->depth_source = ROLLTUI_FACT_PROBE;
    }
  }
  if ((asked & ROLLTUI_TERMQ_VERSION) && r->version[0]) snprintf(f->name, sizeof f->name, "%s", r->version);
}

/* ---- remembered answers --------------------------------------------------------------------- */

#define TERMCACHE_MAX_ENTRIES 16
#define TERMCACHE_MAX_BYTES (1024 * 1024)
#define TERMCACHE_FILE "terminal-facts.json"

void rolltui_termcache_path(const char* dir, RolltuiStr* out) {
  const char* d;
  rolltui_str_clear(out);
  if (dir && *dir) {
    rolltui_str_append(out, dir, strlen(dir));
  } else if ((d = nz(getenv("ROLL_CONFIG_DIR")))) {
    rolltui_str_append(out, d, strlen(d));
    rolltui_str_append(out, "/rolltui", 8);
  } else if ((d = nz(getenv("XDG_CONFIG_HOME")))) {
    rolltui_str_append(out, d, strlen(d));
    rolltui_str_append(out, "/roll/rolltui", 13);
  } else if ((d = nz(getenv("HOME")))) {
    rolltui_str_append(out, d, strlen(d));
    rolltui_str_append(out, "/.config/roll/rolltui", 21);
  } else {
    return;
  }
  rolltui_str_append(out, "/" TERMCACHE_FILE, sizeof("/" TERMCACHE_FILE) - 1);
}

/* The whole file, or 0. Refuses a file that is not a plausible size: this is a cache of a dozen
 * short entries, and a megabyte is something else. */
static int read_file(const char* path, char** out, size_t* out_len) {
  FILE* f = fopen(path, "rb");
  char* buf = NULL;
  size_t len = 0, cap = 0;
  if (!f) return 0;
  for (;;) {
    size_t k;
    buf = (char*)rolltui_grow(buf, &cap, len + 4096, 1);
    k = fread(buf + len, 1, 4096, f);
    len += k;
    if (k < 4096 || len > TERMCACHE_MAX_BYTES) break;
  }
  fclose(f);
  if (len > TERMCACHE_MAX_BYTES) {
    rolltui_mem_free(buf);
    return 0;
  }
  *out = buf;
  *out_len = len;
  return 1;
}

/* The parsed file when it is ours (an object with `schema` 1), else NULL. */
static RolltuiJsonValue* load_root(const char* path) {
  char* text = NULL;
  size_t len = 0;
  RolltuiJsonValue* root;
  if (!read_file(path, &text, &len)) return NULL;
  root = rolltui_json_parse(text, len, NULL);
  rolltui_mem_free(text);
  if (!root) return NULL;
  if (!rolltui_json_is_object(root) || (int)rolltui_json_as_number(rolltui_json_get(root, "schema", 6), 0) != 1) {
    rolltui_json_free(root);
    return NULL;
  }
  return root;
}

static const char* const kDepthNames[] = {"mono", "16", "256", "truecolor"};

int rolltui_termcache_load(const char* path, const char* key, RolltuiTermCacheEntry* out) {
  RolltuiJsonValue* root;
  const RolltuiJsonValue* e;
  const RolltuiJsonValue* v;
  int found = 0;
  memset(out, 0, sizeof *out);
  if (!path || !*path) return 0;
  root = load_root(path);
  if (!root) return 0;
  e = rolltui_json_get(rolltui_json_get(root, "entries", 7), key, strlen(key));
  if (rolltui_json_is_object(e)) {
    size_t n = 0;
    const char* s;
    out->probed = (long long)rolltui_json_as_number(rolltui_json_get(e, "probed", 6), 0);
    out->responsive = (unsigned char)rolltui_json_as_bool(rolltui_json_get(e, "responsive", 10), 0);
    out->keyboard = (unsigned char)rolltui_json_as_number(rolltui_json_get(e, "keyboard", 8), ROLLTUI_PROTOCOL_LEGACY);
    if (out->keyboard >= ROLLTUI_PROTOCOL_COUNT) out->keyboard = ROLLTUI_PROTOCOL_LEGACY;
    v = rolltui_json_get(e, "wide", 4);
    if (rolltui_json_is_number(v)) {
      out->has_wide = 1;
      out->ambiguous_wide = rolltui_json_as_number(v, 0) != 0;
    }
    s = rolltui_json_as_string(rolltui_json_get(e, "depth", 5), "", 0, &n);
    if (n) {
      const int d = rolltui_color_depth_from_name(s, n);
      if (d >= 0) {
        out->has_depth = 1;
        out->depth = (unsigned char)d;
      }
    }
    out->answers_background = (unsigned char)rolltui_json_as_bool(rolltui_json_get(e, "answers_background", 18), 0);
    s = rolltui_json_as_string(rolltui_json_get(e, "background", 10), "", 0, &n);
    if (n && rolltui_color_parse(s, n, &out->background)) out->has_background = 1;
    s = rolltui_json_as_string(rolltui_json_get(e, "name", 4), "", 0, &n);
    if (n) {
      if (n >= sizeof out->name) n = sizeof out->name - 1;
      memcpy(out->name, s, n);
      out->name[n] = '\0';
    }
    found = out->probed > 0;
  }
  rolltui_json_free(root);
  return found;
}

static RolltuiJsonValue* entry_to_json(const RolltuiTermCacheEntry* e) {
  RolltuiJsonValue* o = rolltui_json_object();
  rolltui_json_set(o, "probed", 6, rolltui_json_number((double)e->probed));
  rolltui_json_set(o, "responsive", 10, rolltui_json_bool(e->responsive));
  rolltui_json_set(o, "keyboard", 8, rolltui_json_number(e->keyboard));
  if (e->has_wide) rolltui_json_set(o, "wide", 4, rolltui_json_number(e->ambiguous_wide ? 1 : 0));
  if (e->has_depth && e->depth < ROLLTUI_DEPTH_COUNT)
    rolltui_json_set(o, "depth", 5, rolltui_json_string(kDepthNames[e->depth], strlen(kDepthNames[e->depth])));
  rolltui_json_set(o, "answers_background", 18, rolltui_json_bool(e->answers_background));
  if (e->has_background) {
    char buf[ROLLTUI_COLOR_STRING_MAX];
    const size_t n = rolltui_color_to_string(e->background, buf, sizeof buf);
    rolltui_json_set(o, "background", 10, rolltui_json_string(buf, n));
  }
  if (e->name[0]) rolltui_json_set(o, "name", 4, rolltui_json_string(e->name, strlen(e->name)));
  return o;
}

/* Drops the oldest entries until at most `keep` remain. */
static void prune(RolltuiJsonValue* entries, size_t keep) {
  while (rolltui_json_object_size(entries) > keep) {
    size_t i, oldest = 0, klen = 0;
    double oldest_at = 0;
    const char* k;
    char key[1024];
    for (i = 0; i < rolltui_json_object_size(entries); ++i) {
      const double at = rolltui_json_as_number(rolltui_json_get(rolltui_json_object_value_at(entries, i), "probed", 6), 0);
      if (i == 0 || at < oldest_at) {
        oldest = i;
        oldest_at = at;
      }
    }
    k = rolltui_json_object_key_at(entries, oldest, &klen);
    if (klen >= sizeof key) klen = sizeof key - 1;
    memcpy(key, k, klen); /* `erase` frees the key it was handed a view of */
    key[klen] = '\0';
    rolltui_json_object_erase(entries, key, klen);
  }
}

static int write_root(const char* path, RolltuiJsonValue* root) {
  RolltuiStr text = {0};
  int ok;
  rolltui_json_dump(root, 2, &text);
  rolltui_str_append(&text, "\n", 1);
  ok = rolltui_preset_write_file_atomic(path, strlen(path), text.p, text.n, NULL);
  rolltui_str_free(&text);
  return ok;
}

int rolltui_termcache_store(const char* path, const char* key, const RolltuiTermCacheEntry* e) {
  RolltuiJsonValue* root;
  RolltuiJsonValue* entries;
  int ok;
  if (!path || !*path || !key || !*key) return 0;
  root = load_root(path);
  if (!root) {
    root = rolltui_json_object();
    rolltui_json_set(root, "schema", 6, rolltui_json_number(1));
  }
  if (!rolltui_json_is_object(rolltui_json_get(root, "entries", 7)))
    rolltui_json_set(root, "entries", 7, rolltui_json_object());
  entries = (RolltuiJsonValue*)rolltui_json_get(root, "entries", 7);
  rolltui_json_set(entries, key, strlen(key), entry_to_json(e));
  prune(entries, TERMCACHE_MAX_ENTRIES);
  ok = write_root(path, root);
  rolltui_json_free(root);
  return ok;
}

int rolltui_termcache_forget(const char* path, const char* key) {
  RolltuiJsonValue* root;
  RolltuiJsonValue* entries;
  int removed = 0;
  if (!path || !*path || !key) return 0;
  root = load_root(path);
  if (!root) return 0;
  entries = (RolltuiJsonValue*)rolltui_json_get(root, "entries", 7);
  if (rolltui_json_is_object(entries)) removed = rolltui_json_object_erase(entries, key, strlen(key));
  if (removed) write_root(path, root);
  rolltui_json_free(root);
  return removed;
}

int rolltui_termcache_fresh(const RolltuiTermCacheEntry* e, long long now) {
  const long long hour = 60LL * 60;
  const long long age = now - e->probed;
  if (age < 0) return 0;
  return age < (e->responsive ? 7 * 24 * hour : hour);
}
