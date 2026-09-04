  // RE-RECORDED 2026-09-03 at the close of m5b. Phase 13 end to end:
  //   steady   887 →   0   streaming 2772 → 289   resize 70272 → 11180   steady KB 455 → 0
  //
  // **A STEADY FRAME ALLOCATES NOTHING, and that is asserted as `== 0` rather than as a
  // band.** This is the phase's target and the reason for it is the instrument, not the
  // speed: at `6 ± 3` a reading of 7 is an argument about head-room; at 0, any allocation
  // at all is a signal with a cause. There is no floor left for the next accidental one to
  // hide under.
  //
  // The other two frames keep bands, because they are ALLOWED to allocate — they are the
  // phase's named exceptions (a re-lay builds the layout cache, which must outlive the
  // frame). What is asserted about them is that they do not GROW.
//
// budget_test.cpp — Phase 13 m1: THE BUDGET, IN ctest, BEFORE ANYTHING IS OPTIMISED.
//
// Every later milestone of this phase is judged by this file, which is why it comes
// first: optimising before the instrument exists means none of it can be shown to have
// worked. What it measures is a REAL render — a 40-entry document through the shipped
// `default` layout, laid out and composed exactly as a host does it — counted by a
// replacement `operator new`.
//
// THE TRAP THIS FILE IS BUILT AROUND, and it is this project's oldest: **a counter that
// reports zero because it was never armed looks exactly like a frame that allocates
// nothing.** A budget test that has quietly stopped counting passes forever and is worse
// than no test at all, because it manufactures confidence. So:
//
//   1. THE BUDGET IS A RANGE, not a ceiling. A count BELOW the floor fails just as loudly
//      as one above it. "It got faster" is not a thing this test may silently accept —
//      it is either a real improvement (re-record, deliberately, with a journal entry) or
//      it is the counter having come unarmed.
//   2. THE COUNTER IS PROVED ARMED ON EVERY RUN, twice and independently: an exact
//      accounting test (allocate a known number of blocks between arm and disarm and
//      demand exactly that many) and a LIVE NEGATIVE CONTROL — a registered widget kind
//      whose draw deliberately wastes allocations, put into a real layout and rendered,
//      which must move the measured number by at least what it wasted. The control is not
//      a patch a human applies once; it runs in ctest, so the day the counter breaks the
//      control fails with it.
//   3. THE NUMBERS CARRY THEIR DATE, and the head-room is stated rather than felt.
//
// WALL-CLOCK IS REPORTED BUT ONLY LOOSELY GATED, and that is deliberate. A tight timing
// assertion in a test suite is the classic source of failures with no cause, and
// "intermittent" is a banned diagnosis here (CLAUDE.md) — so time is gated at a ceiling
// only a catastrophe crosses, and the ALLOCATION counts, which are deterministic for a
// fixed binary and a fixed input, are what actually guard the draw path.
//
// WHAT THIS TEST DELIBERATELY DOES NOT DO: judge whether the numbers are good. They are
// not — 946 allocations to repaint an unchanged screen is the finding that scoped this
// phase — but 203 µs is 1.2% of a terminal frame, so nothing here is a performance
// emergency and no milestone may claim otherwise (plan/phase-13.md).
//
#include <cstdlib>
#include <cstring>
#include <new>
#include <chrono>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "rolltui/Document.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Memory.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Widgets.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
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

struct Cost {
  long allocs = 0;
  long long bytes = 0;
  long micros = 0;
};

// Runs `fn` with the counter armed. Nothing outside `fn` is counted, so building the
// scene never lands in a frame's number.
template <typename F>
Cost measure(F&& fn) {
  const auto t0 = std::chrono::steady_clock::now();
  g_allocs = 0;
  g_bytes = 0;
  g_on = true;
  fn();
  g_on = false;
  Cost c;
  c.allocs = g_allocs;
  c.bytes = g_bytes;
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

// The scene: a 40-entry document in the shipped `default` layout, with every source a
// host binds actually bound. Deliberately NOT a preset store — no file is read, so the
// numbers cannot depend on the machine running them.
struct Scene {
  Document doc;
  Windows windows;
  WindowStack stack;
  Theme theme = *builtin_theme("default-dark");

  Scene() : stack(*builtin_layout("default")) {
    for (int i = 0; i < 40; ++i) {
      DocEntry e;
      e.id = "e" + std::to_string(i);
      e.markdown = true;
      e.text = entry_text(i);
      doc.entries.push_back(std::move(e));
    }
    windows.bind_document("session", &doc);
    windows.bind_rows("status", [](Rows& out) {
      out.add("theme", "default-dark");
      out.add("layout", "default");
      out.add("size", "120x40");
      out.add("depth", "truecolor");
    });
    windows.bind_submit("prompt", [](const std::string&) {});
    WidgetEnv env;
    env.now_ms = 1;
    windows.set_env(env);
  }

  // One frame, exactly as a host paints it: prepare (instantiate, autosize, lay out),
  // then compose into a fresh Frame.
  void paint(int w, int h, Frame& into) {
    const Rect box{0, 0, w, h};
    windows.prepare(stack, box);
    into.reset(w, h, theme.style(Role::background));  // m5: reuse, exactly as a host does
    stack.compose(into, box, theme, [&](const ResolvedNode& rn, Frame& f) { windows.draw(rn, f, theme); });
  }
};

// THE LIVE NEGATIVE CONTROL. A registered kind whose draw wastes a known number of
// allocations, so the measured cost of a layout containing it must exceed the same
// layout's without it by at least that much. It runs on every ctest run: the counter
// cannot come unarmed without this failing.
constexpr int kWasted = 500;
bool g_waste = false;  // the control's one variable: the SAME scene, measured twice

class WastefulWidget : public Widget {
 public:
  void layout(const ResolvedNode&) override {}
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    if (!g_waste) return;
    // Heap-allocated, escaping any small-buffer optimisation, and used — so no compiler
    // may fold it away. A control that gets optimised out is a control that lies.
    std::string sink;
    for (int i = 0; i < kWasted; ++i) {
      std::vector<int> v(8, i);  // one allocation each, unambiguously
      sink += static_cast<char>('a' + (v[0] % 26));
    }
    f.put_text(rn.inner.x, rn.inner.y, sink.substr(0, 8), theme.style(Role::text), rn.inner.w);
  }
};

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
  }

  // ---- the baseline ------------------------------------------------------------------
  // MEASURED 2026-09-03 on Apple Clang / macOS, Release. The head-room is ±25% on
  // allocations and ±35% on bytes: wide enough that an incidental change in a container's
  // growth does not fail the suite, tight enough that a new per-cell or per-grapheme
  // allocation cannot hide. Re-record DELIBERATELY, with a journal entry saying which
  // milestone moved which number and by how much — that is the whole point of the phase.
  Scene scene;
  Frame frame;
  // Warm: the transcript's layout cache is a memo, and measuring a cold cache would be
  // measuring the cache and not the draw path (plan/phase-13.md: "the layout cache is
  // already doing its job — this phase must not touch it").
  for (int i = 0; i < 3; ++i) scene.paint(120, 40, frame);

  const Cost steady = measure([&] { scene.paint(120, 40, frame); });
  std::printf("  steady-state 120x40 : %s\n", fmt(steady).c_str());

  // A streaming frame: the last entry grows, so exactly one entry re-lays.
  scene.doc.entries.back().text += "\nanother streamed line arrives.\n";
  scene.doc.entries.back().version++;
  const Cost streaming = measure([&] { scene.paint(120, 40, frame); });
  std::printf("  streaming    120x40 : %s\n", fmt(streaming).c_str());

  // A resize: every entry re-wraps at the new width. The most expensive frame there is,
  // and the one a user makes by dragging a corner.
  const Cost resize = measure([&] { scene.paint(100, 40, frame); });
  std::printf("  resize       →100x40: %s\n", fmt(resize).c_str());
  for (int i = 0; i < 3; ++i) scene.paint(120, 40, frame);  // back to the baseline width

  // MEASURED 2026-09-03, Apple Clang / macOS / Release. **The bands are ±2%, and that
  // number is evidence rather than caution.** Three consecutive runs gave 887 / 2772 /
  // 70272 with no variation at all: an allocation count is a deterministic function of
  // this binary and this input, which is exactly why it is worth gating on where
  // wall-clock is not.
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
  // is the finding this phase exists for — quote the before/after in the journal (m3's
  // rule) and re-record. A toolchain change that moved it is a re-record too, but a
  // deliberate one, and the same act either way: look at the number before you write it
  // down.
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
  // RE-RECORDED 2026-09-03 by Phase 13 m3, which is the only reason these may move. The
  // numbers this test was born with, and what m3 did to them:
  //
  //   steady 120x40    887 → 144   (-84%)   streaming  2772 →   815   (-71%)
  //   resize →100x40 70272 → 24944 (-65%)   steady KB   455 →   248   (-45%)
  //
  // One change earned most of it: `unicode::graphemes()` was allocating FOUR vectors per
  // call (decode, codepoints, boundaries, result) plus three more inside
  // `grapheme_boundaries`, on a path that runs for every string drawn and every span of
  // every row. Reusing those buffers — same algorithm, same UAX #29 answers, conformance
  // suites untouched and still green — took 887 to 448 on its own.
  // RE-RECORDED 2026-09-03 at the end of m5b. Phase 13 end to end:
  //   steady   887 →   6   streaming 2772 → 303   resize 70272 → 11467   steady KB 455 → 0
  // A steady frame is SIX allocations, all in widget draws: four in the `rows:` window and
  // one in the transcript's. `prepare`, `resolve`, the frame reset and `compose` with no
  // slot renderer are all EXACTLY ZERO. The target is 0 and this is not it; the six are
  // itemised in plan/phase-13.md m5b.
  constexpr long kStreaming = 289, kResize = 11180;
  // BYTES RE-RECORDED 2026-09-03 by m4 (248 KB → 173 KB); the COUNTS did not move at all,
  // and that was the prediction stated before the change was written: taking `std::string`
  // out of `Cell` deletes 4,800 constructions and 16 bytes per cell, but those strings were
  // SSO and never reached the heap. The budget said exactly that by failing on bytes alone
  // and passing all three allocation assertions with +0.
  // THE PHASE'S TARGET, and it is an equality: no band, no head-room, no floor.
  check(steady.allocs == 0, "A STEADY FRAME ALLOCATES NOTHING [" + fmt(steady) + "]");
  check(steady.bytes == 0, "…and takes no bytes: nothing is constructed either [" + std::to_string(steady.bytes) + " B]");

  {
    auto [lo, hi] = band(kStreaming, 0.02);
    check(in_range(streaming.allocs, lo, hi), "a streaming frame re-lays ONE entry [" + fmt(streaming) + "; " +
                                                  delta(streaming.allocs, kStreaming) + ", band " + std::to_string(lo) + "-" +
                                                  std::to_string(hi) + "]");
  }
  check(streaming.allocs > steady.allocs, "…and costs more than a steady frame, which is the only ordering that makes sense");
  // THE EXPENSIVE ONE, and the number this phase should be judged against: a resize
  // re-wraps every entry, and at ~70k allocations and several milliseconds it is the one
  // frame a user can actually feel — they make it by dragging a corner, repeatedly. It is
  // recorded here rather than acted on, because m1 does not optimise anything.
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
  // `register_kind` puts the NAME in the process-wide layout vocabulary and the FACTORY in
  // one Windows, so registering on the wrong instance leaves the layout naming a kind that
  // instance cannot build — which draws an error panel and measures LOWER. The first draft
  // of this control did exactly that and read 887 → 815, i.e. it "failed" by getting
  // cheaper. Worth keeping in the comment: a control that moves the wrong way is still
  // telling you something.
  {
    Scene control;
    std::string why;
    check(control.windows.register_kind("wasteful", [] { return std::make_unique<WastefulWidget>(); },
                                        SourceRule::Forbidden, "", &why),
          "registered a deliberately wasteful widget kind on the Windows that will draw it [" + why + "]");
    Node* status = control.stack.find("status");
    check(status != nullptr, "the shipped layout has the window the control draws into");
    if (status) status->content = "wasteful";
    Frame f;
    for (int i = 0; i < 3; ++i) control.paint(120, 40, f);
    const Cost before = measure([&] { control.paint(120, 40, f); });

    // THE SAME SCENE, MEASURED TWICE, with one boolean between the runs. The first draft
    // swapped a window's CONTENT instead, which meant the replaced widget's own cost came
    // out of the delta (887 → 1248 for 500 wasted: the `rows` widget it displaced was
    // worth 139 of them). A control whose arithmetic has two unknowns in it is not a
    // control — so the waste is a switch on one widget, and the delta is only the waste.
    g_waste = true;
    for (int i = 0; i < 3; ++i) control.paint(120, 40, f);
    const Cost after = measure([&] { control.paint(120, 40, f); });
    g_waste = false;
    std::printf("  control      120x40 : %ld allocs → %ld with a kind that wastes %d\n", before.allocs, after.allocs, kWasted);
    check(after.allocs - before.allocs >= kWasted,
          "THE COUNTER IS ARMED ON THE DRAW PATH: a widget wasting " + std::to_string(kWasted) +
              " allocations moves the frame's number by at least that [" + std::to_string(before.allocs) + " → " +
              std::to_string(after.allocs) + "]");
    // This used to assert `before.allocs > 0`, on the reasoning that two zero readings
    // would be what a DEAD counter reports. m5b made that assumption stale: the
    // un-wasteful frame is now legitimately zero, which is the phase's whole target. The
    // armed-ness is carried by the DELTA above and by the exact-accounting check at the top
    // of this file, both of which are unaffected — so what is asserted here is the half
    // that still means something.
    check(after.allocs > 0 && before.allocs == 0,
          "…and the un-wasteful frame is zero while the wasteful one is not: the counter reads a REAL difference [" +
              std::to_string(before.allocs) + " → " + std::to_string(after.allocs) + "]");
  }

  // ---- the library's own entry point, and what it can honestly claim ----------------
  // Phase 13's runtime half: the numbers above come from replacing the global operator new,
  // which only a TEST can do. `rolltui::mem` is the same counting in the LIBRARY, readable
  // by a host at runtime — one pipeline, two consumers.
  {
    const mem::Stats before = mem::stats();
    void* a = mem::alloc(128);
    void* b = mem::realloc(a, 256);
    mem::free(b);
    const mem::Stats after = mem::stats();
    check(after.allocations == before.allocations + 1 && after.frees == before.frees + 1,
          "rolltui::mem counts one allocation and one free for alloc→realloc→free (a realloc that GREW is not a new block)");
    check(after.bytes_requested == before.bytes_requested + 128 + 256, "…and accumulates the bytes requested");
    check(mem::alloc(0) == nullptr, "a zero-byte request is a nullptr, not a one-byte block");
    mem::free(nullptr);  // must be a no-op
    check(mem::stats().frees == after.frees, "…and freeing nullptr counts nothing");
    // THE HONEST LIMIT, asserted rather than only documented: std::string and std::vector
    // do NOT route through this in C++, so these figures cover the library's own explicit
    // allocations and no more. A test that pretended otherwise would be the exact
    // "instrument that under-reports while looking healthy" failure this file exists for.
    const mem::Stats s0 = mem::stats();
    { std::vector<int> v(1000, 7); (void)v; }
    check(mem::stats().allocations == s0.allocations,
          "a std::vector allocates WITHOUT touching rolltui::mem — in C++ this entry point is partial by "
          "construction, and Phase 14's verdict is what reports how partial");
  }

  return report("rolltui budget_test");
}
