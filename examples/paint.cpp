//
// rolltui/tools/paint.cpp — `rolltui-paint`, the library's THIRD host (the plan,
// milestone 6) and the phase's proof that a rolltui app need not be chat-shaped.
//
// It is a text-mode painting app: a canvas you drag on, a tool palette that is a menu
// FILE, and NO TRANSCRIPT AND NO INPUT. That absence is the point. roll and the studio
// are both a document with a prompt under it, so every property the library grew could
// quietly have been a property of that shape; this host has neither, and it is one
// screen's worth of code:
//
//   - ONE STRUCT, `Canvas`, which is a real widget PLUGIN with data of its own (its
//     pixels) — not a rows/text binding, deliberately, because the mechanism must not
//     depend on the shortcut the library happens to ship for stateless composites.
//   - ONE REGISTRATION, `register_canvas_kind()`, after which `canvas:sheet` behaves
//     exactly like `input:prompt`: created on demand, owned by `RolltuiWindows`, keyed
//     by content, and handed every event the stack routes to its window — INCLUDING the
// drags that leave the window, because a press captures the pointer.
//   - NOTHING ELSE. There is no name switch, no `custom_at`, and no line anywhere below
//     that knows what the studio is. The screen this app runs in the proof was authored
//     in the studio, by a person who never had to be told what this app can build — and the
//     word for that screen appears in no source file, which `files_only_test`'s grep asserts.
//
// THIS APP PUBLISHES NOTHING ABOUT ITSELF, and that is the design rather than an omission.
// A file saying what a layout may name inside this app is the app bounding the design. The
// direction is one-way: a designer names what a screen needs, and the app REPORTS what it
// cannot yet provide, at end of init (`rolltui_gaps_collect`).
//
// `--frame WxH` prints one frame and exits (the studio's convention, and what the tests
// read); `--stroke x,y-x,y` synthesises a press, ONE drag to the far end and a release, and
// `--drag x,y-x,y` synthesises the same drags with no press at all — so a test can prove both
// what the canvas draws from a sparse stroke and what it refuses to draw without one, neither
// of which a still frame can show. Everything else is the ordinary interactive loop.
//
// ============================================================================================
// THIS FILE CALLS THE C, and the shape it uses is the one every other host copies rather
// than each inventing. Three decisions, each forced by a measurement rather than chosen:
//
//   1. **NO PER-FRAME FRAME AT ALL.** `rolltui_swap` owns both frames for the whole run and
//      lends the back one per repaint. `Frame prev; bool have_prev;` — which all three hosts
//      had written identically — is gone, and with it the ONE hot RAII site a host had. A
//      frame type with no borrowing constructor cannot be swapped in underneath: adopting
//      the double buffer IS the draw path's transition, not a bolt-on to it.
//   2. **APP-LIFETIME HANDLES ARE PLAIN MEMBERS RELEASED IN ONE DESTRUCTOR.** Not a
//      `Handle<T, New, Free>` template — that is the wrapper `rolltui.h` rule 5 forbids
//      three hosts from each writing. `App` owning its own resources is ordinary C++ and is
//      ONE place; a missed release leaks once and `rolltui_shutdown`'s `live_bytes == 0` is
//      what catches it. App lifetime is the easy 90% and it needs no machinery.
//   3. **NO JSON TYPE ANYWHERE.** A host that only wants to WRITE a file should never end up
//      depending on the parser, which is what happens the moment a library hands something
//      back as a tree instead of as text.
//
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

#include "rolltui/rolltui.h"

// ADDITIVE, NOT SUBTRACTIVE: the product cannot drive itself. The shared script vocabulary is the
// same one the studio and the explorer use, so one spelling drives all three.
#ifdef ROLLTUI_SELFTEST
#include "rolltui/selftest/script.hpp"
#endif
#include "tool_str.hpp"

namespace {

// The tool palette. A MENU FILE the app carries in its own binary — the middle of Phase
// the middle of the three menu rungs — so a user can shadow it with menus/tools.json. A
// DESIGNER working on a screen for this app sees `menu:tools` as a labelled placeholder, the
// same honest answer it already gives for a foreign widget kind: the tool is not this app and
// cannot build one.
//
// Every tool a person picks comes out of this file — the ramp, the ink, the brush size and its
// shape. Two of them are the menu's TYPED input fields (`"kind": "input"`, `"type": "int"` with
// a range and `"type": "color"`), driven from a HOST rather than from an editor, which is what
// this example is for.
constexpr const char* kToolsMenu = R"({
  "id": "root", "label": "tools", "items": [
    { "id": "ramp", "label": "Texture", "kind": "choice", "value": "ascii",
      "items": [ { "id": "ascii", "label": "ascii   .:-=+*#%@" },
                 { "id": "blocks", "label": "blocks  \u2591\u2592\u2593\u2588" } ] },
    { "id": "ink", "label": "Ink", "kind": "input", "type": "color",
      "value": "#d8dce2", "hint": "#rrggbb, 0-255 or none" },
    { "id": "size", "label": "Size", "kind": "input", "type": "int",
      "min": 1, "max": 5, "step": 1, "value": "1", "hint": "cells across" },
    { "id": "shape", "label": "Shape", "kind": "choice", "value": "square",
      "items": [ { "id": "square", "label": "square" }, { "id": "round", "label": "round" } ] },
    { "id": "open", "label": "Open a picture", "kind": "input", "type": "text", "value": "" },
    { "id": "clear", "label": "Clear the sheet" } ] }
)";

// THE TWO RAMPS, and the second is a deliberate Unicode probe. CLAUDE.md records that U+2588
// FULL BLOCK is East Asian AMBIGUOUS and overflowed a one-cell column on a wide-ambiguous
// terminal. A painting app whose best tool is a block ramp should meet that
// rather than avoid it, so `--ambiguous-wide` is a real mode here and a golden frame runs in it.
struct Ramp {
  const char* name;
  const char* cells[10];  // lightest to darkest; a cell is one grapheme
  int steps;
};
constexpr Ramp kRamps[] = {
    {"ascii", {" ", ".", ":", "-", "=", "+", "*", "#", "%", "@"}, 10},
    {"blocks", {" ", "\xE2\x96\x91", "\xE2\x96\x91", "\xE2\x96\x92", "\xE2\x96\x92",
                "\xE2\x96\x93", "\xE2\x96\x93", "\xE2\x96\x88", "\xE2\x96\x88", "\xE2\x96\x88"}, 10},
};

// WALL 7, AND IT WAS FIXED AT THE API RATHER THAN HERE (phase file). The menu offers
// `"type": "color"` and a committed input hands the host TEXT, and `rolltui_color_parse` — the
// library's own, which the theme loader has always used — was INTERNAL. A public input type
// whose value has no public parser is a contradiction in the surface, so the parser and its
// `to_string` pair are public now and this app calls them. The five-line hex parser that stood
// here for one commit is deleted: `rolltui.h` rule 5 says fix the API, never ship the wrapper.

// What a stroke lays down. THE CELL CARRIES GLYPH AND COLOUR TOGETHER — see the log: a
// `RolltuiCell` is the library's own and is not a host's to build, so this is the parallel
// structure the phase asked me to be plain about.
struct Ink {
  int ramp = 0;   // WHICH ramp, per cell: a picture mixes them, so the cell has to carry it.
  int level = 0;  // Storing only the level made the last ramp chosen repaint the whole sheet.
  RolltuiStyleColor color = RolltuiStyleColor::rgb(0xd8, 0xdc, 0xe2);
};

// HOW DARK A CELL GETS IS A CONSEQUENCE OF DRAWING, NOT A FIELD SOMEBODY SETS. A level in the
// palette is a number you have to think about before you can make a mark; drawing over the same
// place is what a person already does when they want it darker, in every medium there is.
//
// The step is per distinct cell ENTRY and never per event, which is the whole of "not too
// sensitive": a slow drag reports the same cell many times and would max it out instantly,
// while a fast one reports it once. Entry is a property of the picture, so both hands paint
// the same. See `canvas_stamp`.
constexpr int kFirstLevel = 2;  // one pass is visible, and light
constexpr int kMaxLevel = 9;    // the ramp's darkest step

struct Tool {
  int ambiguous = 0;  // the app's --ambiguous-wide, lent to the canvas (wall 8)
  int ramp = 0;
  RolltuiStyleColor color = RolltuiStyleColor::rgb(0xd8, 0xdc, 0xe2);
  int size = 1;
  // ROUND OR SQUARE, and it is here for the second reason an example's feature can be here:
  // it probes NOTHING about the API — no public function, no wall, no header growth — and it
  // makes the app better to use and to read. Those are two independent tests (see
  // the plan), and passing either is enough for pure app-side code. What is never
  // allowed is app-side polish that grows the PUBLIC surface.
  bool round = false;
};

// The app's own screen, for a run with no --layout: one canvas and the palette beside it.
// A file, in the sense that matters — it is parsed by the same loader as any other, so this
// app's minimum size is stated once, here, rather than in a second place that could drift.
constexpr const char* kDefaultLayout = R"({
  "name": "paint", "min_width": 20, "min_height": 6, "focus": "sheet",
  "actions": { "app.theme": "Edit the theme", "app.keys": "Edit the keys" },
  "popups": [
    { "id": "theme", "x": "100%", "y": 0, "w": "50%", "h": "100%", "anchor": "top-right",
      "min_w": 34, "modal": true,
      "root": { "id": "theme", "content": "theme", "border": "rounded", "title": "theme",
                "focusable": true, "background": "panel_background" } },
    { "id": "keys", "x": "100%", "y": 0, "w": "50%", "h": "100%", "anchor": "top-right",
      "min_w": 34, "modal": true,
      "root": { "id": "keys", "content": "keys", "border": "rounded", "title": "keys",
                "focusable": true, "background": "panel_background" } } ],
  "root": { "row": [
    { "id": "sheet", "content": "canvas:sheet", "border": "single", "title": "sheet", "focusable": true },
    { "id": "tools", "content": "menu:tools", "size": 22, "border": "single", "title": "tools", "focusable": true } ] }
})";

constexpr const char* kCanvasKind = "canvas";
constexpr const char* kCanvasSource = "sheet";
constexpr const char* kCanvasDescribes = "a sheet the app paints on";

// ---- the one widget ----------------------------------------------------------------------
// A plugin with DATA of its own, which is the case the vtable exists for and the case a
// two-callback adapter cannot serve: the pixels belong to the canvas, not to a document
// the host bound. It keeps them in its own coordinates, so a resize or a layout reload
// moves the viewport and not the picture.
//
// Three of the nine slots are filled: `destroy`, `layout` and `draw` are required, plus
// `problem` (a source this app does not have is a NAMED problem and an error panel, exactly
// as an unbound `rows:` source is — a host's own kind is not exempt) and `handle`. The other
// four are NULL, and `rolltui_widgets.h` states what each NULL means; that is the whole
// difference from a virtual nobody was asked about.
// Defined below, beside the other file helpers; declared here because the PNG loader is the
// first thing that needs it.
std::string read_file(const std::string& path, bool& ok);

// ---- a PNG, as ASCII ------------------------------------------------------------------------
// PNG is a documented format and zlib is a system library, so this needs nothing vendored and
// answers no licence question. Deliberately NARROW: 8-bit non-interlaced truecolour, with or
// without alpha, which is what almost every screenshot and export is. Anything else is REFUSED
// BY NAME rather than half-decoded — a picture that comes out wrong is worse than one that does
// not come out, and "unsupported" is a sentence a person can act on.
struct Image {
  int w = 0, h = 0;
  std::vector<unsigned char> rgb;  // GROWING HEAP, one per load: w*h*3, freed with the vector
};

std::string load_png(const std::string& path, Image& out) {
  bool ok = false;
  const std::string bytes = read_file(path, ok);
  if (!ok) return "cannot open " + path;
  if (bytes.size() < 8 || std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) != 0) return "not a PNG: " + path;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.data());
  const std::size_t n = bytes.size();
  auto be32 = [](const unsigned char* q) {
    return (static_cast<unsigned long>(q[0]) << 24) | (static_cast<unsigned long>(q[1]) << 16) |
           (static_cast<unsigned long>(q[2]) << 8) | static_cast<unsigned long>(q[3]);
  };
  int depth = 0, colour = 0, interlace = 0;
  std::string idat;
  for (std::size_t at = 8; at + 8 <= n;) {
    const unsigned long len = be32(p + at);
    if (at + 12 + len > n) break;
    const char* type = bytes.data() + at + 4;
    const unsigned char* data = p + at + 8;
    if (std::memcmp(type, "IHDR", 4) == 0 && len >= 13) {
      out.w = static_cast<int>(be32(data));
      out.h = static_cast<int>(be32(data + 4));
      depth = data[8];
      colour = data[9];
      interlace = data[12];
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      idat.append(reinterpret_cast<const char*>(data), len);
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      break;
    }
    at += 12 + len;
  }
  if (out.w <= 0 || out.h <= 0) return "no image header in " + path;
  if (depth != 8) return "only 8-bit PNGs are supported (this one is " + std::to_string(depth) + "-bit)";
  if (colour != 2 && colour != 6) return "only truecolour PNGs are supported (colour type " + std::to_string(colour) + ")";
  if (interlace != 0) return "interlaced PNGs are not supported";
  if (idat.empty()) return "no image data in " + path;

  const int chan = colour == 6 ? 4 : 3;
  const std::size_t stride = static_cast<std::size_t>(out.w) * chan;
  std::vector<unsigned char> raw((stride + 1) * static_cast<std::size_t>(out.h));
  uLongf got = static_cast<uLongf>(raw.size());
  if (uncompress(raw.data(), &got, reinterpret_cast<const Bytef*>(idat.data()),
                 static_cast<uLong>(idat.size())) != Z_OK || got != raw.size())
    return "the image data in " + path + " did not decompress";

  // UN-FILTER, in place, row by row. Each scanline states its own filter in its first byte and
  // refers to the row above, so this cannot be done per row in isolation.
  out.rgb.assign(static_cast<std::size_t>(out.w) * out.h * 3, 0);
  std::vector<unsigned char> prev(stride, 0), cur(stride, 0);
  for (int y = 0; y < out.h; ++y) {
    const unsigned char f = raw[(stride + 1) * static_cast<std::size_t>(y)];
    const unsigned char* src = raw.data() + (stride + 1) * static_cast<std::size_t>(y) + 1;
    for (std::size_t i = 0; i < stride; ++i) {
      const int a = i >= static_cast<std::size_t>(chan) ? cur[i - chan] : 0;
      const int b = prev[i];
      const int c = i >= static_cast<std::size_t>(chan) ? prev[i - chan] : 0;
      int v = src[i];
      switch (f) {
        case 0: break;
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) / 2; break;
        case 4: {
          const int pa = std::abs(b - c), pb = std::abs(a - c), pc = std::abs(a + b - 2 * c);
          v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
          break;
        }
        default: return "unknown scanline filter in " + path;
      }
      cur[i] = static_cast<unsigned char>(v & 0xff);
    }
    for (int x = 0; x < out.w; ++x) {
      const std::size_t s = static_cast<std::size_t>(x) * chan, d = (static_cast<std::size_t>(y) * out.w + x) * 3;
      out.rgb[d] = cur[s];
      out.rgb[d + 1] = cur[s + 1];
      out.rgb[d + 2] = cur[s + 2];
    }
    prev = cur;
  }
  return {};
}

struct Canvas {
  std::string source;            // what the layout named after the colon
  const Tool* tool;              // the host's current tool, read at paint time — BORROWED
  RolltuiWindows* windows;       // BORROWED: where `draw` asks for the frame's style table
  RolltuiDrawScratch* draw_scratch = nullptr;
  RolltuiRect inner{};
  // A PAINTED CELL IS GLYPH-LEVEL PLUS COLOUR, and it is the host's own struct: `RolltuiCell`
  // is the library's grid cell, built by the frame, and there is no public way for a host to
  // make or hold one. So a paint app keeps a parallel picture and turns it into cells at draw
  // time — stated in the wall log rather than left implicit.
  std::map<std::pair<int, int>, Ink> pixels;
  // THE STROKE, which is the state this widget was missing and the reason two separate
  // complaints were one defect. A canvas that paints on any `Drag` paints on a drag whose
  // `Press` it never saw, and nothing ever turns that off because there is nothing to turn
  // off. A canvas that paints ONE footprint per event draws a dotted line, because terminal
  // motion arrives per cell at best and skips outright under speed. A button that is down and
  // a point it was last at fixes both: no press, no paint; and the gap between two reports is
  // filled rather than left.
  bool down = false;
  int last_x = 0, last_y = 0;
  // WHERE THE BRUSH WAS FOR THE LAST STAMP, which is what makes a cell's darkening a function
  // of the picture rather than of how fast the hand moved. A footprint is a pure function of
  // its centre, so the previous one needs no set and no allocation to test against.
  bool stamped = false;
  int stamp_x = 0, stamp_y = 0;
};

void canvas_destroy(void* ctx) {
  Canvas* c = static_cast<Canvas*>(ctx);
  rolltui_draw_scratch_free(c->draw_scratch);
  delete c;
}

int canvas_problem(void* ctx, RolltuiStr* out) {
  const Canvas* c = static_cast<const Canvas*>(ctx);
  if (c->source == kCanvasSource) return 0;
  const std::string why = "nothing is bound to '" + c->source + "'";
  rolltui_str_set(out, why.data(), why.size());
  return 1;
}

void canvas_layout(void* ctx, const RolltuiResolvedNode* rn) { static_cast<Canvas*>(ctx)->inner = rn->inner; }

void canvas_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  Canvas* c = static_cast<Canvas*>(ctx);
  const RolltuiRect r = rn->inner;
  // The frame's style table for THIS draw, which is what lets a widget ask "what does
  // Role::text look like" without the vtable carrying a fourth parameter every kind must
  // accept (`rolltui_widgets.h`'s note on `rolltui_windows_styles`).
  const RolltuiStyle* styles = rolltui_windows_styles(c->windows);
  const RolltuiStyle base = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT);
  for (const auto& [at, ink] : c->pixels) {
    const auto [x, y] = at;
    if (x < 0 || y < 0 || x >= r.w || y >= r.h) continue;  // the picture outlives the viewport
    const Ramp& ramp = kRamps[ink.ramp % 2];
    const int step = ink.level < 0 ? 0 : (ink.level >= ramp.steps ? ramp.steps - 1 : ink.level);
    const char* g = ramp.cells[step];
    // A HOST NAMES ITS OWN COLOUR HERE, and it is worth being explicit that this is NOT the
    // effects rule. `rolltui.h` says an EFFECT never invents a colour — it picks the base style
    // or a role the theme named — because an effect is the THEME's motion and must stay legible
    // in `mono`. A canvas's pixel is the USER's content, the same way a document's text is, and
    // content has always carried its own colour. What keeps mono honest is the RAMP: the shape
    // survives with no colour at all.
    RolltuiStyle st = base;
    if (ink.color.kind != RolltuiStyleColor::Kind::None) st.fg = ink.color;
    // WALL 8: `rolltui_windows_env` is INTERNAL, so a host's widget cannot read back the
    // ambiguity the library was told about — and, exactly as with the bindings table in wall 1,
    // it does not need to: the HOST set it and lends it through the tool. The block ramp is
    // EA-AMBIGUOUS, so on a wide-ambiguous terminal U+2588 is TWO cells and `put_text` refuses
    // to cut one in half — a visible refusal rather than a torn row.
    // THE AMBIGUOUS-WIDTH FALLBACK, AND IT IS THE APP'S TO MAKE (the Unicode probe).
    // U+2588 and friends are East Asian AMBIGUOUS: on a terminal that renders them two cells
    // wide, `put_text` will not cut one in half and lays down NOTHING — measured, and the whole
    // block ramp vanished. The library is right to refuse and it says so the only way a draw
    // call can: it RETURNS THE CELLS IT USED. So a host that checks the return can fall back,
    // and this one falls back to the ascii ramp's step of the same darkness — the picture keeps
    // its shape, which is the same reason the ramp exists for `mono`.
    if (rolltui_frame_put_text(f, c->draw_scratch, r.x + x, r.y + y, g, std::strlen(g), st, 1,
                               c->tool->ambiguous, 0) == 0) {
      const char* fallback = kRamps[0].cells[step];
      rolltui_frame_put_text(f, c->draw_scratch, r.x + x, r.y + y, fallback, std::strlen(fallback), st, 1,
                             c->tool->ambiguous, 0);
    }
  }
}

// THE BRUSH'S SHAPE, as one predicate. A brush is a SIZE and a SHAPE, so a stroke lays a
// footprint rather than one cell; the round mask is the ordinary discrete disc, a cell being
// in when its centre is within the radius, which at these sizes is the difference between a
// blunt end and a bevelled one. It is a predicate rather than a loop because the stamp asks
// it about the CURRENT centre and the entry test asks the same question about the previous
// one — two callers, one shape, so a brush cannot be round going and square coming back.
int in_footprint(const Tool& t, int cx, int cy, int x, int y) {
  const int n = t.size < 1 ? 1 : (t.size > 5 ? 5 : t.size);
  const int rad = n / 2;
  const int dx = x - cx, dy = y - cy;
  if (dx < -rad || dx > rad || dy < -rad || dy > rad) return 0;
  if (n % 2 == 0 && (dx == -rad || dy == -rad)) return 0;  // an even brush grows right/down
  if (t.round && rad > 0 && dx * dx + dy * dy > rad * rad) return 0;
  return 1;
}

// ONE FOOTPRINT, at one point in the canvas's own coordinates — and the one place a cell's
// darkness is decided.
//
// A CELL STILL UNDER THE BRUSH FROM THE LAST STAMP IS SKIPPED ENTIRELY. That is what makes
// the step per ENTRY: dragging slowly across one cell reports it a dozen times and darkens it
// once, dragging fast reports it once and darkens it once. Deepening per event would make the
// picture a record of the hand's speed, which is the "too sensitive" this replaced.
//
// A cell with no ink starts light; every later entry moves it one step down the ramp until it
// reaches the darkest. There is no way to go back up — an eraser would be a tool, and clearing
// the sheet is the one the palette offers.
void canvas_stamp(Canvas* c, int cx, int cy) {
  const int n = c->tool->size < 1 ? 1 : (c->tool->size > 5 ? 5 : c->tool->size);
  const int rad = n / 2;
  for (int dy = -rad; dy <= rad; ++dy)
    for (int dx = -rad; dx <= rad; ++dx) {
      const int x = cx + dx, y = cy + dy;
      if (!in_footprint(*c->tool, cx, cy, x, y)) continue;
      if (c->stamped && in_footprint(*c->tool, c->stamp_x, c->stamp_y, x, y)) continue;
      const auto it = c->pixels.find({x, y});
      Ink laid;
      laid.ramp = c->tool->ramp;  // the cell remembers which ramp drew it
      laid.color = c->tool->color;
      laid.level = it == c->pixels.end() ? kFirstLevel : std::min(it->second.level + 1, kMaxLevel);
      c->pixels[{x, y}] = laid;
    }
  c->stamped = true;  // AFTER the loop: the whole footprint is tested against the OLD centre
  c->stamp_x = cx;
  c->stamp_y = cy;
}

// THE SEGMENT FROM THE LAST POINT TO THIS ONE, which is what makes a fast drag a line instead
// of a row of dots. A terminal reports motion at best once per cell entered and drops reports
// under speed, so the two points a widget is handed are the ends of a gap it has to fill
// itself — there is no event for the cells in between and there never will be.
//
// One stamp per step along the longer axis, so the walk is dense in cells rather than in
// distance: a 40-wide, 2-tall segment gets 40 stamps and no cell is skipped.
void canvas_stroke_to(Canvas* c, int cx, int cy) {
  const int dx = cx - c->last_x, dy = cy - c->last_y;
  const int steps = std::max(std::abs(dx), std::abs(dy));
  for (int st = 1; st <= steps; ++st) canvas_stamp(c, c->last_x + dx * st / steps, c->last_y + dy * st / steps);
  c->last_x = cx;
  c->last_y = cy;
}

// A PRESS OPENS THE STROKE, A DRAG CONTINUES IT ONLY WHILE IT IS OPEN, A RELEASE CLOSES IT.
// The drags past the window's own edge are included, because the press captured the pointer.
// Nothing here clamps to the window: a stroke that leaves the canvas keeps its shape and
// simply is not drawn until it comes back.
//
// A `Drag` with no stroke open is NOT this widget's event and is refused as one — the press
// landed somewhere else, or was never reported at all. Returning 0 rather than painting is
// what makes a pointer moving across the sheet leave it alone.
int canvas_handle(void* ctx, const RolltuiEvent* e) {
  Canvas* c = static_cast<Canvas*>(ctx);
  if (e->kind != ROLLTUI_EVENT_MOUSE) return 0;
  // `RolltuiMouseEvent::Kind` is spelled per language (rolltui_keys.h): the scoped enum in
  // C++, eight bare bytes plus a comment in C. A KNOWN LIMIT rather than a fixed one — this
  // host is C++ and can name them, but a pure-C consumer has no word for any of the eight.
  // The fix is the shape `ROLLTUI_ROLE_LIST` uses one level up.
  using K = RolltuiMouseEvent::Kind;
  const int cx = e->mouse.x - c->inner.x, cy = e->mouse.y - c->inner.y;
  switch (e->mouse.kind) {
    case K::Press:
      c->down = true;
      c->last_x = cx;
      c->last_y = cy;
      c->stamped = false;  // a new stroke enters every cell it lands on, including ones it left
      canvas_stamp(c, cx, cy);
      return 1;
    case K::Drag:
      if (!c->down) return 0;
      canvas_stroke_to(c, cx, cy);
      return 1;
    case K::Release:
      if (!c->down) return 0;
      canvas_stroke_to(c, cx, cy);  // the release carries a position, and it is part of the line
      c->down = false;
      return 1;
    default:
      return 0;
  }
}

// The plugin is a BORROW of a table the implementor keeps — one `static const` per kind, the
// shape `rolltui_widgets.h` states and the library's own eight already use.
constexpr RolltuiWidgetPlugin kCanvasPlugin = {
    /*destroy=*/canvas_destroy,
    /*layout=*/canvas_layout,
    /*draw=*/canvas_draw,
    /*problem=*/canvas_problem,
    /*note_at=*/nullptr,
    /*desired_outer=*/nullptr,
    /*handle=*/canvas_handle,
    /*scroll_extent=*/nullptr,
    /*scroll_to=*/nullptr,
};

// What the factory is registered WITH: the two borrows a canvas needs and nothing else. One
// per app, held by `App`, so the factory's `ctx` outlives every widget it builds.
// FIT A PICTURE TO THE SHEET. The image is resampled by BOX AVERAGE rather than by nearest
// neighbour: a terminal cell is enormous next to a pixel, so nearest picks one pixel out of
// thousands and the result is noise rather than a picture.
//
// Two things come out of each cell and both are kept. The AVERAGE COLOUR goes in as the cell's
// ink, and the LUMINANCE picks the ramp step — so the picture survives a mono terminal, where
// the colour is gone and the ramp is all that is left. That is the same reason a stroke carries
// a ramp per cell rather than one for the sheet.
//
// Aspect: a terminal cell is about twice as tall as it is wide, so the vertical sample is half
// the height it would otherwise be. Without that every picture comes out stretched.
void fit_image_into(const Image& img, Canvas& c, int ramp) {
  if (img.w <= 0 || img.h <= 0 || c.inner.w <= 0 || c.inner.h <= 0) return;
  c.pixels.clear();
  const int steps = kRamps[ramp % 2].steps;
  for (int cy = 0; cy < c.inner.h; ++cy) {
    for (int cx = 0; cx < c.inner.w; ++cx) {
      const int x0 = (int)((long long)cx * img.w / c.inner.w);
      const int x1 = (int)((long long)(cx + 1) * img.w / c.inner.w);
      const int y0 = (int)((long long)cy * img.h / c.inner.h);
      const int y1 = (int)((long long)(cy + 1) * img.h / c.inner.h);
      long long r = 0, g = 0, b = 0, n = 0;
      for (int y = y0; y < (y1 > y0 ? y1 : y0 + 1) && y < img.h; ++y)
        for (int x = x0; x < (x1 > x0 ? x1 : x0 + 1) && x < img.w; ++x) {
          const std::size_t at = ((std::size_t)y * img.w + x) * 3;
          r += img.rgb[at];
          g += img.rgb[at + 1];
          b += img.rgb[at + 2];
          ++n;
        }
      if (n == 0) continue;
      r /= n; g /= n; b /= n;
      // Rec. 601 luma, which is what a person means by "how dark is this".
      const int luma = (int)((299 * r + 587 * g + 114 * b) / 1000);
      Ink ink;
      ink.ramp = ramp % 2;
      // DARKEST pixel gets the DENSEST glyph: the ramp runs light to dark, and a picture drawn
      // the other way up reads as its own negative.
      ink.level = (255 - luma) * (steps - 1) / 255;
      ink.color = RolltuiStyleColor::rgb((unsigned char)r, (unsigned char)g, (unsigned char)b);
      if (ink.level > 0) c.pixels[{c.inner.x + cx, c.inner.y + cy}] = ink;
    }
  }
}

struct CanvasFactoryCtx {
  const Tool* tool;
  RolltuiWindows* windows;
};

RolltuiWidget canvas_factory(void* ctx, RolltuiWindows* /*w*/, const char* content, size_t len) {
  const CanvasFactoryCtx* fc = static_cast<const CanvasFactoryCtx*>(ctx);
  const char* source = nullptr;
  std::size_t source_len = 0;
  RolltuiStr why{};
  unsigned char problem = 0;
  // A content the registry cannot parse is not an error path — a zeroed widget means "I
  // cannot build this", and `Windows` draws the error panel and names it in the report.
  if (!rolltui_content_parse(rolltui_windows_context(fc->windows), content, len, nullptr, nullptr, nullptr, nullptr,
                             &source, &source_len, &problem, &why))
    return RolltuiWidget{};
  Canvas* c = new Canvas{std::string(source, source_len), fc->tool, fc->windows, rolltui_draw_scratch_new(), {}, {}};
  return RolltuiWidget{&kCanvasPlugin, c};
}

// ---- the app -----------------------------------------------------------------------------

// APP LIFETIME, RELEASED IN ONE DESTRUCTOR — see the header note. Every handle below is
// created once here and freed once there; none of them is per-frame, which is why no wrapper
// type is needed for any of them.
// Where a person's own presets live — the same rungs `rolltui_app_file` walks for an app's files.
std::string user_presets_dir() {
  if (const char* d = std::getenv("ROLL_CONFIG_DIR"); d && *d) return std::string(d) + "/rolltui";
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return std::string(x) + "/roll/rolltui";
  const char* home = std::getenv("HOME");
  return std::string(home && *home ? home : ".") + "/.config/roll/rolltui";
}

struct App {
  RolltuiContext* ctx = rolltui_context_new();  // OWNED: this app's session (Phase 25)
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* effects = nullptr;
  RolltuiEffectScratch* effect_scratch = rolltui_effect_scratch_new();
  RolltuiDrawScratch* draw_scratch = rolltui_draw_scratch_new();
  RolltuiBindings* bindings = rolltui_bindings_clone(rolltui_bindings_default(ctx));
  RolltuiWindows* windows = rolltui_windows_new(ctx);
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  RolltuiComposeScratch* compose_scratch = rolltui_compose_scratch_new();
  RolltuiLayout* layout = nullptr;  // OWNED (Phase 23: a layout is a handle)
  // OWNED: this app's own presets. The editors are the library's kinds; the STORE is what gives
  // them something of this app's to edit.
  RolltuiPresetStore* theme_store = nullptr;
  RolltuiPresetStore* keys_store = nullptr;
  unsigned long long theme_seen = 0;
  CanvasFactoryCtx factory_ctx{};
  // CALLER-FILLED, one per run: the status line's fields, reset and refilled every frame so
  // the array and each row's buffer are reused rather than rebuilt.
  RolltuiRows status_rows{};
  int w = 80, h = 24;
  Tool tool;
  std::string note;
  // The clock effects are applied at — 0 under --frame, so a frame dump stays a pure
  // function of state; the real one in the event loop.
  std::uint64_t effect_ms = 0;

  App() {
    layout = rolltui_layout_new();
    // The eight built-in kinds and the five vocabularies they draw with. A bare
    // `rolltui_windows_new(ctx)` has neither — this is the one line that makes a pure-C
    // window table usable, and every host calls it.
    rolltui_context_set_library_defaults(ctx);
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App() {
    rolltui_preset_store_free(keys_store);
    rolltui_preset_store_free(theme_store);
    rolltui_rows_release(&status_rows);
    rolltui_layout_free(layout);
    rolltui_compose_scratch_free(compose_scratch);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(windows);
    rolltui_bindings_free(bindings);
    rolltui_draw_scratch_free(draw_scratch);
    rolltui_effect_scratch_free(effect_scratch);
    rolltui_effect_map_free(effects);
    rolltui_context_free(ctx);  // LAST: the registries every handle above resolved through
  }

  RolltuiRect area() const { return {0, 0, w, h > 1 ? h - 1 : 0}; }
  const RolltuiStyle& style(unsigned char role) const {
    return *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, role);
  }

  void set_theme(const char* name) {
    rolltui_effect_map_free(effects);
    effects = rolltui_theme_builtin_fill(name, std::strlen(name), styles, ROLLTUI_ROLE_COUNT);
  }

  void mount() {
    // ONE registration, in the two halves `rolltui_widgets.h` describes: the NAME with the
    // layout vocabulary (rung 2, which refuses a library name) and the FACTORY with the
    // window table. After this the library builds `canvas:<source>` like any built-in, and
    // this host never sees a window id again.
    factory_ctx = {&tool, windows};
    register_canvas_kind(ctx);
    rolltui_context_register_kind(ctx, kCanvasKind, std::strlen(kCanvasKind), canvas_factory, &factory_ctx,
                                  nullptr);
    rolltui_context_add_menu(ctx, "tools", 5, kToolsMenu, std::strlen(kToolsMenu));
    rolltui_windows_bind_rows(
        windows, "brush", 5,
        [](void* ctx, RolltuiRows* out) {
          App& a = *static_cast<App*>(ctx);
          const char* ramp = kRamps[a.tool.ramp % 2].name;
          rolltui_rows_add(out, "shading", 7, ramp, std::strlen(ramp));
          char ink[ROLLTUI_COLOR_STRING_MAX];
          const std::size_t n = rolltui_color_to_string(a.tool.color, ink, sizeof ink);
          rolltui_rows_add(out, "ink", 3, ink, n);
          const std::string brush = std::to_string(a.tool.size) + (a.tool.round ? " round" : " square");
          rolltui_rows_add(out, "brush", 5, brush.data(), brush.size());
          const std::string marks = std::to_string(a.marks());
          rolltui_rows_add(out, "marks", 5, marks.data(), marks.size());
        },
        this, nullptr);
    set_help_scopes();
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
    declare_actions();  // the SCREEN says what this app can do (Phase 10 m4)
  }

  // The kind table belongs to a CONTEXT now, so this takes the session it registers
  // into. Registering the same name twice in one context is idempotent —
  // `rolltui_widget_kind_register` refuses only a name already registered with ANOTHER rule.
  static void register_canvas_kind(RolltuiContext* ctx) {
    rolltui_widget_kind_register(ctx, kCanvasKind, std::strlen(kCanvasKind), ROLLTUI_SOURCE_REQUIRED, kCanvasDescribes,
                                 std::strlen(kCanvasDescribes));
  }

  // The SCREEN's own actions first: they are the ones a person came to this app for,
  // and they are the ones no source here names.
  static const std::vector<std::string>& help_scopes() {
    static const std::vector<std::string> s = {"app", "menu", "stack"};
    return s;
  }
  void set_help_scopes() {
    rolltui_context_set_help(ctx, "", 0, "", 0);
    rolltui_context_clear_help_scopes(ctx);
    for (const std::string& s : help_scopes()) rolltui_context_add_help_scope(ctx, s.data(), s.size());
  }
  // `RolltuiLayout::actions` is already the flat array `rolltui_bindings_declare` takes, so
  // the `action_decls()` conversion the C++ shim needed has no counterpart here at all.
  void declare_actions() {
    std::size_t n = 0;
    const RolltuiLayoutAction* a = rolltui_layout_actions(layout, &n);
    rolltui_bindings_declare(bindings, a, n, nullptr, 0);
  }

  // The app's own widget, by the CONTENT the library would key it under — composed by
  // `rolltui_content_format` from the same two words registered above rather than spelled a
  // third time, since the join rule (a colon exactly when the source rule says so) is the
  // library's and not this host's to restate. `vt == &kCanvasPlugin` is what makes the cast
  // safe: `widget_for` hands back an opaque {vt, ctx} and this host only ever recognises its
  // own table.
  Canvas* canvas() {
    RolltuiStr content{};
    rolltui_content_format(kCanvasKind, std::strlen(kCanvasKind), kCanvasSource, std::strlen(kCanvasSource),
                           ROLLTUI_SOURCE_REQUIRED, &content);
    RolltuiWidget* w = rolltui_windows_widget_for(windows, content.p ? content.p : "", content.n);
    rolltui_str_free(&content);
    return w && w->vt == &kCanvasPlugin ? static_cast<Canvas*>(w->ctx) : nullptr;
  }
  std::size_t marks() {
    const Canvas* c = canvas();
    return c ? c->pixels.size() : 0;
  }

  void set_layout(RolltuiLayout* l) {  // TAKES OWNERSHIP
    rolltui_layout_free(layout);
    layout = l;
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
    declare_actions();
  }

  // An edit in the theme editor bumps the store's version; a frame drawing the old styles would
  // make the editor look broken. Compared by version, so an unchanged frame costs nothing.
  void sync_theme() {
    if (!theme_store) return;
    const unsigned long long v = rolltui_preset_store_version(theme_store);
    if (v == theme_seen) return;
    theme_seen = v;
    RolltuiThemePresetValue* w = (RolltuiThemePresetValue*)rolltui_preset_store_working(theme_store);
    if (!w) return;
    RolltuiThemeReport rep{};
    RolltuiStyle got[ROLLTUI_ROLE_COUNT]{};
    RolltuiStr name{};
    const int named = rolltui_theme_mode_from_name(w->mode.p ? w->mode.p : "", w->mode.n);
    RolltuiEffectMap* eff = rolltui_theme_load(w->colours, named >= 0 ? named : ROLLTUI_MODE_DARK,
                                               rolltui_theme_default_vocab(), got, &name, &rep);
    if (eff) {
      std::copy(std::begin(got), std::end(got), styles);
      rolltui_effect_map_free(effects);
      effects = eff;
      RolltuiScrollbarGlyphs g;
      rolltui_theme_scrollbar_glyphs(w->colours, &g);
      rolltui_context_set_scrollbar_glyphs(ctx, &g);
    }
    rolltui_str_free(&name);
    rolltui_theme_report_release(&rep);
    rolltui_preset_store_value_free(theme_store, w);
  }

  void prepare() {
    sync_theme();
    const RolltuiWidgetEnv env{static_cast<unsigned char>(tool.ambiguous), effect_ms};
    rolltui_context_set_env(ctx, &env);
    rolltui_context_set_bindings(ctx, bindings);
    rolltui_windows_sync(windows, stack);
    rolltui_windows_autosize(windows, stack, area());
    rolltui_windows_layout(windows, stack, area());
    if (rolltui_windows_report_count(windows) == 0) {
      note.clear();
    } else {
      RolltuiStr s{};
      rolltui_windows_report_summary(windows, &s);
      note.assign(s.p ? s.p : "", s.n);
      rolltui_str_free(&s);
    }
  }

  // A PICTURE IS A DRAWING SOMEBODY ELSE MADE, so it lands as ordinary ink: the same cells a
  // stroke sets, in the same ramp, which is why a loaded picture can be painted over and cleared
  // like anything else. A failure is SAID rather than swallowed — a sheet that stays blank with
  // no reason given is the wrong answer wearing a success.
  std::string picture_note;
  void open_picture(const std::string& path) {
    Canvas* c = canvas();
    if (!c) { picture_note = "no sheet to open it into"; return; }
    Image img;
    const std::string why = load_png(path, img);
    if (!why.empty()) { picture_note = why; return; }
    fit_image_into(img, *c, tool.ramp);
    picture_note = "opened " + path + " (" + std::to_string(img.w) + "x" + std::to_string(img.h) + ")";
  }

  void handle(const RolltuiEvent& e) {
    // The app's own scope first, so a global chord works wherever the focus is. ANY `app.<id>`
    // naming a popup this screen declares opens it: a panel is a layout entry and a chord, and
    // this file gains nothing per panel.
    if (e.kind == ROLLTUI_EVENT_KEY) {
      std::size_t alen = 0;
      const char* a = rolltui_bindings_action_for(bindings, &e.key, "app", 3, &alen);
      if (a && rolltui_window_stack_action_popup(stack, layout, a, alen)) return;
    }
    RolltuiStr window{};
    const unsigned char kind =
        rolltui_window_stack_route(stack, &e, area(), bindings, rolltui_stack_default_actions(), &window);
    const std::string target = str_of(window);
    rolltui_str_free(&window);
    if (kind != ROLLTUI_ROUTE_DELIVER) return;
    if (rolltui_windows_handle(windows, target.data(), target.size(), &e)) return;
    // A menu window is the host's to drive, exactly as in every other host.
    if (RolltuiMenu* m = rolltui_windows_menu_at(windows, target.data(), target.size())) {
      RolltuiMenuEvent ev{};
      rolltui_menu_handle(m, &e, bindings, rolltui_menu_default_actions(), &ev);
      // THE TYPED FIELDS FROM A HOST'S SIDE, which nothing in this tree did previously.
      // What the API makes easy: a committed value arrives already validated and CANONICAL, so
      // there is no range check, no re-format and no error path here — the field refused
      // anything that could not become a valid value while it was still being typed. What it
      // makes hard is only that the value is text, which `rolltui_color_parse` now answers.
      if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && ev.value.n != 0) {
        if (view_of(ev.id) == "ramp") tool.ramp = view_of(ev.value) == "blocks" ? 1 : 0;
        if (view_of(ev.id) == "shape") tool.round = view_of(ev.value) == "round";
      }
      if (ev.kind == ROLLTUI_MENU_EVENT_INPUT && ev.value.n != 0) {
        const std::string v = str_of(ev.value);
        if (view_of(ev.id) == "size") tool.size = std::atoi(v.c_str());
        if (view_of(ev.id) == "ink") rolltui_color_parse(v.data(), v.size(), &tool.color);
        if (view_of(ev.id) == "open") open_picture(v);
      }
      if (ev.kind == ROLLTUI_MENU_EVENT_ACTIVATE && view_of(ev.id) == "clear" && canvas()) canvas()->pixels.clear();
      rolltui_menu_event_release(&ev);
    }
  }

  // Draws into the frame the swap LENT — this host owns no frame at all, which is the whole
  // of what adopting `rolltui_swap` bought (see the header note).
  void render_into(RolltuiFrame* f) {
    prepare();
    rolltui_window_stack_compose(stack, f, area(), styles, rolltui_layout_default_roles(), draw_slot, this,
                                 tool.ambiguous, compose_scratch);
    if (h > 1) {
      rolltui_frame_fill(f, draw_scratch, RolltuiRect{0, h - 1, w, 1}, style(ROLLTUI_ROLE_PANEL_BACKGROUND),
                         nullptr, 0);
      // A STATUS LINE IS A LIST OF NAMED FACTS, so it is built as rows and drawn as rows: the
      // names muted, the answers bright, the same two roles the panel above uses. Read as one
      // string in one colour it was a run of words in which `b1s` and `ascii` looked like the
      // same kind of thing, and nothing said which was which.
      //
      // `status_rows` is a member the app RESETS and refills — the array and every row's
      // buffer survive, so a frame in which nothing changed allocates nothing to say so.
      const RolltuiLayoutNode* focused = rolltui_window_stack_focused(stack);
      char inkstr[ROLLTUI_COLOR_STRING_MAX];
      const std::size_t inkn = rolltui_color_to_string(tool.color, inkstr, sizeof inkstr);
      char num[64];
      std::size_t lname_n = 0;
      const char* lname = rolltui_layout_name(layout, &lname_n);
      // ORDERED BY WHAT A PAINTER NEEDS, because a status line is truncated from the right and
      // the order therefore decides what survives a narrow window. What the next stroke will
      // lay down comes first; the screen's own dimensions come last.
      status_rows.reset();
      rolltui_rows_add(&status_rows, "", 0, lname, lname_n);
      // A WINDOW REPORT OUTRANKS EVERY TOOL. An unbound source or an unknown kind is drawn as
      // an error panel AND said here, and a status line long enough to push it off the right
      // edge hides the one thing that must never be hidden — so it goes before the brush.
      if (!note.empty()) rolltui_rows_add(&status_rows, "", 0, note.data(), note.size());
      status_rows.add("texture", kRamps[tool.ramp % 2].name);
      rolltui_rows_add(&status_rows, "ink", 3, inkstr, inkn);
      std::snprintf(num, sizeof num, "%d %s", tool.size, tool.round ? "round" : "square");
      status_rows.add("brush", num);
      std::snprintf(num, sizeof num, "%zu", marks());
      status_rows.add("marks", num);
      status_rows.add("focus", focused ? rolltui_layout_node_id(focused, nullptr) : "-");
      std::snprintf(num, sizeof num, "%dx%d", w, h);
      status_rows.add("size", num);
      rolltui_frame_put_fields(f, draw_scratch, 1, h - 1, &status_rows, style(ROLLTUI_ROLE_LABEL),
                               style(ROLLTUI_ROLE_VALUE), w - 1, tool.ambiguous);
    }
    // EFFECTS ARE ONE LINE, and it is the same line roll and the studio have. A paint app
    // marks nothing today, so this frame is unchanged; the point is that a `canvas` that DID
    // mark a span would move here with no library change.
    if (rolltui_frame_mark_count(f) != 0 && effects && !rolltui_effect_map_empty(effects)) {
      RolltuiEffectReport rep{};
      rolltui_effects_apply(ctx, f, effect_scratch, styles, nullptr, effects, effect_ms, 0, &rep, nullptr, nullptr);
    }
  }

  static void draw_slot(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
    App& a = *static_cast<App*>(ctx);
    rolltui_windows_draw(a.windows, rn, f, a.styles, rolltui_windows_default_roles());
  }

  // The redraw interval this frame's motion asks for, or `idle_ms` when nothing moves — the
  // whole of a host's animation loop (`rolltui_effects.h`).
  int poll_timeout_ms(const RolltuiFrame* f, int idle_ms) const {
    if (!f || rolltui_frame_mark_count(f) == 0 || !effects || rolltui_effect_map_empty(effects)) return idle_ms;
    const int tick = rolltui_effects_tick_ms(ctx, f, effects);
    if (tick <= 0) return idle_ms;
    return idle_ms <= 0 ? tick : (idle_ms < tick ? idle_ms : tick);
  }
};

// ---- plumbing ------------------------------------------------------------------------------

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

[[maybe_unused]] bool parse_size(const std::string& s, int& w, int& h) {
  const std::size_t x = s.find('x');
  if (x == std::string::npos) return false;
  w = std::atoi(s.substr(0, x).c_str());
  h = std::atoi(s.substr(x + 1).c_str());
  return w > 0 && h > 0;
}

// One load, from TEXT, to the enduring `RolltuiLayout` a host holds. The report is the
// caller's to read and release; the loaded carrier is never retained past the call, which is
// `rolltui_loaded_layout_to_layout`'s own contract.
RolltuiLayout* load_layout_text(RolltuiContext* ctx, std::string_view text, RolltuiLayoutReport* rep) {
  std::size_t defaults_n = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx, &defaults_n);
  return rolltui_load_layout_text(text.data(), text.size(), defaults, defaults_n,
                                  rolltui_layout_default_hooks(), rep);
}

int usage() {
  std::fprintf(stderr,
               "usage: rolltui-paint [--ambiguous-wide]\n"
#ifdef ROLLTUI_SELFTEST
               "                     [--presets DIR] [--layout NAME|FILE] [--theme NAME]\n"
               "                     [--frame WxH] [--present truecolor|256|16|mono]\n"
               "                     [--keys \"F4 Down Enter Type:name Click 5,3\"] [--open PICTURE.png]\n"
               "                     [--ramp ascii|blocks] [--ink #rrggbb] [--size N] [--shape square|round]\n"
               "                     [--stroke X,Y-X,Y] [--drag X,Y-X,Y] [--dot X,Y]\n"
               "                     --stroke presses, drags ONCE to the far end and releases;\n"
               "                     --drag sends the same drags with NO press\n"
               "                     tool flags and strokes are applied IN THE ORDER WRITTEN\n"
#endif
               "\n");
  return 2;
}

// ONE FRAME'S INPUT, held by the loop and refilled — never rebuilt per frame, so the three
// arrays keep their capacity for the life of the run (CLAUDE.md's strategy 3, caller-filled).
//
// It exists because `rolltui_terminal_poll`'s events are BORROWED for the length of the emit
// call (`rolltui_terminal.h` rule 3): a handler must not run inside the decoder — handling one
// event can resize the app — so the bytes are copied here first and acted on after.
struct Pending {
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
  std::vector<RolltuiEvent> events;
  std::vector<std::string> texts;    // the borrowed bytes, owned for this frame
  std::vector<std::size_t> text_of;  // events[i]'s entry in `texts`, or kNone
  bool quit = false;
  int w = 0, h = 0;
  bool resized = false;

  void begin(int cur_w, int cur_h) {
    events.clear();
    texts.clear();
    text_of.clear();
    quit = false;
    resized = false;
    w = cur_w;
    h = cur_h;
  }
};

void collect_event(void* ctx, const RolltuiTermEvent* e) {
  Pending& p = *static_cast<Pending*>(ctx);
  if (e->kind == ROLLTUI_TERM_EVENT_RESIZE) {
    p.w = e->w;
    p.h = e->h;
    p.resized = true;
    return;
  }
  if (e->kind == ROLLTUI_TERM_EVENT_KEY && e->key.ctrl && e->key.key == ROLLTUI_KEY_CHAR && e->key.ch == 'q') {
    p.quit = true;
    return;
  }
  RolltuiEvent ev{};
  ev.kind = e->kind;
  ev.key = e->key;
  ev.mouse = e->mouse;
  // `text` is patched by the caller once `texts` has stopped growing — an INDEX is recorded
  // here rather than the length, because an empty-but-present `text` is a real state (an
  // Unknown key with no bytes left) and keying off the length would desynchronise the two
  // vectors the moment one appeared.
  std::size_t slot = Pending::kNone;
  if (e->text) {
    slot = p.texts.size();
    p.texts.emplace_back(e->text, e->text_len);
    ev.text_len = e->text_len;
  }
  p.text_of.push_back(slot);
  p.events.push_back(ev);
}

[[maybe_unused]] RolltuiEvent mouse_event(RolltuiMouseEvent::Kind kind, int x, int y) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_MOUSE;
  e.mouse.kind = kind;
  e.mouse.button = 1;
  e.mouse.x = x;
  e.mouse.y = y;
  return e;
}

}  // namespace

int main(int argc, char** argv) {
  std::string presets_dir, layout_arg, theme_arg = "default-dark", frame_spec, present_depth, keys_spec, open_path;
  bool ambiguous = false;
  // THE SCRIPT, IN ORDER. `--stroke` used to be one shot with one tool, which could only ever
  // draw a line of one glyph. A picture needs the tool to change BETWEEN strokes, so the tool
  // flags and the strokes are collected as an ordered list and replayed after the app is built.
  std::vector<std::pair<std::string, std::string>> script;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    [[maybe_unused]] auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    // WHAT A FLAG ON THIS COMMAND LINE MAY BE, and the four are not close:
    //   1. A SELF-TEST HOOK — compiled in only for `rolltui-paint-selftest`, which is this same
    //      source built again WITH them. The shipped binary does not contain them, so the binary
    //      that gets verified is not the one that ships.
    //   2. A REAL FEATURE RUN HEADLESSLY — a shipped capability reached without a terminal. Stays.
    //   3. A TERMINAL FACT — something true of the terminal the process cannot yet ask for.
    //      `--ambiguous-wide` is the last one; it becomes an auto-detected setting.
    //   4. CONFIGURATION — a theme, a layout, a bindings file, a preset directory, a mode, a
    //      depth. **These may never come back.** Each names something the preset system already
    //      holds, autosaves and offers a UI for, and a flag beside it is a second configuration
    //      system with neither discoverability nor persistence, competing with the one that has
    //      both — and winning by accident, because a flag is what a person finds first.
    // `rolltui-product-flags-test` holds all three products to this.
    if (a == "--ambiguous-wide") ambiguous = true;  // a fact about the terminal, not a test hook
#ifdef ROLLTUI_SELFTEST
    else if (a == "--presets") presets_dir = next();
    else if (a == "--layout") layout_arg = next();
    else if (a == "--theme") theme_arg = next();
    else if (a == "--frame") frame_spec = next();
    else if (a == "--keys") keys_spec = next();
    else if (a == "--open") open_path = next();
    else if (a == "--present") present_depth = next();
    else if (a == "--stroke" || a == "--drag" || a == "--ramp" || a == "--ink" || a == "--size" ||
             a == "--shape" || a == "--dot")
      script.emplace_back(a, next());
#endif
    else return usage();
  }

  App app;
  app.tool.ambiguous = ambiguous ? 1 : 0;
  app.set_theme(theme_arg.c_str());
  if (!app.effects) app.set_theme("default-dark");  // an unknown --theme keeps the app's own look
  rolltui_context_set_dir(app.ctx, presets_dir.data(), presets_dir.size());

  // The two lines that make the library's editors this app's: a kind is the library's, a store is
  // what it edits. Opened on a person's own preset directory, which is a different question from
  // where a `--presets` points the screen.
  {
    const std::string store_dir = presets_dir.empty() ? user_presets_dir() : presets_dir;
    RolltuiThemePresetReport trep{};
    RolltuiBindingsPresetReport brep{};
    app.theme_store = rolltui_preset_store_new(rolltui_preset_domain_theme(app.ctx),
                                               store_dir.data(), store_dir.size(), 0, "", 0);
    app.keys_store = rolltui_preset_store_new(rolltui_preset_domain_bindings(app.ctx),
                                              store_dir.data(), store_dir.size(), 0, "", 0);
    rolltui_preset_store_start(app.theme_store, &trep);
    rolltui_preset_store_start(app.keys_store, &brep);
    rolltui_theme_preset_report_release(&trep);
    rolltui_bindings_preset_report_release(&brep);
    rolltui_windows_set_theme_store(app.windows, "theme", 5, app.theme_store, /*persist=*/1);
    rolltui_windows_set_bindings_store(app.windows, "keys", 4, app.keys_store, /*persist=*/1);
  }

  RolltuiLayoutReport rep{};
  RolltuiLayout* loaded = nullptr;  // OWNED
  bool have = false;
  if (!layout_arg.empty()) {
    const bool path = layout_arg.find('/') != std::string::npos || layout_arg.find(".json") != std::string::npos;
    const std::string file = path ? layout_arg : presets_dir + "/layouts/" + layout_arg + ".json";
    bool ok = false;
    const std::string text = read_file(file, ok);
    if (ok) {
      loaded = load_layout_text(app.ctx, text, &rep);
      have = loaded != nullptr;
    } else {
      std::size_t n = 0;
      if (const char* builtin = rolltui_layout_builtin_json(layout_arg.data(), layout_arg.size(), &n))
        if (n != 0) { loaded = load_layout_text(app.ctx, std::string_view(builtin, n), &rep); have = loaded != nullptr; }
    }
    if (!have) {
      std::fprintf(stderr, "rolltui-paint: no layout '%s' (%s)\n", layout_arg.c_str(), rep.error.c_str());
      rolltui_layout_free(loaded);
      rolltui_layout_report_release(&rep);
      return 1;
    }
  } else {
    loaded = load_layout_text(app.ctx, kDefaultLayout, &rep);
    have = loaded != nullptr;
  }
  app.set_layout(loaded);  // TAKES OWNERSHIP
  app.mount();
  for (std::size_t i = 0; i < rep.bad_values_n; ++i)
    std::fprintf(stderr, "rolltui-paint: %s\n", rep.bad_values[i].c_str());
  rolltui_layout_report_release(&rep);

  // ---- END OF INIT: what this screen NAMES that this app does not PROVIDE --------
  // `mount()` has registered the canvas kind and bound this app's sources, and the layout is
  // in place, so this is the first moment the question can be answered — and the last one
  // before a frame is drawn. It REPORTS. A screen that names something this app has not built
  // yet is a design that ran ahead of the code, which is allowed and is the point: nothing
  // below branches on the answer, and paint draws the screen either way.
  {
    RolltuiGapReport gaps{};
    rolltui_gaps_collect(app.windows, app.layout, app.bindings, &gaps);
    if (!rolltui_gap_report_clean(&gaps)) {
      RolltuiStr say{};
      rolltui_gap_report_summary(&gaps, &say);
      std::fprintf(stderr, "rolltui-paint: %s\n", say.p ? say.p : "");
      rolltui_str_free(&say);
    }
    rolltui_gap_report_release(&gaps);
  }

#ifdef ROLLTUI_SELFTEST
  if (!frame_spec.empty()) {
    if (!parse_size(frame_spec, app.w, app.h)) return usage();
    app.prepare();
    // The shared script FIRST, so a chord that opens a panel is in force before the tool flags
    // paint a picture into whatever is on top.
    // `--open` before the tool script, so a picture can be painted over the way a person would.
    if (!open_path.empty()) {
      app.open_picture(open_path);
      std::fprintf(stderr, "%s\n", app.picture_note.c_str());
    }
    if (!keys_spec.empty()) {
      for (const rolltui_selftest::Step& st : rolltui_selftest::scripted_keys(keys_spec, app.w, app.h))
        if (!st.tick) app.handle(st.ev);
      app.prepare();
    }
    for (const auto& [flag, val] : script) {
      if (flag == "--ramp") app.tool.ramp = val == "blocks" ? 1 : 0;
      else if (flag == "--size") app.tool.size = std::atoi(val.c_str());
      else if (flag == "--shape") app.tool.round = val == "round";
      else if (flag == "--ink") rolltui_color_parse(val.data(), val.size(), &app.tool.color);
      else if (flag == "--dot") {
        int x = 0, y = 0;
        if (std::sscanf(val.c_str(), "%d,%d", &x, &y) != 2) return usage();
        app.handle(mouse_event(RolltuiMouseEvent::Kind::Press, x, y));
        app.handle(mouse_event(RolltuiMouseEvent::Kind::Release, x, y));
      } else {
        int x1, y1, x2, y2;
        if (std::sscanf(val.c_str(), "%d,%d-%d,%d", &x1, &y1, &x2, &y2) != 4) return usage();
        // THREE EVENTS FOR THE WHOLE STROKE, AND THAT IS THE POINT. This used to synthesise a
        // drag at every cell along the line, which meant the script drew the line and the
        // widget only stamped — so a golden frame proved nothing about what happens when a
        // terminal reports two points and nothing between them, which is the ordinary case.
        // The far end is now ONE drag, so the line in the frame is the widget's interpolation
        // or it is not there at all.
        if (flag == "--drag") {
          // A drag whose press this app never saw — a pointer crossing the sheet with the
          // button up, or a press that landed in another window. Nothing may be painted.
          app.handle(mouse_event(RolltuiMouseEvent::Kind::Drag, x1, y1));
          app.handle(mouse_event(RolltuiMouseEvent::Kind::Drag, x2, y2));
          app.handle(mouse_event(RolltuiMouseEvent::Kind::Release, x2, y2));
        } else {
          app.handle(mouse_event(RolltuiMouseEvent::Kind::Press, x1, y1));
          app.handle(mouse_event(RolltuiMouseEvent::Kind::Drag, x2, y2));
          app.handle(mouse_event(RolltuiMouseEvent::Kind::Release, x2, y2));
        }
      }
    }
    // Even the one-shot path goes through the swap: it is the only place a frame is made, so
    // there is exactly one draw path rather than a printing one beside a presenting one.
    RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f);
    // `--present DEPTH` writes the ESCAPE BYTES rather than the glyphs, which is the only way to
    // see what a hand-picked RGB becomes at `256`, `16` and `mono` — the down-conversion is the
    // renderer's, and a paint app is the first consumer whose colours are USER DATA rather than
    // the theme's expression of a state.
    if (!present_depth.empty()) {
      const int d = rolltui_color_depth_from_name(present_depth.data(), present_depth.size());
      RolltuiStr bytes{};
      rolltui_swap_present(swap, d < 0 ? ROLLTUI_DEPTH_TRUECOLOR : static_cast<unsigned char>(d), &bytes);
      std::fwrite(bytes.p ? bytes.p : "", 1, bytes.n, stdout);
      rolltui_str_free(&bytes);
      rolltui_swap_free(swap);
      return 0;
    }
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    std::fwrite(text.p ? text.p : "", 1, text.n, stdout);
    rolltui_str_free(&text);
    rolltui_swap_free(swap);
    return 0;
  }
#endif

  RolltuiTerminalOptions opts{};
  RolltuiTerminal* term = rolltui_terminal_new(STDIN_FILENO, STDOUT_FILENO, opts);
  if (!rolltui_terminal_is_tty(term)) {
    rolltui_terminal_free(term);
    std::fprintf(stderr, "not a terminal (rolltui-paint-selftest --frame WxH renders one)\n");
    return 1;
  }
  app.w = rolltui_terminal_width(term);
  app.h = rolltui_terminal_height(term);

  // THE DOUBLE BUFFER IS THE LIBRARY'S.
  // What this replaces in all three hosts: `Frame prev; bool have_prev; … Frame f =
  // render(); write(render_diff(have_prev ? &prev : nullptr, f)); prev = std::move(f);`.
  // Two frames for the whole run, nothing owned inside the loop, and `begin` calls
  // `rolltui_frame_reset` — so this repaint lands INSIDE the budget rather than beside it.
  RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
  RolltuiStr out{};  // the caller's buffer, kept across frames and refilled — rule 3(b)
  Pending pending;   // likewise: one per run, refilled — see its definition
  int rc = 0;
  for (;;) {
    app.effect_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f);
    const int timeout = app.poll_timeout_ms(f, 250);
    out.clear();
    rolltui_swap_present(swap, ROLLTUI_DEPTH_TRUECOLOR, &out);
    rolltui_terminal_write(term, out.p ? out.p : "", out.n);

    pending.begin(app.w, app.h);
    rolltui_terminal_poll(term, timeout, collect_event, &pending);
    if (pending.quit) break;
    // The pointers only now, because `texts` reallocating would have dangled every earlier one.
    for (std::size_t i = 0; i < pending.events.size(); ++i)
      if (pending.text_of[i] != Pending::kNone) pending.events[i].text = pending.texts[pending.text_of[i]].data();
    for (const RolltuiEvent& e : pending.events) app.handle(e);
    if (pending.resized) {
      app.w = pending.w;
      app.h = pending.h;
    }
  }
  rolltui_str_free(&out);
  rolltui_swap_free(swap);
  rolltui_terminal_free(term);
  return rc;
}
