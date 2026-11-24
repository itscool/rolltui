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
  RolltuiLayout* l = load(R"({ "resizable": false, "name": "d", "root": { "grid": [
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


// ---- the fifth batch: symbols, areas, meters, flow, gradients, axes ---------------------------------------------------------------

std::string glyph(const Drawn& d, int x, int y) {
  std::size_t len = 0;
  const char* g = rolltui_frame_glyph(d.f, x, y, &len);
  return std::string(g, len);
}

void test_symbols_area_floor() {
  RolltuiChart* c = rolltui_chart_new();
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  const std::vector<double> half(40, 50.0);
  rolltui_chart_set_series(c, 0, "", 0, 0, half.data(), half.size());
  rolltui_chart_set_range(c, 0, 100);
  rolltui_chart_set_kind(c, ROLLTUI_CHART_AREA);
  Drawn a(20, 4);
  rolltui_chart_draw(c, a.f, ds, RolltuiRect{0, 0, 20, 4}, a.styles, 0);
  check(a.braille_at(10, 3) && a.braille_at(10, 2) && glyph(a, 10, 3) == "\xE2\xA3\xBF", "an area fills to its foot: the rows under the value full ⣿");
  check(!a.braille_at(10, 0), "…and nothing above it");
  rolltui_chart_set_symbols(c, ROLLTUI_CHART_SYMBOLS_BLOCKS);
  Drawn b(20, 4);
  rolltui_chart_draw(c, b.f, ds, RolltuiRect{0, 0, 20, 4}, b.styles, 0);
  check(glyph(b, 10, 3) == "\xE2\x96\x88", "in quadrant blocks, a full cell is █");
  rolltui_chart_set_symbols(c, ROLLTUI_CHART_SYMBOLS_SHADES);
  Drawn sh(20, 4);
  rolltui_chart_draw(c, sh.f, ds, RolltuiRect{0, 0, 20, 4}, sh.styles, 0);
  check(glyph(sh, 10, 3) == "\xE2\x96\x88" && sh.braille_cells() == 0, "in shades, a cell a value and no braille");
  // A FLOOR: any value above the least shows
  const std::vector<double> tiny = {0, 0.4, 0, 0.4};
  rolltui_chart_clear(c);
  rolltui_chart_set_kind(c, ROLLTUI_CHART_BARS);
  rolltui_chart_set_series(c, 0, "", 0, 0, tiny.data(), tiny.size());
  rolltui_chart_set_range(c, 0, 100);
  Drawn fl(8, 4);
  rolltui_chart_draw(c, fl.f, ds, RolltuiRect{0, 0, 8, 4}, fl.styles, 0);
  fl.read();
  check(fl.rows[3].find("\xE2\x96\x81") != std::string::npos, "a bar for a value too small to round to a block still shows the least one ▁");
  rolltui_draw_scratch_free(ds);
  rolltui_chart_free(c);
}

void test_meter_auto_flow() {
  RolltuiChart* c = rolltui_chart_new();
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  const double cpu[] = {10, 50};
  rolltui_chart_set_series(c, 0, "cpu", 3, 0, cpu, 2);
  rolltui_chart_set_range(c, 0, 100);
  rolltui_chart_set_kind(c, ROLLTUI_CHART_METER);
  Drawn m(24, 1);
  rolltui_chart_draw(c, m.f, ds, RolltuiRect{0, 0, 24, 1}, m.styles, 0);
  m.read();
  check(m.rows[0].rfind("cpu ", 0) == 0 && m.rows[0].find("\xE2\x96\xA0") != std::string::npos, "a meter: the label, then ■ along the row");
  const RolltuiCellRaw on = m.cell(4, 0), off = m.cell(23, 0);
  check(on.style.fg.r == m.styles[ROLLTUI_ROLE_ACCENT_1].fg.r && off.style.fg.r == m.styles[ROLLTUI_ROLE_TEXT_MUTED].fg.r,
        "…the filled part in the series' colour, the rest a track: the newest value, half way");
  rolltui_chart_set_flags(c, ROLLTUI_CHART_INVERT);
  Drawn inv(24, 1);
  rolltui_chart_draw(c, inv.f, ds, RolltuiRect{0, 0, 24, 1}, inv.styles, 0);
  check(inv.cell(23, 0).style.fg.r == inv.styles[ROLLTUI_ROLE_ACCENT_1].fg.r && inv.cell(4, 0).style.fg.r == inv.styles[ROLLTUI_ROLE_TEXT_MUTED].fg.r,
        "…inverted, it fills from its right");
  rolltui_chart_set_flags(c, 0);
  rolltui_chart_set_kind(c, ROLLTUI_CHART_AUTO);
  Drawn one(24, 1), two(24, 3), tall(24, 8);
  rolltui_chart_draw(c, one.f, ds, RolltuiRect{0, 0, 24, 1}, one.styles, 0);
  rolltui_chart_draw(c, two.f, ds, RolltuiRect{0, 0, 24, 3}, two.styles, 0);
  rolltui_chart_draw(c, tall.f, ds, RolltuiRect{0, 0, 24, 8}, tall.styles, 0);
  one.read();
  two.read();
  check(one.rows[0].find("\xE2\x96\xA0") != std::string::npos && two.braille_cells() == 0 && tall.braille_cells() > 0,
        "by the room: a meter on one row, a sparkline on three, a line on more");
  // FLOW: the newest at the right, the left empty while short; never stretched
  rolltui_chart_clear(c);
  rolltui_chart_set_flags(c, ROLLTUI_CHART_FLOW);
  rolltui_chart_set_kind(c, ROLLTUI_CHART_SPARK);
  const double few[] = {1, 5, 9};
  rolltui_chart_set_series(c, 0, "", 0, 0, few, 3);
  Drawn fw(12, 1);
  rolltui_chart_draw(c, fw.f, ds, RolltuiRect{0, 0, 12, 1}, fw.styles, 0);
  fw.read();
  check(fw.rows[0].size() > 0 && glyph(fw, 0, 0) == " " && glyph(fw, 11, 0) == "\xE2\x96\x88" && glyph(fw, 8, 0) == " ",
        "a flowing sparkline: three values at the right edge, the rest of the row empty [" + fw.rows[0] + "]");
  check(rolltui_chart_capacity(c, 12) == 12, "a sparkline holds a value a cell");
  rolltui_chart_set_kind(c, ROLLTUI_CHART_LINE);
  check(rolltui_chart_capacity(c, 12) == 24, "…a braille line two");
  // A KEPT HISTORY: the newest kept, oldest first
  RolltuiHistory* h = rolltui_history_new(4);
  for (int i = 1; i <= 6; ++i) rolltui_history_push(h, i);
  check(rolltui_history_count(h) == 4, "a history keeps its newest four");
  rolltui_chart_set_kind(c, ROLLTUI_CHART_SPARK);
  rolltui_chart_set_series_history(c, 0, "", 0, 0, h);
  rolltui_chart_set_range(c, 0, 6);
  Drawn hs(6, 1);
  rolltui_chart_draw(c, hs.f, ds, RolltuiRect{0, 0, 6, 1}, hs.styles, 0);
  check(glyph(hs, 5, 0) == "\xE2\x96\x88" && glyph(hs, 1, 0) == " " && glyph(hs, 2, 0) != " ", "…drawn oldest first, 3 4 5 6, padded on the left");
  rolltui_history_resize(h, 2);
  check(rolltui_history_count(h) == 2, "…and resized, it keeps the newest");
  rolltui_history_free(h);
  rolltui_draw_scratch_free(ds);
  rolltui_chart_free(c);
}

void test_gradient_axes() {
  RolltuiGradient g{};
  g.start.kind = g.mid.kind = g.end.kind = RolltuiStyleColorRaw::Kind::Rgb;
  g.start.g = 200;
  g.mid.r = 200, g.mid.g = 200;
  g.end.r = 200;
  RolltuiStyleColorRaw at{};
  rolltui_gradient_at(&g, 0, &at);
  check(at.g == 200 && at.r == 0, "a gradient's start at 0");
  rolltui_gradient_at(&g, 0.5, &at);
  check(at.r == 200 && at.g == 200, "…its mid at a half");
  rolltui_gradient_at(&g, 1, &at);
  check(at.r == 200 && at.g == 0, "…its end at 1");
  rolltui_gradient_at(&g, 0.25, &at);
  check(at.r == 100 && at.g == 200, "…and blended between");
  RolltuiChart* c = rolltui_chart_new();
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  const std::vector<double> full(40, 100.0);
  rolltui_chart_set_series(c, 0, "", 0, 0, full.data(), full.size());
  rolltui_chart_set_range(c, 0, 100);
  rolltui_chart_set_kind(c, ROLLTUI_CHART_AREA);
  rolltui_chart_set_gradient(c, 0, &g);
  Drawn d(20, 5);
  rolltui_chart_draw(c, d.f, ds, RolltuiRect{0, 0, 20, 5}, d.styles, 0);
  check(d.cell(10, 4).style.fg.g == 200 && d.cell(10, 4).style.fg.r == 0 && d.cell(10, 0).style.fg.r == 200 && d.cell(10, 0).style.fg.g == 0,
        "an area coloured by height: its foot the start, its top the end");
  // AXES, and time along the foot
  rolltui_chart_clear(c);
  rolltui_chart_set_series(c, 0, "", 0, 0, full.data(), full.size());
  rolltui_chart_set_range(c, 0, 100);
  rolltui_chart_set_kind(c, ROLLTUI_CHART_BARS);
  rolltui_chart_set_flags(c, ROLLTUI_CHART_AXES | ROLLTUI_CHART_FLOW);
  rolltui_chart_set_time(c, 1700000000, 60);
  Drawn ax(40, 8);
  rolltui_chart_draw(c, ax.f, ds, RolltuiRect{0, 0, 40, 8}, ax.styles, 0);
  ax.read();
  check(ax.rows[6].find("\xE2\x94\x94\xE2\x94\x80") != std::string::npos, "the axes: a line up the left meeting one along the foot └─ [" + ax.rows[6] + "]");
  check(ax.rows[7].find(":") != std::string::npos && ax.rows[7].size() >= 5, "…and HH:MM under it [" + ax.rows[7] + "]");
  rolltui_draw_scratch_free(ds);
  rolltui_chart_free(c);
}

}  // namespace

int main() {
  rolltui_context_set_library_defaults(ctx());
  test_line();
  test_bars_and_spark();
  test_window_and_grid();
  test_symbols_area_floor();
  test_meter_auto_flow();
  test_gradient_axes();
  return testkit::report("chart_test");
}
