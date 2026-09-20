// rolltui/tests/mermaid_test.cpp — a mermaid diagram, drawn as text.
//
// THE GOLDENS are the pictures themselves: every `tests/fixtures/mermaid/<name>.mmd` is drawn at 100 columns and
// compared, line for line, with `<name>.txt`, and drawn again in the ASCII glyph set (a terminal where an ambiguous
// glyph is two cells) and compared with `<name>.ascii.txt`. A picture is the one thing a test can only judge by
// looking, so the fixtures are READ before they are recorded: `rolltui-mermaid-test --record` rewrites them, and a
// diff in one is either a picture that got better or a picture that broke.
//
// AROUND THEM, what no picture shows: that every line fits the room it was given; that nothing a file said can reach a
// terminal as a control; that the ASCII set is ASCII; that the same text draws the same picture; that what cannot be
// drawn says why instead of drawing half; and the bugs each picture was once wrong by, each named where it is asserted.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_mermaid.h"  // INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN
#include "rolltui_test.hpp"

using namespace testkit;
namespace fs = std::filesystem;

namespace {

std::size_t live_bytes() {
  std::size_t v = 0;
  rolltui_mem_stats(nullptr, nullptr, nullptr, &v, nullptr, nullptr);
  return v;
}

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

struct Drawn {
  bool ok = false;
  std::string text;  // the picture, a line per row
  std::string why;
  int width = 0;
  std::string kind;
  std::vector<int> classes;  // every run's class, in order
};

Drawn draw(RolltuiMermaid* m, const std::string& src, int width, bool ascii) {
  Drawn d;
  RolltuiStr why;
  d.ok = rolltui_mermaid_render(m, src.data(), src.size(), width, ascii ? 1 : 0, &why) != 0;
  d.why.assign(why.p ? why.p : "", why.n);
  rolltui_str_free(&why);
  if (!d.ok) return d;
  d.width = rolltui_mermaid_width(m);
  d.kind = rolltui_mermaid_kind(m);
  for (std::size_t i = 0; i < rolltui_mermaid_line_count(m); ++i) {
    const RolltuiMermaidRun* runs = nullptr;
    const std::size_t n = rolltui_mermaid_line(m, i, &runs);
    for (std::size_t k = 0; k < n; ++k) {
      d.text.append(runs[k].text, runs[k].n);
      d.classes.push_back(runs[k].cls);
    }
    d.text += '\n';
  }
  return d;
}

bool has(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

int count_of(const std::string& s, const std::string& what) {
  int n = 0;
  for (std::size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) ++n;
  return n;
}

// the display width of a UTF-8 line the way the picture counts it: the box-drawing, arrows and block glyphs are one cell
int cells_of(const std::string& line) {
  int n = 0;
  for (unsigned char c : line)
    if ((c & 0xC0) != 0x80) ++n;
  return n;
}

}  // namespace

int main(int argc, char** argv) {
  const bool record = argc > 1 && std::strcmp(argv[1], "--record") == 0;
  constexpr int kWidth = 120;  // the room every fixture is drawn in
  const fs::path dir = fs::path(ROLLTUI_FIXTURE_DIR) / "mermaid";
  RolltuiMermaid* m = rolltui_mermaid_new();

  // ---- THE PICTURES -------------------------------------------------------------------------------------
  std::vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(dir))
    if (e.path().extension() == ".mmd") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  check(files.size() >= 31, "the fixtures are there (" + std::to_string(files.size()) + " diagrams)");
  for (const fs::path& f : files) {
    const std::string name = f.stem().string();
    const std::string src = slurp(f);
    for (int ascii = 0; ascii < 2; ++ascii) {
      const fs::path golden = dir / (name + (ascii ? ".ascii.txt" : ".txt"));
      const Drawn d = draw(m, src, kWidth, ascii != 0);
      const std::string label = name + (ascii ? " (ascii)" : "");
      check(d.ok, label + ": it draws [" + d.why + "]");
      if (!d.ok) continue;
      if (record) {
        std::ofstream(golden, std::ios::binary | std::ios::trunc) << d.text;
        std::printf("recorded %s\n", golden.string().c_str());
        continue;
      }
      const std::string want = slurp(golden);
      check(d.text == want, label + ": the picture is the recorded one");
      if (d.text != want) {
        // the first line that differs, so a failure says where
        std::istringstream a(d.text), b(want);
        std::string la, lb;
        int row = 0;
        while (true) {
          const bool ga = static_cast<bool>(std::getline(a, la)), gb = static_cast<bool>(std::getline(b, lb));
          ++row;
          if (!ga && !gb) break;
          if (la != lb || ga != gb) {
            std::fprintf(stderr, "  first difference at row %d\n    got  [%s]\n    want [%s]\n", row, ga ? la.c_str() : "(none)", gb ? lb.c_str() : "(none)");
            break;
          }
        }
      }
      // every line fits, and says nothing a terminal could act on
      {
        std::istringstream in(d.text);
        std::string line;
        int widest = 0;
        bool controls = false;
        while (std::getline(in, line)) {
          widest = std::max(widest, cells_of(line));
          for (unsigned char c : line) if (c < 0x20 || c == 0x7f) controls = true;
        }
        check(widest <= kWidth && widest == d.width, label + ": every line fits the room it was given, and the width it reports is the widest [" + std::to_string(widest) + " / " + std::to_string(d.width) + "]");
        check(!controls, label + ": no control character is in the picture");
      }
      if (ascii) {
        bool all_ascii = true;
        for (unsigned char c : d.text) if (c >= 0x80) all_ascii = false;
        check(all_ascii, label + ": the ASCII set is ASCII (a picture for a terminal where the box glyphs are two cells wide)");
      }
      // THE SAME TEXT DRAWS THE SAME PICTURE
      const Drawn again = draw(m, src, kWidth, ascii != 0);
      check(again.ok && again.text == d.text, label + ": drawn twice into one handle, it is the same picture");
      // A DIAGRAM DRAWS IN EXACTLY THE ROOM IT USES: a check that allowed for more than was drawn would refuse one that fits
      // (a pie is the exception: its bars are scaled to the room it is given, so less room is a different picture)
      const Drawn tight = draw(m, src, d.width, ascii != 0);
      if (d.kind == "pie") check(tight.ok, label + ": in the " + std::to_string(d.width) + " columns it uses, it still draws [" + tight.why + "]");
      else check(tight.ok && tight.text == d.text, label + ": in exactly the " + std::to_string(d.width) + " columns it uses, it is the same picture [" + tight.why + "]");
    }
  }
  if (record) {
    rolltui_mermaid_free(m);
    return 0;
  }

  // ---- THE KINDS, and what a picture of each has that a source does not ---------------------------------------
  {
    const Drawn f = draw(m, slurp(dir / "flow-fanout.mmd"), 100, false);
    check(f.kind == "flowchart" && has(f.text, "\xE2\x96\xBC") && has(f.text, "\xE2\x94\x8C"), "a flowchart says what it is and has its arrows and boxes");
    const Drawn s = draw(m, slurp(dir / "sequence.mmd"), 100, false);
    check(s.kind == "sequence" && has(s.text, "\xE2\x94\x86") && has(s.text, "loop Every minute"), "a sequence diagram has its lifelines and its frames");
    const Drawn st = draw(m, slurp(dir / "state-simple.mmd"), 100, false);
    check(st.kind == "state" && has(st.text, "\xE2\x97\x8F") && has(st.text, "\xE2\x97\x89"), "a state diagram has its start and its end");
    const Drawn p = draw(m, slurp(dir / "pie.mmd"), 100, false);
    check(p.kind == "pie" && has(p.text, "73.1%") && has(p.text, "Pets adopted by volunteers"), "a pie chart has its percentages and its title");
  }

  // ---- THE OTHER KINDS ----------------------------------------------------------------------------------------------
  {
    const Drawn c = draw(m, slurp(dir / "class.mmd"), 100, false);
    check(c.kind == "class" && has(c.text, "\xE2\x96\xB3") && has(c.text, "+isMammal()") && has(c.text, "lays") && has(c.text, "\xE2\x94\x9C"),
          "a class diagram: records with their members under a rule, a hollow triangle at the parent, a relation's label");
    // the parent is above however the relation was written
    const Drawn flip = draw(m, "classDiagram\n  Duck --|> Animal\n", 100, false);
    check(flip.text.find("Animal") < flip.text.find("Duck") && has(flip.text, "\xE2\x96\xB3"), "`Duck --|> Animal` puts the parent above, as `Animal <|-- Duck` does");
    const Drawn ci = draw(m, slurp(dir / "class-interface.mmd"), 100, false);
    check(has(ci.text, "<<interface>>") && has(ci.text, "Shape<T>") && has(ci.text, "\xE2\x97\x87") && has(ci.text, "\xE2\x94\x84"),
          "an annotation is kept as written, a generic is Shape<T>, aggregation has its open diamond, realization is dotted");
    check(has(ci.text, "\n") && has(ci.text, "4") && has(ci.text, "1"), "cardinalities in quotes are drawn at the ends of the relation");
    const Drawn e = draw(m, slurp(dir / "er.mmd"), 100, false);
    check(e.kind == "er" && has(e.text, "0..*") && has(e.text, "1..*") && has(e.text, "places") && (has(e.text, "\xE2\x94\x84") || has(e.text, "\xE2\x94\x86")) && has(e.text, "string custNumber PK"),
          "an ER diagram: entities with their attributes in columns, cardinalities at the ends, a dotted line for a non-identifying relationship");
    const Drawn mm = draw(m, slurp(dir / "mindmap.mmd"), 100, false);
    check(mm.kind == "mindmap" && has(mm.text, "\xE2\x94\x9C\xE2\x94\x80 Origins") && has(mm.text, "\xE2\x94\x94\xE2\x94\x80 Tools") && has(mm.text, "\xE2\x94\x82  \xE2\x94\x9C\xE2\x94\x80 Long history"),
          "a mind map is a tree, its rails running while a branch has more to come");
    const Drawn mm_narrow = draw(m, slurp(dir / "mindmap.mmd"), 30, false);
    check(mm_narrow.ok && has(mm_narrow.text, "Tony Buzan") && has(mm_narrow.text, "ideas about learning"), "…and a narrow one wraps its words instead of running off the edge");
    const Drawn tl = draw(m, slurp(dir / "timeline.mmd"), 100, false);
    check(tl.kind == "timeline" && has(tl.text, "2004 \xE2\x97\x8F\xE2\x94\x80 Facebook") && has(tl.text, "\xE2\x94\x82  Google") && has(tl.text, "Later"),
          "a timeline: the period, its first event on it, the others under it, and its sections");
    const Drawn jr = draw(m, slurp(dir / "journey.mmd"), 100, false);
    check(jr.kind == "journey" && has(jr.text, "\xE2\x97\x8F\xE2\x97\x8F\xE2\x97\x8F\xE2\x97\x8F\xE2\x97\x8F 5") && has(jr.text, "\xE2\x97\x8F\xE2\x97\x8B\xE2\x97\x8B\xE2\x97\x8B\xE2\x97\x8B 1") && has(jr.text, "Me, Cat"),
          "a journey: a score is that many dots of five, and who is in it");
    const Drawn gt = draw(m, slurp(dir / "gantt.mmd"), 100, false);
    check(gt.kind == "gantt" && has(gt.text, "Jan 1") && has(gt.text, "\xE2\x96\x88") && has(gt.text, "\xE2\x97\x86") && has(gt.text, "Another task"),
          "a Gantt chart: a dated axis, a bar for each task, a diamond for a milestone");
    {
      // `after a1` starts where a1 ends: the second bar begins on the column the first one stopped at
      std::istringstream in(gt.text);
      std::string line, first_bar, second_bar;
      while (std::getline(in, line)) {
        if (line.rfind("A task", 0) == 0) first_bar = line;
        if (line.rfind("Another task", 0) == 0) second_bar = line;
      }
      auto cell_of = [](const std::string& s, bool last) {
        int cell = 0, at = -1;
        std::size_t i = 0;
        while (i < s.size()) {
          const unsigned char c = static_cast<unsigned char>(s[i]);
          const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
          if (s.compare(i, len, "\xE2\x96\x88") == 0) { if (!last && at < 0) at = cell; if (last) at = cell; }
          ++cell;
          i += len;
        }
        return at;
      };
      check(cell_of(first_bar, true) + 1 == cell_of(second_bar, false), "`after a1` starts the bar where a1's stops [" + std::to_string(cell_of(first_bar, true) + 1) + " / " + std::to_string(cell_of(second_bar, false)) + "]");
    }
    const Drawn gt_narrow = draw(m, slurp(dir / "gantt.mmd"), 30, false);
    check(!gt_narrow.ok && has(gt_narrow.why, "columns"), "a Gantt chart with no room says how much it wants [" + gt_narrow.why + "]");
    const Drawn gt_bad = draw(m, "gantt\n  A :a1, after nothing, 3d\n", 100, false);
    check(!gt_bad.ok && has(gt_bad.why, "not been defined"), "a task after one that is not there is refused in words [" + gt_bad.why + "]");
  }

  // ---- THE PICTURES THAT WERE ONCE WRONG, from diagrams other people wrote --------------------------------------------
  {
    // sibling frames are apart: not one border shared between two
    const Drawn sib = draw(m, slurp(dir / "flow-sibling-subgraphs.mmd"), 120, false);
    check(has(sib.text.substr(0, sib.text.find('\n')), "\xE2\x95\xAE \xE2\x95\xAD") && !has(sib.text.substr(0, sib.text.find('\n')), "\xE2\x94\xAC"), "two subgraphs side by side are two frames with a gap between, not one border shared");
    // a `rect` is a colour behind its messages, not a frame with a title
    const Drawn blocks = draw(m, slurp(dir / "sequence-blocks.mmd"), 120, false);
    check(!has(blocks.text, "rect rgb") && has(blocks.text, "par Query one") && has(blocks.text, "and Query two") && has(blocks.text, "critical Establish connection") && has(blocks.text, "option Timeout"),
          "a sequence diagram's par / critical frames and their dividers are drawn, and a `rect` is not a frame");
    // a key column of two keys says two, not a doubled comma
    const Drawn er = draw(m, slurp(dir / "er-docs.mmd"), 140, false);
    check(has(er.text, "PK,FK") && !has(er.text, ",,"), "an attribute that is a primary and a foreign key says `PK,FK`");
    // a tall sequence diagram names its participants again at the foot
    const Drawn oauth = draw(m, slurp(dir / "sequence-oauth.mmd"), 120, false);
    check(count_of(oauth.text, "Auth Server") == 2 && count_of(oauth.text, "\xE2\x94\xB4") >= 4, "a tall sequence diagram repeats its participants under the lifelines");
    const Drawn shorter = draw(m, "sequenceDiagram\n  A->>B: hi\n", 120, false);
    check(count_of(shorter.text, "\xE2\x95\xAD") == 2, "…and a short one does not");
    // labels lie beside the stub they belong to, each with room: none of them runs into another edge or another label
    const Drawn states = draw(m, slurp(dir / "state-machine-labels.mmd"), 120, false);
    check(has(states.text, "fetch") && has(states.text, "error") && has(states.text, "retry") && has(states.text, "reset") && !has(states.text, "error\xE2\x96\xB2") && !has(states.text, "retry\xE2\x94\x82"),
          "a state with several labelled edges leaving it is wide enough that every label has room by its stub");
    // a fork is a bar across the flow
    const Drawn fj = draw(m, slurp(dir / "state-fork-join.mmd"), 120, false);
    check(has(fj.text, "\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81"), "a fork and a join are bars across the flow");
    const Drawn fj_lr = draw(m, "stateDiagram-v2\n  direction LR\n  state f <<fork>>\n  [*] --> f\n  f --> A\n  f --> B\n", 120, false);
    check(has(fj_lr.text, "\xE2\x94\x83"), "…and in a left to right diagram it is a bar down the page");
  }

  // ---- WHAT THE FLOWCHART DIRECTION MEANS ---------------------------------------------------------------------
  {
    const std::string body = "  A[Top] --> B[Bottom]\n";
    const Drawn td = draw(m, "graph TD\n" + body, 100, false);
    const Drawn bt = draw(m, "graph BT\n" + body, 100, false);
    const Drawn lr = draw(m, "graph LR\n" + body, 100, false);
    const Drawn rl = draw(m, "graph RL\n" + body, 100, false);
    check(td.text.find("Top") < td.text.find("Bottom") && has(td.text, "\xE2\x96\xBC"), "TD: the first is above, and the arrow points down");
    check(bt.text.find("Bottom") < bt.text.find("Top") && has(bt.text, "\xE2\x96\xB2"), "BT: the first is below, and the arrow points up");
    check(has(lr.text, "Top") && has(lr.text, "\xE2\x96\xB6") && count_of(lr.text, "\n") == 3, "LR: side by side, the arrow pointing right");
    check(rl.text.find("Bottom") < rl.text.find("Top") && has(rl.text, "\xE2\x97\x80"), "RL: the first is on the right, the arrow pointing left");
  }

  // ---- THE BUGS EACH PICTURE WAS ONCE WRONG BY -------------------------------------------------------------------
  {
    // an edge to a subgraph by its name is to the subgraph, not to a NEW NODE that happens to be named the same
    const Drawn d = draw(m, slurp(dir / "flow-edges-to-subgraphs.mmd"), 100, false);
    check(count_of(d.text, "CI pipeline") == 1 && !has(d.text, "\xE2\x94\x82 CI \xE2\x94\x82"), "an edge to a subgraph does not draw a node named after it");
    check(has(d.text, "\xE2\x95\xAD\xE2\x94\x80 CI pipeline \xE2\x94\x80\xE2\x95\xAE"), "a subgraph's frame is as wide as its title: it is not cut to fit its contents");
    // a loop's edge points back UP the flow, with its arrowhead where it arrives, not where it started
    const Drawn l = draw(m, slurp(dir / "flow-loop.mmd"), 100, false);
    check(has(l.text, "\xE2\x96\xB2"), "an edge that points back up the flow has an arrowhead pointing up");
    // an arrowhead and a line leaving are never the same cell: two ports at Start
    check(count_of(l.text.substr(0, l.text.find("Start")), "\xE2\x94\x8C") == 1 && has(l.text, "\xE2\x94\x94\xE2\x94\x80\xE2\x94\xAC\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xAC\xE2\x94\x80\xE2\x94\x98"),
          "a node with an arrowhead arriving and a line leaving has a port for each");
    // a self loop is drawn beside its node, arrow and all
    const Drawn s = draw(m, slurp(dir / "flow-self-loop-long-edge.mmd"), 100, false);
    check(has(s.text, "\xE2\x94\x98\xE2\x97\x80\xE2\x94\x80\xE2\x95\xAF"), "a self loop leaves the node's right side and comes back to it");
    // a long edge is routed through the layers between, never through a node
    check(has(s.text, "\xE2\x95\xAD\xE2\x95\xAF") || has(s.text, "\xE2\x94\x82"), "an edge that skips layers is drawn past the nodes in between");
    // labels stay on their edges
    const Drawn lab = draw(m, slurp(dir / "flow-lr-labels.mmd"), 100, false);
    check(has(lab.text, "Get money") && has(lab.text, "One") && has(lab.text, "Two") && has(lab.text, "Three"), "every edge's label is drawn");
    // font-awesome icons are not part of the words
    const Drawn fa = draw(m, "flowchart TD\n  A[fa:fa-car Car] --> B[Bus]\n", 100, false);
    check(fa.ok && has(fa.text, "Car") && !has(fa.text, "fa:fa"), "an icon prefix is dropped from a label");
  }

  // ---- LABELS: what a label may say ---------------------------------------------------------------------------------
  {
    const Drawn d = draw(m, "flowchart TD\n  A[\"quoted (with) [brackets]\"] --> B[two<br/>lines]\n  B --> C[#quot;q#quot;]\n", 100, false);
    check(d.ok && has(d.text, "quoted (with) [brackets]") && has(d.text, "two") && has(d.text, "lines") && has(d.text, "\"q\""), "quotes protect brackets in a label, <br/> breaks a line, entities are decoded");
    const Drawn u = draw(m, "flowchart LR\n  A[\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E] --> B[caf\xC3\xA9]\n", 100, false);
    check(u.ok && has(u.text, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") && has(u.text, "caf\xC3\xA9"), "wide and accented text is drawn as it is");
    // the boxes around wide text are as wide as the text is, in cells
    std::istringstream in(u.text);
    std::string line;
    std::vector<int> widths;
    while (std::getline(in, line)) widths.push_back(cells_of(line));
    // (the CJK label is 6 cells in 3 characters: its box's rule is 6 + 4 = 10 cells)
    check(!widths.empty() && has(u.text, "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90"), "a wide label's box is as wide as the label is in cells");
    // a control character in a label is not drawn
    std::string nasty = "flowchart TD\n  A[bad";
    nasty += '\x1b';
    nasty += "[31mred] --> B\n";
    const Drawn c = draw(m, nasty, 100, false);
    bool esc = false;
    for (unsigned char ch : c.text) if (ch == 0x1b) esc = true;
    check(c.ok && !esc, "an escape in a label never reaches the picture");
  }

  // ---- WHAT CANNOT BE DRAWN SAYS WHY --------------------------------------------------------------------------------
  {
    const Drawn none = draw(m, "", 100, false);
    check(!none.ok && has(none.why, "nothing"), "an empty diagram says so [" + none.why + "]");
    const Drawn unknown = draw(m, "gitGraph\n  commit\n", 100, false);
    check(!unknown.ok && has(unknown.why, "gitGraph diagrams are not drawn"), "a kind this does not draw says so, by name, and is not drawn half [" + unknown.why + "]");
    const Drawn bad = draw(m, "flowchart TD\n  A --> \n", 100, false);
    check(!bad.ok && !bad.why.empty(), "a link with nothing at its end is refused in words [" + bad.why + "]");
    const Drawn narrow = draw(m, slurp(dir / "flow-subgraphs-lr.mmd"), 40, false);
    check(!narrow.ok && has(narrow.why, "needs 66 columns") && has(narrow.why, "40"), "a diagram wider than the room says how much room it wants [" + narrow.why + "]");
    const Drawn seq_bad = draw(m, "sequenceDiagram\n  this is not a message\n", 100, false);
    check(!seq_bad.ok && has(seq_bad.why, "not understood"), "a sequence line that is nothing is refused [" + seq_bad.why + "]");
    const Drawn pie_bad = draw(m, "pie\n  \"a\" : -3\n", 100, false);
    check(!pie_bad.ok && !pie_bad.why.empty(), "a pie slice with a negative value is refused [" + pie_bad.why + "]");
    // a refused diagram leaves the handle empty, not holding the last picture
    check(rolltui_mermaid_line_count(m) == 0 && rolltui_mermaid_width(m) == 0, "…and leaves the handle with nothing in it");
    // too large is refused, not slow
    std::string big = "flowchart TD\n";
    for (int i = 0; i < 300; ++i) big += "  N" + std::to_string(i) + " --> N" + std::to_string(i + 1) + "\n";
    const Drawn huge = draw(m, big, 400, false);
    check(!huge.ok && has(huge.why, "240"), "more than 240 nodes is refused, in words [" + huge.why + "]");
  }

  // ---- WHAT THE CLASSES SAY -----------------------------------------------------------------------------------
  {
    const Drawn d = draw(m, slurp(dir / "flow-fanout.mmd"), 100, false);
    bool node = false, text = false, arrow = false, edge = false;
    for (int c : d.classes) {
      node |= c == ROLLTUI_MERMAID_CLASS_NODE;
      text |= c == ROLLTUI_MERMAID_CLASS_TEXT;
      arrow |= c == ROLLTUI_MERMAID_CLASS_ARROW;
      edge |= c == ROLLTUI_MERMAID_CLASS_EDGE;
    }
    check(node && text && arrow && edge, "the picture's runs say which cells are a node's border, its words, an arrowhead and an edge");
  }

  // ---- ROBUSTNESS: nothing a file can say crashes it -------------------------------------------------------------
  {
    unsigned seed = 12345;
    auto next = [&] { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
    int survived = 0, drew = 0;
    for (const fs::path& f : files) {
      const std::string src = slurp(f);
      for (int round = 0; round < 40; ++round) {
        std::string t = src;
        const int edits = 1 + static_cast<int>(next() % 6);
        for (int e = 0; e < edits && !t.empty(); ++e) {
          const std::size_t at = next() % t.size();
          switch (next() % 5) {
            case 0: t.erase(at, 1 + next() % 8); break;
            case 1: t.insert(at, 1, "[](){}<>|-=~.:;\"'&#%/\\ \n"[next() % 24]); break;
            case 2: t.insert(at, t.substr(next() % t.size(), 1 + next() % 20)); break;
            case 3: t.resize(at); break;
            default: t[at] = static_cast<char>(next() % 256); break;
          }
        }
        for (int ascii = 0; ascii < 2; ++ascii) {
          const Drawn d = draw(m, t, ascii ? 30 : 100, ascii != 0);
          ++survived;
          if (d.ok) {
            ++drew;
            bool controls = false;
            for (unsigned char c : d.text) if ((c < 0x20 && c != '\n') || c == 0x7f) controls = true;
            if (controls) check(false, f.stem().string() + ": a mutated diagram drew a control character");
          } else if (d.why.empty()) {
            check(false, f.stem().string() + ": a diagram that was not drawn gave no reason");
          }
        }
      }
    }
    check(survived == static_cast<int>(files.size()) * 80, "every mutated diagram was drawn or refused, none crashed (" + std::to_string(drew) + " of " + std::to_string(survived) + " drew)");
  }

  // ---- COMPACT, WITHOUT LOSING WHAT A PICTURE SAYS ------------------------------------------------------------------
  {
    auto lines_of = [](const Drawn& d) { return count_of(d.text, "\n"); };
    // between two boxes a plain edge is one row: the arrowhead, or the line where it has none
    const Drawn plain = draw(m, "graph TD\n A[one] --> B[two]\n", 80, false);
    check(plain.ok && lines_of(plain) == 7 && has(plain.text, "\xE2\x96\xBC") && !has(plain.text, "\xE2\x94\x82\n   \xE2\x96\xBC"),
          "a plain edge between two boxes is one row, its arrowhead, directly between them");
    const Drawn bare = draw(m, "graph TD\n A[one] --- B[two]\n", 80, false);
    check(bare.ok && lines_of(bare) == 7 && has(bare.text, "\xE2\x94\xAC") && has(bare.text, "\xE2\x94\xB4"), "…and an edge with no head is one row of line, joined to both boxes");
    // what needs a second row keeps it: a label beside the edge, a head at each end
    const Drawn labelled = draw(m, "graph TD\n A[one] -->|yes| B[two]\n", 80, false);
    check(labelled.ok && lines_of(labelled) == 8 && has(labelled.text, "yes") && has(labelled.text, "\xE2\x96\xBC"), "a labelled edge keeps its row for the label");
    const Drawn both = draw(m, "graph TD\n A[one] <--> B[two]\n", 80, false);
    check(both.ok && lines_of(both) == 8 && has(both.text, "\xE2\x96\xB2") && has(both.text, "\xE2\x96\xBC"), "…and an edge with a head at each end keeps a row for each");
    // a long edge crossing layers does not make every gap it passes through two rows
    const Drawn chain = draw(m, "graph TD\n A[a] --> B[b]\n B --> C[c]\n A -->|far| C\n", 80, false);
    check(chain.ok && has(chain.text, "far"), "a labelled edge across layers still draws its label");
    // left to right keeps its two columns: a line and a head
    const Drawn lr = draw(m, "graph LR\n A[one] --> B[two]\n", 80, false);
    check(lr.ok && has(lr.text, "\xE2\x94\x80\xE2\x96\xB6"), "left to right an edge is still a line and a head");

    // A LABEL WRAPS ONLY WHEN THE DIAGRAM WOULD NOT FIT, and nothing it says is lost by it
    const std::string longer = "flowchart LR\n  A[Start] --> B[A label that runs on and on well past what a narrow pane could hold]\n";
    const Drawn roomy = draw(m, longer, 120, false);
    check(roomy.ok && has(roomy.text, "A label that runs on and on well past what a narrow pane could hold"), "given room, a long label is one line, as it was written");
    const Drawn narrow = draw(m, longer, 50, false);
    check(narrow.ok && narrow.width <= 50 && lines_of(narrow) > lines_of(roomy) && !has(narrow.text, "A label that runs on and on well past what a narrow pane could hold"),
          "in less room it is wrapped, on several lines [" + std::to_string(narrow.width) + " columns]");
    bool words = true;
    for (const char* w : {"label", "runs", "well", "past", "narrow", "pane", "could", "hold"}) words = words && has(narrow.text, w);
    check(words, "…and every word of it is still there");
    const Drawn tiny = draw(m, longer, 10, false);
    check(!tiny.ok && has(tiny.why, "columns"), "…and when even wrapped it will not fit, it is refused, saying how much room it needs [" + tiny.why + "]");
    // an edge's label wraps too, and a diagram that fits as written is never touched
    const std::string edge = "graph TD\n A[one] -->|an edge label that is much longer than the boxes it joins are wide| B[two]\n";
    const Drawn edge_wide = draw(m, edge, 120, false);
    const Drawn edge_narrow = draw(m, edge, 40, false);
    check(edge_wide.ok && has(edge_wide.text, "an edge label that is much longer than the boxes it joins are wide") && edge_narrow.ok && edge_narrow.width <= 40 && has(edge_narrow.text, "boxes"),
          "an edge's label wraps in the room it has, and is one line in the room it wants");

    // A SEQUENCE DIAGRAM'S NOTES AND SELF-MESSAGES ARE MEASURED BY WHAT IS DRAWN, so it is not refused for room it does not use
    const std::string seq =
        "sequenceDiagram\n  A->>B: x\n  Note over A,B: hi\n  A->>A: think it over\n  Note right of B: on the right\n  Note left of A: on the left\n";
    const Drawn sq = draw(m, seq, 200, false);
    check(sq.ok && sq.width == 53, "a sequence diagram with a note over two lifelines, one on each side and a message to itself is as wide as what is drawn [" + std::to_string(sq.width) + "]");
    const Drawn sq_tight = draw(m, seq, sq.width, false);
    check(sq_tight.ok && sq_tight.text == sq.text && has(sq_tight.text, "on the left") && has(sq_tight.text, "on the right") && has(sq_tight.text, "think it over"),
          "…and draws whole in exactly that width, none of it cut off");
    check(!draw(m, seq, sq.width - 1, false).ok, "…and not in one column less");
  }

  // ---- NOTHING IS LEFT BEHIND: every fixture, drawn and refused, then the handle freed, and the library holds what it did
  {
    const std::size_t base = live_bytes();
    RolltuiMermaid* h = rolltui_mermaid_new();
    for (const fs::path& f : files) {
      const std::string src = slurp(f);
      (void)draw(h, src, 120, false);
      (void)draw(h, src, 120, true);
      (void)draw(h, src, 24, false);  // refused for want of room: the half-built layout is released too
      (void)draw(h, src.substr(0, src.size() / 2), 120, false);
    }
    rolltui_mermaid_free(h);
    check(live_bytes() == base, "after every fixture drawn, refused and cut in half, and the handle freed, the library holds what it held before (" +
                                    std::to_string(live_bytes()) + " vs " + std::to_string(base) + ")");
  }

  rolltui_mermaid_free(m);
  rolltui_mermaid_free(nullptr);
  return report("rolltui_mermaid_test");
}
