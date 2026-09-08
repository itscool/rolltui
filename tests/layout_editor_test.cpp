//
// layout_editor_test.cpp — the layout editor's model (milestone 16): selection by Tab,
// split into a row / a column, swap, hide, border and content as live-previewed
// choices (Enter commits, Escape cancels), title and size as inputs (live, refused
// when not a size), Alt+arrow nudges, delete with collapse, popups (add, place,
// anchor, remove), the seam drag, undo/redo, save/load outcomes, and the produced
// layout round-tripping through the loader clean.
//
// THE DESIGN EDITOR: the widget-kind picker over the closed table, the source field typed
// by the kind, the menu-file choice, and the actions level. The property to hold on to is that EXACTLY ONE of Source / Menu file is
// enabled for any kind, so the editor never offers two ways to say one thing.
//
// Every menu lookup goes through value_of / enabled_of, which NAME a missing item
// instead of dereferencing a null (CLAUDE.md: a control that crashes reports nothing —
// this file segfaulted on `find("content")->value` the moment that item was renamed).
//
// drives the editor through `rolltui/c/*.h` directly — no `rolltui/*.hpp`.
//
#include <string>

#include "layout_editor.hpp"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_layout.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui::tools;
using namespace rolltui_test;

namespace {
RolltuiChord key(unsigned char k, bool shift = false, bool alt = false) {
  RolltuiChord e{};
  e.key = k;
  e.shift = shift ? 1 : 0;
  e.alt = alt ? 1 : 0;
  return e;
}
RolltuiChord ch(char c) {
  RolltuiChord e{};
  e.key = ROLLTUI_KEY_CHAR;
  e.ch = static_cast<char32_t>(c);
  return e;
}
RolltuiChord ctrl(char c) {
  RolltuiChord e = ch(c);
  e.ctrl = 1;
  return e;
}
LayoutEditor::Outcome handle(LayoutEditor& ed, const RolltuiChord& k) {
  const RolltuiEvent e{ROLLTUI_EVENT_KEY, k, {}, nullptr, 0};
  return ed.handle(&e);
}
void type(LayoutEditor& ed, const std::string& s) { for (char c : s) handle(ed, ch(c)); }
// Reaches a top-level item by filter and acts on it.
void act(LayoutEditor& ed, const std::string& filter) {
  handle(ed, key(ROLLTUI_KEY_ESCAPE));
  handle(ed, key(ROLLTUI_KEY_ESCAPE));
  handle(ed, key(ROLLTUI_KEY_HOME));
  type(ed, filter);
  handle(ed, key(ROLLTUI_KEY_ENTER));
}
// …and one inside a named scope. The editor's top level is the selected NODE; tree surgery,
// the screen's own properties and the layout file are levels of their own, so reaching one of
// those is a filter, an Enter and a filter.
void act(LayoutEditor& ed, const std::string& scope, const std::string& filter) {
  act(ed, scope);
  type(ed, filter);
  handle(ed, key(ROLLTUI_KEY_ENTER));
}
// A NODE THAT IS NOT THERE IS A NAMED ANSWER, NEVER A DEREF. This file has crashed twice on a
// menu item being renamed out from under it, and a test that dies cannot say what it found —
// so every lookup a check reads goes through one of these.
const Node* node_or_null(const LayoutEditor& ed, std::string_view id) {
  return LayoutEditor::find_node(ed.current().base.root, id);
}
bool node_visible(const LayoutEditor& ed, std::string_view id) {
  const Node* n = node_or_null(ed, id);
  return n && n->visible;
}
MenuItem* find(RolltuiMenu* m, std::string_view id) { return rolltui_menu_find(m, id.data(), id.size()); }
// A missing item is a NAMED answer, never a null deref: this file crashed on one.
std::string value_of(const LayoutEditor& ed, const char* id) {
  const MenuItem* it = find(const_cast<LayoutEditor&>(ed).menu(), id);
  return it ? str_of(it->value) : "(no item '" + std::string(id) + "')";
}
bool enabled_of(const LayoutEditor& ed, const char* id) {
  const MenuItem* it = find(const_cast<LayoutEditor&>(ed).menu(), id);
  return it && it->enabled;
}
std::string content_of(const LayoutEditor& ed, const char* node) {
  const Node* n = LayoutEditor::find_node(ed.current().base.root, node);
  return n ? str_of(n->content) : "(no node '" + std::string(node) + "')";
}
std::string editing_text_of(LayoutEditor& ed) {
  std::size_t len = 0;
  const char* p = rolltui_input_text(rolltui_menu_editor(ed.menu()), &len);
  return std::string(p, len);
}
std::string edit_reason_of(LayoutEditor& ed) {
  std::size_t len = 0;
  const char* p = rolltui_menu_edit_reason(ed.menu(), &len);
  return std::string(p, len);
}
// Dumps/parses a whole layout through the C loader/dumper — layout_to_json/load_layout's
// shape, minus the deleted C++ wrapper.
std::string dump_layout(const Layout& l) {
  RolltuiStr out{};
  rolltui_layout_to_json_text(l.name.data(), l.name.size(), l.min_width, l.min_height, l.actions.data(), l.actions.size(),
                             &l.base, l.popups.data(), l.popups.size(), rolltui_layout_default_hooks(), &out);
  const std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  return s;
}
std::optional<Layout> parse_layout(std::string_view text, bool* clean) {
  RolltuiLoadedLayout loaded;
  rolltui_loaded_layout_init(&loaded);
  RolltuiLayoutReport rep{};
  std::size_t dn = 0;
  const RolltuiLayoutAction* dflt = rolltui_layout_shipped_default_actions(rolltui_test::test_context(), &dn);
  const int ok = rolltui_load_layout_text_into(text.data(), text.size(), &loaded, dflt, dn, rolltui_layout_default_hooks(), &rep);
  if (clean) *clean = ok && rolltui_layout_report_clean(&rep);
  std::optional<Layout> result;
  if (ok) {
    Layout l{};
    rolltui_loaded_layout_to_layout(&loaded, &l);
    result = std::move(l);
  }
  rolltui_loaded_layout_release(&loaded);
  rolltui_layout_report_release(&rep);
  return result;
}
}  // namespace

int main() {
  RolltuiContext* ctx = rolltui_context_new();
  LayoutEditor ed{ctx};
  ed.load(builtin_layout(rolltui_test::test_context(), "default"));
  using O = LayoutEditor::Outcome::Kind;
  check(ed.selected() == "transcript" && ed.selected_node() && ed.selected_node()->is_window(), "loading selects the first window in tree order [" + ed.selected() + "]");
  // ---- selection ----
  {
    handle(ed, key(ROLLTUI_KEY_TAB));
    check(ed.selected() == "input", "Tab selects the next node [" + ed.selected() + "]");
    handle(ed, key(ROLLTUI_KEY_TAB));
    check(ed.selected() == "status", "…then the status window");
    handle(ed, key(ROLLTUI_KEY_TAB, true));
    check(ed.selected() == "input", "Shift-Tab goes back");
    ed.select("transcript");
    check(ed.selected() == "transcript" && view_of(find(ed.menu(), "root")->label).find("transcript") != std::string::npos, "select(id) and the breadcrumb names the node");
    check(value_of(ed, "border") == "single" && value_of(ed, "kind") == "transcript" && value_of(ed, "source") == "session" && value_of(ed, "size") == "fill",
          "the menu shows the selected node's border, kind, source and size");
  }
  // ---- split into a row ----
  {
    act(ed, "tree", "split into a row");
    const Node* t = LayoutEditor::find_node(ed.current().base.root, "transcript");
    const Node* t2 = LayoutEditor::find_node(ed.current().base.root, "transcript-2");
    std::size_t idx = 0;
    const Node* parent = LayoutEditor::parent_of(const_cast<Node&>(ed.current().base.root), "transcript", &idx);
    check(t && t2 && parent && parent->kind == Node::Kind::Row && parent->children.size() == 2 && t2->content == "transcript:session" && idx == 0,
          "the transcript became a row of [transcript, transcript-2], the copy with the same slot");
    check(ed.undo_depth() == 1 && ed.selected() == "transcript", "…as one commit, with the original selected");
    bool clean = false;
    std::optional<Layout> back = parse_layout(dump_layout(ed.current()), &clean);
    check(back && clean && *back == ed.current(), "the edited layout round-trips through the loader clean");
  }
  // ---- swap, hide, delete with collapse ----
  {
    act(ed, "tree", "swap with the next");
    std::size_t idx = 9;
    LayoutEditor::parent_of(const_cast<Node&>(ed.current().base.root), "transcript", &idx);
    check(idx == 1 && ed.undo_depth() == 2, "swap with the next sibling moves it to index 1");
    act(ed, "tree", "swap with the next");
    check(ed.status_line().find("no sibling") != std::string::npos && ed.undo_depth() == 2, "swapping past the end is refused with a reason and commits nothing");
    ed.select("transcript-2");
    act(ed, "visible");
    check(node_or_null(ed, "transcript-2") && !node_visible(ed, "transcript-2") && ed.undo_depth() == 3,
          "the Visible toggle hides the node");
    act(ed, "tree", "delete");
    const Node* t = LayoutEditor::find_node(ed.current().base.root, "transcript");
    std::size_t tidx = 9;
    const Node* parent = LayoutEditor::parent_of(const_cast<Node&>(ed.current().base.root), "transcript", &tidx);
    check(t && !LayoutEditor::find_node(ed.current().base.root, "transcript-2") && parent && parent->kind == Node::Kind::Column,
          "deleting the copy collapses the one-child row back into the column [" + ed.selected() + "]");
    check(ed.undo_depth() == 4 && ed.selected_node() != nullptr, "…as a commit, with a valid selection");
  }
  // ---- border: preview, cancel, commit ----
  {
    ed.select("transcript");
    act(ed, "border");
    check(rolltui_menu_level(ed.menu())->id == "border" && rolltui_menu_selected_item(ed.menu())->id == "single", "the Border choice opens on the current value");
    handle(ed, key(ROLLTUI_KEY_DOWN));
    check(ed.previewing() && LayoutEditor::find_node(ed.current().base.root, "transcript")->border == Border::Rounded, "moving to rounded previews it live");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    check(!ed.previewing() && LayoutEditor::find_node(ed.current().base.root, "transcript")->border == Border::Single, "Escape puts single back");
    act(ed, "border");
    handle(ed, key(ROLLTUI_KEY_DOWN));
    handle(ed, key(ROLLTUI_KEY_DOWN));
    LayoutEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && ed.committed().base.root.children[0].children[0].border == Border::Double, "Enter commits double");
  }
  // ---- title and size inputs, nudges ----
  {
    act(ed, "title");
    check(rolltui_menu_selected_item(ed.menu())->value == "transcript", "the Title input is prefilled with the current title");
    for (int i = 0; i < 10; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "chat");
    check(ed.previewing() && LayoutEditor::find_node(ed.current().base.root, "transcript")->title == "chat", "typing a title previews it");
    LayoutEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && LayoutEditor::find_node(ed.committed().base.root, "transcript")->title == "chat", "Enter commits the title");
    ed.select("input");
    act(ed, "size");
    for (int i = 0; i < 4; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "5");
    check(LayoutEditor::find_node(ed.current().base.root, "input")->size == SplitSize::fixed(Dim::abs(5)), "typing a size applies it live");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && LayoutEditor::find_node(ed.committed().base.root, "input")->size == SplitSize::fixed(Dim::abs(5)), "Enter commits the size");
    act(ed, "size");
    type(ed, "x");
    check(rolltui_menu_editing(ed.menu()) && editing_text_of(ed) == "5" && edit_reason_of(ed).find("not the start of a size") == 0 &&
              LayoutEditor::find_node(ed.current().base.root, "input")->size == SplitSize::fixed(Dim::abs(5)),
          "a key that cannot begin a size is refused at the keystroke with the reason; the text and the size are kept [" + edit_reason_of(ed) + "]");
    for (int i = 0; i < 2; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "fil");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Changed && rolltui_menu_editing(ed.menu()) && ed.status_line().find("not a size yet") != std::string::npos,
          "Enter on a text that is not yet a size is refused with the reason [" + ed.status_line() + "]");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    check(!rolltui_menu_editing(ed.menu()) && LayoutEditor::find_node(ed.current().base.root, "input")->size == SplitSize::fixed(Dim::abs(5)), "Escape restores the committed size");
    o = handle(ed, key(ROLLTUI_KEY_DOWN, false, true));  // Alt+Down: input is in a column
    check(o.kind == O::Committed && LayoutEditor::find_node(ed.committed().base.root, "input")->size == SplitSize::fixed(Dim::abs(6)), "Alt+Down nudges the size to 6, a commit");
    o = handle(ed, key(ROLLTUI_KEY_RIGHT, false, true));
    check(o.kind == O::Changed && ed.status_line().find("up/down") != std::string::npos, "Alt+Right on a column child says which way it sizes");
    ed.select("status");
    o = handle(ed, key(ROLLTUI_KEY_LEFT, false, true));
    check(o.kind == O::Committed && LayoutEditor::find_node(ed.committed().base.root, "status")->size == SplitSize::fixed(Dim::abs(31)), "Alt+Left narrows the status window from 32 to 31");
  }
  // ---- the seam drag ----
  {
    ed.begin_drag("status");
    ed.drag_to(40);
    check(ed.dragging() && ed.previewing() && LayoutEditor::find_node(ed.current().base.root, "status")->size == SplitSize::fixed(Dim::abs(40)), "a drag previews the new size");
    LayoutEditor::Outcome o = ed.end_drag();
    check(o.kind == O::Committed && !ed.dragging() && LayoutEditor::find_node(ed.committed().base.root, "status")->size == SplitSize::fixed(Dim::abs(40)), "the release commits once");
  }
  // ---- popups ----
  {
    act(ed, "this screen", "popups");
    const std::size_t before = ed.current().popups.size();
    handle(ed, key(ROLLTUI_KEY_END));  // add a popup
    handle(ed, key(ROLLTUI_KEY_ENTER));
    type(ed, "note");
    LayoutEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && ed.current().popups.size() == before + 1 && ed.current().popup("note", 4) && ed.current().popup("note", 4)->root.content == "text:note",
          "adding a popup creates a centred modal one with a text slot");
    check(find(ed.menu(), "popup.note.x") != nullptr, "…and the menu grows a level for it");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    act(ed, "this screen", "popups");
    type(ed, "note");
    handle(ed, key(ROLLTUI_KEY_ENTER));  // the note level
    handle(ed, key(ROLLTUI_KEY_DOWN));
    handle(ed, key(ROLLTUI_KEY_DOWN));   // w
    handle(ed, key(ROLLTUI_KEY_ENTER));
    for (int i = 0; i < 5; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "80%");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && ed.committed().popup("note", 4)->placement.w == Dim::rel(0.8), "a placement dim typed as 80% commits");
    handle(ed, key(ROLLTUI_KEY_DOWN));
    handle(ed, key(ROLLTUI_KEY_DOWN));   // anchor
    handle(ed, key(ROLLTUI_KEY_ENTER));
    handle(ed, key(ROLLTUI_KEY_HOME));
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && ed.committed().popup("note", 4)->placement.anchor == Anchor::TopLeft, "the anchor choice commits top-left");
    handle(ed, key(ROLLTUI_KEY_END));    // remove
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && !ed.current().popup("note", 4), "remove deletes the popup");
  }
  // ---- undo / redo, outcomes ----
  {
    const std::size_t depth = ed.undo_depth();
    handle(ed, ctrl('z'));
    check(ed.undo_depth() == depth - 1 && ed.current().popup("note", 4) != nullptr, "Ctrl-Z brings the popup back");
    handle(ed, ctrl('y'));
    check(ed.undo_depth() == depth && !ed.current().popup("note", 4), "Ctrl-Y removes it again");
    ed.set_layouts({"default", "stacked", "two"});
    act(ed, "layout file", "load layout");
    handle(ed, key(ROLLTUI_KEY_DOWN));
    LayoutEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o == LayoutEditor::Outcome{O::LoadLayout, "stacked"}, "Load layout asks the host");
    act(ed, "layout file", "save layout");
    type(ed, "two");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o == LayoutEditor::Outcome{O::SaveAs, "two"}, "Save layout file as asks the host with the name");
    // Reset is a Layout file operation, so reaching it is two filters — and this block reads
    // the OUTCOME, which `act` does not return, so the keys are spelled out.
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    handle(ed, key(ROLLTUI_KEY_HOME));
    type(ed, "layout file");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    type(ed, "reset to the loaded");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::ResetLoaded, "Reset to the loaded layout is an outcome the host confirms, never applied here");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));  // clears the filter
    handle(ed, key(ROLLTUI_KEY_ESCAPE));  // leaves the Layout file level
    o = handle(ed, key(ROLLTUI_KEY_ESCAPE));
    check(o.kind == O::Closed, "Escape at the top asks the host to close");
  }
  // ---- the kind picker, the source field, the menu-file choice ----
  {
    ed.load(builtin_layout(rolltui_test::test_context(), "default"));  // a fresh baseline: the blocks above left it split about
    ed.set_menus({"main", "extra"});
    ed.set_sources({"transcript:session", "rows:status", "input:prompt", "text:pane"});
    ed.select("transcript");
    check(enabled_of(ed, "source") && !enabled_of(ed, "menu_file"),
          "on a transcript the Source field is the one that owns the source, the Menu file choice is off");
    check(find(ed.menu(), "source") && find(ed.menu(), "source")->spec.type == InputType::Name && find(ed.menu(), "source")->spec.hint == "session",
          "…typed Name, hinted with the contents the host offers for that kind");
    // THE KIND IS AN INPUT, NOT A CHOICE. Every claim below is the one this block
    // has always made — the preview is live, the source carries over, `help` drops it, Escape
    // puts the whole content back — driven by typing a name instead of stepping a closed list.
    // What changed is what the field ACCEPTS, and that is asserted at the end of this file.
    auto retype_kind = [&](const std::string& name) {
      act(ed, "widget kind");
      for (int i = 0; i < 12; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
      type(ed, name);
    };
    act(ed, "widget kind");
    check(rolltui_menu_editing(ed.menu()) && editing_text_of(ed) == "transcript",
          "the Widget kind input opens on the current kind [" + editing_text_of(ed) + "]");
    check(find(ed.menu(), "kind") && str_of(find(ed.menu(), "kind")->spec.hint).find("this tool previews:") == 0,
          "…with what this binary can preview as its HINT, which is all a design tool honestly knows");
    for (int i = 0; i < 12; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "rows");
    check(ed.previewing() && content_of(ed, "transcript") == "rows:session", "typing a kind previews it live and KEEPS the source [" + content_of(ed, "transcript") + "]");
    for (int i = 0; i < 4; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "help");
    check(content_of(ed, "transcript") == "help", "…and `help`, which takes no source, drops it");
    for (int i = 0; i < 4; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "file");
    check(content_of(ed, "transcript") == "file:session", "…typing off `help` again restores the source from before the preview, not from the previewed content");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    check(!ed.previewing() && content_of(ed, "transcript") == "transcript:session", "Escape puts the whole content back");
    // Commit `menu`, and the two source fields swap places.
    retype_kind("menu");
    LayoutEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && content_of(ed, "transcript") == "menu:session", "Enter commits the menu kind, source kept");
    check(!enabled_of(ed, "source") && enabled_of(ed, "menu_file"),
          "on a `menu` the Menu file field owns the source and the Source input is off \xE2\x80\x94 exactly one of the two, always");
    check(str_of(find(ed.menu(), "menu_file")->spec.hint) == "resolves here: main | extra",
          "…hinted with the menu files that RESOLVE, which is again what this binary knows and not what the target has");
    check(ed.selection_line().find("selected: transcript") != std::string::npos, "the selection line names the node");
    act(ed, "menu file");
    for (int i = 0; i < 12; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "extra");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && content_of(ed, "transcript") == "menu:extra", "the Menu file field writes the source [" + content_of(ed, "transcript") + "]");
    // Back to a transcript, and the source typed by hand.
    retype_kind("transcript");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    act(ed, "source");
    for (int i = 0; i < 8; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "scratch");
    check(ed.previewing() && content_of(ed, "transcript") == "transcript:scratch", "typing a source previews it live");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && content_of(ed, "transcript") == "transcript:scratch", "Enter commits the typed source");
    act(ed, "source");
    type(ed, "/");
    check(rolltui_menu_editing(ed.menu()) && editing_text_of(ed) == "scratch" && ed.status_line().find("refused") != std::string::npos,
          "a '/' is refused in a Name source at the keystroke, the text kept [" + ed.status_line() + "]");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    // A `file:` source is a path, so the same key is accepted there.
    retype_kind("file");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    check(find(ed.menu(), "source") && find(ed.menu(), "source")->spec.type == InputType::Text, "a `file` source is Text, not Name: a path has slashes in it");
    act(ed, "source");
    handle(ed, key(ROLLTUI_KEY_END));  // an edit opens with the whole value selected; End appends instead
    type(ed, "/x");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && content_of(ed, "transcript") == "file:scratch/x", "…so a path commits [" + content_of(ed, "transcript") + "]");
    // An empty source on a kind that requires one is left saying so, not invented.
    act(ed, "source");
    for (int i = 0; i < 12; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(rolltui_menu_editing(ed.menu()) && ed.status_line().find("a value is needed") != std::string::npos,
          "an empty source for a kind that requires one is refused with the reason [" + ed.status_line() + "]");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    // Put it back where the rest of the test expects it.
    retype_kind("transcript");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    act(ed, "source");
    for (int i = 0; i < 12; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "session");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    check(content_of(ed, "transcript") == "transcript:session", "…and back to transcript:session for the rest of the test");
    bool clean = false;
    std::optional<Layout> back = parse_layout(dump_layout(ed.current()), &clean);
    check(back && clean && *back == ed.current(), "every content the kind picker wrote round-trips through the loader clean");
  }
  // ---- the actions level ----
  {
    const std::size_t before = ed.current().actions.size();
    act(ed, "this screen", "actions this screen");
    handle(ed, key(ROLLTUI_KEY_END));  // add an action
    handle(ed, key(ROLLTUI_KEY_ENTER));
    type(ed, "app.zoom");
    LayoutEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && ed.current().actions.size() == before + 1 && ed.current().actions.back().name == "app.zoom" &&
              ed.current().actions.back().description.empty(),
          "adding an action declares it with no description invented for it");
    check(find(ed.menu(), "action.app.zoom.desc") != nullptr, "…and the level grows a submenu for it, keyed by the dotted name");
    // The loader's own rules, in the editor, refusing by the same words.
    handle(ed, key(ROLLTUI_KEY_END));
    handle(ed, key(ROLLTUI_KEY_ENTER));
    type(ed, "input.zoom");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Changed && ed.current().actions.size() == before + 1 && ed.status_line().find("the library's and cannot be declared") != std::string::npos,
          "a library scope is refused with the loader's own words [" + ed.status_line() + "]");
    handle(ed, key(ROLLTUI_KEY_END));
    handle(ed, key(ROLLTUI_KEY_ENTER));
    type(ed, "zoom");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Changed && ed.status_line().find("<scope>.<verb>") != std::string::npos, "a name with no scope is refused [" + ed.status_line() + "]");
    handle(ed, key(ROLLTUI_KEY_END));
    handle(ed, key(ROLLTUI_KEY_ENTER));
    type(ed, "app.zoom");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Changed && ed.status_line().find("already declared") != std::string::npos, "a duplicate is refused [" + ed.status_line() + "]");
    // The description, and the removal.
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    act(ed, "this screen", "actions this screen");
    type(ed, "app.zoom");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    handle(ed, key(ROLLTUI_KEY_ENTER));  // "what it does"
    type(ed, "zoom the transcript");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && ed.current().actions.back().description == "zoom the transcript", "the description commits");
    handle(ed, key(ROLLTUI_KEY_END));    // remove this action
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && ed.current().actions.size() == before && ed.status_line().find("kept and inert") != std::string::npos,
          "remove drops the declaration and says what happens to a chord for it [" + ed.status_line() + "]");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
  }
  // ---- creating a layout, not inheriting one ----
  // The three fields the editor could not reach (min_width, min_height, the layer's
  // focus), and the skeleton. The layout in `ed` at this point is the shipped default,
  // split about and carrying an extra action — which is exactly the state the milestone's
  // measurement was taken from, so it is the right thing to create a new layout out of.
  {
    ed.load(builtin_layout(rolltui_test::test_context(), "default"));
    check(value_of(ed, "min_width") == "60" && value_of(ed, "min_height") == "8" && value_of(ed, "focus") == "input",
          "the layout-wide fields show the loaded screen's thresholds and focus [" + value_of(ed, "focus") + "]");
    act(ed, "this screen", "minimum width");
    for (int i = 0; i < 4; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
    type(ed, "72");
    LayoutEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && ed.committed().min_width == 72, "the minimum width commits");
    act(ed, "this screen", "focused window");
    handle(ed, key(ROLLTUI_KEY_HOME));
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && ed.committed().base.focus.empty() && ed.status_line().find("first focusable") != std::string::npos,
          "the focus choice's first option is \"(none)\", and it is a real answer, not an empty one [" + ed.status_line() + "]");
    act(ed, "this screen", "focused window");
    handle(ed, key(ROLLTUI_KEY_END));
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && ed.committed().base.focus == "input",
          "…and the rest are the base layer's FOCUSABLE windows [" + str_of(ed.committed().base.focus) + "]");
    // The skeleton itself, as a value: nothing carried, whatever was open.
    const Layout before = ed.current().clone();
    check(before.popups.size() == 7 && before.actions.size() == 8 && before.min_width == 72,
          "the screen it is created FROM has seven popups, eight actions and a threshold");
    act(ed, "layout file", "new layout");
    type(ed, "kiosk");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    const Layout& made = ed.current();
    check(o.kind == O::Committed && made.name == "kiosk" && made.popups.empty() && made.actions.empty() && made.min_width == 0 &&
              made.min_height == 0,
          "New layout carries no popup, no action and no threshold out of it");
    check(made.base.root.is_window() && made.base.root.id == "main" && made.base.root.content == "text:" &&
              made.base.focus == "main" && ed.selected() == "main",
          "…one window naming nothing a host must have bound, focused, and selected [" + str_of(made.base.root.content) + "]");
    check(ed.undo() && ed.current() == before, "…and it is one commit: Ctrl-Z is the screen that was open");
    ed.redo();
    // The one inheritance, and it is the TARGET's.
    ed.set_default_min(40, 12);
    act(ed, "layout file", "new layout");
    type(ed, "kiosk2");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    check(ed.current().min_width == 40 && ed.current().min_height == 12 && ed.current().popups.empty(),
          "under a profile the thresholds come from the APP — the one place inheriting is right");
    check(ed.skeleton("x").min_width == 40 && ed.skeleton("x").actions.empty(), "…and the skeleton says so as a value");
    ed.set_default_min(0, 0);
  }
  // ---- the picker offers what the HOST offers ----
  // The kinds are what the host offers, never the library's closed table read directly. The
  // registry belongs to a CONTEXT, so this case gets a session of its own and freeing it is
  // what keeps the case out of another one's way.
  {
    RolltuiContext* target = rolltui_context_new();
    std::string why;
    (void)why;  // register's refusal reason, C-side (rolltui_widget_kind_register has no `why` out-param)
    const int ok1 = rolltui_widget_kind_register(target, "canvas", sizeof("canvas") - 1, ROLLTUI_SOURCE_REQUIRED,
                                                "a surface this app paints", sizeof("a surface this app paints") - 1);
    const int ok2 = rolltui_widget_kind_register(target, "approval", sizeof("approval") - 1, ROLLTUI_SOURCE_FORBIDDEN, "", 0);
    check(ok1 == ROLLTUI_REGISTER_OK && ok2 == ROLLTUI_REGISTER_OK,
          "the target app registers two kinds: one that names a source and one that takes none");
    LayoutEditor te{target};
    te.load(builtin_layout(target, "default"));
    te.select("transcript");
    auto kind_hint = [](LayoutEditor& e) {
      const MenuItem* it = find(e.menu(), "kind");
      return it ? str_of(it->spec.hint) : std::string("(no item 'kind')");
    };
    auto retype = [&](LayoutEditor& e, const char* field, const std::string& name) {
      act(e, field);
      for (int i = 0; i < 14; ++i) handle(e, key(ROLLTUI_KEY_BACKSPACE));
      type(e, name);
    };
    check(kind_hint(te) == "this tool previews: transcript | input | menu | rows | text | file | help",
          "told nothing about a target, the HINT is this binary's own table [" + kind_hint(te) + "]");
    // A host that has registered kinds of its own says so, and the hint grows — it is a list of
    // what can be PREVIEWED here, never a list of what may be named.
    te.set_kinds({"transcript", "input", "menu", "rows", "text", "file", "help", "canvas", "approval"});
    te.set_sources({"transcript:session", "canvas:main"});
    check(kind_hint(te).find("canvas | approval") != std::string::npos,
          "a host's own registered kinds join the hint after the library's [" + kind_hint(te) + "]");
    // Typing one writes a content the LOADER accepts — the registered name, not
    // "registered", which is what a Content field assembled by hand would have said.
    retype(te, "widget kind", "canvas");
    LayoutEditor::Outcome o = handle(te, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && content_of(te, "transcript") == "canvas:session",
          "a registered kind commits like any other, keeping the source [" + content_of(te, "transcript") + "]");
    check(enabled_of(te, "source") && find(te.menu(), "source") && find(te.menu(), "source")->spec.type == InputType::Name &&
              find(te.menu(), "source")->spec.hint == "main",
          "…its source field obeys the rule ITS HOST gave it, hinted from the profile's own contents [" +
              (find(te.menu(), "source") ? str_of(find(te.menu(), "source")->spec.hint) : std::string("(none)")) + "]");
    // A registered kind that takes no source disables the field exactly as `help` does,
    // and DROPS the source rather than writing a content the loader would refuse.
    retype(te, "widget kind", "approval");
    o = handle(te, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && content_of(te, "transcript") == "approval" && !enabled_of(te, "source"),
          "a registered kind whose source is forbidden drops it, like `help` [" + content_of(te, "transcript") + "]");
    bool clean = false;
    std::optional<Layout> back = parse_layout(dump_layout(te.current()), &clean);
    check(back && clean && *back == te.current(), "the layout the picker wrote round-trips through the loader clean");
    // The hint for a kind with no sample content offered for it is the app's OWN words.
    te.set_sources({});
    retype(te, "widget kind", "canvas");
    handle(te, key(ROLLTUI_KEY_ENTER));
    check(find(te.menu(), "source") && find(te.menu(), "source")->spec.hint == "a surface this app paints",
          "…and with no sample content the hint is what the app said its source names");

    // ---- A KIND IN NEITHER RUNG IS WRITTEN DOWN, AND SAID ------------------------------------
    // A DESIGN TOOL MAY NOT REFUSE A KIND IT DOES NOT KNOW. The tempting rule — "the picker is
    // a list of what EXISTS, never a way to invent a kind" — has one app in it. This tool is
    // authoring for ANOTHER app, and the kinds it can resolve are its own, so refusing an
    // unknown name refuses every kind the TARGET has and this tool does not. The screen names
    // what it needs; the app REPORTS what it cannot provide (`rolltui_gaps_collect`).
    te.set_kinds({"transcript", "canvas"});
    retype(te, "source", "session");  // so the assertion below is about the KIND and not about a carried source
    handle(te, key(ROLLTUI_KEY_ENTER));
    retype(te, "widget kind", "sundial");
    o = handle(te, key(ROLLTUI_KEY_ENTER));
    check(o.kind == O::Committed && content_of(te, "transcript") == "sundial:session",
          "a kind in neither rung is WRITTEN, source and all [" + content_of(te, "transcript") + "]");
    check(te.status_line().find("not a kind this tool can build") != std::string::npos &&
              te.status_line().find("previews as a placeholder") != std::string::npos,
          "…and what is said is about this TOOL, not about the screen [" + te.status_line() + "]");
    check(enabled_of(te, "source") && find(te.menu(), "source") &&
              str_of(find(te.menu(), "source")->spec.hint) == "what 'sundial' is given in the app this screen is for",
          "…its source field stays usable, hinted at the app that owns the answer [" +
              (find(te.menu(), "source") ? str_of(find(te.menu(), "source")->spec.hint) : std::string("(none)")) + "]");
    check(te.selection_line().find("not previewable here") != std::string::npos,
          "…and the selection line says the same thing in the same direction [" + te.selection_line() + "]");
    {
      bool clean2 = false;
      std::optional<Layout> back2 = parse_layout(dump_layout(te.current()), &clean2);
      check(back2 && clean2 && *back2 == te.current(),
            "…and a screen naming a kind nobody here has round-trips through the loader clean: it is a file, not a claim");
    }
    rolltui_context_free(target);  // and with it the two kinds — no clearing to remember
  }
  // ---- the tree, which is the thing this editor edits and could not show ------------------
  // A list of fields says what the selected node IS without ever saying where it sits, so
  // splitting a row and swapping siblings were moves made blind. The rows are in the SAME
  // order Tab walks, so the row that is highlighted is the row that moves — one ordering, not
  // a second one to drift out of step with the first.
  {
    RolltuiContext* tctx = rolltui_context_new();
    rolltui_context_set_library_defaults(tctx);
    LayoutEditor ed(tctx);
    ed.load(builtin_layout(tctx, "default"));
    RolltuiRows rows{};
    ed.tree_rows(rows);
    std::vector<std::string> labels, values;
    for (std::size_t i = 0; i < rows.n; ++i) {
      labels.emplace_back(view_of(rows.v[i].label));
      values.emplace_back(view_of(rows.v[i].value));
    }
    check(rows.n > 3, "the tree has a row per node (" + std::to_string(rows.n) + ")");
    check(!labels.empty() && labels[0] == "(row)" && values[0] == "row",
          "a container names the way it divides, which is the whole of what it is [" +
              (labels.empty() ? "" : labels[0] + " / " + values[0]) + "]");
    // DEPTH IS THE INDENT, so the shape is readable without a second column for it.
    bool indented = false;
    for (const std::string& l : labels)
      if (l.rfind("    ", 0) == 0) indented = true;
    check(indented, "a child is indented under its parent");
    bool named = false;
    for (std::size_t i = 0; i < labels.size(); ++i)
      if (labels[i].find("transcript") != std::string::npos && values[i] == "transcript:session") named = true;
    check(named, "a window says what it shows");

    // THE SAME ORDER TAB WALKS. The selected row is the row that moves, and if these two
    // walks ever disagreed the highlight would point at a node the operations do not touch.
    const std::vector<std::string> ids = ed.all_ids();
    check(ed.tree_selected() < rows.n, "the selection is a row of the tree (" + std::to_string(ed.tree_selected()) + ")");
    const std::string first_sel = ed.selected();
    check(labels[ed.tree_selected()].find(first_sel) != std::string::npos,
          "…and it is the row for the selected node [" + labels[ed.tree_selected()] + " vs " + first_sel + "]");
    const std::size_t before = ed.tree_selected();
    handle(ed, key(ROLLTUI_KEY_TAB));
    ed.tree_rows(rows);
    check(ed.tree_selected() != before && ed.selected() != first_sel,
          "Tab moves the highlight, because it is the same walk");
    check(view_of(rows.v[ed.tree_selected()].label).find(ed.selected()) != std::string::npos,
          "…and it lands on the row for the node Tab selected");

    // A POPUP'S ROOT IS A NODE THE EDITOR CAN SELECT, so it is a row — under a heading,
    // because a popup is not part of the base tree and a flat list would say it was.
    bool heading = false;
    for (const std::string& l : labels)
      if (l == "popup") heading = true;
    check(heading, "each popup is announced, so its root does not read as part of the base tree");
    rolltui_rows_release(&rows);
    rolltui_context_free(tctx);
  }

  rolltui_context_free(ctx);
  return report("rolltui layout_editor_test");
}
