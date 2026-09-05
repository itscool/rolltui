//
// layout_editor_test.cpp — the layout editor's model (milestone 16): selection by Tab,
// split into a row / a column, swap, hide, border and content as live-previewed
// choices (Enter commits, Escape cancels), title and size as inputs (live, refused
// when not a size), Alt+arrow nudges, delete with collapse, popups (add, place,
// anchor, remove), the seam drag, undo/redo, save/load outcomes, and the produced
// layout round-tripping through the loader clean.
//
// Phase 10 m5 — THE DESIGN EDITOR: the widget-kind picker over Layout.hpp's closed
// table, the source field typed by the kind, the menu-file choice, and the actions
// level. The property to hold on to is that EXACTLY ONE of Source / Menu file is
// enabled for any kind, so the editor never offers two ways to say one thing.
//
// Every menu lookup goes through value_of / enabled_of, which NAME a missing item
// instead of dereferencing a null (CLAUDE.md: a control that crashes reports nothing —
// this file segfaulted on `find("content")->value` the moment that item was renamed).
//
#include <string>

#include "layout_editor.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui::tools;
using namespace rolltui_test;

namespace {
KeyEvent key(Key k, bool shift = false, bool alt = false) { KeyEvent e; e.key = k; e.shift = shift; e.alt = alt; return e; }
KeyEvent ch(char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); return e; }
KeyEvent ctrl(char c) { KeyEvent e = ch(c); e.ctrl = true; return e; }
void type(LayoutEditor& ed, const std::string& s) { for (char c : s) ed.handle(ch(c)); }
// Reaches a top-level item by filter and acts on it.
void act(LayoutEditor& ed, const std::string& filter) {
  ed.handle(key(Key::Escape));
  ed.handle(key(Key::Escape));
  ed.handle(key(Key::Home));
  type(ed, filter);
  ed.handle(key(Key::Enter));
}
// A missing item is a NAMED answer, never a null deref: this file crashed on one.
std::string value_of(const LayoutEditor& ed, const char* id) {
  const MenuItem* it = ed.menu().find(id);
  return it ? it->value.str() : "(no item '" + std::string(id) + "')";
}
bool enabled_of(const LayoutEditor& ed, const char* id) {
  const MenuItem* it = ed.menu().find(id);
  return it && it->enabled;
}
std::string content_of(const LayoutEditor& ed, const char* node) {
  const Node* n = LayoutEditor::find_node(ed.current().base.root, node);
  return n ? n->content.str() : "(no node '" + std::string(node) + "')";
}
}  // namespace

int main() {
  LayoutEditor ed;
  ed.load(*builtin_layout("default"));
  using O = LayoutEditor::Outcome::Kind;
  check(ed.selected() == "transcript" && ed.selected_node() && ed.selected_node()->is_window(), "loading selects the first window in tree order [" + ed.selected() + "]");
  // ---- selection ----
  {
    ed.handle(key(Key::Tab));
    check(ed.selected() == "input", "Tab selects the next node [" + ed.selected() + "]");
    ed.handle(key(Key::Tab));
    check(ed.selected() == "status", "…then the status window");
    ed.handle(key(Key::Tab, true));
    check(ed.selected() == "input", "Shift-Tab goes back");
    ed.select("transcript");
    check(ed.selected() == "transcript" && ed.menu().find("root")->label.find("transcript") != std::string::npos, "select(id) and the breadcrumb names the node");
    check(value_of(ed, "border") == "single" && value_of(ed, "kind") == "transcript" && value_of(ed, "source") == "session" && value_of(ed, "size") == "fill",
          "the menu shows the selected node's border, kind, source and size");
  }
  // ---- split into a row ----
  {
    act(ed, "split into a row");
    const Node* t = LayoutEditor::find_node(ed.current().base.root, "transcript");
    const Node* t2 = LayoutEditor::find_node(ed.current().base.root, "transcript-2");
    std::size_t idx = 0;
    const Node* parent = LayoutEditor::parent_of(const_cast<Node&>(ed.current().base.root), "transcript", &idx);
    check(t && t2 && parent && parent->kind == Node::Kind::Row && parent->children.size() == 2 && t2->content == "transcript:session" && idx == 0,
          "the transcript became a row of [transcript, transcript-2], the copy with the same slot");
    check(ed.undo_depth() == 1 && ed.selected() == "transcript", "…as one commit, with the original selected");
    LayoutLoadReport rep;
    std::optional<Layout> back = load_layout(layout_to_json(ed.current()), rep);
    check(back && rep.clean() && *back == ed.current(), "the edited layout round-trips through the loader clean");
  }
  // ---- swap, hide, delete with collapse ----
  {
    act(ed, "swap with the next");
    std::size_t idx = 9;
    LayoutEditor::parent_of(const_cast<Node&>(ed.current().base.root), "transcript", &idx);
    check(idx == 1 && ed.undo_depth() == 2, "swap with the next sibling moves it to index 1");
    act(ed, "swap with the next");
    check(ed.status_line().find("no sibling") != std::string::npos && ed.undo_depth() == 2, "swapping past the end is refused with a reason and commits nothing");
    ed.select("transcript-2");
    act(ed, "visible");
    check(!LayoutEditor::find_node(ed.current().base.root, "transcript-2")->visible && ed.undo_depth() == 3, "the Visible toggle hides the node");
    act(ed, "delete");
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
    check(ed.menu().level().id == "border" && ed.menu().selected_item()->id == "single", "the Border choice opens on the current value");
    ed.handle(key(Key::Down));
    check(ed.previewing() && LayoutEditor::find_node(ed.current().base.root, "transcript")->border == Border::Rounded, "moving to rounded previews it live");
    ed.handle(key(Key::Escape));
    check(!ed.previewing() && LayoutEditor::find_node(ed.current().base.root, "transcript")->border == Border::Single, "Escape puts single back");
    act(ed, "border");
    ed.handle(key(Key::Down));
    ed.handle(key(Key::Down));
    LayoutEditor::Outcome o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && ed.committed().base.root.children[0].children[0].border == Border::Double, "Enter commits double");
  }
  // ---- title and size inputs, nudges ----
  {
    act(ed, "title");
    check(ed.menu().selected_item()->value == "transcript", "the Title input is prefilled with the current title");
    for (int i = 0; i < 10; ++i) ed.handle(key(Key::Backspace));
    type(ed, "chat");
    check(ed.previewing() && LayoutEditor::find_node(ed.current().base.root, "transcript")->title == "chat", "typing a title previews it");
    LayoutEditor::Outcome o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && LayoutEditor::find_node(ed.committed().base.root, "transcript")->title == "chat", "Enter commits the title");
    ed.select("input");
    act(ed, "size");
    for (int i = 0; i < 4; ++i) ed.handle(key(Key::Backspace));
    type(ed, "5");
    check(LayoutEditor::find_node(ed.current().base.root, "input")->size == SplitSize::fixed(Dim::abs(5)), "typing a size applies it live");
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && LayoutEditor::find_node(ed.committed().base.root, "input")->size == SplitSize::fixed(Dim::abs(5)), "Enter commits the size");
    act(ed, "size");
    type(ed, "x");
    check(ed.menu().editing() && ed.menu().editing_text() == "5" && ed.status_line().find("refused: not the start of a size") != std::string::npos &&
              LayoutEditor::find_node(ed.current().base.root, "input")->size == SplitSize::fixed(Dim::abs(5)),
          "a key that cannot begin a size is refused at the keystroke with the reason; the text and the size are kept [" + ed.status_line() + "]");
    for (int i = 0; i < 2; ++i) ed.handle(key(Key::Backspace));
    type(ed, "fil");
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Changed && ed.menu().editing() && ed.status_line().find("not a size yet") != std::string::npos,
          "Enter on a text that is not yet a size is refused with the reason [" + ed.status_line() + "]");
    ed.handle(key(Key::Escape));
    check(!ed.menu().editing() && LayoutEditor::find_node(ed.current().base.root, "input")->size == SplitSize::fixed(Dim::abs(5)), "Escape restores the committed size");
    o = ed.handle(key(Key::Down, false, true));  // Alt+Down: input is in a column
    check(o.kind == O::Committed && LayoutEditor::find_node(ed.committed().base.root, "input")->size == SplitSize::fixed(Dim::abs(6)), "Alt+Down nudges the size to 6, a commit");
    o = ed.handle(key(Key::Right, false, true));
    check(o.kind == O::Changed && ed.status_line().find("up/down") != std::string::npos, "Alt+Right on a column child says which way it sizes");
    ed.select("status");
    o = ed.handle(key(Key::Left, false, true));
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
    act(ed, "popups");
    const std::size_t before = ed.current().popups.size();
    ed.handle(key(Key::End));  // add a popup
    ed.handle(key(Key::Enter));
    type(ed, "note");
    LayoutEditor::Outcome o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && ed.current().popups.size() == before + 1 && ed.current().popup("note") && ed.current().popup("note")->root.content == "text:note",
          "adding a popup creates a centred modal one with a text slot");
    check(ed.menu().find("popup.note.x") != nullptr, "…and the menu grows a level for it");
    ed.handle(key(Key::Escape));
    act(ed, "popups");
    type(ed, "note");
    ed.handle(key(Key::Enter));  // the note level
    ed.handle(key(Key::Down));
    ed.handle(key(Key::Down));   // w
    ed.handle(key(Key::Enter));
    for (int i = 0; i < 5; ++i) ed.handle(key(Key::Backspace));
    type(ed, "80%");
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && ed.committed().popup("note")->placement.w == Dim::rel(0.8), "a placement dim typed as 80% commits");
    ed.handle(key(Key::Down));
    ed.handle(key(Key::Down));   // anchor
    ed.handle(key(Key::Enter));
    ed.handle(key(Key::Home));
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && ed.committed().popup("note")->placement.anchor == Anchor::TopLeft, "the anchor choice commits top-left");
    ed.handle(key(Key::End));    // remove
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && !ed.current().popup("note"), "remove deletes the popup");
  }
  // ---- undo / redo, outcomes ----
  {
    const std::size_t depth = ed.undo_depth();
    ed.handle(ctrl('z'));
    check(ed.undo_depth() == depth - 1 && ed.current().popup("note") != nullptr, "Ctrl-Z brings the popup back");
    ed.handle(ctrl('y'));
    check(ed.undo_depth() == depth && !ed.current().popup("note"), "Ctrl-Y removes it again");
    ed.set_layouts({"default", "stacked", "two"});
    act(ed, "load layout");
    ed.handle(key(Key::Down));
    LayoutEditor::Outcome o = ed.handle(key(Key::Enter));
    check(o == LayoutEditor::Outcome{O::LoadLayout, "stacked"}, "Load layout asks the host");
    act(ed, "save layout");
    type(ed, "two");
    o = ed.handle(key(Key::Enter));
    check(o == LayoutEditor::Outcome{O::SaveAs, "two"}, "Save layout file as asks the host with the name");
    ed.handle(key(Key::Escape));
    ed.handle(key(Key::Escape));
    ed.handle(key(Key::Home));
    type(ed, "reset to the loaded");
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::ResetLoaded, "Reset to the loaded layout is an outcome the host confirms, never applied here");
    ed.handle(key(Key::Escape));
    o = ed.handle(key(Key::Escape));
    check(o.kind == O::Closed, "Escape at the top asks the host to close");
  }
  // ---- m5: the kind picker, the source field, the menu-file choice ----
  {
    ed.load(*builtin_layout("default"));  // a fresh baseline: the blocks above left it split about
    ed.set_menus({"main", "extra"});
    ed.set_sources({"transcript:session", "rows:status", "input:prompt", "text:pane"});
    ed.select("transcript");
    check(enabled_of(ed, "source") && !enabled_of(ed, "menu_file"),
          "on a transcript the Source field is the one that owns the source, the Menu file choice is off");
    check(ed.menu().find("source") && ed.menu().find("source")->spec.type == InputType::Name && ed.menu().find("source")->spec.hint == "session",
          "…typed Name, hinted with the contents the host offers for that kind");
    // The kind choice, previewed and committed.
    act(ed, "widget kind");
    check(ed.menu().level().id == "kind" && ed.menu().selected_item() && ed.menu().selected_item()->id == "transcript", "the Widget kind choice opens on the current kind");
    for (int i = 0; i < 3; ++i) ed.handle(key(Key::Down));  // transcript → input → menu → rows
    check(ed.previewing() && content_of(ed, "transcript") == "rows:session", "moving down the kind list previews the new kind and KEEPS the source [" + content_of(ed, "transcript") + "]");
    ed.handle(key(Key::Down));
    ed.handle(key(Key::Down));
    ed.handle(key(Key::Down));  // rows → text → file → help
    check(content_of(ed, "transcript") == "help", "…and `help`, which takes no source, drops it");
    ed.handle(key(Key::Up));
    check(content_of(ed, "transcript") == "file:session", "…stepping back off `help` restores the source from before the preview, not from the previewed content");
    ed.handle(key(Key::Escape));
    check(!ed.previewing() && content_of(ed, "transcript") == "transcript:session", "Escape puts the whole content back");
    // Commit `menu`, and the two source fields swap places.
    act(ed, "widget kind");
    for (int i = 0; i < 2; ++i) ed.handle(key(Key::Down));
    LayoutEditor::Outcome o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && content_of(ed, "transcript") == "menu:session", "Enter commits the menu kind, source kept");
    check(!enabled_of(ed, "source") && enabled_of(ed, "menu_file"),
          "on a `menu` the Menu file choice owns the source and the Source input is off \xE2\x80\x94 exactly one of the two, always");
    check(ed.selection_line().find("selected: transcript") != std::string::npos, "the selection line names the node");
    act(ed, "menu file");
    ed.handle(key(Key::End));
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && content_of(ed, "transcript") == "menu:extra", "the Menu file choice writes the source [" + content_of(ed, "transcript") + "]");
    // Back to a transcript, and the source typed by hand.
    act(ed, "widget kind");
    ed.handle(key(Key::Home));
    ed.handle(key(Key::Enter));
    act(ed, "source");
    for (int i = 0; i < 8; ++i) ed.handle(key(Key::Backspace));
    type(ed, "scratch");
    check(ed.previewing() && content_of(ed, "transcript") == "transcript:scratch", "typing a source previews it live");
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && content_of(ed, "transcript") == "transcript:scratch", "Enter commits the typed source");
    act(ed, "source");
    type(ed, "/");
    check(ed.menu().editing() && ed.menu().editing_text() == "scratch" && ed.status_line().find("refused") != std::string::npos,
          "a '/' is refused in a Name source at the keystroke, the text kept [" + ed.status_line() + "]");
    ed.handle(key(Key::Escape));
    // A `file:` source is a path, so the same key is accepted there.
    act(ed, "widget kind");
    ed.handle(key(Key::Home));
    for (int i = 0; i < 5; ++i) ed.handle(key(Key::Down));  // file
    ed.handle(key(Key::Enter));
    check(ed.menu().find("source") && ed.menu().find("source")->spec.type == InputType::Text, "a `file` source is Text, not Name: a path has slashes in it");
    act(ed, "source");
    ed.handle(key(Key::End));  // an edit opens with the whole value selected; End appends instead
    type(ed, "/x");
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && content_of(ed, "transcript") == "file:scratch/x", "…so a path commits [" + content_of(ed, "transcript") + "]");
    // An empty source on a kind that requires one is left saying so, not invented.
    act(ed, "source");
    for (int i = 0; i < 12; ++i) ed.handle(key(Key::Backspace));
    o = ed.handle(key(Key::Enter));
    check(ed.menu().editing() && ed.status_line().find("a value is needed") != std::string::npos,
          "an empty source for a kind that requires one is refused with the reason [" + ed.status_line() + "]");
    ed.handle(key(Key::Escape));
    // Put it back where the rest of the test expects it.
    act(ed, "widget kind");
    ed.handle(key(Key::Home));
    ed.handle(key(Key::Enter));
    act(ed, "source");
    for (int i = 0; i < 12; ++i) ed.handle(key(Key::Backspace));
    type(ed, "session");
    ed.handle(key(Key::Enter));
    check(content_of(ed, "transcript") == "transcript:session", "…and back to transcript:session for the rest of the test");
    LayoutLoadReport rep;
    std::optional<Layout> back = load_layout(layout_to_json(ed.current()), rep);
    check(back && rep.clean() && *back == ed.current(), "every content the kind picker wrote round-trips through the loader clean");
  }
  // ---- m5: the actions level ----
  {
    const std::size_t before = ed.current().actions.size();
    act(ed, "actions this screen");
    ed.handle(key(Key::End));  // add an action
    ed.handle(key(Key::Enter));
    type(ed, "app.zoom");
    LayoutEditor::Outcome o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && ed.current().actions.size() == before + 1 && ed.current().actions.back().name == "app.zoom" &&
              ed.current().actions.back().description.empty(),
          "adding an action declares it with no description invented for it");
    check(ed.menu().find("action.app.zoom.desc") != nullptr, "…and the level grows a submenu for it, keyed by the dotted name");
    // The loader's own rules, in the editor, refusing by the same words.
    ed.handle(key(Key::End));
    ed.handle(key(Key::Enter));
    type(ed, "input.zoom");
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Changed && ed.current().actions.size() == before + 1 && ed.status_line().find("the library's and cannot be declared") != std::string::npos,
          "a library scope is refused with the loader's own words [" + ed.status_line() + "]");
    ed.handle(key(Key::End));
    ed.handle(key(Key::Enter));
    type(ed, "zoom");
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Changed && ed.status_line().find("<scope>.<verb>") != std::string::npos, "a name with no scope is refused [" + ed.status_line() + "]");
    ed.handle(key(Key::End));
    ed.handle(key(Key::Enter));
    type(ed, "app.zoom");
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Changed && ed.status_line().find("already declared") != std::string::npos, "a duplicate is refused [" + ed.status_line() + "]");
    // The description, and the removal.
    ed.handle(key(Key::Escape));
    act(ed, "actions this screen");
    type(ed, "app.zoom");
    ed.handle(key(Key::Enter));
    ed.handle(key(Key::Enter));  // "what it does"
    type(ed, "zoom the transcript");
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && ed.current().actions.back().description == "zoom the transcript", "the description commits");
    ed.handle(key(Key::End));    // remove this action
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && ed.current().actions.size() == before && ed.status_line().find("kept and inert") != std::string::npos,
          "remove drops the declaration and says what happens to a chord for it [" + ed.status_line() + "]");
    ed.handle(key(Key::Escape));
    ed.handle(key(Key::Escape));
  }
  // ---- Phase 11 m5: creating a layout, not inheriting one ----
  // The three fields the editor could not reach (min_width, min_height, the layer's
  // focus), and the skeleton. The layout in `ed` at this point is the shipped default,
  // split about and carrying an extra action — which is exactly the state the milestone's
  // measurement was taken from, so it is the right thing to create a new layout out of.
  {
    ed.load(*builtin_layout("default"));
    check(value_of(ed, "min_width") == "60" && value_of(ed, "min_height") == "8" && value_of(ed, "focus") == "input",
          "the layout-wide fields show the loaded screen's thresholds and focus [" + value_of(ed, "focus") + "]");
    act(ed, "minimum width");
    for (int i = 0; i < 4; ++i) ed.handle(key(Key::Backspace));
    type(ed, "72");
    LayoutEditor::Outcome o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && ed.committed().min_width == 72, "the minimum width commits");
    act(ed, "focused window");
    ed.handle(key(Key::Home));
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && ed.committed().base.focus.empty() && ed.status_line().find("first focusable") != std::string::npos,
          "the focus choice's first option is \"(none)\", and it is a real answer, not an empty one [" + ed.status_line() + "]");
    act(ed, "focused window");
    ed.handle(key(Key::End));
    o = ed.handle(key(Key::Enter));
    check(o.kind == O::Committed && ed.committed().base.focus == "input",
          "…and the rest are the base layer's FOCUSABLE windows [" + ed.committed().base.focus + "]");
    // The skeleton itself, as a value: nothing carried, whatever was open.
    const Layout before = ed.current();
    check(before.popups.size() == 5 && before.actions.size() == 6 && before.min_width == 72,
          "the screen it is created FROM has five popups, six actions and a threshold");
    act(ed, "new layout");
    type(ed, "kiosk");
    o = ed.handle(key(Key::Enter));
    const Layout& made = ed.current();
    check(o.kind == O::Committed && made.name == "kiosk" && made.popups.empty() && made.actions.empty() && made.min_width == 0 &&
              made.min_height == 0,
          "New layout carries no popup, no action and no threshold out of it");
    check(made.base.root.is_window() && made.base.root.id == "main" && made.base.root.content == "text:" &&
              made.base.focus == "main" && ed.selected() == "main",
          "…one window naming nothing a host must have bound, focused, and selected [" + made.base.root.content + "]");
    check(ed.undo() && ed.current() == before, "…and it is one commit: Ctrl-Z is the screen that was open");
    ed.redo();
    // The one inheritance, and it is the TARGET's.
    ed.set_default_min(40, 12);
    act(ed, "new layout");
    type(ed, "kiosk2");
    ed.handle(key(Key::Enter));
    check(ed.current().min_width == 40 && ed.current().min_height == 12 && ed.current().popups.empty(),
          "under a profile the thresholds come from the APP — the one place inheriting is right");
    check(ed.skeleton("x").min_width == 40 && ed.skeleton("x").actions.empty(), "…and the skeleton says so as a value");
    ed.set_default_min(0, 0);
  }
  // ---- Phase 11 m4: the picker offers what the TARGET can build ----
  // The kinds are no longer the library's table read straight out of Layout.hpp — they
  // are what the host offers, which under an app profile is the library's PLUS that app's
  // registered ones. Last in the file on purpose: it registers process-wide kinds, and
  // clears them again at the end so nothing after it inherits another app's vocabulary.
  {
    clear_registered_widget_kinds();
    std::string why;
    check(register_widget_kind("canvas", SourceRule::Required, "a surface this app paints", &why) &&
              register_widget_kind("approval", SourceRule::Forbidden, "", &why),
          "the target app registers two kinds: one that names a source and one that takes none [" + why + "]");
    LayoutEditor te;
    te.load(*builtin_layout("default"));
    te.select("transcript");
    auto options = [](const LayoutEditor& e) {
      const MenuItem* it = e.menu().find("kind");
      std::string s;
      if (!it) return std::string("(no item 'kind')");
      for (const MenuItem& o : it->children) s += (s.empty() ? "" : " ") + o.id;
      return s;
    };
    check(options(te) == "transcript input menu rows text file help",
          "told nothing about a target, the picker is the library's own table [" + options(te) + "]");
    // What the studio does under --app: the library's, then the profile's.
    te.set_kinds({"transcript", "input", "menu", "rows", "text", "file", "help", "canvas", "approval"});
    te.set_sources({"transcript:session", "canvas:main"});
    check(options(te).find("canvas approval") != std::string::npos,
          "under a profile the app's own kinds are offered after the library's [" + options(te) + "]");
    // Choosing one writes a content the LOADER accepts — the registered name, not
    // "registered", which is what a Content field assembled by hand would have said.
    act(te, "widget kind");
    type(te, "canvas");
    LayoutEditor::Outcome o = te.handle(key(Key::Enter));
    check(o.kind == O::Committed && content_of(te, "transcript") == "canvas:session",
          "a registered kind commits like any other, keeping the source [" + content_of(te, "transcript") + "]");
    check(enabled_of(te, "source") && te.menu().find("source") && te.menu().find("source")->spec.type == InputType::Name &&
              te.menu().find("source")->spec.hint == "main",
          "…its source field obeys the rule ITS HOST gave it, hinted from the profile's own contents [" +
              (te.menu().find("source") ? te.menu().find("source")->spec.hint : std::string("(none)")) + "]");
    // A registered kind that takes no source disables the field exactly as `help` does,
    // and DROPS the source rather than writing a content the loader would refuse.
    act(te, "widget kind");
    type(te, "approval");
    o = te.handle(key(Key::Enter));
    check(o.kind == O::Committed && content_of(te, "transcript") == "approval" && !enabled_of(te, "source"),
          "a registered kind whose source is forbidden drops it, like `help` [" + content_of(te, "transcript") + "]");
    LayoutLoadReport rep;
    std::optional<Layout> back = load_layout(layout_to_json(te.current()), rep);
    check(back && rep.clean() && *back == te.current(), "the layout the picker wrote round-trips through the loader clean");
    // The hint for a kind the profile gave no sample content for is the app's OWN words.
    te.set_sources({});
    act(te, "widget kind");
    type(te, "canvas");
    te.handle(key(Key::Enter));
    check(te.menu().find("source") && te.menu().find("source")->spec.hint == "a surface this app paints",
          "…and with no sample content the hint is what the app said its source names");
    // The picker is a list of what EXISTS, never a way to invent a kind. A profile
    // naming a kind its binary does not actually register is the drift this whole file
    // format exists to remove one level down, so it is refused by name rather than
    // written into a layout that would draw an error panel in the real app.
    te.set_kinds({"transcript", "canvas", "sundial"});
    const std::string before_bogus = content_of(te, "transcript");
    act(te, "widget kind");
    type(te, "sundial");
    o = te.handle(key(Key::Enter));
    check(content_of(te, "transcript") == before_bogus &&
              te.status_line().find("not a widget kind this app can build") != std::string::npos,
          "a kind in neither rung is refused by name and writes nothing [" + te.status_line() + "]");
    clear_registered_widget_kinds();
  }
  return report("rolltui layout_editor_test");
}
