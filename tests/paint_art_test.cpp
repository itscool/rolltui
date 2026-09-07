//
// paint_art_test.cpp — what `rolltui-paint` can and cannot do.
//
// PAINT IS THE ADVERSARIAL PROBE, and this suite is written to that: a canvas of arbitrary
// coloured cells uses no theme role, marks no state and fits none of the library's document /
// list / field vocabulary, so what it hits are LIMITS to report rather than requirements to
// meet. The explorer is the aligned probe and its suite is `explorer_test.cpp`.
//
// The three things asserted here are the three that answer a question:
//   1. the ramps make a real picture, and the picture is one the library DREW rather than a
//      claim about it (a scene at a fixed size, from an ordered tool script);
//   2. a hand-picked RGB DOWN-CONVERTS at every depth, because paint is the first consumer in
//      the tree whose colour is USER DATA rather than the theme's expression of a state — and
//      at `mono` the intensity ramp is what carries the picture, which is the symmetry that
//      makes shipping both ramps a design rather than a feature;
//   3. the block ramp is East Asian AMBIGUOUS, so on a wide-ambiguous terminal the library
//      REFUSES to cut a two-cell glyph into one cell and lays down nothing. That refusal is
//      correct and it is reported the only way a draw call can — the returned cell count — and
//      paint falls back to the ascii step of the same darkness.
//
#include <cstdio>
#include <cstdlib>
#include <string>

#include "rolltui_test.hpp"

using namespace rolltui_test;

#ifndef ROLLTUI_PAINT_BIN
#error "ROLLTUI_PAINT_BIN must name the paint binary"
#endif

namespace {

std::string run(const std::string& cmd, int& rc) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) { rc = -1; return out; }
  char buf[4096];
  while (std::size_t n = std::fread(buf, 1, sizeof buf, p)) out.append(buf, n);
  rc = pclose(p);
  return out;
}
bool has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }
int count(const std::string& h, const std::string& n) {
  int c = 0;
  for (std::size_t i = h.find(n); i != std::string::npos; i = h.find(n, i + n.size())) ++c;
  return c;
}

// One scene: water in three block shades, a shoreline, two hills and a sun. Every tool change
// is a flag and the order is the order they were written, which is the whole of paint's script.
//
// NOTHING HERE SETS A DARKNESS, because there is no longer a way to. Every shade in the picture
// is a number of passes: the sea is built up in overlapping bands with a two-cell brush, the
// hills are drawn twice, and the sun is seven clicks in one place with an eighth in its centre.
// That is what the script looked like before, per shade, as a `--level` flag — the difference
// is that a person painting now gets the same result from the same gesture.
const char* kScene =
    " --ramp blocks --ink '#2c4a70' --size 2"
    " --stroke 1,12-44,12 --stroke 1,13-44,13"
    " --stroke 1,14-44,14 --stroke 1,14-44,14 --stroke 1,15-44,15 --stroke 1,15-44,15"
    " --ink '#6f8fae' --size 1 --stroke 4,11-40,11"
    " --ramp ascii --ink '#e8d8a0' --size 3"
    " --dot 34,4 --dot 34,4 --dot 34,4 --dot 34,4 --dot 34,4 --dot 34,4 --dot 34,4 --size 1 --dot 34,4"
    " --ink '#7aa86a' --size 2 --stroke 10,10-15,5 --stroke 15,5-20,10"
    " --stroke 10,10-15,5 --stroke 15,5-20,10";

}  // namespace

int main() {
  const std::string bin = std::string("'") + ROLLTUI_PAINT_BIN + "'";
  const std::string base = bin + " --frame 64x20 --theme default-dark";
  int rc = 0;

  // ---- 1. it draws a picture, in BOTH ramps at once -----------------------------------------
  const std::string art = run(base + kScene + " 2>&1", rc);
  check(rc == 0 && !art.empty(), "paint draws a scene from an ordered tool script (rc " + std::to_string(rc) + ")");
  check(has(art, "\xE2\x96\x92") && has(art, "\xE2\x96\x93"),
        "…the block ramp lays down two different shades, so intensity is visible and not just on/off");
  check(has(art, "@") && has(art, "%"),
        "…and the ascii ramp's darkest steps are in the SAME picture: a cell carries its own ramp");
  check(count(art, "\xE2\x96\x92") > 30, "…the water is a body rather than a line (" +
                                             std::to_string(count(art, "\xE2\x96\x92")) + " cells)");
  check(has(art, "\xE2\x96\x93"), "…and a third shade, from a band the brush crossed four times");
  // The tool panel is the app's menu FILE, and its typed fields render as fields.
  check(has(art, "Ink:") && has(art, "Size:"),
        "the palette's TYPED fields (int with a range, colour) draw as fields — nothing in the tree drove them from a host before");
  // EVERY TOOL SHOWS WHAT IT IS SET TO. A choice whose value the file never declared drew as a
  // name and an arrow, so the palette could not answer the first question anyone asks it.
  check(has(art, "ascii") && has(art, "square"),
        "…and every choice shows its CURRENT answer, not just that it has options");
  // AND THE FIELD THAT IS GONE. A darkness you set before you can make a mark is a number
  // standing between a person and the picture; drawing over the same place is what a person
  // already does when they want it darker. The palette is one field shorter for it.
  check(!has(art, "Level:"), "…and the palette no longer asks for a shading LEVEL: darkness is a consequence of drawing");

  // ---- 2. a hand-picked RGB down-converts at every depth ------------------------------------
  const std::string tc = run(base + kScene + " --present truecolor 2>&1", rc);
  check(rc == 0 && has(tc, "38;2;111;143;174"), "at truecolor the exact RGB a person chose is emitted");
  const std::string c256 = run(base + kScene + " --present 256 2>&1", rc);
  check(rc == 0 && has(c256, "38;5;") && !has(c256, "38;2;"),
        "at 256 it is down-converted to an indexed colour, by the renderer and not by the app");
  const std::string c16 = run(base + kScene + " --present 16 2>&1", rc);
  check(rc == 0 && !has(c16, "38;5;") && !has(c16, "38;2;"), "at 16 it is down-converted again");
  const std::string mono = run(base + kScene + " --present mono 2>&1", rc);
  check(rc == 0 && !has(mono, "38;2;") && !has(mono, "38;5;"), "at mono no colour is emitted at all");
  // AND THE SYMMETRY THAT MAKES THE RAMP A DESIGN: with every colour gone, the picture is still
  // there, because density carries what colour cannot.
  check(has(mono, "\xE2\x96\x92") && has(mono, "@"),
        "…and the picture SURVIVES mono, because the intensity ramp is what carries it");

  // ---- 3. the ambiguous-width probe ----------------------------------------------------------
  const std::string blocks = base + " --ramp blocks --ink '#6f8fae' --size 1" +
                             " --stroke 2,3-20,3 --stroke 2,3-20,3 --stroke 2,3-20,3" +
                             " --stroke 2,3-20,3 --stroke 2,3-20,3 --stroke 2,3-20,3";
  const std::string narrow = run(blocks + " 2>&1", rc);
  check(rc == 0 && has(narrow, "\xE2\x96\x88"), "on an ordinary terminal the block ramp draws blocks");
  const std::string wide = run(blocks + " --ambiguous-wide 2>&1", rc);
  check(rc == 0 && !has(wide, "\xE2\x96\x88"),
        "on a WIDE-AMBIGUOUS terminal the library refuses to cut a two-cell block into one cell");
  check(has(wide, "#"),
        "…and paint falls back to the ascii step of the same darkness, which it learns from the CELLS RETURNED by put_text");

  // ---- 4. THE STROKE: what a still frame cannot show ---------------------------------------
  // Two properties, and neither is visible in a golden frame — which is why they were both
  // wrong while every frame in this suite was green. A canvas with no stroke state paints on
  // any drag, including one whose press it never saw; and a canvas that paints one footprint
  // per event draws a dotted line, because a terminal reports motion once per cell at best and
  // drops reports outright under speed.
  //
  // `--stroke` presses, drags ONCE to the far end and releases. `--drag` sends the same two
  // drags with no press. So the line below is the widget's own interpolation or it is absent,
  // and the difference between the two flags is the whole of the stroke.
  // `marks N` in the status line is the app's own count of painted cells, which is the property
  // itself rather than a glyph that could also come from the palette's text.
  const std::string pen = base + " --ramp ascii --ink '#d8dce2' --size 1";
  const std::string empty = run(pen + " 2>&1", rc);
  check(rc == 0 && has(empty, "marks 0"), "an untouched sheet is empty");

  const std::string dragged = run(pen + " --drag 2,2-30,2 2>&1", rc);
  check(rc == 0 && dragged == empty,
        "a DRAG WITH NO PRESS paints nothing — the frame is byte-identical to the untouched sheet");

  const std::string line = run(pen + " --stroke 2,2-30,2 2>&1", rc);
  check(rc == 0 && has(line, std::string(29, ':')) && has(line, "marks 29"),
        "…while a press-drag-release across 29 cells leaves a CONTINUOUS line, from two reported points");

  const std::string diag = run(pen + " --stroke 1,1-16,9 2>&1", rc);
  check(rc == 0 && has(diag, "marks 16"),
        "…and a diagonal marks every column it crosses, one cell per column and no gap");

  // A RELEASE REALLY CLOSES IT. Without this the flag would only prove that the FIRST drag of a
  // run is refused, not that a stroke ever ends: a leaked down flag paints the second row too.
  const std::string after = run(pen + " --stroke 2,2-30,2 --drag 2,5-30,5 2>&1", rc);
  check(rc == 0 && after == line, "a release ENDS the stroke: drags after it paint nothing");

  // ---- 5. SHADING IS A CONSEQUENCE OF DRAWING ----------------------------------------------
  // The step is per distinct cell ENTRY, never per event, and that distinction is the whole of
  // "not too sensitive": a slow hand reports one cell a dozen times and a fast one reports it
  // once, and both must leave the same picture. A stroke that crosses itself is the one gesture
  // that shows the difference in a still frame.
  const std::string once = run(pen + " --stroke 2,2-30,2 2>&1", rc);
  check(rc == 0 && has(once, std::string(29, ':')), "one pass over a line is the ramp's LIGHT step");
  const std::string twice = run(pen + " --stroke 2,2-30,2 --stroke 30,2-2,2 2>&1", rc);
  check(rc == 0 && has(twice, std::string(29, '-')) && has(twice, "marks 29"),
        "…a second pass over the SAME cells deepens each one step, and adds no new mark");

  // A CROSS, drawn as one horizontal and one vertical stroke: every cell is one pass except the
  // one they share, which is two. That single darker cell is the gradient, in a golden frame.
  const std::string cross = run(pen + " --stroke 2,4-30,4 --stroke 16,1-16,9 2>&1", rc);
  check(rc == 0 && has(cross, ":::-:::") && count(cross, "-") == 1,
        "a stroke crossing another leaves exactly one deeper cell where they meet");

  // AND THE SENSITIVITY, WHICH IS WHERE A WIDE BRUSH SHOWS IT. A three-cell brush covers every
  // cell of its line three times as it passes over. Deepening on each of those is deepening per
  // EVENT, and it draws a line with a dark core and pale edges — a picture of how the brush
  // moved rather than of where it went. A cell still under the brush from the last stamp is not
  // entered again, so the line is one darkness across.
  const std::string fat = run(pen + " --size 3 --stroke 2,4-30,4 2>&1", rc);
  check(rc == 0 && has(fat, "marks 93") && count(fat, "-") == 0 && count(fat, "=") == 0,
        "a WIDE brush lays a line of ONE darkness: a cell the brush has not left is not re-entered");
  check(rc == 0 && has(fat, std::string(31, ':')), "…and that one darkness is the light first pass, across its full width");

  // A NEW PRESS IS ALWAYS A NEW ENTRY, including on ink the last stroke just laid. That is the
  // only way to deepen, and it is deliberate: lifting and pressing again is the gesture.
  const std::string slow = run(pen + " --stroke 2,2-8,2 --stroke 8,2-16,2 --stroke 16,2-30,2 2>&1", rc);
  check(rc == 0 && count(slow, "-") == 2,
        "a stroke reported in three pieces deepens only where a press LANDS on painted ink (" +
            std::to_string(count(slow, "-")) + " cells)");

  // ---- 6. THE WAY OUT OF A SUBMENU, WITH THE MOUSE --------------------------------------
  // The palette is a menu FILE and it declares no way back, because no file does: the widget
  // puts one on every level below the root. A person who has descended into a choice and does
  // not know the chord is exactly the person this is for, so the assertion clicks.
  const std::string into = run(base + " --dot 46,2 2>&1", rc);
  check(rc == 0 && has(into, "tools \xE2\x80\xBA Texture") && has(into, "\xE2\x97\x82 Back"),
        "clicking a choice descends, and the first row is a visible way back");
  const std::string out = run(base + " --dot 46,2 --dot 46,2 2>&1", rc);
  check(rc == 0 && has(out, "Size:") && !has(out, "\xE2\x97\x82 Back"),
        "…and clicking that row returns to the top level, where there is nowhere to go back to");

  return report("rolltui paint_art_test");
}
