//
// designable_test.cpp — WHAT A PERSON CAN DESIGN WITHOUT WRITING JSON,
// as a check rather than a sentence.
//
// WHAT A PERSON CAN AND CANNOT DESIGN IN THE TOOL is worth nothing written down as a
// sentence, because the way it goes wrong is silent: a loader grows a key, no editor grows a
// field, and the sentence goes on being true-looking. Four such keys have been found by
// accident, while doing something else.
//
// SO THE CLAIM IS MECHANISED. This file reads the two loaders' own source for every JSON key
// they accept, and holds each one to a named menu item in an editor — the item that sets it.
// A key with no entry FAILS. There is no "not covered yet" state and no ratchet: a key either
// has a field, or is STRUCTURE that an operation makes rather than a field (a `row` is what
// "Split into a row" does), and the table says which, per key, in the source.
//
// WHY ONLY TWO OF THE FOUR FORMATS. A layout and a menu have a CLOSED key set, so "every key"
// is a set a test can enumerate. A theme's keys are the role names and a bindings file's are
// the action names — both OPEN vocabularies, and both editors are built by iterating exactly
// that vocabulary out of the library (the theme editor's Roles level, the keys editor's Actions
// by scope), so a new role or a new action grows the editor with no code. Nothing can drift
// there, which is why this file does not watch it.
//
// AND THE TWO PERMANENT EXCEPTIONS, which are not key-coverage at all and is why they survive:
//   1. A FOREIGN WIDGET KIND previews as a labelled placeholder. `content` is fully settable —
//      you can type `canvas:sheet` and it is SAVED — but this binary cannot draw another app's
//      canvas, so it draws `[canvas:sheet]` and says so. Not a gap: the artifact records the
// intent, and the app reports the gap to its own developer.
//   2. AN ACTION NAME CANNOT BE VERIFIED, because the action belongs to the app. A menu item's
//      `action` and an input's `validator` are the same case, which is the point: a second
//      instance of one exception, not a third exception.
// Both are about VERIFICATION and neither is about authoring. Everything else is authorable,
// and that is what the table below is for.
//
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <regex>
#include <string>
#include <vector>

#include "layout_editor.hpp"
#include "menu_editor.hpp"

/* INTERNAL headers, BY NAME — this suite tests the editors, and opts in via
 * ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_widget_menu.h"
#include "rolltui_test.hpp"

#ifndef ROLLTUI_SOURCE_DIR
#error "ROLLTUI_SOURCE_DIR must point at rolltui/"
#endif

using namespace rolltui;
using namespace rolltui::tools;
using namespace rolltui_test;
using namespace testkit;

namespace {

std::string read_file(const std::string& path, bool& ok) {
  FILE* f = std::fopen(path.c_str(), "rb");
  ok = f != nullptr;
  if (!f) return {};
  std::string out;
  char buf[8192];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

// Every key the loader compares against, read from its own source. `streq(k, <len>, "key")` is
// the one idiom both loaders use, which is what makes this scannable at all — and if that ever
// stops being true the count assertion below fails rather than the set quietly emptying.
std::vector<std::string> loader_keys(const char* file) {
  bool ok = false;
  const std::string src = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/c/" + file, ok);
  check(ok && src.size() > 1000, std::string("read the loader's source (") + file + ")");
  std::vector<std::string> out;
  const std::regex re(R"RX(streq\(k,\s*\w+,\s*"([a-z_]+)"\))RX");
  for (std::sregex_iterator i(src.begin(), src.end(), re), e; i != e; ++i) {
    const std::string k = (*i)[1].str();
    if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
  }
  std::sort(out.begin(), out.end());
  return out;
}

// A key is either a FIELD (a menu item id that sets it) or STRUCTURE (made by an operation, so
// there is nothing to type). `field` empty means structure, and `why` then has to say what makes
// it. Both columns are mandatory: an entry with neither is what this table exists to refuse.
struct Covered {
  const char* key;
  const char* field;
  const char* why;
};

const Covered kLayout[] = {
    {"actions", "action.add", "the Actions level: add by name, then its description"},
    {"anchor", "popup.find.anchor", "per popup"},
    {"background", "background", "a choice over the library's role table"},
    {"border", "border", ""},
    {"clamp", "popup.find.clamp", "per popup"},
    {"column", "", "STRUCTURE: 'Split into a column' makes one"},
    {"content", "kind", "the kind, with Source or Menu file beside it — exactly one enabled"},
    {"dismiss", "popup.find.dismiss", "per popup"},
    {"focus", "focus", "a choice over the focusable windows"},
    {"focusable", "focusable", ""},
    {"h", "popup.find.h", "per popup"},
    {"id", "id", "renaming carries the selection and `focus` with it"},
    {"max_h", "popup.find.max_h", "per popup; empty means unbounded"},
    {"max_w", "popup.find.max_w", "per popup; empty means unbounded"},
    {"min_h", "popup.find.min_h", "per popup; empty means unbounded"},
    {"min_height", "min_height", "the screen's own threshold"},
    {"min_w", "popup.find.min_w", "per popup; empty means unbounded"},
    {"min_width", "min_width", "the screen's own threshold"},
    {"modal", "popup.find.modal", "per popup"},
    {"name", "", "STRUCTURE: the file's name, typed at New layout or Save as"},
    {"popups", "popup.add", "add by id; each gets its own level"},
    {"root", "", "STRUCTURE: the tree itself"},
    {"row", "", "STRUCTURE: 'Split into a row' makes one"},
    {"size", "size", ""},
    {"title", "title", ""},
    {"visible", "visible", ""},
    {"w", "popup.find.w", "per popup"},
    {"x", "popup.find.x", "per popup"},
    {"y", "popup.find.y", "per popup"},
};

const Covered kMenu[] = {
    {"action", "action", "THE EXCEPTION: a name in the app's table, written unverified"},
    {"checked", "checked", "Toggle only"},
    {"dropdown", "dropdown", "Choice only"},
    {"enabled", "enabled", ""},
    {"hint", "hint", "Input only"},
    {"id", "id", "an index path selects, so an id may repeat where the format allows it"},
    {"items", "add_child", "STRUCTURE, but by a field: a child's id. On a Choice this is an OPTION"},
    {"kind", "kind", ""},
    {"label", "label", ""},
    {"max", "max", "Int and Float only"},
    {"max_len", "max_len", "Text and Name only"},
    {"min", "min", "Int and Float only"},
    {"min_len", "min_len", "Text and Name only"},
    {"optional", "optional", "Input only"},
    {"precision", "precision", "Float only"},
    {"shortcut", "shortcut", "display only; a live chord wins where an action is set"},
    {"step", "step", "Int and Float only"},
    {"type", "input_type", "Input only"},
    {"validator", "validator", "THE SAME EXCEPTION as `action`: a name the app registers"},
    {"value", "value", "Choice and Input"},
};

template <std::size_t N>
void hold(const std::vector<std::string>& keys, const Covered (&table)[N], RolltuiMenu* menu, const char* what) {
  for (const std::string& k : keys) {
    const Covered* row = nullptr;
    for (const Covered& c : table)
      if (k == c.key) row = &c;
    if (!row) {
      check(false, std::string(what) + ": '" + k + "' is a key the loader accepts and nothing in this table "
                                                  "claims — wire it to a field, or record it as structure");
      continue;
    }
    if (!*row->field) {
      check(*row->why != '\0', std::string(what) + ": '" + k + "' is structure, and says what makes it");
      continue;
    }
    check(rolltui_menu_find(menu, row->field, std::strlen(row->field)) != nullptr,
          std::string(what) + ": '" + k + "' is set by the '" + row->field + "' field");
  }
  // The other direction: a table row for a key the loader dropped is dead weight that would
  // otherwise sit here looking like coverage.
  for (const Covered& c : table)
    check(std::find(keys.begin(), keys.end(), std::string(c.key)) != keys.end(),
          std::string(what) + ": the table's '" + c.key + "' is still a key the loader accepts");
}

}  // namespace

int main() {
  RolltuiContext* ctx = rolltui_context_new();
  rolltui_context_set_library_defaults(ctx);

  // ---- the LAYOUT format -----------------------------------------------------------------
  {
    const std::vector<std::string> keys = loader_keys("rolltui_layout.c");
    check(keys.size() >= 25, "scanned the layout loader's key set (" + std::to_string(keys.size()) + " keys)");
    LayoutEditor ed(ctx);
    // The shipped `default` screen, because it HAS popups: the popup fields are built per popup,
    // so an editor holding a layout with none would answer for a table it never made.
    ed.load(builtin_layout(ctx, "default"));
    hold(keys, kLayout, ed.menu(), "layout");
    // The popup fields are addressed per popup, so the table names one that must be there.
    check(rolltui_menu_find(ed.menu(), "popup.find.min_w", 16) != nullptr,
          "…and a popup's bounds are per popup, named for it");
  }

  // ---- the MENU format --------------------------------------------------------------------
  {
    const std::vector<std::string> keys = loader_keys("rolltui_widget_menu.c");
    check(keys.size() >= 15, "scanned the menu loader's key set (" + std::to_string(keys.size()) + " keys)");
    MenuEditor ed(ctx);
    ed.load(ed.skeleton("probe"));
    hold(keys, kMenu, ed.menu(), "menu");
  }

  // ---- THE TWO EXCEPTIONS, as behaviour rather than as a paragraph -------------------------
  {
    MenuEditor ed(ctx);
    ed.load(ed.skeleton("probe"));
    ed.set_actions({"app.one", "app.two"});
    // (2) An action this binary has never heard of is a field's value, not an error, and the
    //     known actions are the field's HINT — never its option list, which would make the
    //     editor the authority on a vocabulary that is the app's.
    const MenuItem* field = rolltui_menu_find(ed.menu(), "action", 6);
    check(field && field->kind == MenuItem::Kind::Input,
          "an item's action is an INPUT: any name is typable, because the app owns the table");
    check(field && field->spec.hint.n != 0 && field->children.n == 0,
          "…and what this binary knows is the field's hint, never a list it would police");
  }
  {
    LayoutEditor ed(ctx);
    // (1) A foreign widget kind is a typed value that SAVES, whatever this binary can draw.
    const MenuItem* kind = rolltui_menu_find(ed.menu(), "kind", 4);
    check(kind && kind->kind == MenuItem::Kind::Input,
          "a widget kind is an INPUT: another app's kind is typable, and the file keeps it");
  }

  rolltui_context_free(ctx);
  return report("rolltui_designable_test");
}
