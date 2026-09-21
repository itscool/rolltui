// rolltui/tests/menu_levels_test.cpp — EVERY MENU GOES BACK ONE LEVEL ON ESCAPE, AND THE NEXT WIDGET KIND CANNOT FORGET IT.
//
// The window stack asks the focused widget's `back` slot to close one inner level before it pops a popup. A kind that holds a
// menu and leaves the slot empty makes Escape close the whole popup from any depth, and reopening it comes back at that depth:
// the theme editor and the key editor were registered `NULL /* back: no levels */` while their menus were three levels deep,
// and nothing said so until a person hit Escape in one. Each kind's behaviour is tested where it lives (`menu_test`,
// `theme_kind_test`, `keys_kind_test`, and dirktui_test on the real binary); THIS is the ratchet that keeps the set of
// kinds honest: the plugins that declare "no levels" are named here, and a new one appearing (or one of these gaining a menu)
// fails until somebody decides.
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "testkit/testkit.hpp"

#ifndef ROLLTUI_SOURCE_DIR
#error "ROLLTUI_SOURCE_DIR must point at rolltui/"
#endif

int main() {
  using testkit::check;
  std::ifstream f(std::string(ROLLTUI_SOURCE_DIR) + "/c/rolltui_widget_kinds.c", std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string src = ss.str();
  check(src.size() > 10000, "the widget kinds' source is readable");

  // every `static const RolltuiWidgetPlugin kXPlugin = { ... };`, and whether it says it has no levels
  std::set<std::string> no_levels, has_back;
  const std::string tag = "static const RolltuiWidgetPlugin k";
  for (std::size_t at = src.find(tag); at != std::string::npos; at = src.find(tag, at + 1)) {
    const std::size_t name_end = src.find("Plugin", at + tag.size());
    const std::size_t close = src.find("};", at);
    if (name_end == std::string::npos || close == std::string::npos) continue;
    const std::string name = src.substr(at + tag.size(), name_end - (at + tag.size()));
    (src.substr(at, close - at).find("back: no levels") != std::string::npos ? no_levels : has_back).insert(name);
  }

  // These hold NO menu: a text page, a file, help, a rows list, an error, the column browser (its levels are columns, which
  // Left steps through), a single-line input, and the transcript.
  const std::set<std::string> flat = {"Text", "File", "Help", "Rows", "Error", "Picker", "Input", "Transcript"};
  // These hold a menu with levels, so each closes one level before the popup closes.
  const std::set<std::string> menus = {"Menu", "Theme", "Keys"};
  check(no_levels == flat, "the kinds that declare `no levels` are exactly the ones that hold no menu: adding one, or giving one a menu, means deciding here [" + std::to_string(no_levels.size()) + " found]");
  check(has_back == menus, "the kinds that hold a menu each have a `back`: Menu, Theme and Keys [" + std::to_string(has_back.size()) + " found]");
  return testkit::report("rolltui_menu_levels_test");
}
