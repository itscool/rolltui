// rolltui/tests/hints_test.cpp — the hint bar: a status line's keys, drawn once and clickable.
#include <cstring>
#include <string>
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h"  /* INTERNAL: a frame to draw into; this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"
int main() {
  using testkit::check;
  RolltuiHintBar* bar = rolltui_hint_bar_new();
  rolltui_hint_bar_add(bar, "F1", 2, "help", 4, "app.help", 8);
  rolltui_hint_bar_add(bar, "F2", 2, "settings", 8, "app.menu", 8);
  rolltui_hint_bar_add(bar, "Ctrl-G", 6, "jump", 4, "app.jump", 8);
  RolltuiFrame* f = rolltui_frame_new(40, 2, RolltuiStyle{});
  RolltuiDrawScratch* s = rolltui_draw_scratch_new();
  const int used = rolltui_hint_bar_draw(bar, f, s, 2, 1, 36, RolltuiStyle{}, RolltuiStyle{}, RolltuiStyle{}, 0);
  RolltuiStr text{};
  rolltui_frame_to_text(f, &text);
  const std::string frame(text.p ? text.p : "", text.n);
  check(frame.find("F1 help  F2 settings  Ctrl-G jump") != std::string::npos && used == 33,
        "the bar draws chord, label, and two cells between hints, and says how wide it came to [" + std::to_string(used) + "]");
  size_t n = 0;
  const char* hit = rolltui_hint_bar_hit(bar, 2 + 9, 1, &n);  // on "F2"
  check(hit && std::string(hit, n) == "app.menu", "a press on a hint answers with its action");
  hit = rolltui_hint_bar_hit(bar, 2 + 9 + 7, 1, &n);  // on "settings"
  check(hit && std::string(hit, n) == "app.menu", "…anywhere on it, label included");
  check(rolltui_hint_bar_hit(bar, 2 + 7, 1, &n) == nullptr, "the gap between two hints is nobody's");
  check(rolltui_hint_bar_hit(bar, 2 + 9, 0, &n) == nullptr, "…and another row is nobody's");
  // too narrow for the last hint: it is not drawn, and not hittable
  RolltuiFrame* g = rolltui_frame_new(40, 2, RolltuiStyle{});
  const int used2 = rolltui_hint_bar_draw(bar, g, s, 0, 0, 24, RolltuiStyle{}, RolltuiStyle{}, RolltuiStyle{}, 0);
  RolltuiStr text2{};  // its own sink: `to_text` appends
  rolltui_frame_to_text(g, &text2);
  const std::string frame2(text2.p ? text2.p : "", text2.n);
  rolltui_str_free(&text2);
  check(used2 == 20 && frame2.find("F2 settings") != std::string::npos && frame2.find("Ctrl-G") == std::string::npos,
        "a hint that does not fit whole is left out rather than cut [" + std::to_string(used2) + "]");
  check(rolltui_hint_bar_hit(bar, 22, 0, &n) == nullptr, "…and a press where it would have been is nobody's");
  rolltui_hint_bar_clear(bar);
  check(rolltui_hint_bar_hit(bar, 2 + 9, 1, &n) == nullptr, "a cleared bar answers nothing");
  // A HINT WITH NO CHORD is a state to click — "sort name", "+dotfiles" — drawn as its label alone.
  rolltui_hint_bar_add(bar, "F1", 2, "help", 4, "app.help", 8);
  rolltui_hint_bar_add(bar, "", 0, "sort name", 9, "app.sort", 8);
  RolltuiFrame* k = rolltui_frame_new(40, 1, RolltuiStyle{});
  const int used3 = rolltui_hint_bar_draw(bar, k, s, 0, 0, 40, RolltuiStyle{}, RolltuiStyle{}, RolltuiStyle{}, 0);
  RolltuiStr text3{};
  rolltui_frame_to_text(k, &text3);
  const std::string frame3(text3.p ? text3.p : "", text3.n);
  check(used3 == 18 && frame3.find("F1 help  sort name") != std::string::npos, "a hint without a chord is its label alone, no gap where the chord would be [" + std::to_string(used3) + "]");
  hit = rolltui_hint_bar_hit(bar, 9, 0, &n);
  check(hit && std::string(hit, n) == "app.sort", "…and a press on it answers with its action");
  rolltui_str_free(&text3);
  rolltui_frame_free(k);
  // A BREADCRUMB: " › " between parts, the tail kept — when the bar is short of room the HEAD is
  // dropped behind an ellipsis, and the last parts (the place the eye is at, the pencil) stay.
  RolltuiHintBar* crumb = rolltui_hint_bar_new();
  rolltui_hint_bar_set_separator(crumb, " \xE2\x80\xBA ", 5);
  rolltui_hint_bar_set_keep_tail(crumb, 1);
  rolltui_hint_bar_add(crumb, "", 0, "/", 1, "c0", 2);
  rolltui_hint_bar_add(crumb, "", 0, "Users", 5, "c1", 2);
  rolltui_hint_bar_add(crumb, "", 0, "scott", 5, "c2", 2);
  rolltui_hint_bar_add(crumb, "", 0, "src", 3, "c3", 2);
  rolltui_hint_bar_add(crumb, "", 0, "\xE2\x9C\x8E", 3, "edit", 4);
  RolltuiFrame* c1 = rolltui_frame_new(40, 1, RolltuiStyle{});
  const int cu = rolltui_hint_bar_draw(crumb, c1, s, 0, 0, 40, RolltuiStyle{}, RolltuiStyle{}, RolltuiStyle{}, 0);
  RolltuiStr ct{};
  rolltui_frame_to_text(c1, &ct);
  const std::string cf(ct.p ? ct.p : "", ct.n);
  check(cu == 25 && cf.find("/ \xE2\x80\xBA Users \xE2\x80\xBA scott \xE2\x80\xBA src \xE2\x9C\x8E") != std::string::npos, "with room, every part with the separator between and the pencil after a space [" + std::to_string(cu) + "]");
  hit = rolltui_hint_bar_hit(crumb, 12, 0, &n);
  check(hit && std::string(hit, n) == "c2", "a press on a part answers with its action");
  check(rolltui_hint_bar_hit(crumb, 10, 0, &n) == nullptr, "…and a press on a separator is nobody's");
  rolltui_str_free(&ct);
  rolltui_frame_free(c1);
  RolltuiFrame* c2 = rolltui_frame_new(40, 1, RolltuiStyle{});
  const int cu2 = rolltui_hint_bar_draw(crumb, c2, s, 0, 0, 16, RolltuiStyle{}, RolltuiStyle{}, RolltuiStyle{}, 0);
  RolltuiStr ct2{};
  rolltui_frame_to_text(c2, &ct2);
  const std::string cf2(ct2.p ? ct2.p : "", ct2.n);
  check(cu2 <= 16 && cf2.find("\xE2\x80\xA6 \xE2\x80\xBA ") == 0 && cf2.find("src \xE2\x9C\x8E") != std::string::npos && cf2.find("Users") == std::string::npos,
        "short of room the head goes behind an ellipsis and the tail stays whole, pencil included [" + cf2.substr(0, 24) + "]");
  hit = rolltui_hint_bar_hit(crumb, cu2 - 1, 0, &n);
  check(hit && std::string(hit, n) == "edit", "…and the pencil is still there to press");
  rolltui_str_free(&ct2);
  rolltui_frame_free(c2);
  rolltui_hint_bar_free(crumb);
  // DISABLED: drawn, muted, never hit — an action that cannot be taken now.
  rolltui_hint_bar_clear(bar);
  rolltui_hint_bar_add(bar, "F1", 2, "help", 4, "app.help", 8);
  rolltui_hint_bar_add(bar, "^C", 2, "copy", 4, "picker.copy", 11);
  rolltui_hint_bar_enable(bar, "picker.copy", 11, 0);
  RolltuiFrame* dz = rolltui_frame_new(40, 1, RolltuiStyle{});
  rolltui_hint_bar_draw(bar, dz, s, 0, 0, 40, RolltuiStyle{}, RolltuiStyle{}, RolltuiStyle{}, 0);
  RolltuiStr dt{};
  rolltui_frame_to_text(dz, &dt);
  check(std::string(dt.p ? dt.p : "", dt.n).find("^C copy") != std::string::npos && rolltui_hint_bar_hit(bar, 10, 0, &n) == nullptr,
        "a disabled hint is still drawn but a press on it is nobody's");
  rolltui_hint_bar_enable(bar, "picker.copy", 11, 1);
  hit = rolltui_hint_bar_hit(bar, 10, 0, &n);
  check(hit && std::string(hit, n) == "picker.copy", "…and enabled again, it answers");
  rolltui_str_free(&dt);
  rolltui_frame_free(dz);
  rolltui_str_free(&text);
  rolltui_frame_free(g);
  rolltui_frame_free(f);
  rolltui_draw_scratch_free(s);
  rolltui_hint_bar_free(bar);
  return testkit::report("rolltui_hints_test");
}
