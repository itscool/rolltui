#ifndef ROLLTUI_C_TERMFACTS_H
#define ROLLTUI_C_TERMFACTS_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`
 * (`RolltuiTermFacts` and the `rolltui_terminal_*` calls that hand it out). What is below is the
 * library's own — reached by its `.c` files, and by a suite that opts in by including this header
 * by name (`ROLLTUI_INTERNAL_OPT_IN`).
 *
 * rolltui/c/rolltui_termfacts.h — WHAT A TERMINAL IS, as pure functions.
 *
 * Everything here is a function of bytes and strings: the environment as a struct, the questions
 * as a byte string, the answers as a scanner over a buffer, the remembered answers as a file. None
 * of it touches the tty, which is `rolltui_terminal.c`'s and the reason this file can be tested
 * without a pty and without a clock: a test hands it an environment and a buffer and reads the
 * facts that come out.
 *
 * THE ORDER OF TRUST, and it is the reason for the four sources on every fact:
 *   FORCED  a person or a host said so outright                 — nothing overrides it
 *   PROBE   the terminal answered a question this run           — it knows itself
 *   CACHE   the terminal answered a question an earlier run     — right until it is not, so it is
 *                                                                 re-checked and corrected
 *   ENV     the environment said so                             — a guess about a program that
 *                                                                 may not be the one drawing
 *   DEFAULT nothing said anything                               — the answer that fails safest
 * "Fails safest" is a direction, not a taste: guessing FEWER colours than the terminal has costs
 * some quantisation, and guessing MORE than it has paints nonsense (a terminal that does not
 * know `38;2;r;g;b` reads it as stray attributes and basic colours), so every uncertainty here
 * resolves DOWN.
 */

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the environment, read once ------------------------------------------------------------- */
/* Every pointer BORROWS from `getenv` and is NULL when the variable is unset or empty. The two
 * arrays are copies, because `uname` and `gethostname` fill a buffer rather than lend a string. A
 * struct rather than a series of `getenv` calls so a test can BE the environment. */
typedef struct RolltuiTermEnv {
  const char* colorterm;
  const char* term;
  const char* term_program;
  const char* term_program_version;
  const char* lc_terminal;         /* iTerm2 forwards these over ssh, which TERM_PROGRAM is not */
  const char* lc_terminal_version;
  const char* colorfgbg;           /* "fg;bg" — the rxvt convention, set by a few terminals */
  const char* force_depth;         /* ROLL_COLOR_DEPTH */
  const char* force_wide;          /* ROLL_AMBIGUOUS_WIDE */
  int no_probe;                    /* ROLL_TERM_PROBE=0: ask the keyboard question and nothing else */
  const char* tmux;
  const char* sty;                 /* GNU screen */
  const char* ssh_connection;
  const char* locale;              /* LC_ALL, else LC_CTYPE, else LANG */
  char os_release[64];             /* uname(2) release: "25.6.0" on macOS 26 */
  char host[64];
} RolltuiTermEnv;

void rolltui_termenv_read(RolltuiTermEnv* e);

/* What the environment alone says. Sets EVERY field of `f` (the source of each is DEFAULT or ENV
 * or FORCED), so it is also how a fact struct is initialised. */
void rolltui_termfacts_from_env(const RolltuiTermEnv* e, RolltuiTermFacts* f);

/* tmux or screen: whatever answers a question is the multiplexer's model of a terminal, and the
 * terminal it is attached to can change between runs without one byte of the environment
 * changing. Answers from behind one are used but never remembered. */
int rolltui_termenv_multiplexed(const RolltuiTermEnv* e);

/* A terminal this environment names that is KNOWN not to draw 24-bit colour (Apple's Terminal before
 * macOS 26). Its depth is decided, so nothing is gained by asking it to confirm one — and an exotic
 * question sent to the one terminal whose answers are already known is all risk: it is left out of the
 * exchange. */
int rolltui_termenv_no_24bit_known(const RolltuiTermEnv* e);

/* One line naming this terminal AND the things whose change means the remembered answers are
 * stale: the program's version, the operating system's release, the terminal's names and
 * versions, which machine an ssh session comes from, the locale. `app_key` may be NULL.
 * Returns the length written (NUL-terminated, truncated at `cap`). */
size_t rolltui_termfacts_fingerprint(const RolltuiTermEnv* e, const char* app_key, char* out, size_t cap);

/* THIS PROGRAM'S OWN IDENTITY — its file name, size and modification time — so that a new build
 * asks the terminal again without any host doing anything to say it was rebuilt. Empty when the
 * executable cannot be found. Two calls in one process agree. */
size_t rolltui_termfacts_exe_identity(char* out, size_t cap);

/* ---- the ceiling ----------------------------------------------------------------------------- */
/* THE DEPTH OF THE TERMINAL THIS PROCESS IS DRAWING TO, published so that no host has to remember to
 * ask for it. Every host used to say `rolltui_swap_present(swap, ROLLTUI_DEPTH_TRUECOLOR, ...)`, and
 * two of three never learned that was a request and not a fact. It is a request now: `rolltui_swap_present`
 * treats the depth it is handed as the MOST the host would like, and never sends more than the terminal
 * has. `forced` (a person or a host said the depth outright) replaces the request instead of capping
 * it. `depth` < 0 withdraws the ceiling — no terminal is entered, so a golden test or a tool that writes
 * to a pipe gets exactly the depth it asked for. */
void rolltui_termfacts_set_active(int depth, int forced);
unsigned char rolltui_termfacts_clamp_depth(unsigned char requested);

/* THE OTHER TWO FACTS A HOST CONSUMES, published the same way and for the same reason.
 *   ambiguous width  is ORed into the one place a glyph's width is decided (`rolltui_u_codepoint_width`),
 *                    so every width in the library — a border, a scrollbar thumb, a wrapped line — is
 *                    measured the way THIS terminal draws it whether or not the host was told: a host
 *                    that says one cell on a terminal that measured two is wrong, and a host that
 *                    forgot to ask is wrong the same way. A host that says two (`--ambiguous-wide`)
 *                    still gets two.
 *   mode             is what `rolltui_theme_load` follows when it is handed a mode below zero — which is
 *                    what `rolltui_theme_mode_from_name` answers for "auto" — so the obvious line of host
 *                    code does the right thing.
 * -1 withdraws either: no terminal is entered. */
void rolltui_termfacts_set_active_wide(int wide);
int rolltui_termfacts_active_wide(void); /* 1 only when a terminal measured (or was told) two cells */
void rolltui_termfacts_set_active_mode(int mode);
int rolltui_termfacts_active_mode(void); /* ROLLTUI_MODE_*, or -1 when no terminal is entered */

/* ---- the questions -------------------------------------------------------------------------- */
#define ROLLTUI_TERMQ_KEYBOARD 1   /* CSI ? u, CSI ? 4 m */
#define ROLLTUI_TERMQ_BACKGROUND 2 /* OSC 11 */
#define ROLLTUI_TERMQ_SGR 4        /* set a 24-bit background, then ask the terminal what it is set to */
#define ROLLTUI_TERMQ_VERSION 8    /* XTVERSION */
#define ROLLTUI_TERMQ_WIDTH 16     /* draw one ambiguous glyph, ask where the cursor is, put it back */
#define ROLLTUI_TERMQ_ALL 31

/* The bytes that ask the questions in `want`, in ONE write, ending in Primary DA — the question
 * every terminal answers, and the reason an answer that never comes is a definite "no" rather than
 * a timeout guess: replies arrive in the order asked, so DA1 arriving means everything that was
 * going to be answered has been. Returns the length; NUL-terminated within `cap`. */
size_t rolltui_termprobe_queries(unsigned want, char* out, size_t cap);

/* ---- the answers ---------------------------------------------------------------------------- */
#define ROLLTUI_TERMR_SGR_NONE 0   /* no readable answer */
#define ROLLTUI_TERMR_SGR_TRUE 1   /* it says the background is set to a 24-bit colour */
#define ROLLTUI_TERMR_SGR_256 2    /* it says the background is a palette colour: it reduced ours */

typedef struct RolltuiTermReplies {
  int da1;          /* Primary DA arrived: the batch is over */
  int kitty;        /* `CSI ? flags u` */
  int modkeys;      /* `CSI > 4 ; v m` */
  int key_last;     /* which of those two came LAST: 1 kitty, 2 modifyOtherKeys — the order keys are judged in */
  int has_bg;       /* OSC 11 arrived and parsed */
  RolltuiStyleColor bg;
  int cpr;          /* `CSI row ; col R`, only when the caller said one was expected */
  int cpr_row, cpr_col;
  int sgr;          /* ROLLTUI_TERMR_SGR_* */
  char version[64]; /* XTVERSION: "ghostty 1.1.3" */
} RolltuiTermReplies;

/* Removes every RECOGNISED terminal reply from `buf[0..len)` in place, folds what they said into
 * `*out`, and returns the length of what is left — which is input, somebody typing, in order. A
 * reply that has begun but not finished is LEFT in the buffer, so a caller that reads on and
 * scans again finds it whole. `expect_cpr` is whether a cursor-position report is outstanding: `CSI r ; c R`
 * is also Shift-F3, and only a caller that asked can say which it was. */
size_t rolltui_termreplies_take(char* buf, size_t len, int expect_cpr, RolltuiTermReplies* out);

/* Folds what the terminal answered into the facts, for the questions `asked`. A FORCED fact is
 * never touched; everything else takes the terminal's word over the environment's. */
void rolltui_termfacts_apply_replies(RolltuiTermFacts* f, const RolltuiTermReplies* r, unsigned asked);

/* ---- remembered answers ---------------------------------------------------------------------- */
typedef struct RolltuiTermCacheEntry {
  long long probed;              /* unix seconds of the run that asked */
  unsigned char responsive;      /* the terminal answered at all */
  unsigned char keyboard;        /* ROLLTUI_PROTOCOL_* */
  unsigned char has_wide;        /* `ambiguous_wide` was asked and answered */
  unsigned char ambiguous_wide;
  unsigned char has_depth;       /* the terminal CONFIRMED its depth (not merely the environment) */
  unsigned char depth;
  unsigned char answers_background; /* it answered OSC 11: worth re-checking each run */
  unsigned char has_background;
  RolltuiStyleColor background;  /* the last one seen; a starting point, re-checked */
  char name[64];
} RolltuiTermCacheEntry;

/* `<dir>/terminal-facts.json`, `dir` being the caller's or, when NULL, the person's rolltui
 * configuration directory. Empty when there is no such directory to name (no HOME). */
void rolltui_termcache_path(const char* dir, RolltuiStr* out);

/* 1 and the entry, or 0: no file, a file that is not ours, no such fingerprint. A damaged file
 * is not an error — it is a miss, and the next store replaces it. */
int rolltui_termcache_load(const char* path, const char* key, RolltuiTermCacheEntry* out);

/* Writes `e` under `key`, keeping the newest sixteen fingerprints. Atomic (a reader never sees
 * half a file). 0 when it could not — a read-only home is not a reason to stop drawing. */
int rolltui_termcache_store(const char* path, const char* key, const RolltuiTermCacheEntry* e);

/* Drops one fingerprint. 1 when it was there. */
int rolltui_termcache_forget(const char* path, const char* key);

/* Trusted without asking again: seven days for a terminal that answered, one HOUR for one that did
 * not. A silent terminal skips the question next time and saves every launch the wait, but silence
 * can be a slow link or a terminal that was not ready, so that guess is not allowed to stand for
 * long. A `probed` in the future is not fresh — a clock that went backwards is a reason to ask. */
int rolltui_termcache_fresh(const RolltuiTermCacheEntry* e, long long now);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_TERMFACTS_H */
