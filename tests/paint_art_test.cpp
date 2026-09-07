//
// paint_art_test.cpp — what `rolltui-paint` can and cannot do .
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

// One scene: water in two block shades, a shoreline, two hills and a sun. Every tool change is
// a flag and the order is the order they were written, which is the whole of paint's script.
const char* kScene =
    " --ramp blocks --ink '#2c4a70' --level 3 --size 1 --stroke 1,13-44,13 --stroke 1,14-44,14"
    " --level 6 --stroke 1,15-44,15"
    " --ink '#6f8fae' --level 9 --size 2 --stroke 4,11-40,11"
    " --ramp ascii --ink '#e8d8a0' --level 9 --size 3 --dot 34,4"
    " --ink '#7aa86a' --level 8 --size 2 --stroke 10,10-15,5 --stroke 15,5-20,10";

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
  // The tool panel is the app's menu FILE, and its typed fields render as fields.
  check(has(art, "Level:") && has(art, "Ink:") && has(art, "Brush size:"),
        "the palette's TYPED fields (int with a range, colour) draw as fields — nothing in the tree drove them from a host before");

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
  const std::string blocks = base + " --ramp blocks --ink '#6f8fae' --level 9 --size 1 --stroke 2,3-20,3";
  const std::string narrow = run(blocks + " 2>&1", rc);
  check(rc == 0 && has(narrow, "\xE2\x96\x88"), "on an ordinary terminal the block ramp draws blocks");
  const std::string wide = run(blocks + " --ambiguous-wide 2>&1", rc);
  check(rc == 0 && !has(wide, "\xE2\x96\x88"),
        "on a WIDE-AMBIGUOUS terminal the library refuses to cut a two-cell block into one cell");
  check(has(wide, "@"),
        "…and paint falls back to the ascii step of the same darkness, which it learns from the CELLS RETURNED by put_text");

  return report("rolltui paint_art_test");
}
