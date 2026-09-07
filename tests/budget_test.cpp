//
// budget_test.cpp — THE ALLOCATION BUDGET, IN ctest.
//
// What it measures is a REAL render — a 40-entry document through the shipped `default`
// layout, laid out and composed exactly as a host does it — counted by a replacement
// `operator new` and by the library's own entry point.
//
// THE TRAP THIS FILE IS BUILT AROUND: **a counter that reports zero because it was never
// armed looks exactly like a frame that allocates nothing.** A budget test that has
// quietly stopped counting passes forever and is worse than no test at all, because it
// manufactures confidence. So:
//
//   1. THE BUDGET IS A RANGE, not a ceiling. A count BELOW the floor fails just as loudly
//      as one above it. "It got faster" is not a thing this test may silently accept — it
//      is either a real improvement (re-record deliberately, and say in the journal which
//      change moved which number and by how much) or it is the counter having come
//      unarmed.
//   2. THE COUNTER IS PROVED ARMED ON EVERY RUN, twice and independently: an exact
//      accounting test (allocate a known number of blocks between arm and disarm and
//      demand exactly that many) and a LIVE NEGATIVE CONTROL — a registered widget kind
//      whose draw deliberately wastes allocations, put into a real layout and rendered,
//      which must move the measured number by at least what it wasted. The control is not
//      a patch a human applies once; it runs in ctest, so the day the counter breaks the
//      control fails with it.
//   3. THE NUMBERS CARRY THEIR DATE AND THEIR TOOLCHAIN, and the head-room is stated
//      rather than felt.
//
// WALL-CLOCK IS REPORTED BUT ONLY LOOSELY GATED, and that is deliberate. A tight timing
// assertion in a test suite is the classic source of failures with no cause, and
// "intermittent" is a banned diagnosis here (CLAUDE.md) — so time is gated at a ceiling
// only a catastrophe crosses, and the ALLOCATION counts, which are deterministic for a
// fixed binary and a fixed input, are what actually guard the draw path.
//
// WHY THE COUNTS ARE WORTH GATING AT ALL, since a steady frame is ~1% of a terminal's
// budget and no number here is a performance emergency: the point is the INSTRUMENT. On a
// path asserted at zero, any allocation is a signal with a cause; under a band with
// head-room in it, the same allocation is an argument.
//
// THIS FILE CALLS THE C DIRECTLY, in the idiom `rolltui/examples/paint.cpp` uses: an
// app-lifetime fixture whose plain members are released in one destructor, `rolltui_swap`
// in place of a frame the host resets itself, and a host widget as a
// `RolltuiWidgetPlugin` table.
//
// **ONE GAP, NAMED RATHER THAN WORKED AROUND**: `rolltui_mem_realloc` — the growing-heap
// strategy's realloc half — has no public declaration, and that is deliberate. Its own
// header says why: "growth is the thing the closed set exists to stop being invented, and
// leaving its declaration in an internal header makes that structural rather than a grep
// control's promise." This file's exact-accounting check needs to trigger a GROWING
// realloc specifically (the assertion right below is about how a grow is counted), so it
// is the one call this file cannot get from `rolltui.h` — `rolltui_mem_alloc` and
// `rolltui_mem_free` are public and used directly below, and `rolltui_mem_realloc` is
// reached through the internal `rolltui/c/rolltui_alloc.h`, for that one call. See the
// include for why.
#include <cstdlib>
#include <cstring>
#include <new>
#include <chrono>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_alloc.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_markdown.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
// …AND ONE DELIBERATE REACH PAST IT, which is not a gap. `rolltui_mem_realloc`
// is declared only in the INTERNAL `rolltui/c/rolltui_alloc.h`, on purpose: growth is the thing
// the closed set exists to stop being invented, and keeping its declaration out of the public
// header makes that restriction STRUCTURAL rather than a grep control's promise
// (`rolltui_mem_alloc`/`_free` moved to the public `rolltui_mem.h` and are used directly below).
//
// This file is the one consumer entitled to reach for it, because it is not a consumer of the
// UI API at all — it is the ALLOCATOR'S OWN accounting test, and the assertion below exists to
// prove that a growing realloc counts as an allocation. Publishing `realloc` to satisfy one
// test would undo the restriction for every other caller, which is the trade backwards.
#include "rolltui_test.hpp"
#include "rolltui/c/rolltui_layout.h"  // INTERNAL: this test opts in

using namespace rolltui_test;

// ---- the counter ----------------------------------------------------------------------
// Global replacement operators. `g_on` gates COUNTING, never the allocation itself, so a
// disarmed counter still allocates normally and nothing about the program changes shape
// between armed and disarmed runs.

namespace {
bool g_on = false;
long g_allocs = 0;
long long g_bytes = 0;
}  // namespace

void* operator new(std::size_t n) {
  if (g_on) { ++g_allocs; g_bytes += static_cast<long long>(n); }
  void* p = std::malloc(n ? n : 1);  // malloc(0) may return null; operator new may not
  if (!p) throw std::bad_alloc();
  return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  if (g_on) { ++g_allocs; g_bytes += static_cast<long long>(n); }
  return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return operator new(n, t); }
// Over-aligned forms: libc++ routes some containers through these, and a missing overload
// would allocate through the DEFAULT operator and be invisible to the count — a silent
// hole in the instrument, which is the one thing this file may not have.
void* operator new(std::size_t n, std::align_val_t a) {
  if (g_on) { ++g_allocs; g_bytes += static_cast<long long>(n); }
  void* p = nullptr;
  if (posix_memalign(&p, static_cast<std::size_t>(a) < sizeof(void*) ? sizeof(void*) : static_cast<std::size_t>(a),
                     n ? n : 1) != 0)
    throw std::bad_alloc();
  return p;
}
void* operator new[](std::size_t n, std::align_val_t a) { return operator new(n, a); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace {

// `rolltui::mem::Stats`'s SHAPE, not its rule: `Memory.hpp` stays included only for the one
// `rolltui::mem::realloc` call the header comment names, so every READ of the counters goes
// through the public `rolltui_mem_stats` into a plain local struct instead.
struct MemStats {
  std::size_t allocations = 0, frees = 0, bytes_requested = 0, live_bytes = 0, peak_bytes = 0, live_blocks = 0;
};
MemStats mem_stats() {
  MemStats s;
  rolltui_mem_stats(&s.allocations, &s.frees, &s.bytes_requested, &s.live_bytes, &s.peak_bytes, &s.live_blocks);
  return s;
}

struct Cost {
  long allocs = 0;
  long long bytes = 0;
  long micros = 0;
};

// Runs `fn` with the counter armed. Nothing outside `fn` is counted, so building the
// scene never lands in a frame's number.
//
// TWO SOURCES, ADDED TOGETHER. The replacement `operator new` above sees every C++
// container; it does NOT see `rolltui::mem`, which is a `malloc` wrapper. The Frame's
// cells, links and spilled glyphs are behind `rolltui_mem_alloc`, so a counter reading
// only `operator new` would report zero for them — exactly the failure this file's header
// is built around, aimed at its own counter. `mem_stats()` is read across the same window
// and the deltas are summed.
template <typename F>
Cost measure(F&& fn) {
  const auto t0 = std::chrono::steady_clock::now();
  g_allocs = 0;
  g_bytes = 0;
  const MemStats m0 = mem_stats();
  g_on = true;
  fn();
  g_on = false;
  const MemStats m1 = mem_stats();
  Cost c;
  c.allocs = g_allocs + static_cast<long>(m1.allocations - m0.allocations);
  c.bytes = g_bytes + static_cast<long long>(m1.bytes_requested - m0.bytes_requested);
  c.micros = static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
  return c;
}

std::string entry_text(int i) {
  // Varied markdown, so compose does the work a real transcript makes it do: headings,
  // prose that wraps, a list, a fenced block, inline code and a link.
  std::string s = "## Entry " + std::to_string(i) + "\n\n";
  s += "Some prose that is long enough to wrap at any sensible width, with `inline code` "
       "and a [link](https://example.invalid/path) in it, for entry " + std::to_string(i) + ".\n\n";
  s += "- first item\n- second item, rather longer than the first so that it wraps too\n- third\n\n";
  s += "```c\nint value_" + std::to_string(i) + " = " + std::to_string(i * 7) + ";\nreturn value_" + std::to_string(i) + ";\n```\n";
  return s;
}

// windows.prepare(stack, box)'s three steps, done directly — there is no single
// `rolltui_windows_prepare`; a host composes sync + autosize + layout itself
// (`rolltui/tools/paint.cpp`'s `App::prepare` is the worked example).
void windows_prepare(RolltuiWindows* w, RolltuiWindowStack* s, RolltuiRect box) {
  rolltui_windows_sync(w, s);
  rolltui_windows_autosize(w, s, box);
  rolltui_windows_layout(w, s, box);
}

// The scene: a 40-entry document in the shipped `default` layout, with every source a
// host binds actually bound. Deliberately NOT a preset store — no file is read, so the
// numbers cannot depend on the machine running them.
struct Scene {
  RolltuiContext* ctx = rolltui_context_new();  // OWNED: this scene's session
  RolltuiDocument doc{};
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* effects = nullptr;
  RolltuiWindows* windows = rolltui_windows_new(ctx);
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  RolltuiComposeScratch* compose_scratch = rolltui_compose_scratch_new();
  // The scene calls the SWAP rather than resetting a frame it owns, which is what makes
  // "exactly as a host paints it" true of this code and not only of the comment.
  // `rolltui_swap_begin` IS a frame reset plus lending the pointer back
  // (`rolltui/c/rolltui_swap.c`), so it introduces no allocation shape of its own.
  RolltuiSwap* swap = rolltui_swap_new(0, 0, RolltuiStyle{});

  Scene() {
    rolltui_context_set_library_defaults(ctx);
    effects = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);

    std::size_t json_n = 0;
    const char* json = rolltui_layout_builtin_json("default", 7, &json_n);
    RolltuiLoadedLayout loaded{};
    rolltui_loaded_layout_init(&loaded);
    std::size_t defaults_n = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(rolltui_test::test_context(), &defaults_n);
    RolltuiLayoutReport rep{};
    rolltui_load_layout_text_into(json, json_n, &loaded, defaults, defaults_n, rolltui_layout_default_hooks(), &rep);
    RolltuiLayout layout{};
    rolltui_layout_init(&layout);
    rolltui_loaded_layout_to_layout(&loaded, &layout);
    rolltui_loaded_layout_release(&loaded);
    rolltui_layout_report_release(&rep);
    rolltui_window_stack_set_base(stack, &layout.base);  // COPIES; `layout` need not outlive this
    rolltui_layout_release(&layout);

    for (int i = 0; i < 40; ++i) {
      RolltuiDocEntry* e = rolltui_document_add(&doc);
      set_str(e->id, "e" + std::to_string(i));
      e->markdown = 1;
      set_str(e->text, entry_text(i));
    }
    rolltui_windows_bind_document(windows, "session", 7, &doc);
    rolltui_windows_bind_rows(
        windows, "status", 6,
        [](void*, RolltuiRows* out) {
          rolltui_rows_add(out, "theme", 5, "default-dark", 12);
          rolltui_rows_add(out, "layout", 6, "default", 7);
          rolltui_rows_add(out, "size", 4, "120x40", 6);
          rolltui_rows_add(out, "depth", 5, "truecolor", 9);
        },
        nullptr, nullptr);
    rolltui_windows_bind_submit(
        windows, "prompt", 6, [](void*, const char*, std::size_t) {}, nullptr, nullptr, /*on_submit=*/0);

    rolltui_context_set_bindings(ctx, rolltui_bindings_default(rolltui_test::test_context()));
    const RolltuiWidgetEnv env{0, 1};
    rolltui_context_set_env(ctx, &env);
  }
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;
  ~Scene() {
    rolltui_swap_free(swap);
    rolltui_compose_scratch_free(compose_scratch);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(windows);
    rolltui_effect_map_free(effects);
    rolltui_document_release(&doc);
    rolltui_context_free(ctx);  // LAST
  }

  // One frame, exactly as a host paints it: prepare (instantiate, autosize, lay out),
  // then compose into the frame the SWAP lends.
  //
  // THE INSTRUMENT MUST PAINT THE WAY A HOST PAINTS, or it measures a path nobody runs.
  // A host that builds a fresh frame every repaint and throws it away spends ~153 KB at
  // 120x40 that a budget over a reused frame never sees, so this scene reuses through the
  // swap, as the shipped hosts do. `rolltui_swap_present` — which turns a frame into the
  // bytes a terminal would receive — is never called here: this suite measures the draw
  // path, and diffing/output is a different (and differently measured) concern.
  void paint(int w, int h) {
    const RolltuiRect box{0, 0, w, h};
    windows_prepare(windows, stack, box);
    RolltuiFrame* f = rolltui_swap_begin(swap, w, h, styles[ROLLTUI_ROLE_BACKGROUND]);
    rolltui_window_stack_compose(stack, f, box, styles, rolltui_layout_default_roles(), draw_slot, this,
                                 /*ambiguous_wide=*/0, compose_scratch);
  }

  static void draw_slot(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
    Scene* s = static_cast<Scene*>(ctx);
    rolltui_windows_draw(s->windows, rn, f, s->styles, rolltui_windows_default_roles());
  }
};

// THE LIVE NEGATIVE CONTROL. A registered kind whose draw wastes a known number of
// allocations, so the measured cost of a layout containing it must exceed the same
// layout's without it by at least that much. It runs on every ctest run: the counter
// cannot come unarmed without this failing.
//
// `Widget` is deleted along with `Widgets.hpp`, so the control is a
// `RolltuiWidgetPlugin` — the same nine-slot table `rolltui/tools/paint.cpp`'s `Canvas`
// fills — rather than a C++ subclass. `ctx` carries its own draw scratch (CLAUDE.md's
// caller-owns-working-memory rule) and a BORROW of the `Windows` it was built for, which is
// how a plugin reaches the live style table at draw time (`rolltui_windows_styles`, the same
// call `Canvas::draw` makes).
constexpr int kWasted = 500;
bool g_waste = false;  // the control's one variable: the SAME scene, measured twice

struct WastefulCtx {
  RolltuiWindows* windows;  // BORROWED
  RolltuiDrawScratch* draw_scratch;
};

void wasteful_destroy(void* ctx) {
  WastefulCtx* c = static_cast<WastefulCtx*>(ctx);
  rolltui_draw_scratch_free(c->draw_scratch);
  delete c;
}
void wasteful_layout(void*, const RolltuiResolvedNode*) {}
void wasteful_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  if (!g_waste) return;
  // Heap-allocated, escaping any small-buffer optimisation, and used — so no compiler
  // may fold it away. A control that gets optimised out is a control that lies.
  WastefulCtx* c = static_cast<WastefulCtx*>(ctx);
  std::string sink;
  for (int i = 0; i < kWasted; ++i) {
    std::vector<int> v(8, i);  // one allocation each, unambiguously
    sink += static_cast<char>('a' + (v[0] % 26));
  }
  const std::string glyph = sink.substr(0, 8);
  const RolltuiStyle* styles = rolltui_windows_styles(c->windows);
  const RolltuiStyle text_style = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT);
  rolltui_frame_put_text(f, c->draw_scratch, rn->inner.x, rn->inner.y, glyph.data(), glyph.size(), text_style,
                         rn->inner.w, 0, 0);
}

constexpr RolltuiWidgetPlugin kWastefulPlugin = {
    wasteful_destroy, wasteful_layout, wasteful_draw, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};

RolltuiWidget wasteful_factory(void* ctx, RolltuiWindows*, const char*, std::size_t) {
  RolltuiWindows* windows = static_cast<RolltuiWindows*>(ctx);
  return RolltuiWidget{&kWastefulPlugin, new WastefulCtx{windows, rolltui_draw_scratch_new()}};
}

std::string fmt(const Cost& c) {
  return std::to_string(c.allocs) + " allocs / " + std::to_string(c.bytes / 1024) + " KB / " + std::to_string(c.micros) + " us";
}

// A range with STATED head-room. `lo` fails as loudly as `hi`: a count that collapses is
// the counter coming unarmed until proved otherwise (see the header).
bool in_range(long v, long lo, long hi) { return v >= lo && v <= hi; }

}  // namespace

int main() {
  // ---- the counter is armed, and counts EXACTLY -------------------------------------
  // The most direct form of the check: allocate a known number of blocks and demand that
  // number back. If this ever reads 0, everything below is meaningless.
  {
    const Cost c = measure([] {
      std::vector<std::unique_ptr<int>> keep;
      keep.reserve(64);  // one allocation
      for (int i = 0; i < 63; ++i) keep.push_back(std::make_unique<int>(i));
    });
    check(c.allocs == 64, "the counter is ARMED and exact: 64 allocations measured as " + std::to_string(c.allocs));
    check(c.bytes > 0, "…and it accumulates bytes (" + std::to_string(c.bytes) + ")");
    const Cost quiet = measure([] {});
    check(quiet.allocs == 0, "…and an empty body measures zero, so the count is the body's and not the harness's");
    // THE SECOND HALF OF THE COUNTER, PROVED ARMED FOR THE SAME REASON THE FIRST IS. With
    // the Frame's cells, link table and spilled glyphs come from
    // `rolltui::mem`, which the replacement `operator new` cannot see. If this term were
    // dead, a steady frame would read zero for the wrong reason — and would keep reading it
    // however much the C allocated. It ran in both configurations, so the day the addition
    // is dropped the test fails whichever way the flag is set.
    const Cost owned = measure([] {
      void* p = rolltui_mem_alloc(4096);
      rolltui_mem_free(p);
    });
    check(owned.allocs == 1 && owned.bytes >= 4096,
          "…and the counter sees rolltui::mem too, which operator new cannot [" + std::to_string(owned.allocs) +
              " allocs / " + std::to_string(owned.bytes) + " B]");
  }

  // ---- the baseline ------------------------------------------------------------------
  // MEASURED on Apple Clang / macOS, Release. The head-room is ±25% on allocations and
  // ±35% on bytes: wide enough that an incidental change in a container's growth does not
  // fail the suite, tight enough that a new per-cell or per-grapheme allocation cannot
  // hide. Re-record DELIBERATELY, with a journal entry saying which change moved which
  // number and by how much.
  Scene scene;
  // Warm: the transcript's layout cache is a memo, and measuring a cold cache would be
  // measuring the cache and not the draw path (the plan: "the layout cache is
  // already doing its job — this phase must not touch it").
  for (int i = 0; i < 3; ++i) scene.paint(120, 40);

  const Cost steady = measure([&] { scene.paint(120, 40); });
  std::printf("  steady-state 120x40 : %s\n", fmt(steady).c_str());

  // A streaming frame: the last entry grows, so exactly one entry re-lays.
  scene.doc.back().text += "\nanother streamed line arrives.\n";
  scene.doc.back().version++;
  const Cost streaming = measure([&] { scene.paint(120, 40); });
  std::printf("  streaming    120x40 : %s\n", fmt(streaming).c_str());

  // A resize: every entry re-wraps at the new width. The most expensive frame there is,
  // and the one a user makes by dragging a corner.
  const Cost resize = measure([&] { scene.paint(100, 40); });
  std::printf("  resize       →100x40: %s\n", fmt(resize).c_str());
  for (int i = 0; i < 3; ++i) scene.paint(120, 40);  // back to the baseline width

  // MEASURED on Apple Clang / macOS / Release. **The bands are ±2%, and that number is
  // evidence rather than caution.** Three consecutive runs gave 887 / 2772 / 70272 with no
  // variation at all: an allocation count is a deterministic function of this binary and
  // this input, which is exactly why it is worth gating on where wall-clock is not.
  //
  // THE FIRST DRAFT USED ±25% AND WAS NOT A GATE. Its control — forty wasted allocations
  // added to `Transcript::draw` — moved the steady frame 887 → 928 and the assertion still
  // PASSED, because a quarter of 887 is 222. A budget that cannot see a per-entry
  // regression on a 40-entry document is decoration: 40 allocations is 4.5%, and 4.5% was
  // inside the head-room. The head-room existed to survive a TOOLCHAIN change, not to
  // absorb our own changes, and sizing it for the former had silently disabled the latter.
  //
  // WHICH FRAME IS THE SENSITIVE GATE, stated because ±2% means very different things at
  // 887 and at 70,272: the STEADY frame is the fine one (±17 allocations), and it is where
  // a draw-path regression shows up first — the control's forty wasted allocations moved
  // all three numbers by exactly +41, and only the steady band refused it. The streaming
  // and resize bands are ±55 and ±1,405, so they catch gross changes and not per-entry
  // ones. That is the right split rather than a compromise: a per-entry cost is visible in
  // the steady frame too, and the big frames are dominated by work the steady one does not
  // do, so a tight absolute band on them would fail on a toolchain change while telling us
  // nothing the steady frame had not already said.
  //
  // So: when one of these fails, read the delta it prints. A change of OURS that moved it
  // is the finding this file exists to produce — quote the before/after in the journal
  // and re-record. A toolchain change that moved it is a re-record too, but a deliberate
  // one, and the same act either way: look at the number before you write it down.
  // ±2% with a floor of ±3, because 2% of 144 is 2 and a band that tight would fail on a
  // single incidental allocation rather than on a regression worth reading about. The
  // counts are still bit-identical across runs at the new numbers (checked three times).
  auto band = [](long v, double pct) {
    const long slack = std::max<long>(3, static_cast<long>(v * pct));
    return std::pair<long, long>{std::max<long>(0, v - slack), v + slack};
  };
  auto delta = [](long got, long want) {
    const long d = got - want;
    return std::string(d >= 0 ? "+" : "") + std::to_string(d) + " vs the recorded " + std::to_string(want);
  };
  // WHAT THESE FOUR NUMBERS ARE, and what it takes to move one.
  //
  // A STEADY frame is asserted at exactly 0 further down. The other two are recorded with
  // bands because they are ALLOWED to allocate: a re-lay builds the layout cache, which
  // must outlive the frame. What is asserted about them is that they do not GROW.
  //
  // The reductions that produced them are the same finding four times over — storage that
  // was invented per call became storage the caller already owned:
  //   - `graphemes()` allocated four vectors per call plus three more inside
  //     `grapheme_boundaries`, on a path that runs for every string drawn and every span
  //     of every row. Reusing those buffers — same algorithm, same UAX #29 answers,
  //     conformance suites green — was worth about half of a steady frame on its own.
  //   - a SPAN OWNS NOTHING. It is an offset and a length into pools the caller's store
  //     owns, not a string and two vectors per span.
  //   - a span COPIED into another line is a descriptor, or is not copied at all: the
  //     transcript puts a body line behind its prefix by REFERENCING the body's spans.
  //   - a document is PARSED once per (id, version), not once per width.
  //
  // WHAT A RESIZE'S REMAINING ALLOCATIONS ARE, since the number is otherwise hard to read:
  // a SECOND resize to the same width costs 2, so all but two of them are buffers reaching
  // a high-water mark they never leave — GROWING, AMORTISED, by name. Most are
  // `rolltui::mem` growing the stores' pools, and the rest are each entry's own wrap engine
  // growing its line array, because a NARROWER width makes more lines than that entry had
  // ever needed before.
  //
  // POOLED STORAGE IS WHY A RE-PARSE IS CHEAP: the block tree is index arrays over one
  // byte pool, so re-parsing the streaming entry refills buffers it already had rather
  // than reconstructing owning containers. `rolltui/c/rolltui_md_lines.h` and
  // `rolltui/c/rolltui_wrap.h` state the data model; a handed-over wrap result is
  // immutable with all three of its sizes known when it exists, so its handle and its
  // three arrays are ONE allocation with the arrays carved out.
  //
  // BYTES AND COUNTS MOVE INDEPENDENTLY, and a change that moves only one is not a mistake
  // in the instrument. Taking a `std::string` out of `Cell` cut 4,800 constructions and 16
  // bytes per cell while leaving every allocation count at +0, because those strings were
  // small enough never to reach the heap.
  constexpr long kStreaming = 6, kResize = 80;
  // THE TARGET, and it is an equality: no band, no head-room, no floor.
  check(steady.allocs == 0, "A STEADY FRAME ALLOCATES NOTHING [" + fmt(steady) + "]");
  check(steady.bytes == 0, "…and takes no bytes: nothing is constructed either [" + std::to_string(steady.bytes) + " B]");

  {
    auto [lo, hi] = band(kStreaming, 0.02);
    check(in_range(streaming.allocs, lo, hi), "a streaming frame re-lays ONE entry [" + fmt(streaming) + "; " +
                                                  delta(streaming.allocs, kStreaming) + ", band " + std::to_string(lo) + "-" +
                                                  std::to_string(hi) + "]");
  }
  check(streaming.allocs > steady.allocs, "…and costs more than a steady frame, which is the only ordering that makes sense");
  // THE EXPENSIVE ONE: a resize re-wraps every entry, and it is the one frame a user can
  // actually feel — they make it by dragging a corner, repeatedly. It is the number to
  // watch when a change touches wrapping, layout or the stores.
  {
    auto [lo, hi] = band(kResize, 0.02);
    check(in_range(resize.allocs, lo, hi), "a resize re-wraps every entry [" + fmt(resize) + "; " + delta(resize.allocs, kResize) +
                                               ", band " + std::to_string(lo) + "-" + std::to_string(hi) + "]");
  }
  check(resize.allocs > steady.allocs * 10, "…and it is an order of magnitude more than a steady frame, not a rounding error");
  // Wall-clock, gated only where a catastrophe lives — see the header for why this is not
  // a tight assertion.
  check(steady.micros < 20000,
        "a steady-state frame is far inside a terminal's ~16 ms budget [" + std::to_string(steady.micros) + " us]");

  // ---- THE LIVE NEGATIVE CONTROL -----------------------------------------------------
  // The same scene twice: once as the shipped layout, once with ONE window's content
  // swapped for a kind that wastes a known number of allocations. If the counter has come
  // unarmed, or has stopped measuring the DRAW path specifically, this cannot move.
  //
  // The factory is registered on THIS Windows, not on any other. That is not a detail:
  // `rolltui_widget_kind_register` puts the NAME in the CONTEXT's layout vocabulary and
  // `rolltui_windows_register_kind` puts the FACTORY in one Windows, so registering the name
  // and pointing the factory at the wrong instance leaves the layout naming a kind that
  // instance cannot build — which draws an error panel and measures LOWER. The first draft
  // of this control did exactly that and read 887 → 815, i.e. it "failed" by getting
  // cheaper. Worth keeping in the comment: a control that moves the wrong way is still
  // telling you something.
  {
    Scene control;
    const int verdict = rolltui_widget_kind_register(control.ctx, "wasteful", 8, ROLLTUI_SOURCE_FORBIDDEN, "", 0);
    check(verdict == ROLLTUI_REGISTER_OK,
          "registered a deliberately wasteful widget kind on the Windows that will draw it [verdict " +
              std::to_string(verdict) + "]");
    if (verdict == ROLLTUI_REGISTER_OK)
      rolltui_context_register_kind(control.ctx, "wasteful", 8, wasteful_factory, control.windows, nullptr);
    RolltuiLayoutNode* status = rolltui_window_stack_find(control.stack, "status", 6);
    check(status != nullptr, "the shipped layout has the window the control draws into");
    if (status) status->content = "wasteful";
    for (int i = 0; i < 3; ++i) control.paint(120, 40);
    const Cost before = measure([&] { control.paint(120, 40); });

    // THE SAME SCENE, MEASURED TWICE, with one boolean between the runs. The first draft
    // swapped a window's CONTENT instead, which meant the replaced widget's own cost came
    // out of the delta (887 → 1248 for 500 wasted: the `rows` widget it displaced was
    // worth 139 of them). A control whose arithmetic has two unknowns in it is not a
    // control — so the waste is a switch on one widget, and the delta is only the waste.
    g_waste = true;
    for (int i = 0; i < 3; ++i) control.paint(120, 40);
    const Cost after = measure([&] { control.paint(120, 40); });
    g_waste = false;
    std::printf("  control      120x40 : %ld allocs → %ld with a kind that wastes %d\n", before.allocs, after.allocs, kWasted);
    check(after.allocs - before.allocs >= kWasted,
          "THE COUNTER IS ARMED ON THE DRAW PATH: a widget wasting " + std::to_string(kWasted) +
              " allocations moves the frame's number by at least that [" + std::to_string(before.allocs) + " → " +
              std::to_string(after.allocs) + "]");
    // This deliberately does NOT assert `before.allocs > 0`. Two zero readings would be
    // what a DEAD counter reports, but the un-wasteful frame is legitimately zero — that
    // is the target — so armed-ness is carried instead by the DELTA above and by the
    // exact-accounting check at the top of this file. What is asserted here is the half
    // that still means something with a zero baseline.
    check(after.allocs > 0 && before.allocs == 0,
          "…and the un-wasteful frame is zero while the wasteful one is not: the counter reads a REAL difference [" +
              std::to_string(before.allocs) + " → " + std::to_string(after.allocs) + "]");
  }

  // ---- the library's own entry point, and what it can honestly claim ----------------
  // the runtime half: the numbers above come from replacing the global operator new,
  // which only a TEST can do. `rolltui::mem` is the same counting in the LIBRARY, readable
  // by a host at runtime — one pipeline, two consumers.
  {
    const MemStats before = mem_stats();
    void* a = rolltui_mem_alloc(128);
    void* b = rolltui_mem_realloc(a, 256);
    rolltui_mem_free(b);
    const MemStats after = mem_stats();
    // A GROWING REALLOC COUNTS AS AN ALLOCATION, because it hands out new storage and
    // copies into it. Counting it as neither would make a buffer that grows through
    // `realloc` look free next to one whose every growth is a counted `operator new`.
    // `live_blocks` is tracked separately, so it still says one block.
    check(after.allocations == before.allocations + 2 && after.frees == before.frees + 1,
          "rolltui::mem counts alloc→realloc→free as two allocations and one free (a grow IS new storage)");
    check(after.live_blocks == before.live_blocks && mem_stats().live_blocks == before.live_blocks,
          "…and live_blocks says the grow was not a new BLOCK, which is the other half of the same fact");
    check(after.bytes_requested == before.bytes_requested + 128 + 256, "…and accumulates the bytes requested");
    check(rolltui_mem_alloc(0) == nullptr, "a zero-byte request is a nullptr, not a one-byte block");
    rolltui_mem_free(nullptr);  // must be a no-op
    check(mem_stats().frees == after.frees, "…and freeing nullptr counts nothing");
    // ---- MEMORY USAGE, QUERYABLE AT RUNTIME -----------------------------
    // `bytes_requested` is cumulative and answers "how much did we churn"; it CANNOT answer
    // "how much are we holding", which is the question a status pane asks. These three
    // assertions are what keep the two from being confused — and what keep `live_bytes` from
    // being a gauge stuck at zero, which would look exactly like a library that allocates
    // nothing.
    {
      const MemStats base = mem_stats();
      void* big = rolltui_mem_alloc(64 * 1024);
      const MemStats held = mem_stats();
      check(held.live_bytes >= base.live_bytes + 64 * 1024,
            "live_bytes RISES by at least what was asked for [" + std::to_string(base.live_bytes) + " → " +
                std::to_string(held.live_bytes) + "]");
      check(held.peak_bytes >= held.live_bytes, "…and peak_bytes is never below what is live right now");
      rolltui_mem_free(big);
      const MemStats after_free = mem_stats();
      check(after_free.live_bytes == base.live_bytes,
            "…and FALLS back exactly on free, which is what makes it a gauge and not a counter");
      check(after_free.peak_bytes >= held.live_bytes, "…while peak_bytes REMEMBERS the high-water mark");
      check(after_free.bytes_requested > base.bytes_requested,
            "…and bytes_requested only ever goes up: it is churn, not occupancy");

      // WHAT THE GAUGE CAN HONESTLY SEE. CLAUDE.md's rule is that the library's entry
      // point covers only its OWN explicit allocations: in C++ `std::string` and
      // `std::vector` go through the global `operator new` and are invisible to it, while
      // in C every allocation is an explicit call and the rule is TOTAL. By this point a
      // 40-entry scene has been painted several times and is still held, so the gauge is
      // being asked about a real workload rather than a toy.
      //
      // The markdown PARSE TREE is the sharpest case, because it is exactly the kind of
      // structure a C++ implementation would build out of `std::vector<Block>` holding
      // `std::string`s and hand the gauge nothing to see. Here it is index arrays over one
      // byte pool, so the whole parse is VISIBLE — asserted below on one line rather than
      // described.
      check(base.live_bytes > 100000, "the gauge reports REAL occupancy for the painted scene [" +
                                          std::to_string(base.live_bytes) + " B]");
      {
        const MemStats before_parse = mem_stats();
        static constexpr char kMd[] =
            "# A heading\n\nA paragraph with *emphasis* and a [link](https://example.com/some/path).\n\n"
            "- one\n- two\n- three\n\n```cpp\nint x = 1;\nint y = 2;\n```\n\n> a quote\n";
        RolltuiMdDoc* parsed = rolltui_md_doc_new();
        rolltui_md_parse(parsed, kMd, sizeof(kMd) - 1);
        const MemStats after_parse = mem_stats();
        const long long delta =
            static_cast<long long>(after_parse.live_bytes) - static_cast<long long>(before_parse.live_bytes);
        check(rolltui_md_doc_block_count(parsed) > 0, "…the control's own subject exists: the parse produced blocks");
        check(delta > 0, "the gauge SEES the whole parse tree [" + std::to_string(delta) +
                             " B] — in C every allocation is an explicit call, so the entry point is TOTAL");
        rolltui_md_doc_free(parsed);
      }
    }
    // THE HONEST LIMIT, asserted rather than only documented: `std::string` and
    // `std::vector` do NOT route through this entry point, so these figures cover the
    // library's own explicit allocations and no more. Any C++ a host writes around the
    // library is outside them. A test that pretended otherwise would be the exact
    // "instrument that under-reports while looking healthy" failure this file exists for.
    const MemStats s0 = mem_stats();
    { std::vector<int> v(1000, 7); (void)v; }
    check(mem_stats().allocations == s0.allocations,
          "a std::vector allocates WITHOUT touching rolltui::mem: this entry point sees the library's own "
          "allocations and nothing a C++ container does");
  }

  return report("rolltui budget_test");
}
