//
// chart_test.cpp — NUMBERS DRAWN: a line in braille, bars and a sparkline in eighth blocks, the range and the series' names, a
// `chart:` window bound by a host, and a dashboard's grid of windows.
//
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to read cells back */
#include "rolltui_test.hpp"

using testkit::check;

namespace {

RolltuiContext* ctx() { return rolltui_test::test_context(); }

struct Drawn {
  RolltuiFrame* f = nullptr;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = nullptr;
  std::vector<std::string> rows;
  Drawn(int w, int h) {
    fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
    f = rolltui_frame_new(w, h, styles[ROLLTUI_ROLE_BACKGROUND]);
  }
  Drawn(const Drawn&) = delete;
  ~Drawn() {
    rolltui_frame_free(f);
    rolltui_effect_map_free(fx);
  }
  void read() {
    RolltuiStr t{};
    rolltui_frame_to_text(f, &t);
    const std::string all = str_of(t);
    rolltui_str_free(&t);
    rows.clear();
    std::size_t at = 0;
    while (at < all.size()) {
      const std::size_t nl = all.find('\n', at);
      rows.push_back(all.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
      if (nl == std::string::npos) break;
      at = nl + 1;
    }
  }
  int braille_cells() const {
    int n = 0;
    for (int y = 0; y < rolltui_frame_height(f); ++y)
      for (int x = 0; x < rolltui_frame_width(f); ++x) {
        std::size_t len = 0;
        const char* g = rolltui_frame_glyph(f, x, y, &len);
        if (len == 3 && static_cast<unsigned char>(g[0]) == 0xE2 && (static_cast<unsigned char>(g[1]) & 0xFC) == 0xA0) ++n;
      }
    return n;
  }
  bool braille_at(int x, int y) const {
    std::size_t len = 0;
    const char* g = rolltui_frame_glyph(f, x, y, &len);
    return len == 3 && static_cast<unsigned char>(g[0]) == 0xE2 && (static_cast<unsigned char>(g[1]) & 0xFC) == 0xA0;
  }
  RolltuiCellRaw cell(int x, int y) const {
    RolltuiCellRaw c{};
    rolltui_frame_cell(f, x, y, &c);
    return c;
  }
};

bool has(const std::vector<std::string>& rows, std::string_view s) {
  for (const std::string& r : rows)
    if (r.find(s) != std::string::npos) return true;
  return false;
}

void test_line() {
  RolltuiChart* c = rolltui_chart_new();
  std::vector<double> up, flat;
  for (int i = 0; i <= 50; ++i) up.push_back(i * 2.0), flat.push_back(30);
  rolltui_chart_set_series(c, 0, "cpu", 3, 0, up.data(), up.size());
  rolltui_chart_set_series(c, 1, "mem", 3, 0, flat.data(), flat.size());
  rolltui_chart_set_range(c, 0, 100);
  Drawn d(40, 10);
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  rolltui_chart_draw(c, d.f, ds, RolltuiRect{0, 0, 40, 10}, d.styles, 0);
  d.read();
  check(d.rows[0].find("cpu") == 0 && d.rows[0].find("mem") != std::string::npos, "the series' names on the top row [" + d.rows[0] + "]");
  check(d.cell(0, 0).style.bold && d.cell(0, 0).style.fg.r == d.styles[ROLLTUI_ROLE_ACCENT_1].fg.r &&
            d.cell(5, 0).style.fg.r == d.styles[ROLLTUI_ROLE_ACCENT_2].fg.r,
        "…each bold, in the accents in order");
  check(d.rows[1].find("100") == 0 && d.rows[9].find("0 ") == 2, "the range on the left, right-aligned: 100 at the top, 0 at the foot [" + d.rows[1] + "|" + d.rows[9] + "]");
  check(d.braille_cells() > 30, "the line is drawn in braille [" + std::to_string(d.braille_cells()) + " cells]");
  check(d.braille_at(4, 9) && d.braille_at(39, 1), "a rising series runs from the foot on the left to the top on the right");
  // a gap where a value is NaN, and the newest kept when there are more than fit
  std::vector<double> gap = {10, 10, 10, std::numeric_limits<double>::quiet_NaN(), 90, 90, 90};
  rolltui_chart_clear(c);
  rolltui_chart_set_series(c, 0, "", 0, 0, gap.data(), gap.size());
  rolltui_chart_set_range(c, 0, 100);
  Drawn g(20, 6);
  rolltui_chart_draw(c, g.f, ds, RolltuiRect{0, 0, 20, 6}, g.styles, 0);
  g.read();
  bool joined = false;
  for (int y = 1; y < 5; ++y) joined = joined || g.braille_at(10, y);
  check(!joined, "a NaN is a gap: the line is not drawn across it");
  std::vector<double> many;
  for (int i = 0; i < 500; ++i) many.push_back(i < 450 ? 0 : 100);
  rolltui_chart_clear(c);
  rolltui_chart_set_series(c, 0, "", 0, 0, many.data(), many.size());
  Drawn m(30, 6);
  rolltui_chart_draw(c, m.f, ds, RolltuiRect{0, 0, 30, 6}, m.styles, 0);
  check(m.braille_at(29, 0), "more values than fit shows the newest: the last fifty, high, reach the top right");
  // THE RANGE IS WHAT IS SHOWN: a spike long scrolled off no longer sets the scale for the values on screen.
  std::vector<double> spiked(400, 20.0);
  spiked[0] = 5000;
  for (std::size_t i = 300; i < spiked.size(); ++i) spiked[i] = 20 + static_cast<double>(i % 10);
  rolltui_chart_clear(c);
  rolltui_chart_set_series(c, 0, "", 0, 0, spiked.data(), spiked.size());
  Drawn sp(30, 6);
  rolltui_chart_draw(c, sp.f, ds, RolltuiRect{0, 0, 30, 6}, sp.styles, 0);
  sp.read();
  check(sp.rows[0].find("29") == 0 && !has(sp.rows, "5k"), "a spike scrolled off no longer sets the scale: the top is the shown most [" + sp.rows[0] + "]");
  rolltui_draw_scratch_free(ds);
  rolltui_chart_free(c);
}

void test_bars_and_spark() {
  RolltuiChart* c = rolltui_chart_new();
  const double v[3] = {1, 2, 4};
  rolltui_chart_set_kind(c, ROLLTUI_CHART_BARS);
  rolltui_chart_set_series(c, 0, "", 0, 0, v, 3);
  Drawn d(12, 8);
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  rolltui_chart_draw(c, d.f, ds, RolltuiRect{0, 0, 12, 8}, d.styles, 0);
  d.read();
  // the tallest bar is the last: the column of its last cell, full from the top row to the foot, read from the frame's cells
  auto full_at = [&](int x, int y) {
    std::size_t len = 0;
    const char* g = rolltui_frame_glyph(d.f, x, y, &len);
    return len == 3 && std::string(g, len) == "\xE2\x96\x88";
  };
  int last = -1;
  for (int x = 0; x < 12; ++x)
    if (full_at(x, 0)) last = x;
  int full = 0, first_short = -1;
  for (int y = 0; y < 8; ++y) full += last >= 0 && full_at(last, y);
  for (int x = 0; x < 12 && first_short < 0; ++x)
    if (full_at(x, 7) && !full_at(x, 0)) first_short = x;
  check(full == 8, "bars stand on zero in full blocks, the tallest the whole height");
  check(first_short >= 0 && first_short < last, "…and shorter ones beside it");
  Drawn a(12, 8);
  rolltui_chart_draw(c, a.f, ds, RolltuiRect{0, 0, 12, 8}, a.styles, 1);
  a.read();
  check(has(a.rows, "#") && !has(a.rows, "\xE2\x96\x88"), "where ambiguous glyphs are wide, bars are ASCII");
  const double s[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  rolltui_chart_clear(c);
  rolltui_chart_set_kind(c, ROLLTUI_CHART_SPARK);
  rolltui_chart_set_series(c, 0, "", 0, 0, s, 8);
  Drawn sp(8, 1);
  rolltui_chart_draw(c, sp.f, ds, RolltuiRect{0, 0, 8, 1}, sp.styles, 0);
  sp.read();
  check(sp.rows[0] == "\xE2\x96\x81\xE2\x96\x82\xE2\x96\x83\xE2\x96\x84\xE2\x96\x85\xE2\x96\x86\xE2\x96\x87\xE2\x96\x88",
        "a sparkline is a block a value, from the least to the most [" + sp.rows[0] + "]");
  rolltui_draw_scratch_free(ds);
  rolltui_chart_free(c);
}

// ---- a chart window, and a grid of them ----------------------------------------------------------------------------------------

void fill_cpu(void*, RolltuiChart* out) {
  const double v[6] = {10, 40, 20, 80, 60, 90};
  rolltui_chart_set_series(out, 0, "cpu", 3, 0, v, 6);
  rolltui_chart_set_range(out, 0, 100);
}

std::vector<std::string> paint(RolltuiWindows* w, const RolltuiLayout* l, int wd, int ht) {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  rolltui_window_stack_set_base(stack, rolltui_layout_base(l));
  const RolltuiRect area{0, 0, wd, ht};
  rolltui_windows_sync(w, stack);
  rolltui_windows_layout(w, stack, area);
  RolltuiFrame* f = rolltui_frame_new(wd, ht, styles[ROLLTUI_ROLE_BACKGROUND]);
  RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
  struct Slot {
    RolltuiWindows* w;
    RolltuiStyle* s;
  } slot{w, styles};
  rolltui_window_stack_compose(
      stack, f, area, styles, rolltui_layout_default_roles(),
      [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
        Slot* sl = static_cast<Slot*>(c);
        rolltui_windows_draw(sl->w, rn, fr, sl->s, rolltui_windows_default_roles());
      },
      &slot, 0, cs);
  RolltuiStr t{};
  rolltui_frame_to_text(f, &t);
  const std::string all = str_of(t);
  rolltui_str_free(&t);
  std::vector<std::string> rows;
  std::size_t at = 0;
  while (at < all.size()) {
    const std::size_t nl = all.find('\n', at);
    rows.push_back(all.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
    if (nl == std::string::npos) break;
    at = nl + 1;
  }
  rolltui_compose_scratch_free(cs);
  rolltui_frame_free(f);
  rolltui_window_stack_free(stack);
  rolltui_effect_map_free(fx);
  return rows;
}

RolltuiLayout* load(std::string_view json, std::string* problems) {
  RolltuiLayoutReport rep{};
  std::size_t nd = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
  RolltuiLayout* l = rolltui_load_layout_text(json.data(), json.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
  for (std::size_t i = 0; i < rep.bad_values_n; ++i) *problems += str_of(rep.bad_values[i]) + "; ";
  rolltui_layout_report_release(&rep);
  return l;
}

void test_window_and_grid() {
  std::string problems;
  RolltuiLayout* l = load(R"({ "name": "d", "root": { "grid": [
      [ { "id": "cpu", "content": "chart:cpu", "border": "single", "title": "cpu" }, { "id": "b", "content": "text:top right" } ],
      [ { "id": "c", "content": "text:bottom left" }, { "id": "d", "content": "chart:x" } ] ] } })",
                          &problems);
  check(l && problems.empty(), "a grid of rows of cells loads [" + problems + "]");
  RolltuiWindows* w = rolltui_windows_new(ctx());
  rolltui_windows_bind_chart(w, "cpu", 3, fill_cpu, nullptr, nullptr);
  const auto rows = paint(w, l, 60, 16);
  check(rows[0].find("cpu") != std::string::npos && rows[0].find("top right") != std::string::npos &&
            rows[8].find("bottom left") == 0,
        "the cells are its quadrants, rows of the grid and cells across");
  bool braille = false;
  for (int y = 1; y < 8; ++y) braille = braille || rows[y].find("\xE2\xA0") != std::string::npos || rows[y].find("\xE2\xA1") != std::string::npos ||
                                        rows[y].find("\xE2\xA2") != std::string::npos || rows[y].find("\xE2\xA3") != std::string::npos;
  check(braille, "a chart window draws what its binding filled");
  check(has(rows, "nothing is bound to 'x'"), "a chart nothing is bound to says so");
  rolltui_windows_free(w);
  rolltui_layout_free(l);
}

}  // namespace

int main() {
  rolltui_context_set_library_defaults(ctx());
  test_line();
  test_bars_and_spark();
  test_window_and_grid();
  return testkit::report("chart_test");
}
