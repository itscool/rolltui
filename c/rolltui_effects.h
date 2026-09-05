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
 * ---- THE VIEW BECAME THE DEFINITION, WHICH IS THE m2 SEAM CLOSING --------------------
 *
 * In m2 `RolltuiEffectSpec` was a VIEW of one `rolltui::EffectSpec`, because the spec was
 * the THEME's data — `std::string kind`, `std::vector<std::string> frames`,
 * `std::vector<Role> roles` — and `Theme` had not ported. That header said, in as many
 * words: **"when `Theme` ports in m3 the view becomes the definition and `fill_view` is
 * deleted — that deletion is the evidence m6 should read, not this paragraph."**
 *
 * This is that. `RolltuiEffectMap` below OWNS every spec a theme carries, in C, and
 * `rolltui::EffectMap` is a handle to one. Three things went with the change and none of
 * them is a rewrite anybody chose — each was a piece of machinery that existed ONLY to
 * bridge two owners:
 *   - `fill_view`, the one place a `std::vector<Role>` was reinterpreted as bytes and a
 *     fallback role substituted (the map is told its fallback once, at construction);
 *   - `SpecViews`, twelve inline views with a heap spill, rebuilt on every `apply_effects`
 *     and every `effect_tick_ms` (the map already IS the flat array, so there is nothing
 *     to build);
 *   - the `frame` accessor and the `owner` token beside it, which existed because a
 *     `std::vector<std::string>` has no contiguous (pointer, length) array to lend. A map
 *     keeps its frames as exactly that array, so a kind indexes it.
 *
 * **ROLES STILL CROSS AS BYTES AND THE C STILL NAMES NONE OF THEM.** `roles` is one byte
 * per entry and `styles` is the theme's whole role table; the C indexes one with the other
 * and never learns what a role is called. `role_count` is never zero, because the map
 * substitutes the fallback it was HANDED at construction — so no rung of this file has an
 * opinion about which role that is.
 *
 * **AN UNKNOWN KIND IS REPORTED THROUGH A CALLBACK, not through an OUT field on the spec.**
 * It used to be `unresolved`, written into the caller's throwaway view. A map is the
 * theme's own storage and a theme is `const` while it is being drawn with, so the applier
 * says the name instead of marking it — which also costs nothing at all in the ordinary
 * case, where every kind resolves and the callback is never reached.
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

/* One frame of a glyph cycle: a BORROW of bytes the owning map keeps, valid for as long as
 * that map is not changed. */
typedef struct RolltuiEffectFrame {
  const char* bytes;
  size_t len;
} RolltuiEffectFrame;

/* ONE THEME SPEC, owned by the `RolltuiEffectMap` it lives in. Every pointer here borrows
 * that map's storage and is valid until the map next changes — the same stated window
 * `rolltui_effect_kind_name` has, and for the same reason. */
typedef struct RolltuiEffectSpec {
  const char* kind;
  size_t kind_len;
  const unsigned char* roles; /* rolltui::Role values, one byte each */
  size_t role_count;          /* NEVER zero: the map substitutes the fallback it was handed */
  /* …and what the spec itself was GIVEN, which is 0 when it named none. The two are
   * separate because collapsing them would lose a distinction a file can make: a
   * serialiser must write no "roles" key for a spec that had none, and the substituted
   * fallback is indistinguishable from a spec that named exactly that one role. The
   * applier reads `role_count`; anything writing the theme back out reads this. */
  size_t own_role_count;
  const RolltuiEffectFrame* frames;
  size_t frame_count;
  int period_ms; /* one full cycle; 0 or less: a STILL effect, no tick */
  int width;     /* shimmer: the sweeping window, in cells */
  int steps;     /* how many distinct pictures a period has (0 → the kind's own) */
  unsigned char backward;

#ifdef __cplusplus
  std::string_view kind_view() const { return std::string_view(kind, kind_len); }
  std::string_view frame(std::size_t i) const {
    return i < frame_count ? std::string_view(frames[i].bytes, frames[i].len) : std::string_view();
  }
  std::size_t roles_size() const { return role_count; }
  unsigned char role(std::size_t i) const { return roles[i % role_count]; }
#endif
} RolltuiEffectSpec;

/* ---- what a THEME carries, owned in C -------------------------------------------------- */
/* A map of STATE → the specs that state looks like while it lasts. `states` is the caller's
 * state vocabulary size — the C indexes with it and never learns a state's name — and
 * `fallback_role` is the role a spec with none of its own picks, handed over once here so
 * that no rung of this file has an opinion about it.
 *
 * OWNED, LONG-LIVED (CLAUDE.md's strategy 4): one map per `rolltui::Theme`, which frees it. */
typedef struct RolltuiEffectMap RolltuiEffectMap;
RolltuiEffectMap* rolltui_effect_map_new(size_t states, unsigned char fallback_role);
void rolltui_effect_map_free(RolltuiEffectMap* m);
RolltuiEffectMap* rolltui_effect_map_clone(const RolltuiEffectMap* m);
void rolltui_effect_map_clear(RolltuiEffectMap* m);
int rolltui_effect_map_equal(const RolltuiEffectMap* a, const RolltuiEffectMap* b);
int rolltui_effect_map_empty(const RolltuiEffectMap* m);
size_t rolltui_effect_map_count(const RolltuiEffectMap* m, size_t state);
/* A BORROW, valid until the map next changes. NULL for an index past the state's specs. */
const RolltuiEffectSpec* rolltui_effect_map_at(const RolltuiEffectMap* m, size_t state, size_t i);

/* Appends a spec to `state` and returns its index; the two adders then fill it in. A spec
 * is built rather than handed over whole because its three arrays are variable-length, and
 * a builder is what keeps them the MAP's allocations instead of a caller's. */
size_t rolltui_effect_map_add(RolltuiEffectMap* m, size_t state, const char* kind, size_t kind_len, int period_ms,
                              int width, int steps, int backward);
void rolltui_effect_map_add_frame(RolltuiEffectMap* m, size_t state, size_t i, const char* bytes, size_t len);
void rolltui_effect_map_add_role(RolltuiEffectMap* m, size_t state, size_t i, unsigned char role);

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

/* ---- THE EFFECT-STATE VOCABULARY (Phase 17, 2026-09-05) ---------------------------------
 * The same move, and for the same reason, as `ROLLTUI_ROLE_LIST` in `rolltui_style.h`: these
 * five names were an `enum class` plus a parallel array in `rolltui/Effects.cpp`, so a C
 * consumer could reach neither and every one that needed them copied the list. Both spellings
 * now expand this one.
 *
 * A widget MARKS a span with a state and stops; the theme maps state -> effect as data in its
 * file. That mapping is read from a theme file BY NAME, which is exactly why the names have to
 * be reachable from the C that does the reading.
 *
 * ORDER IS ABI — `none` must stay 0, because a zeroed mark means "not marked". */
/* THREE SPELLINGS, ONE LIST: the name a theme FILE uses ("waiting"), the C constant
 * (ROLLTUI_EFFECT_STATE_WAITING) and the identifier C++ reads best (EffectState::Waiting).
 * A third column rather than a second list, because the whole point is that adding a state
 * is one edit. */
#define ROLLTUI_EFFECT_STATE_LIST(X) \
  X(none, NONE, None) \
  X(waiting, WAITING, Waiting) \
  X(streaming, STREAMING, Streaming) \
  X(progress, PROGRESS, Progress) \
  X(flash, FLASH, Flash)

typedef enum RolltuiEffectState {
#define ROLLTUI_EFFECT_STATE_ENUM_(lower, UPPER, Camel) ROLLTUI_EFFECT_STATE_##UPPER,
  ROLLTUI_EFFECT_STATE_LIST(ROLLTUI_EFFECT_STATE_ENUM_)
#undef ROLLTUI_EFFECT_STATE_ENUM_
  ROLLTUI_EFFECT_STATE_COUNT
} RolltuiEffectState;

/* BORROWS a static literal. An out-of-range state reads back as "none", which is what the
 * C++ `effect_state_name` did and what a mark of an unknown state means. */
const char* rolltui_effect_state_name(unsigned char state, size_t* len);

/* The state of that name, or -1 when there is none — what a theme LOADER needs. */
int rolltui_effect_state_from_name(const char* name, size_t len);

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

/* Called once per kind the map names that NOTHING answers for, with its name as a borrow
 * valid for the call. A host says it out loud; the C never judges a kind (Effects.hpp). */
typedef void (*RolltuiEffectUnknownFn)(void* ctx, const char* kind, size_t len);

/* A mark names its state as an int the frame stored and never interpreted
 * (rolltui_screen.h), so the C indexes the map with it and still knows nothing about the
 * state vocabulary — which lives in `Effects.hpp` alone. A state index outside the map's
 * own `states` draws nothing.
 *
 * Called by a host AFTER the whole screen has composed and before the frame diff.
 * `now_ms` is any monotonic millisecond clock. `rep` may not be NULL; `on_unknown` may. */
void rolltui_effects_apply(RolltuiFrame* f, RolltuiEffectScratch* s, const RolltuiStyle* styles, const void* host,
                           const RolltuiEffectMap* map, unsigned long long now_ms, int ambiguous_wide,
                           RolltuiEffectReport* rep, RolltuiEffectUnknownFn on_unknown, void* unknown_ctx);

/* The interval at which this frame must be redrawn for its motion, or 0 when nothing is
 * marked, when the theme maps nothing to what is marked, or when nothing mapped MOVES.
 * Clamped to at least 16 ms so a long span cannot ask for a wakeup per millisecond. */
int rolltui_effects_tick_ms(const RolltuiFrame* f, const RolltuiEffectMap* map);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_EFFECTS_H */
