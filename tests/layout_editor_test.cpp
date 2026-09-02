//
// layout_editor_test.cpp — the layout editor's model (milestone 16): selection by Tab,
// split into a row / a column, swap, hide, border and content as live-previewed
// choices (Enter commits, Escape cancels), title and size as inputs (live, refused
// when not a size), Alt+arrow nudges, delete with collapse, popups (add, place,
// anchor, remove), the seam drag, undo/redo, save/load outcomes, and the produced
// layout round-tripping through the loader clean.
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
    check(ed.menu().find("border")->value == "single" && ed.menu().find("content")->value == "transcript" && ed.menu().find("size")->value == "fill",
          "the menu shows the selected node's border, content and size");
  }
  // ---- split into a row ----
  {
    act(ed, "split into a row");
    const Node* t = LayoutEditor::find_node(ed.current().base.root, "transcript");
    const Node* t2 = LayoutEditor::find_node(ed.current().base.root, "transcript-2");
    std::size_t idx = 0;
    const Node* parent = LayoutEditor::parent_of(const_cast<Node&>(ed.current().base.root), "transcript", &idx);
    check(t && t2 && parent && parent->kind == Node::Kind::Row && parent->children.size() == 2 && t2->content == "transcript" && idx == 0,
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
  return report("rolltui layout_editor_test");
}
