#ifndef ROLLTUI_C_REGEX_H
#define ROLLTUI_C_REGEX_H
/*
 * rolltui/c/rolltui_regex.h — INTERNAL: a small regular-expression matcher, for the syntax highlighter.
 *
 * Not a general library. It is the smallest thing that can describe the tokens of a programming language and be
 * written by a person in a JSON file, and it is held to what a highlighter needs:
 *
 *   ANCHORED. A match is asked for AT a position, never searched for: the highlighter walks a line and asks each
 *   rule whether it starts here. So there is no scan, and a rule that cannot start with the byte under the
 *   cursor is rejected without running (`rolltui_regex_first_byte`).
 *   ONE LINE. The text is a line without its newline. `^` is its start, `$` its end, and nothing spans two.
 *   BYTES. Patterns and text are bytes; a byte of 0x80 or more counts as a word character, so an identifier
 *   with a letter from another script is one word. Case-insensitivity is ASCII.
 *
 * WHAT IT TAKES, and what it refuses with a reason (never silently):
 *   literals, `.`, classes `[a-z_]` `[^"]` with `\d \w \s` inside, `\d \D \w \W \s \S`, `\b \B`, `^ $`,
 *   groups `( )` `(?: )`, lookahead `(?= )` `(?! )`, alternation `|`, `* + ? {n} {n,} {n,m}` each with a lazy
 *   `?` form, `\n \t \r \xHH`, an escaped punctuation mark for itself, and `\1`..`\9`: the text a region's BEGIN
 *   matched in its groups, which is how a here-document knows its terminator and a Lua long bracket its level.
 *   No lookbehind, no named groups, no inline flags, no Unicode classes.
 *
 * IT CANNOT RUN AWAY. A match gives up (and fails) after a fixed number of steps, so a careless pattern in a
 * language file costs a mis-coloured token, not a hung program.
 *
 * THE BOUNDARY'S RULES, as everywhere in `c/`: an opaque handle the caller frees, nothing returned by value.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RolltuiRegex RolltuiRegex;

/* The text `\1`..`\9` stand for: what a region's begin matched in each group. BORROWED for the match call. */
typedef struct RolltuiRegexRefs {
  const char* p[10];
  size_t n[10];
} RolltuiRegexRefs;

/* Where a match landed: group 0 is the whole match, 1..9 the groups; -1 for a group that took no part. */
typedef struct RolltuiRegexMatch {
  int start[10];
  int end[10];
} RolltuiRegexMatch;

/* Compiles `pat` (`n` bytes). NULL, with the reason in `err` (which may be NULL), for a pattern this matcher does
 * not take or that would compile to more than it will hold. `icase` folds ASCII letters. */
RolltuiRegex* rolltui_regex_compile(const char* pat, size_t n, int icase, RolltuiStr* err);
void rolltui_regex_free(RolltuiRegex* re); /* a no-op on NULL */

/* Whether the pattern matches `line` (`len` bytes, no newline) starting exactly at `at`; 1 and `*m` filled (`m` may be
 * NULL), or 0. `refs` may be NULL when the pattern has no `\1`. */
int rolltui_regex_match_at(const RolltuiRegex* re, const char* line, size_t len, size_t at, const RolltuiRegexRefs* refs,
                           RolltuiRegexMatch* m);

/* THE SAME QUESTION ASKED ALONG A LINE, remembering what earlier answers proved. A highlighter asks a rule at every position of a
 * line in turn, and a pattern like `\[[^\]]*\]` on a line with no `]` spends the rest of the line failing at EVERY `[`: the
 * square of the line's length. With a memo, a loop over one byte that has failed from one place records the range it covers, and
 * a later call entering that loop inside the range fails at once — so a line costs what one pass over it costs. The answers are
 * exactly those of `rolltui_regex_match_at`: only work known to fail is skipped.
 *
 * `memo`: `rolltui_regex_memo_slots(re)` slots the CALLER owns and keeps for this regex, zeroed once. `gen`: the caller's number
 * for the line; every call with the same `gen` must ask about the same `line`, `len` and `refs`, so a caller moves to a new
 * number for each line (a slot stamped with another number is empty, so nothing need be cleared). `gen` 0 is never valid, and
 * `memo` NULL remembers nothing. */
typedef struct RolltuiRegexSlot {
  unsigned gen;
  int lo, hi;
} RolltuiRegexSlot;
size_t rolltui_regex_memo_slots(const RolltuiRegex* re); /* 0 for a pattern with a back-reference: it keeps nothing */
int rolltui_regex_match_memo(const RolltuiRegex* re, const char* line, size_t len, size_t at, const RolltuiRegexRefs* refs,
                             RolltuiRegexMatch* m, RolltuiRegexSlot* memo, unsigned gen);

/* THE REFERENCE: every loop compiled as a branch per turn, as before OP_STAR, with nothing remembered — what the differential test
 * in regex_test.cpp holds the matcher to. Not for a highlighter: it fails on a run longer than its backtracking stack. */
#define ROLLTUI_REGEX_PLAIN 1
/* No step budget and a stack sixty-four times as deep, so that two matchers are compared on their ANSWERS and never on where
 * each gives up. The differential test's alone: a highlighter needs the budget that keeps a careless pattern from hanging it. */
#define ROLLTUI_REGEX_UNBOUNDED 2
RolltuiRegex* rolltui_regex_compile_flags(const char* pat, size_t n, int icase, int flags, RolltuiStr* err);

/* Whether a match could BEGIN with byte `b`. Conservative: true whenever the pattern can match empty or starts by
 * looking around, so a false answer is always safe to act on. */
int rolltui_regex_first_byte(const RolltuiRegex* re, unsigned char b);
int rolltui_regex_groups(const RolltuiRegex* re);   /* how many capture groups it has */
int rolltui_regex_uses_refs(const RolltuiRegex* re); /* whether it contains `\1`..`\9` */
int rolltui_regex_can_be_empty(const RolltuiRegex* re);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_REGEX_H */
