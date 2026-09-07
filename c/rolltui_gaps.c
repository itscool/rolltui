/*
 * rolltui/c/rolltui_gaps.c — WHAT THIS SCREEN NAMES THAT THIS APP DOES NOT PROVIDE.
 *
 * ---- WHO THIS IS FOR, which decides everything else about it --------------------------------
 *
 * The DEVELOPER, not the person who wrote the layout. The user's framing, 2026-09-06, and it
 * is the whole reason this file exists rather than a validator:
 *
 *     "I don't think that's feedback slower. its feedback on the right place. 'the app was
 *      designed like this and your code doesn't support it properly yet.'"
 *
 * A screen is the INTENT and the code catches up. So this REPORTS and never fails: it hands a
 * host a list and the host decides whether any of it is fatal, exactly as
 * `rolltui_bindings_load_json` already does with an action nothing declares. Nothing in here
 * refuses a layout, and nothing in here is a reason not to run.
 *
 * ---- WHY IT IS NOT `rolltui_windows_sync`, WHICH ALREADY DOES HALF OF IT --------------------
 *
 * `sync` collects the same per-widget `problem()` strings, and Phase 26 m1 judged it CORRECT
 * and kept it. What it cannot do is this job, for two reasons that are both about WHEN:
 *
 *   1. **It sees the STACK, not the SCREEN.** `sync` walks the layers currently pushed, so a
 *      popup the layout DECLARES and the host has not opened is invisible to it. A developer
 *      wants to know their `details` popup names a kind they never wrote BEFORE a user opens
 *      it, not the first time one does.
 *   2. **It runs per FRAME, and it is framed as "problems this frame".** The same bytes read
 *      as noise in a render loop and as a to-do list at start-up. Same data, different moment,
 *      different audience.
 *
 * So this file is `sync`'s check run over the WHOLE layout at a moment the host chooses. The
 * per-widget rule is NOT duplicated: every "is this satisfied" question is answered by the
 * widget's own `problem()`, which is the one place each kind states what it needs (`rows`
 * says "nothing is bound to 'x'", the error panel says why a content could not be built).
 * Writing those checks again here would be a second spelling of a rule that already has one.
 *
 * ---- THE TWO KINDS OF GAP, and the second one is weaker on purpose --------------------------
 *
 *   A THING THAT DOES NOT EXIST — a window names a kind nobody registered, or a source
 *   nothing is bound to. This is exact: the library resolves both and knows the answer.
 *
 *   A THING NOTHING CAN REACH — the screen declares an action and no chord in the bindings
 *   table serves it. This is a HINT, not a proof, and the difference is stated at the call:
 *   a menu item may still invoke that action, and whether the HOST HANDLES it at all is not
 *   library-visible, because handling is a `switch` in the host's own event loop. What is
 *   visible is the keyboard, and a declared action no key reaches is worth saying.
 *
 * ---- WHAT IS DELIBERATELY NOT CHECKED -------------------------------------------------------
 *
 *   `note_at`. `sync` collects a widget's notes alongside its problem; this does not. A note
 *   is a remark about a window that WORKS (a migration, a fallback that was taken). A gap is
 *   a thing that is not there. Mixing them would make the count meaningless, and the count is
 *   what makes the summary a sentence a developer can act on.
 */
#include "rolltui/rolltui.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_widgets.h"

/* ---- the report -------------------------------------------------------------------------- */

void rolltui_gap_report_release(RolltuiGapReport* r) {
  size_t i;
  if (!r) return;
  for (i = 0; i < r->gaps_n; ++i) rolltui_str_free(&r->gaps[i]);
  rolltui_mem_free(r->gaps);
  memset(r, 0, sizeof *r);
}

int rolltui_gap_report_clean(const RolltuiGapReport* r) { return !r || r->gaps_n == 0; }

/* GROWING AMORTISED (strategy 5): one line per gap, and a screen has few. */
static RolltuiStr* gap_add(RolltuiGapReport* r) {
  r->gaps = (RolltuiStr*)rolltui_grow_zeroed(r->gaps, &r->gaps_cap, r->gaps_n + 1, sizeof *r->gaps);
  return &r->gaps[r->gaps_n++];
}

static void app(RolltuiStr* s, const char* lit) { rolltui_str_append(s, lit, strlen(lit)); }

static void app_num(RolltuiStr* s, size_t v) {
  char b[24];
  size_t n = 0;
  if (v == 0) {
    app(s, "0");
    return;
  }
  while (v && n < sizeof b) {
    b[n++] = (char)('0' + (v % 10));
    v /= 10;
  }
  while (n) rolltui_str_append(s, &b[--n], 1);
}

void rolltui_gap_report_summary(const RolltuiGapReport* r, RolltuiStr* out) {
  size_t i;
  if (!r || !out || r->gaps_n == 0) return; /* "" when clean, the rule every report here has */
  app(out, "this screen names ");
  app_num(out, r->named);
  app(out, " things this app must provide and ");
  app_num(out, r->gaps_n);
  app(out, r->gaps_n == 1 ? " is missing: " : " are missing: ");
  for (i = 0; i < r->gaps_n; ++i) {
    if (i) app(out, "; ");
    rolltui_str_append_str(out, &r->gaps[i]);
  }
}

/* ---- collecting -------------------------------------------------------------------------- */

typedef struct Collect {
  RolltuiWindows* w;
  RolltuiGapReport* out;
  RolltuiStr scratch;
} Collect;

/* "window 'id' wants 'content': <what the widget itself says is missing>". The window id and
 * the content are BOTH named because a developer reading this has to find the window in a file
 * and the missing thing in their own code, and those are two different searches. */
static void note_window(Collect* c, const RolltuiLayoutNode* n) {
  RolltuiWidget* wd = rolltui_windows_widget_for(c->w, n->content.p, n->content.n);
  RolltuiStr* line;
  ++c->out->named;
  if (!wd || !wd->vt || !wd->vt->problem) return;
  if (!wd->vt->problem(wd->ctx, &c->scratch)) return;
  line = gap_add(c->out);
  rolltui_str_clear(line);
  app(line, "window '");
  rolltui_str_append_str(line, &n->id);
  app(line, "' wants '");
  rolltui_str_append_str(line, &n->content);
  app(line, "': ");
  rolltui_str_append_str(line, &c->scratch);
}

static void walk(Collect* c, const RolltuiLayoutNode* n) {
  size_t i;
  if (!n) return;
  if (n->kind == ROLLTUI_NODE_WINDOW) {
    note_window(c, n);
    return;
  }
  for (i = 0; i < n->children.n; ++i) walk(c, n->children.v[i]);
}

void rolltui_gaps_collect(RolltuiWindows* w, const RolltuiLayout* l, const RolltuiBindings* b,
                          RolltuiGapReport* out) {
  Collect c;
  size_t i;
  if (!out) return;
  rolltui_gap_report_release(out); /* REPLACES, as every report on this boundary does */
  if (!w || !l) return;
  memset(&c, 0, sizeof c);
  c.w = w;
  c.out = out;

  /* THE WHOLE SCREEN, not the stack: the base, then every popup the layout DECLARES, opened
   * or not. This is the line `sync` cannot cross and the reason this function exists. */
  walk(&c, &l->base.root);
  for (i = 0; i < l->popups.n; ++i) walk(&c, &l->popups.v[i].root);

  /* The declared actions no chord reaches. A HINT rather than a proof — see the header note:
   * a menu item may still invoke one, and whether the host HANDLES it is a `switch` in the
   * host's own loop and is not library-visible. NULL bindings means the host has no table, so
   * there is nothing to be missing from. */
  if (b)
    for (i = 0; i < l->actions.n; ++i) {
      const RolltuiStr* name = &l->actions.v[i].name;
      RolltuiStr* line;
      ++out->named;
      if (rolltui_bindings_chord_count(b, name->p ? name->p : "", name->n)) continue;
      line = gap_add(out);
      rolltui_str_clear(line);
      app(line, "this screen declares '");
      rolltui_str_append_str(line, name);
      app(line, "': no chord reaches it");
    }

  rolltui_str_free(&c.scratch);
}
