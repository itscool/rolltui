#ifndef ROLLTUI_C_EFFECTS_H
#define ROLLTUI_C_EFFECTS_H
/*
 * rolltui/c/rolltui_effects.h — MOTION, as C (Phase 15 m2).
 *
 * A widget MARKS a span of cells with a STATE; the theme maps state → effect as data; an
 * effect is a pure function of (elapsed, cell index, span length, fraction, base style).
 * Every rule, every built-in kind and the reasoning behind all of it is in
 * `rolltui/Effects.hpp` and asserted over every registered kind in
 * `rolltui/tests/effects_test.cpp`; none of it is repeated here, because the rules are the
 * same in both languages and a second copy is a second thing to drift.
 *
 * THIS IS THE OWNERSHIP-HEAVY HALF OF m2, and the reason it and `Diff` were ported
 * together. `plan/phase-15.md` predicts the port's cost tracks how much of a module is
 * ownership work rather than algorithm — Phase 14 measured +12% for Unicode against +55%
 * for Wrap. `Diff` is a pure function; this is **a process-wide registry that OWNS a name
 * and a host's callable per entry, is guarded by a mutex, and must hand everything back at
 * `rolltui::shutdown()`.** If the prior is right, these two land on opposite sides of it.
 *
 * THE BOUNDARY'S RULES, all inherited from Phase 14:
 *   1. **THE CALLER OWNS EVERY BUFFER**, working memory included, through a handle
 *      (`RolltuiEffectScratch`).
 *   2. **ONE DEFINITION**: `rolltui::EffectCell` and `rolltui::EffectOut` ARE the two
 *      structs below, aliased rather than converted, so a host's kind reads and writes the
 *      same bytes the applier does.
 *   3. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function.
 *
 * ---- WHAT CROSSES AS A BORROW, AND WHY IT IS NOT A SECOND DEFINITION -------------------
 *
 * `RolltuiEffectSpec` below is a VIEW of one `rolltui::EffectSpec`, not a copy of its
 * type. The spec is the THEME's data — `std::string kind`, `std::vector<std::string>
 * frames`, `std::vector<Role> roles` — and `Theme` is m3's module, still C++. There were
 * three ways to reach it from here and only one is honest:
 *   - mirror the owning struct in C, which means porting the theme's parser, serialiser
 *     and editor a milestone early, inside the milestone that is supposed to be small;
 *   - copy each spec across per call, which is an allocation per frame on the one path
 *     that redraws continuously;
 *   - **borrow it**, which is what every other reader of another module's storage in this
 *     library already does (`Line`, `Frame::glyph`, `Scratch`).
 * So the view is built in EXACTLY ONE place (`Effects.cpp`'s `fill_view`), the C never
 * copies a byte of it, and the window is one call. **When `Theme` ports in m3 the view
 * becomes the definition and `fill_view` is deleted** — that deletion is the evidence m6
 * should read, not this paragraph.
 *
 * The one field that is not a plain borrow is `frame`, and it is here because a
 * `std::vector<std::string>` has no contiguous (pointer, length) array to lend. One
 * accessor, supplied by the owner, called for the two frames a kind ever wants — the first
 * (to measure) and the current one.
 *
 * **ROLES CROSS AS BYTES AND THE C NAMES NONE OF THEM.** `roles` is a borrow of the spec's
 * own `std::vector<Role>`, which is one byte per entry, and `styles` is a borrow of the
 * theme's whole role table; the C indexes one with the other and never learns what a role
 * is called. The empty case is substituted by the owner rather than defaulted here, so
 * `role_count` is never zero and no rung of this file has an opinion about which role is
 * the fallback.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
#include <cstring>
#include <string_view>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- plain data, defined once and compiled by both languages ------------------------- */

/* Everything a kind is allowed to know about the cell it is answering for. */
typedef struct RolltuiEffectCell {
  unsigned long long elapsed_ms ROLLTUI_DEFAULT(0); /* since the span entered the state */
  int index ROLLTUI_DEFAULT(0);                     /* 0-based, within the span */
  int length ROLLTUI_DEFAULT(1);                    /* the span's length in cells */
  double fraction ROLLTUI_DEFAULT(0);               /* Progress: 0..1 */
  RolltuiStyle base;                                /* the cell's style as drawn */
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);  /* this terminal's East Asian ambiguous width */
} RolltuiEffectCell;

/* THE GLYPH IS INLINE, WITH A STATED REFUSAL RATHER THAN A SPILL — a deliberate answer to
 * CLAUDE.md's strategy 1, and different from `RolltuiCell`'s on purpose. A cell must hold
 * whatever a DOCUMENT contains, so it spills; an effect glyph is one frame of a cycle a
 * theme author or a host WROTE, so a bound is a constraint on that file rather than on a
 * user's data. Thirty-two bytes is past every emoji ZWJ sequence Unicode defines, and
 * anything longer is REFUSED AND COUNTED in `glyphs_refused` — the same mechanism, and the
 * same report, that property 1 already uses for a glyph of the wrong width. Never silent.
 *
 * `glyph_len` is the length the kind ASKED for, even when it is past the cap, which is
 * what lets the applier tell "too long" from "as long as it fits". */
#define ROLLTUI_EFFECT_GLYPH_MAX 32
typedef struct RolltuiEffectOut {
  unsigned char has_glyph ROLLTUI_DEFAULT(0);
  unsigned char has_style ROLLTUI_DEFAULT(0);
  RolltuiStyle style;
  size_t glyph_len ROLLTUI_DEFAULT(0);
  char glyph[ROLLTUI_EFFECT_GLYPH_MAX];

#ifdef __cplusplus
  // A kind says what it wants drawn. Always records the full length, so an override past
  // the cap is refused by the applier rather than silently truncated into a valid-looking
  // narrower glyph.
  void set_glyph(std::string_view g) {
    has_glyph = 1;
    glyph_len = g.size();
    if (g.size() <= ROLLTUI_EFFECT_GLYPH_MAX && !g.empty()) std::memcpy(glyph, g.data(), g.size());
  }
  // Empty when nothing was set, and when what was set is past the cap — a caller that can
  // see the bytes could only mis-measure them.
  std::string_view glyph_view() const {
    return glyph_len > 0 && glyph_len <= ROLLTUI_EFFECT_GLYPH_MAX ? std::string_view(glyph, glyph_len)
                                                                  : std::string_view();
  }
#endif
} RolltuiEffectOut;

/* One theme spec, BORROWED for the duration of one call (see the note above). */
typedef struct RolltuiEffectSpec {
  const char* kind;
  size_t kind_len;
  const unsigned char* roles; /* rolltui::Role values, one byte each */
  size_t role_count;          /* NEVER zero: the owner substitutes its own fallback */
  size_t frame_count;
  /* The owner's own `EffectSpec`. The C stores it, passes it to a host's kind untouched,
   * and never dereferences it — the one opaque token on this boundary. */
  const void* owner;
  /* Frame `i` of the cycle, as a BORROW valid for the call. */
  const char* (*frame)(const void* owner, size_t i, size_t* len);
  int period_ms; /* one full cycle; 0 or less: a STILL effect, no tick */
  int width;     /* shimmer: the sweeping window, in cells */
  int steps;     /* how many distinct pictures a period has (0 → the kind's own) */
  unsigned char backward;
  /* OUT, set by the applier when nothing answers for `kind`. It lives here rather than in
   * a parallel array so that naming an unknown kind costs the caller no second buffer. */
  unsigned char unresolved;
} RolltuiEffectSpec;

typedef struct RolltuiEffectReport {
  int marks_drawn;    /* marks the theme had an effect for */
  int cells_touched;
  int glyphs_refused; /* overrides dropped: wrong width, or past the glyph cap */
} RolltuiEffectReport;

/* ---- rung 2: the kinds a HOST registered ---------------------------------------------- */

/* `ctx` is whatever the host handed to `rolltui_effect_register`; `host` is whatever the
 * caller handed to `rolltui_effects_apply` (the C++ side passes its `const Theme*`). The C
 * dereferences neither. */
typedef void (*RolltuiEffectFn)(void* ctx, const RolltuiEffectSpec* spec, const RolltuiStyle* styles,
                                const void* host, const RolltuiEffectCell* in, RolltuiEffectOut* out);

/* Why a registration was refused. The MESSAGE is built one level up, where the words are
 * already a `std::string` and can name the kind. */
#define ROLLTUI_EFFECT_OK 0
#define ROLLTUI_EFFECT_NO_NAME 1
#define ROLLTUI_EFFECT_NO_FN 2
#define ROLLTUI_EFFECT_IS_BUILTIN 3 /* rung 1 is never shadowed */
#define ROLLTUI_EFFECT_DUPLICATE 4

/* Registers a kind. The registry COPIES the name and takes ownership of `ctx`, releasing
 * it with `free_ctx` at `clear` or at `rolltui::shutdown()`. On any refusal it takes
 * nothing: `ctx` is still the caller's, and `free_ctx` is not called. */
int rolltui_effect_register(const char* name, size_t name_len, RolltuiEffectFn fn, void* ctx,
                            void (*free_ctx)(void*));
/* Releases every host kind — for a test, and for a host tearing down. This is also what
 * runs at `rolltui::shutdown()`; the registry registers itself the first time it holds
 * anything, which is the rule in rolltui/Lifetime.hpp. */
void rolltui_effect_clear_registered(void);

/* Every kind name that resolves right now, in RESOLUTION ORDER: the library's closed seven
 * first and never shadowed, then the host's. `name` is a BORROW, valid until the registry
 * next changes. */
size_t rolltui_effect_kind_count(void);
const char* rolltui_effect_kind_name(size_t i, size_t* len);
/* Whether anything answers for `name` — a HOST fact, never a theme error. */
int rolltui_effect_kind_resolves(const char* name, size_t len);
int rolltui_effect_is_builtin(const char* name, size_t len);

/* ---- working memory -------------------------------------------------------------------- */
/* The grapheme buffer and the Unicode scratch the glyph kinds need to measure their own
 * frames. One handle per thread, made once, grown over the first few calls and never
 * again — the same shape as `RolltuiUnicodeScratch` and `RolltuiDiffScratch`, and for the
 * same reason: "this function needs somewhere to work" is a missing handle, not a new
 * allocation strategy (CLAUDE.md). */
typedef struct RolltuiEffectScratch RolltuiEffectScratch;
RolltuiEffectScratch* rolltui_effect_scratch_new(void);
void rolltui_effect_scratch_free(RolltuiEffectScratch* s);

/* ---- the pure parts ---------------------------------------------------------------------- */

/* How many distinct pictures one period of `spec` has over a span `length` cells long. */
int rolltui_effect_steps(const RolltuiEffectSpec* spec, int length);

/* ---- applying, and the tick --------------------------------------------------------------- */
/* THE SPECS ARE A FLAT ARRAY PLUS PER-STATE RANGES: `specs[state_first[s] ..
 * state_first[s] + state_count[s])` is what the theme maps state `s` to, for `states`
 * states. Flat because a mark names its state as an int the frame stored and never
 * interpreted (rolltui_screen.h), so the C indexes these arrays with it and still knows
 * nothing about the state vocabulary — which lives in `Effects.hpp` alone. A state index
 * outside `states` draws nothing.
 *
 * Called by a host AFTER the whole screen has composed and before the frame diff.
 * `now_ms` is any monotonic millisecond clock. `rep` may not be NULL. */
void rolltui_effects_apply(RolltuiFrame* f, RolltuiEffectScratch* s, const RolltuiStyle* styles, const void* host,
                           RolltuiEffectSpec* specs, const size_t* state_first, const size_t* state_count,
                           size_t states, unsigned long long now_ms, int ambiguous_wide, RolltuiEffectReport* rep);

/* The interval at which this frame must be redrawn for its motion, or 0 when nothing is
 * marked, when the theme maps nothing to what is marked, or when nothing mapped MOVES.
 * Clamped to at least 16 ms so a long span cannot ask for a wakeup per millisecond. */
int rolltui_effects_tick_ms(const RolltuiFrame* f, const RolltuiEffectSpec* specs, const size_t* state_first,
                            const size_t* state_count, size_t states);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_EFFECTS_H */
