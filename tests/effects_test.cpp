//
// effects_test.cpp — Phase 12 m6. Motion is the theme's, a state is the widget's
// (rolltui/Effects.hpp), and this file is where the claim stops being a paragraph.
//
// The two properties are asserted OVER EVERY REGISTERED KIND — the seven built-ins plus
// two host kinds registered here, one well-behaved and one that deliberately misbehaves:
//
//   1. an effect never changes a span's WIDTH
//   2. an effect never writes OUTSIDE its span
//
// The misbehaving kind is the point of the sweep and not an extra case: it returns a
// two-cell glyph for a one-cell cell, an empty one, and a style — and the frame outside
// its span, and every cell width inside it, must come out identical anyway. That is the
// difference between a guarantee and a convention, and it is what makes "a host may
// register a kind" safe to offer at all.
//
// The tick rule ("no wakeups with no marks") is asserted as a property of the pure
// function BOTH hosts route through, over every built-in theme including the two that map
// every state — so it cannot be true only for the theme that happens to be loaded.
//
#include "rolltui/Effects.hpp"

#include <string>
#include <vector>

#include "rolltui/Json.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {

// A theme mapping ONE state to one spec, so a kind can be exercised on its own.
Theme theme_with(EffectState state, EffectSpec spec) {
  Theme t = *builtin_theme("default-dark");
  t.effects = EffectMap{};
  t.effects.for_state(state).push_back(std::move(spec));
  return t;
}

// Every kind, with a spec that gives it what it needs — the sweep's table. A kind with
// no frames or no roles is a legitimate theme file (and must not crash), but it would
// also do nothing, which is not what the properties need to be tested against.
struct KindCase {
  std::string kind;
  EffectSpec spec;
};

std::vector<KindCase> kind_cases() {
  std::vector<KindCase> out;
  for (const std::string& name : effect_kind_names()) {
    EffectSpec s;
    s.kind = name;
    s.period_ms = 400;
    s.roles = {Role::accent_1, Role::accent_2, Role::error};
    s.frames = {"a", "b", "c"};  // one cell each: what a well-formed theme file carries
    if (name == "wide-liar") s.frames = {"\xE4\xBD\xA0"};  // 2 cells — refused, never written
    out.push_back({name, std::move(s)});
  }
  return out;
}

struct CellShot {
  std::string text;
  std::uint8_t width;
  bool continuation;
  Style style;
  std::uint32_t link;
  bool operator==(const CellShot&) const = default;
};

std::vector<CellShot> shoot(const Frame& f) {
  std::vector<CellShot> out;
  for (int y = 0; y < f.height(); ++y)
    for (int x = 0; x < f.width(); ++x) {
      const Cell c = f.at(x, y);  // BY VALUE (Phase 14 m2): the frame lends no reference
      out.push_back({std::string(f.glyph(x, y)), c.width, c.continuation != 0, c.style, c.link});
    }
  return out;
}

// A frame with a known pattern, a run of text on row 1 including a WIDE glyph, and
// sentinel text on every other row.
Frame make_frame(int w, int h, const Theme& theme) {
  Frame f(w, h, theme.style(Role::background));
  for (int y = 0; y < h; ++y)
    f.put_text(0, y, "0123456789abcdefghij", theme.style(Role::text), w);
  if (h > 1) f.put_text(2, 1, "ab\xE4\xBD\xA0"  // two narrow, one wide (2 cells)
                              "cdefghij",
                        theme.style(Role::md_code_block), w);
  return f;
}

}  // namespace

int main() {
  // ---- the vocabulary --------------------------------------------------------------
  {
    check(effect_state_from_name("waiting") == EffectState::Waiting && effect_state_name(EffectState::Flash) == "flash",
          "state names round-trip");
    check(effect_state_from_name("nope") == EffectState::count_, "an unknown state name is count_, not a silent 'none'");
    EffectMap empty;
    check(empty.empty() && empty.for_state(EffectState::Waiting).empty(), "a theme that maps nothing is empty by construction");
  }

  // ---- the two rungs ---------------------------------------------------------------
  {
    const std::vector<std::string> names = effect_kind_names();
    for (const char* k : {"spinner", "ellipsis", "bar", "pulse", "shimmer", "gradient", "blink"})
      check(effect_kind(k) != nullptr && is_builtin_effect_kind(k), std::string("built-in kind '") + k + "' resolves");
    check(names.size() == 7, "the library's table is CLOSED at seven kinds (" + std::to_string(names.size()) + ")");
    std::string why;
    check(!register_effect_kind("spinner", [](const EffectSpec&, const Theme&, const EffectCell&, EffectOut&) {}, &why) &&
              why.find("library's own") != std::string::npos,
          "registering a LIBRARY kind is refused by name [" + why + "]");
    check(!register_effect_kind("", [](const EffectSpec&, const Theme&, const EffectCell&, EffectOut&) {}, &why), "an empty kind name is refused");
    check(effect_kind("confetti") == nullptr, "an unregistered name resolves to nothing (a HOST fact, not a theme error)");
  }

  // ---- the host's two kinds: one well-behaved, one that lies ------------------------
  {
    std::string why;
    // A well-behaved host kind: one cell wide, a role it was handed, no colour of its own.
    check(register_effect_kind("host-sweep",
                               [](const EffectSpec& s, const Theme& th, const EffectCell& in, EffectOut& out) {
                                 if ((in.index + static_cast<int>(in.elapsed_ms / 100)) % 2) return;
                                 out.has_style = true;
                                 out.style = th.style(s.roles.empty() ? Role::accent_1 : s.roles[0]);
                                 out.has_glyph = true;
                                 out.glyph = "#";
                               },
                               &why),
          "a host registers its own kind [" + why + "]");
    // THE MISBEHAVING ONE. It tries every way a callback could corrupt a frame that the
    // signature allows: a glyph twice as wide as the cell, an empty glyph, and a style.
    check(register_effect_kind("wide-liar",
                               [](const EffectSpec&, const Theme& th, const EffectCell& in, EffectOut& out) {
                                 out.has_style = true;
                                 out.style = th.style(Role::error);
                                 out.has_glyph = true;
                                 out.glyph = (in.index % 2) ? "" : "\xE4\xBD\xA0";  // 0 cells / 2 cells
                               },
                               &why),
          "…and a kind that LIES about its width, which is the sweep's control");
    check(!register_effect_kind("wide-liar", [](const EffectSpec&, const Theme&, const EffectCell&, EffectOut&) {}, &why),
          "a second registration of the same name is refused");
    check(effect_kind_names().size() == 9, "both appear after the library's seven, in resolution order");
  }

  // ---- THE TWO PROPERTIES, over every registered kind -------------------------------
  {
    int refused_total = 0, kinds = 0;
    for (const KindCase& kc : kind_cases()) {
      ++kinds;
      // Span geometries, degenerate ones included: one cell; a span over the wide glyph;
      // a span ENDING on the wide glyph's first half; a span running off the right edge;
      // a span on a row that does not exist; a negative x.
      struct Span { int x, y, cells; };
      const Span spans[] = {{2, 1, 1}, {2, 1, 8}, {0, 1, 20}, {3, 1, 2}, {4, 1, 1}, {14, 1, 40}, {2, 99, 5}, {-3, 1, 6}, {2, 1, 0}};
      for (const Span& sp : spans) {
        for (std::uint64_t tick : {0ull, 137ull, 400ull, 999ull}) {
          for (double frac : {0.0, 0.37, 1.0}) {
            const Theme theme = theme_with(EffectState::Waiting, kc.spec);
            Frame f = make_frame(20, 3, theme);
            const std::vector<CellShot> before = shoot(f);
            f.mark(sp.x, sp.y, sp.cells, EffectState::Waiting, 0, frac);
            const EffectReport rep = apply_effects(f, theme, tick);
            refused_total += rep.glyphs_refused;
            const std::vector<CellShot> after = shoot(f);
            const std::string where = kc.kind + " span(" + std::to_string(sp.x) + "," + std::to_string(sp.y) + "," +
                                      std::to_string(sp.cells) + ") t=" + std::to_string(tick);
            check_quiet(before.size() == after.size(), where + ": the grid keeps its size");
            for (int y = 0; y < f.height(); ++y)
              for (int x = 0; x < f.width(); ++x) {
                const std::size_t i = static_cast<std::size_t>(y * f.width() + x);
                const bool inside = y == sp.y && x >= sp.x && x < sp.x + sp.cells;
                if (!inside) {
                  // PROPERTY 2: nothing outside the span changed, at all.
                  check_quiet(before[i] == after[i],
                              where + ": cell (" + std::to_string(x) + "," + std::to_string(y) + ") outside the span is untouched");
                  continue;
                }
                // PROPERTY 1: the cell's width and its glyph's width are what they were.
                check_quiet(before[i].width == after[i].width && before[i].continuation == after[i].continuation,
                            where + ": cell (" + std::to_string(x) + "," + std::to_string(y) + ") keeps its width");
                check_quiet(after[i].continuation || unicode::display_width(after[i].text) == after[i].width,
                            where + ": cell (" + std::to_string(x) + "," + std::to_string(y) + ")'s glyph fills exactly its cells");
              }
            // Every row still measures the frame's width — the property wrap depends on.
            for (int y = 0; y < f.height(); ++y) {
              int cells = 0;
              for (int x = 0; x < f.width(); ++x) cells += f.at(x, y).width;  // a continuation cell is 0
              check_quiet(cells == f.width(), where + ": row " + std::to_string(y) + " still measures " + std::to_string(f.width()));
            }
          }
        }
      }
    }
    check(kinds == 9, "the sweep ran over every registered kind, built-in and host's (" + std::to_string(kinds) + ")");
    check(refused_total > 0, "…and the lying kind's overrides were REFUSED and counted (" + std::to_string(refused_total) + ")");
  }

  // ---- the lying kind changes no glyph at all --------------------------------------
  {
    EffectSpec s;
    s.kind = "wide-liar";
    const Theme theme = theme_with(EffectState::Waiting, s);
    Frame f = make_frame(20, 3, theme);
    const std::string before = frame_to_text(f);
    f.mark(2, 1, 8, EffectState::Waiting);
    const EffectReport rep = apply_effects(f, theme, 250);
    check(frame_to_text(f) == before, "a kind that lies about width writes no glyph anywhere");
    check(rep.glyphs_refused > 0 && !rep.clean(), "…and the report says so rather than the frame looking fine");
    check(rep.cells_touched > 0, "…while its STYLE still landed: only the illegal half was dropped");
  }

  // ---- determinism, and that the effect does something ------------------------------
  {
    const Theme theme = *builtin_theme("default-dark");
    auto at = [&](std::uint64_t tick) {
      Frame f = make_frame(20, 3, theme);
      f.mark(2, 1, 8, EffectState::Waiting);
      apply_effects(f, theme, tick);
      return frame_to_text(f);
    };
    check(at(0) == at(0), "the same tick gives the same frame, byte for byte (what --tick N records)");
    check(at(0) != at(160), "…and a different tick a different one: the effect is actually drawn");
    check(at(0) == at(640), "…and one full period later, the same one again");
  }

  // ---- a span carries its OWN phase (Mark::since_ms) ---------------------------------
  {
    const Theme theme = *builtin_theme("default-dark");
    Frame f = make_frame(20, 3, theme);
    f.mark(2, 1, 1, EffectState::Waiting, 0);     // started at 0
    f.mark(6, 1, 1, EffectState::Waiting, 1000);  // started later: a different frame of the cycle
    apply_effects(f, theme, 1160);
    check(f.glyph(2, 1) != f.glyph(6, 1),
          "two spans of one state with different start times are at different points of the cycle");
    Frame g = make_frame(20, 3, theme);
    g.mark(2, 1, 1, EffectState::Waiting);
    g.mark(6, 1, 1, EffectState::Waiting);
    apply_effects(g, theme, 1160);
    check(g.glyph(2, 1) == g.glyph(6, 1), "…and two with no start time of their own move together off the shared clock");
  }

  // ---- STACKING: glyph from one kind, colour from another ---------------------------
  {
    Theme theme = *builtin_theme("default-dark");
    theme.effects = EffectMap{};
    EffectSpec spin;
    spin.kind = "spinner";
    spin.frames = {"x", "y"};
    spin.period_ms = 400;
    EffectSpec tint;
    tint.kind = "pulse";
    tint.roles = {Role::error};
    tint.period_ms = 0;
    theme.effects.for_state(EffectState::Waiting) = {spin, tint};
    Frame f = make_frame(20, 3, theme);
    f.mark(2, 1, 4, EffectState::Waiting);
    apply_effects(f, theme, 0);
    check(f.glyph(2, 1) == "x" && f.at(2, 1).style == theme.style(Role::error), "a stacked pair gives the glyph from one and the style from the other");
    check(f.at(3, 1).style == theme.style(Role::error) && f.glyph(3, 1) != "x", "…and the kind that answers for one cell does not answer for the rest");
  }

  // ---- a theme that maps nothing is a STILL UI (the degrade rung) --------------------
  {
    Theme theme = *builtin_theme("default-dark");
    theme.effects = EffectMap{};
    Frame f = make_frame(20, 3, theme);
    const std::vector<CellShot> before = shoot(f);
    f.mark(2, 1, 8, EffectState::Waiting);
    const EffectReport rep = apply_effects(f, theme, 500);
    check(shoot(f) == before && rep.marks_drawn == 0 && rep.clean(), "a theme that maps nothing leaves a marked frame untouched");
    check(!effect_tick_ms(f, theme).has_value(), "…and asks for no wakeup");
  }

  // ---- THE TICK RULE: it runs only while something is marked ------------------------
  {
    for (std::string_view name : builtin_theme_names()) {
      const Theme& theme = *builtin_theme(name);
      Frame f = make_frame(20, 3, theme);
      check(!effect_tick_ms(f, theme).has_value(), std::string("no wakeups with no marks — ") + std::string(name));
      check(poll_timeout_ms(f, theme, 1000) == 1000, std::string("…so a host's idle timeout is untouched — ") + std::string(name));
      f.mark(2, 1, 8, EffectState::Waiting);
      const std::optional<int> tick = effect_tick_ms(f, theme);
      check(tick && *tick >= 16, std::string("a marked waiting span asks for a tick — ") + std::string(name) + " " +
                                     (tick ? std::to_string(*tick) : "none"));
      check(poll_timeout_ms(f, theme, 1000) == *tick, "…and the host's poll timeout becomes it");
      check(poll_timeout_ms(f, theme, 10) == 10, "…but never LONGER than what the host already wanted");
    }
    // A state the theme maps to a STILL effect asks for nothing either: a bar is a
    // picture of a number, and the number changing is already a redraw.
    const Theme& dark = *builtin_theme("default-dark");
    Frame f = make_frame(20, 3, dark);
    f.mark(6, 1, 8, EffectState::Progress, 0, 0.5);  // eight NARROW cells: the arithmetic is the point here, not the wide glyph
    check(!effect_tick_ms(f, dark).has_value(), "a still effect (period_ms 0) asks for no wakeup though its span IS marked");
    const EffectReport rep = apply_effects(f, dark, 0);
    check(rep.marks_drawn == 1 && rep.cells_touched == 4, "…while still drawing: 0.5 of an 8-cell span is 4 cells");
    // A mark whose state the theme maps to nothing that RESOLVES: named, never silent.
    Theme t2 = dark;
    t2.effects = EffectMap{};
    EffectSpec ghost;
    ghost.kind = "confetti";
    ghost.period_ms = 100;
    t2.effects.for_state(EffectState::Flash).push_back(ghost);
    Frame g = make_frame(20, 3, t2);
    g.mark(2, 1, 4, EffectState::Flash);
    const EffectReport grep = apply_effects(g, t2, 0);
    check(grep.unknown_kinds.size() == 1 && grep.unknown_kinds[0] == "confetti", "an unknown kind is NAMED in the report, not silently still");
    check(!effect_tick_ms(g, t2).has_value(), "…and asks for no wakeup, since it cannot draw");
  }

  // ---- the file format --------------------------------------------------------------
  {
    const char* text = R"({
      "name": "fx", "roles": { "text": { "fg": "none" } },
      "effects": {
        "waiting": { "kind": "spinner", "frames": ["-", "\\"], "period_ms": 200 },
        "streaming": [ { "kind": "shimmer", "role": "accent_1", "width": 4 },
                       { "kind": "ellipsis", "frames": ["   ", "...  "] } ],
        "progress": { "kind": "bar", "roles": ["accent_2"], "period_ms": 0, "backward": true },
        "elsewhere": { "kind": "spinner" },
        "flash": { "kind": "blink", "role": "nosuchrole", "wobble": 3 }
      }
    })";
    ThemeLoadReport rep;
    std::optional<Theme> t = load_theme(text, ThemeMode::Dark, rep);
    check(t.has_value(), "a theme file with effects loads");
    check(t->effects.for_state(EffectState::Waiting).size() == 1 && t->effects.for_state(EffectState::Streaming).size() == 2,
          "one spec or an array of them, and an array STACKS");
    check(t->effects.for_state(EffectState::Waiting)[0].frames.size() == 2 && t->effects.for_state(EffectState::Waiting)[0].period_ms == 200,
          "the spec's fields are read");
    check(t->effects.for_state(EffectState::Streaming)[0].roles == std::vector<Role>{Role::accent_1}, "\"role\" and \"roles\" are the same field");
    check(t->effects.for_state(EffectState::Progress)[0].backward && t->effects.for_state(EffectState::Progress)[0].period_ms == 0,
          "a still effect is written as period_ms 0, not as a missing key");
    auto has = [](const std::vector<std::string>& v, const char* needle) {
      for (const std::string& s : v)
        if (s.find(needle) != std::string::npos) return true;
      return false;
    };
    check(has(rep.unknown_keys, "effects.elsewhere"), "an unknown STATE name is reported");
    check(has(rep.unknown_keys, "effects.flash.wobble"), "an unknown spec key is reported");
    check(has(rep.bad_values, "nosuchrole"), "a role name that is not a role is a named bad value");
    check(has(rep.bad_values, "every frame must be 3 cells wide"), "frames of unequal width are refused AT LOAD, where an author can fix them");
    // An unknown KIND is not judged here: rung 2 is the host's, and a theme file is read
    // long before a host has registered anything (the same rule as an unknown widget kind).
    check(!has(rep.bad_values, "spinner") && !has(rep.unknown_keys, "effects.elsewhere.kind"), "…but a kind NAME is never judged by the loader");
  }

  // ---- the round trip -----------------------------------------------------------------
  {
    for (std::string_view name : builtin_theme_names()) {
      const Theme& theme = *builtin_theme(name);
      ThemeLoadReport rep;
      std::optional<Theme> back = load_theme(theme_to_json(theme), ThemeMode::Dark, rep);
      check(back && rep.clean() && back->effects == theme.effects,
            std::string("the built-in '") + std::string(name) + "' round-trips its effects through a theme file");
    }
    // A colour edit through the editor rewrites the file from the parsed theme: the pair
    // writer must carry the motion across or a first edit would silently stop it.
    json::Value pair = theme_pair_to_json_value(*builtin_theme("default-dark"), *builtin_theme("default-light"), "x");
    ThemeLoadReport rep;
    std::optional<Theme> d = load_theme(pair, ThemeMode::Dark, rep), l = load_theme(pair, ThemeMode::Light, rep);
    check(d && l && d->effects == builtin_theme("default-dark")->effects && d->effects == l->effects,
          "a dark/light PAIR file carries one effects object for both variants");
    Theme still = *builtin_theme("default-dark");
    still.effects = EffectMap{};
    std::string err;
    check(!json::parse(theme_to_json(still), err).has("effects") && err.empty(),
          "a theme with no motion writes no \"effects\" key: absent and empty are the same answer here");
  }

  // ---- the built-ins ------------------------------------------------------------------
  {
    const Theme& dark = *builtin_theme("default-dark");
    const Theme& light = *builtin_theme("default-light");
    const Theme& mono = *builtin_theme("mono");
    check(dark.effects == light.effects, "motion is a property of the THEME, not of dark vs light");
    check(!(dark.effects == mono.effects), "…and the mono theme tells the same four states a different way");
    for (const Theme* t : {&dark, &light, &mono})
      for (std::size_t i = 1; i < kEffectStateCount; ++i) {
        const std::vector<EffectSpec>& specs = t->effects.for_state(static_cast<EffectState>(i));
        check_quiet(!specs.empty(), t->name + " maps " + std::string(effect_state_name(static_cast<EffectState>(i))));
        for (const EffectSpec& s : specs) {
          check_quiet(effect_kind(s.kind) != nullptr, t->name + ": kind '" + s.kind + "' resolves");
          if (s.frames.empty()) continue;
          const int w = unicode::display_width(s.frames[0]);
          for (const std::string& fr : s.frames) {
            check_quiet(unicode::display_width(fr) == w, t->name + ": every frame of '" + s.kind + "' is " + std::to_string(w) + " cells");
            // …at BOTH ambiguous-width settings, or the applier would refuse the glyph on
            // a wide-ambiguous terminal and the theme would silently stop moving.
            check_quiet(unicode::display_width(fr, true) == w, t->name + ": '" + s.kind + "' frame is " + std::to_string(w) + " cells when ambiguous is wide too");
          }
        }
      }
    check(true, "every shipped effect's frames are one width at both ambiguous-width settings");
  }

  clear_registered_effect_kinds();
  return report("effects");
}
