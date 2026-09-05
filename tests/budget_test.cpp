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
#include "rolltui/Markdown.hpp"
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
//
// TWO SOURCES, ADDED TOGETHER, AND THE SECOND IS PHASE 14's DOING. The replacement
// `operator new` above sees every C++ container; it does NOT see `rolltui::mem`, which is a
// `malloc` wrapper. That was harmless while every allocation in a frame was a container's —
// and it became a HOLE IN THE INSTRUMENT the moment the port put the Frame's cells,
// links and spilled glyphs behind `rolltui_mem_alloc`. A budget that reports zero because it
// cannot see the allocator is exactly the failure this file's header is built around, aimed
// at its own counter, so `mem::stats()` is read across the same window and the deltas are
// summed. In the C++ build nothing on the frame path called `rolltui::mem`, so the
// second term is 0 and every recorded number below means what it did before.
template <typename F>
Cost measure(F&& fn) {
  const auto t0 = std::chrono::steady_clock::now();
  g_allocs = 0;
  g_bytes = 0;
  const mem::Stats m0 = mem::stats();
  g_on = true;
  fn();
  g_on = false;
  const mem::Stats m1 = mem::stats();
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
    // m5's REUSE path. **NOT what a host did — corrected 2026-09-04, when the comment claimed
    // it was and `Frame::reset` had zero callers outside this file: all three hosts built
    // `Frame f(w, h, fill)` fresh every repaint and `prev = std::move(f)`, so each allocated and
    // freed a whole frame per paint (~153 KB at 120x40) while this budget reported zero.**
    //
    // ONE HOST DOES IT NOW (Phase 17 m3): `rolltui-paint` presents through `rolltui_swap`,
    // whose `begin` calls `rolltui_frame_reset`, so its repaint lands inside this measurement
    // rather than beside it. The studio and `TuiFrontend` are m3's remainder and still build a
    // frame per repaint, which is why this note stays until they follow. When this suite is
    // converted (m2c), the scene below should call the SWAP rather than `reset` directly —
    // that is what makes "exactly as a host paints it" true of the sentence AND the code.
    into.reset(w, h, theme.style(Role::background));
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
    // THE SECOND HALF OF THE COUNTER, PROVED ARMED FOR THE SAME REASON THE FIRST IS. With
    // the Frame's cells, link table and spilled glyphs come from
    // `rolltui::mem`, which the replacement `operator new` cannot see. If this term were
    // dead, a steady frame would read zero for the wrong reason — and would keep reading it
    // however much the C allocated. It ran in both configurations, so the day the addition
    // is dropped the test fails whichever way the flag is set.
    const Cost owned = measure([] {
      void* p = mem::alloc(4096);
      mem::free(p);
    });
    check(owned.allocs == 1 && owned.bytes >= 4096,
          "…and the counter sees rolltui::mem too, which operator new cannot [" + std::to_string(owned.allocs) +
              " allocs / " + std::to_string(owned.bytes) + " B]");
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
  // RE-RECORDED 2026-09-04 by Phase 14 m3, and this is the FIRST TIME THE TWO
  // CONFIGURATIONS NEED TWO NUMBERS. m1 and m2 were identical either way and the journal
  // said so; the wrap engine is not:
  //
  //             steady   streaming   resize    steady KB   resize KB
  //   C++ (OFF)      0         286    10941            0        1381
  //   C   (ON)       0         267    10297            0        1368
  //
  // **THE C IS CHEAPER, AND THE FIRST VERSION OF THIS COMMENT SAID THE OPPOSITE.** It is
  // worth keeping the wrong version's reasoning, because the mistake is the instructive
  // part. The first cut had C at 291/11140 against C++'s 286/10941, and explained the gap
  // as the small-string optimisation: `wrap()` hands over a result by copying three buffers
  // into a fresh handle, which is four allocations in C, while a `std::string` holds a short
  // total inline. That explanation was CORRECT and COMPLETE as far as it went — a probe
  // counted **281 `wrap()` results in the resize frame, 200 with a total text of 1..22
  // bytes**, libc++'s inline capacity, and the C's wrap cost exactly 281 x 4 = 1,124
  // allocations against C++'s 924; on a warm resize the difference was exactly 200.
  //
  // **What was wrong was the conclusion drawn from it: "C cannot do this" instead of "the C
  // is not written well enough yet".** A handed-over result is immutable and all three of
  // its sizes are known the moment it exists, so the handle and its three arrays are ONE
  // allocation with the arrays carved out — which is what `rolltui_wrap_clone` now does.
  // 1,124 became 281, and the number that had been 200 worse than C++ became 643 better.
  // Three `std::` containers cannot follow: each owns its own block by definition. So the
  // real finding points the other way from the first one — see rolltui/c/rolltui_wrap.h.
  //
  // The OFF numbers moved too (289 -> 286, 11180 -> 10941) and that is m3's own doing: the
  // soft-break cut used to build a tail `std::vector` and a tail `std::string` per wrapped
  // line, and the shared-buffer data model the port forced deleted both (rolltui/WrapCpp.cpp).
  // RE-RECORDED 2026-09-04 by Phase 15 m4, and this is the largest single move the budget
  // has ever recorded:
  //
  //             steady   streaming     resize      streaming KB   resize KB   resize us
  //   C++ (OFF)      0    286 →  37   10941 → 122      39 → 9    1383 → 134   6150 → 3358
  //   C   (ON)       0    267 →  13   10297 →  82      39 → 7    1368 → 127
  //
  // **A RESIZE FRAME IS 122 ALLOCATIONS, DOWN FROM 10,941 — 98.9%** — and a SECOND resize to
  // the same width is **2**, measured in both configurations while both existed. Three things did it, all of
  // them the same finding (`plan/phase-15.md` m4, `rolltui/c/rolltui_md_lines.h`):
  //   - a SPAN OWNS NOTHING. It was a `std::string` and two vectors per span; it is an
  //     offset and a length into pools the caller's store owns. That was m1's 4,128.
  //   - a span COPIED into another line is a descriptor, or not copied at all: the
  //     transcript puts a body line behind its prefix by REFERENCING the body's spans.
  //     That was m1's 2,283.
  //   - a document is PARSED once per (id, version) and not once per width. That was m1's
  //     1,243, and it is the one the port only surfaced rather than forced.
  //
  // WHAT THE 122 ARE, and they are a different KIND of number from the 10,941: since a
  // second resize to the same width costs 2, all but two of them are buffers reaching a
  // high-water mark they never leave — GROWING, AMORTISED, by name, and something the old
  // 10,941 could never become. 80 are `rolltui::mem` growing the stores' pools; 40 are each
  // entry's own wrap engine growing its line array, because a NARROWER width makes more
  // lines than that entry had ever needed before.
  //
  // **THE C IS 40 CHEAPER ON A RESIZE AND 24 CHEAPER ON A STREAMING FRAME, and both gaps
  // have one cause: what a re-parse costs when the storage is pooled.** The C++ block tree
  // is `std::vector<Block>` holding `std::string`s, so re-parsing the streaming entry
  // reconstructs owning containers; the C's is index arrays over one byte pool, so it
  // refills buffers it already had. Neither is a better algorithm — it is the same design in
  // two languages, and only one of them has a default that allocates.
  //
  // **C RE-RECORDED 2026-09-04 by Phase 15 m5e: streaming 13 → 6 and resize 82 → 80**, and
  // the number went DOWN, which this test fails on as loudly as an increase — the floor is
  // there so that a collapse has to be explained rather than enjoyed. The cause is the
  // transcript itself, which was the last module of the layer to port:
  // every per-frame working buffer it needs is a `rolltui_grow` array on the handle that
  // reaches a high-water mark and stays — the cluster array the cell walk decodes into, the
  // plain wrap's per-grapheme source offsets, the match list, and the three per-entry arrays
  // `build` fills. The C++ implementation re-creates several of those per call (a
  // `std::vector<RolltuiMdFoldState>` per relaid entry, a `std::string` per unfolded-text
  // cache fill, a `Scratch` per `for_each_cell` instantiation). Same design, two languages,
  // and only one of them has a default that allocates. The C++ numbers did not move at all,
  // which is the control: `TranscriptCpp.cpp` IS the code that was there.
  constexpr long kStreaming = 6, kResize = 80;
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
    // RE-RECORDED by Phase 14 m3, and the change is the point: a GROWING REALLOC counts as
    // an allocation now, because it hands out new storage and copies into it. It used to
    // count as neither, which made the C implementation — where every buffer grows through
    // `realloc` — look free next to a C++ one whose every `std::vector` growth is a counted
    // `operator new`. `live_blocks` is tracked separately, so it still says one block.
    check(after.allocations == before.allocations + 2 && after.frees == before.frees + 1,
          "rolltui::mem counts alloc→realloc→free as two allocations and one free (a grow IS new storage)");
    check(after.live_blocks == before.live_blocks && mem::stats().live_blocks == before.live_blocks,
          "…and live_blocks says the grow was not a new BLOCK, which is the other half of the same fact");
    check(after.bytes_requested == before.bytes_requested + 128 + 256, "…and accumulates the bytes requested");
    check(mem::alloc(0) == nullptr, "a zero-byte request is a nullptr, not a one-byte block");
    mem::free(nullptr);  // must be a no-op
    check(mem::stats().frees == after.frees, "…and freeing nullptr counts nothing");
    // ---- MEMORY USAGE, QUERYABLE AT RUNTIME (Phase 14 m4) -----------------------------
    // `bytes_requested` is cumulative and answers "how much did we churn"; it CANNOT answer
    // "how much are we holding", which is the question a status pane asks. These three
    // assertions are what keep the two from being confused — and what keep `live_bytes` from
    // being a gauge stuck at zero, which would look exactly like a library that allocates
    // nothing.
    {
      const mem::Stats base = mem::stats();
      void* big = mem::alloc(64 * 1024);
      const mem::Stats held = mem::stats();
      check(held.live_bytes >= base.live_bytes + 64 * 1024,
            "live_bytes RISES by at least what was asked for [" + std::to_string(base.live_bytes) + " → " +
                std::to_string(held.live_bytes) + "]");
      check(held.peak_bytes >= held.live_bytes, "…and peak_bytes is never below what is live right now");
      mem::free(big);
      const mem::Stats after_free = mem::stats();
      check(after_free.live_bytes == base.live_bytes,
            "…and FALLS back exactly on free, which is what makes it a gauge and not a counter");
      check(after_free.peak_bytes >= held.live_bytes, "…while peak_bytes REMEMBERS the high-water mark");
      check(after_free.bytes_requested > base.bytes_requested,
            "…and bytes_requested only ever goes up: it is churn, not occupancy");

      // WHAT THE GAUGE CAN HONESTLY SEE, and it differs by configuration — which makes this
      // the sharpest measurement Phase 14 has of its own central claim. CLAUDE.md says the
      // library's entry point covers only its OWN explicit allocations in C++, because
      // `std::string` and `std::vector` go through the global `operator new`, and that in C
      // the same rule would be TOTAL since every allocation is an explicit call. By this
      // point in the test a 40-entry scene has been painted several times and is still held,
      // so the gauge is being asked about a real workload rather than a toy:
      //
      // RE-AIMED 2026-09-04 by Phase 15 m4, and the reason is a finding rather than a
      // relaxation. This used to assert `live_bytes == 0` in the C++ build, because every
      // byte of a painted scene was in a `std::string` or a `std::vector`. The span store is
      // C in both configurations (it is DATA an implementation fills, not an algorithm
      // the flag chooses — `rolltui/c/rolltui_md_lines.h`), so the C++ build routes real
      // occupancy through the entry point too.
      //
      // **The LIMIT it existed to assert has not gone away; its SUBJECT moved**, and the
      // assertion moved with it rather than being deleted. The markdown PARSE TREE is the
      // sharpest subject it has ever had, because the flag decides what the tree IS:
      // `std::vector<Block>` holding `std::string`s in the C++ build, index arrays over
      // one byte pool with it ON. So the SAME parse is INVISIBLE to the gauge in one
      // configuration and VISIBLE in the other — the partial-in-C++/total-in-C claim,
      // measured on one line instead of described.
      check(base.live_bytes > 100000, "the gauge reports REAL occupancy for the painted scene [" +
                                          std::to_string(base.live_bytes) + " B]");
      {
        const mem::Stats before_parse = mem::stats();
        markdown::Document parsed = markdown::parse(
            "# A heading\n\nA paragraph with *emphasis* and a [link](https://example.com/some/path).\n\n"
            "- one\n- two\n- three\n\n```cpp\nint x = 1;\nint y = 2;\n```\n\n> a quote\n");
        const mem::Stats after_parse = mem::stats();
        const long long delta =
            static_cast<long long>(after_parse.live_bytes) - static_cast<long long>(before_parse.live_bytes);
        check(parsed.block_count() > 0, "…the control's own subject exists: the parse produced blocks");
        check(delta > 0, "the gauge SEES the whole parse tree [" + std::to_string(delta) +
                             " B] — in C every allocation is an explicit call, so the entry point is TOTAL");
      }
    }
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
