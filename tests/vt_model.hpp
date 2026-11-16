//
// vt_model.hpp — A TERMINAL, AS FAR AS THE RENDERER SPEAKS TO ONE: what a byte stream leaves on a grid of cells.
//
// The renderer chooses its bytes to be few: a relative move where it is shorter, an SGR that names only what changed, an erase for
// a run of blanks. Each of those is only right if the screen it leaves is the frame, so the suites that check the renderer replay
// what it wrote here and compare the grid with the frame, cell by cell. The model knows the sequences the renderer writes and treats
// anything else as an error, so a new sequence cannot slip past unmodelled.
//
// What it models: CUP, CUF, CUB, CUU, CUD, CR, LF (no scrolling: the renderer never writes past the last row), ECH and EL with
// background colour erase (an erased cell is a space in the current background and nothing else), SGR (the forms `rolltui_sgr`
// writes and the resets a delta uses), DECSTBM with SU and SD, OSC 8 hyperlinks, DECTCEM and synchronized output (2026), and text:
// clusters measured as the library measures them, written at the cursor, a wide one taking two cells, the right margin's
// pending wrap.
//
#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h" /* INTERNAL: a suite including this is in ROLLTUI_INTERNAL_OPT_IN */

namespace vt {

struct Cell {
  std::string glyph = " ";
  RolltuiStyle style{};
  std::string link;
  bool cont = false;
  bool erased = false; /* written by an erase: only its background is meaningful */
};

inline bool color_eq(const RolltuiStyleColorRaw& a, const RolltuiStyleColorRaw& b) {
  return a.kind == b.kind && (a.kind == RolltuiStyleColorRaw::Kind::None ||
                              (a.kind == RolltuiStyleColorRaw::Kind::Indexed ? a.index == b.index : a.r == b.r && a.g == b.g && a.b == b.b));
}
inline bool style_eq(const RolltuiStyle& a, const RolltuiStyle& b) {
  return color_eq(a.fg, b.fg) && color_eq(a.bg, b.bg) && !a.bold == !b.bold && !a.dim == !b.dim && !a.italic == !b.italic &&
         !a.underline == !b.underline && !a.reverse == !b.reverse && !a.strike == !b.strike;
}

class Screen {
 public:
  Screen(int w, int h) : w_(w), h_(h), cells_(static_cast<std::size_t>(w * h)), bottom_(h - 1) {}

  // Applies `bytes`; false with `error()` set at the first sequence the model does not know.
  bool apply(const std::string& bytes) {
    std::size_t used = 0;
    if (!parse(bytes, true, used)) return false;
    return true;
  }

  // A frame dump (`--frame-sgr`): the frame's bytes and a newline after them, which on the last row is nothing.
  bool apply_dump(const std::string& bytes) {
    dump_ = true;
    return apply(bytes);
  }

  // A LIVE TERMINAL'S STREAM, as a pty delivers it: in pieces that may end inside a sequence, and with what a program sends a
  // terminal besides its frames (modes, queries, the title). A piece's unfinished end waits for the next; a sequence that draws
  // nothing is passed over; a line feed on the last row scrolls nothing.
  void feed(const std::string& piece) {
    lenient_ = true;
    dump_ = true;
    held_ += piece;
    std::size_t used = 0;
    parse(held_, false, used);
    held_.erase(0, used);
  }

  // Every place `text` is drawn within one row: its first cell's (x, y).
  std::vector<std::pair<int, int>> find(const std::string& text) const {
    std::vector<std::pair<int, int>> at_cells;
    for (int y = 0; y < h_; ++y) {
      std::string row;
      std::vector<int> x_of; /* the cell each byte of `row` came from */
      for (int x = 0; x < w_; ++x) {
        const Cell& c = at(x, y);
        if (c.cont) continue;
        row += c.glyph;
        x_of.insert(x_of.end(), c.glyph.size(), x);
      }
      for (std::size_t p = row.find(text); p != std::string::npos; p = row.find(text, p + 1)) at_cells.emplace_back(x_of[p], y);
    }
    return at_cells;
  }

  const std::string& error() const { return error_; }
  const Cell& at(int x, int y) const { return cells_[static_cast<std::size_t>(y * w_ + x)]; }
  int cursor_x() const { return x_; }
  int cursor_y() const { return y_; }
  bool cursor_visible() const { return visible_; }

  // Where the grid is not what `f` would show, described; empty when it is.
  std::string differs_from(const RolltuiFrame* f) const {
    for (int y = 0; y < h_; ++y)
      for (int x = 0; x < w_; ++x) {
        RolltuiCellRaw raw{};
        rolltui_frame_cell(f, x, y, &raw);
        const Cell& c = at(x, y);
        if (raw.continuation) {
          if (!c.cont) return where(x, y) + "should be the right half of a wide glyph";
          continue;
        }
        if (c.cont) return where(x, y) + "is the right half of a wide glyph and should not be";
        std::size_t glen = 0;
        const char* g = rolltui_frame_glyph(f, x, y, &glen);
        const std::string want(glen ? std::string(g, glen) : std::string(" "));
        if (c.glyph != want) return where(x, y) + "shows '" + c.glyph + "', not '" + want + "'";
        std::size_t ulen = 0;
        const char* u = rolltui_frame_link(f, raw.link, &ulen);
        if (c.link != std::string(u ? u : "", ulen)) return where(x, y) + "links '" + c.link + "'";
        if (c.erased) { /* a space looks the same in any foreground: its background, and nothing drawn over it, is what shows */
          if (want != " " || raw.style.underline || raw.style.reverse || !color_eq(c.style.bg, raw.style.bg))
            return where(x, y) + "was erased, and the frame's cell is not a plain space on that background";
          continue;
        }
        if (!style_eq(c.style, raw.style)) return where(x, y) + "is in another style";
      }
    int fx = 0, fy = 0, fv = 0;
    rolltui_frame_cursor(f, &fx, &fy, &fv);
    if (!!fv != visible_) return "the cursor's visibility";
    if (fv && (fx != x_ || fy != y_)) return "the cursor is at " + std::to_string(x_) + "," + std::to_string(y_);
    return "";
  }

 private:
  std::string where(int x, int y) const { return "cell " + std::to_string(x) + "," + std::to_string(y) + " "; }
  bool fail(const std::string& why) {
    error_ = why;
    return false;
  }

  static std::vector<int> numbers(const std::string& p) {
    std::vector<int> v;
    std::size_t i = 0;
    if (p.empty()) return v;
    while (true) {
      const std::size_t e = p.find(';', i);
      const std::string part = p.substr(i, e == std::string::npos ? std::string::npos : e - i);
      v.push_back(part.empty() ? 0 : std::atoi(part.c_str()));
      if (e == std::string::npos) break;
      i = e + 1;
    }
    return v;
  }

  Cell blank() const {
    Cell c;
    c.style.bg = style_.bg; /* background colour erase */
    c.erased = true;
    return c;
  }

  void erase(int from, int to) {
    for (int x = from; x < to && x < w_; ++x) {
      Cell& c = cells_[static_cast<std::size_t>(y_ * w_ + x)];
      if (c.cont && x > 0) cells_[static_cast<std::size_t>(y_ * w_ + x - 1)] = blank();
      if (!c.cont && x + 1 < w_ && x + 1 >= to && cells_[static_cast<std::size_t>(y_ * w_ + x + 1)].cont)
        cells_[static_cast<std::size_t>(y_ * w_ + x + 1)] = blank();
      c = blank();
    }
  }

  void scroll(int n) { /* n > 0: the region's rows move up n, blank rows enter at its bottom */
    const int top = top_, bottom = bottom_;
    if (n > 0)
      for (int y = top; y <= bottom; ++y)
        for (int x = 0; x < w_; ++x)
          cells_[static_cast<std::size_t>(y * w_ + x)] = y + n <= bottom ? cells_[static_cast<std::size_t>((y + n) * w_ + x)] : blank();
    else
      for (int y = bottom; y >= top; --y)
        for (int x = 0; x < w_; ++x)
          cells_[static_cast<std::size_t>(y * w_ + x)] = y + n >= top ? cells_[static_cast<std::size_t>((y + n) * w_ + x)] : blank();
  }

  // Applies `b` from its start; `used` is how far. With `final` 0 an unfinished sequence or character at the end is left for later.
  bool parse(const std::string& b, bool final, std::size_t& used) {
    std::size_t i = 0;
    std::string text;
    auto flush_text = [&]() {
      if (!text.empty()) write_text(text);
      text.clear();
    };
    auto unfinished = [&](const char* why) {
      flush_text();
      used = i;
      return final ? fail(why) : true;
    };
    while (i < b.size()) {
      const unsigned char c = static_cast<unsigned char>(b[i]);
      if (c == 0x1b) {
        flush_text();
        if (i + 1 >= b.size()) return unfinished("a lone ESC at the end");
        const char kind = b[i + 1];
        if (kind == '[') {
          std::size_t j = i + 2;
          std::string params, mid;
          while (j < b.size() && b[j] >= 0x30 && b[j] <= 0x3f) params += b[j++];
          while (j < b.size() && b[j] >= 0x20 && b[j] <= 0x2f) mid += b[j++];
          if (j >= b.size()) return unfinished("an unfinished CSI");
          const bool known = mid.empty() && params.find_first_of("<=>") == std::string::npos && csi(params, b[j]);
          if (!known) {
            if (!lenient_) return error_.empty() ? fail("a CSI the model does not know: " + params + mid + b[j]) : false;
            error_.clear(); /* a live stream's query or mode: nothing drawn */
          }
          i = j + 1;
          continue;
        }
        if (kind == ']' || kind == 'P' || kind == '_' || kind == '^') { /* a string: to ST, or BEL for an OSC */
          std::size_t j = i + 2;
          while (j < b.size() && !(b[j] == '\x07' && kind == ']') && !(b[j] == 0x1b && j + 1 < b.size() && b[j + 1] == '\\')) ++j;
          if (j >= b.size() || (b[j] == 0x1b && j + 1 >= b.size())) return unfinished("an unterminated string");
          const std::string body = b.substr(i + 2, j - i - 2);
          const std::size_t next = b[j] == '\x07' ? j + 1 : j + 2;
          if (kind == ']' && body.rfind("8;", 0) == 0) {
            const std::size_t semi = body.find(';', 2);
            link_ = semi == std::string::npos ? std::string() : body.substr(semi + 1);
          } else if (!lenient_) {
            return fail("a string the model does not know: " + body.substr(0, 20));
          }
          i = next;
          continue;
        }
        if (!lenient_) return fail(std::string("an unknown escape: ESC ") + kind);
        i += 2; /* ESC 7, ESC =, and the like: nothing drawn */
        continue;
      }
      if (c == '\r') {
        flush_text();
        x_ = 0;
        pending_ = false;
        ++i;
        continue;
      }
      if (c == '\n') {
        flush_text();
        if (y_ + 1 >= h_ && dump_) { /* a frame dump's closing newline, or a live terminal's last row */
          pending_ = false;
          ++i;
          continue;
        }
        if (y_ + 1 >= h_) return fail("a line feed on the last row, which would scroll");
        ++y_;
        pending_ = false;
        ++i;
        continue;
      }
      if (c < 0x20) {
        if (!lenient_) return fail("a control byte " + std::to_string(c));
        ++i;
        continue;
      }
      if (!final && c >= 0x80) { /* a character cut off at the piece's end waits for the rest */
        const std::size_t n = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        if (i + n > b.size()) return unfinished("");
      }
      text += static_cast<char>(c);
      ++i;
    }
    flush_text();
    used = i;
    return true;
  }

  bool sgr(const std::string& params) {
    std::vector<int> v = numbers(params);
    if (v.empty()) v.push_back(0);
    for (std::size_t k = 0; k < v.size(); ++k) {
      const int p = v[k];
      auto color = [&](RolltuiStyleColorRaw& out) -> bool {
        if (k + 1 >= v.size()) return false;
        out = RolltuiStyleColorRaw{};
        if (v[k + 1] == 5 && k + 2 < v.size()) {
          out.kind = RolltuiStyleColorRaw::Kind::Indexed;
          out.index = static_cast<unsigned char>(v[k + 2]);
          k += 2;
          return true;
        }
        if (v[k + 1] == 2 && k + 4 < v.size()) {
          out.kind = RolltuiStyleColorRaw::Kind::Rgb;
          out.r = static_cast<unsigned char>(v[k + 2]);
          out.g = static_cast<unsigned char>(v[k + 3]);
          out.b = static_cast<unsigned char>(v[k + 4]);
          k += 4;
          return true;
        }
        return false;
      };
      auto indexed = [](int n) {
        RolltuiStyleColorRaw c{};
        c.kind = RolltuiStyleColorRaw::Kind::Indexed;
        c.index = static_cast<unsigned char>(n);
        return c;
      };
      if (p == 0) style_ = RolltuiStyle{};
      else if (p == 1) style_.bold = 1;
      else if (p == 2) style_.dim = 1;
      else if (p == 3) style_.italic = 1;
      else if (p == 4) style_.underline = 1;
      else if (p == 7) style_.reverse = 1;
      else if (p == 9) style_.strike = 1;
      else if (p == 29) style_.strike = 0;
      else if (p == 22) style_.bold = style_.dim = 0;
      else if (p == 23) style_.italic = 0;
      else if (p == 24) style_.underline = 0;
      else if (p == 27) style_.reverse = 0;
      else if (p >= 30 && p <= 37) style_.fg = indexed(p - 30);
      else if (p >= 90 && p <= 97) style_.fg = indexed(p - 90 + 8);
      else if (p >= 40 && p <= 47) style_.bg = indexed(p - 40);
      else if (p >= 100 && p <= 107) style_.bg = indexed(p - 100 + 8);
      else if (p == 39) style_.fg = RolltuiStyleColorRaw{};
      else if (p == 49) style_.bg = RolltuiStyleColorRaw{};
      else if (p == 38) {
        if (!color(style_.fg)) return fail("a 38 with no colour after it: " + params);
      } else if (p == 48) {
        if (!color(style_.bg)) return fail("a 48 with no colour after it: " + params);
      } else
        return fail("an SGR the model does not know: " + std::to_string(p));
    }
    return true;
  }

  bool csi(const std::string& params, char final) {
    if (!params.empty() && params[0] == '?') {
      if (params == "?25" && (final == 'h' || final == 'l')) visible_ = final == 'h';
      else if (params == "?2026" && (final == 'h' || final == 'l')) {
      } else
        return fail("a private mode the model does not know: " + params + final);
      return true;
    }
    const std::vector<int> v = numbers(params);
    auto arg = [&](std::size_t k, int def) { return k < v.size() && v[k] > 0 ? v[k] : def; };
    switch (final) {
      case 'H':
        y_ = arg(0, 1) - 1;
        x_ = arg(1, 1) - 1;
        if (x_ >= w_ || y_ >= h_) return fail("a CUP off the screen");
        pending_ = false;
        return true;
      case 'C': x_ = std::min(w_ - 1, x_ + arg(0, 1)); pending_ = false; return true;
      case 'D': x_ = std::max(0, x_ - arg(0, 1)); pending_ = false; return true;
      case 'A': y_ = std::max(0, y_ - arg(0, 1)); pending_ = false; return true;
      case 'B': y_ = std::min(h_ - 1, y_ + arg(0, 1)); pending_ = false; return true;
      case 'X': erase(x_, x_ + arg(0, 1)); return true;
      case 'K':
        if (arg(0, 0) != 0) return fail("an EL that is not to the end of the line");
        erase(x_, w_);
        pending_ = false;
        return true;
      case 'm': return sgr(params);
      case 'r':
        top_ = arg(0, 1) - 1;
        bottom_ = arg(1, h_) - 1;
        if (top_ < 0 || bottom_ >= h_ || top_ >= bottom_) return fail("a scrolling region out of bounds");
        x_ = 0, y_ = 0, pending_ = false; /* DECSTBM homes the cursor */
        return true;
      case 'S': scroll(arg(0, 1)); return true;
      case 'T': scroll(-arg(0, 1)); return true;
      default: return fail(std::string("a CSI the model does not know: ") + params + final);
    }
  }

  void write_text(const std::string& text) {
    RolltuiDrawScratch* s = rolltui_draw_scratch_new();
    RolltuiFrame* probe = rolltui_frame_new(static_cast<int>(text.size()) * 2 + 2, 1, RolltuiStyle{});
    /* the clusters and their widths, as the library measures them: drawn into a scratch row and read back */
    const int used = rolltui_frame_put_text(probe, s, 0, 0, text.data(), text.size(), RolltuiStyle{}, static_cast<int>(text.size()) * 2 + 2, 0, 0);
    for (int px = 0; px < used; ++px) {
      RolltuiCellRaw raw{};
      rolltui_frame_cell(probe, px, 0, &raw);
      if (raw.continuation) continue;
      std::size_t glen = 0;
      const char* g = rolltui_frame_glyph(probe, px, 0, &glen);
      if (glen == 0) break;
      put(std::string(g, glen), raw.width);
    }
    rolltui_frame_free(probe);
    rolltui_draw_scratch_free(s);
  }

  void put(const std::string& glyph, int width) {
    if (pending_) { /* the right margin's pending wrap: the next glyph goes to the next row's start */
      pending_ = false;
      x_ = 0;
      if (y_ + 1 < h_) ++y_;
    }
    Cell c;
    c.glyph = glyph;
    c.style = style_;
    c.link = link_;
    /* a wide glyph cut by this one goes whole: its left half before, its right half after */
    Cell& at = cells_[static_cast<std::size_t>(y_ * w_ + x_)];
    if (at.cont && x_ > 0) cells_[static_cast<std::size_t>(y_ * w_ + x_ - 1)] = blank();
    if (x_ + width < w_ && cells_[static_cast<std::size_t>(y_ * w_ + x_ + width)].cont)
      cells_[static_cast<std::size_t>(y_ * w_ + x_ + width)] = blank();
    at = c;
    if (width == 2 && x_ + 1 < w_) {
      Cell r;
      r.cont = true;
      r.style = style_;
      cells_[static_cast<std::size_t>(y_ * w_ + x_ + 1)] = r;
    }
    x_ += width;
    if (x_ >= w_) {
      x_ = w_ - 1;
      pending_ = true;
    }
  }

  bool dump_ = false, lenient_ = false;
  std::string held_;
  int w_, h_;
  std::vector<Cell> cells_;
  int bottom_;
  int top_ = 0;
  int x_ = 0, y_ = 0;
  bool pending_ = false, visible_ = true;
  RolltuiStyle style_{};
  std::string link_;
  std::string error_;
};

// HOW A PIECE OF TEXT LOOKS on a replayed screen: an RGB foreground and background as 0xRRGGBB, and bold; -1 for "any".
struct Look {
  long fg = -1, bg = -1;
  int bold = -1;
};
inline bool looks_like(const RolltuiStyle& s, const Look& l) {
  auto rgb = [](const RolltuiStyleColorRaw& c) {
    return c.kind == RolltuiStyleColorRaw::Kind::Rgb ? (static_cast<long>(c.r) << 16 | static_cast<long>(c.g) << 8 | c.b) : -2L;
  };
  return (l.fg < 0 || rgb(s.fg) == l.fg) && (l.bg < 0 || rgb(s.bg) == l.bg) && (l.bold < 0 || !s.bold == !l.bold);
}
// The cells of one place `text` is drawn, from its first.
inline std::vector<const Cell*> cells_of(const Screen& s, std::pair<int, int> at, const std::string& text) {
  std::vector<const Cell*> out;
  std::size_t bytes = 0;
  for (int x = at.first; bytes < text.size(); ++x) {
    const Cell& c = s.at(x, at.second);
    if (c.cont) continue;
    out.push_back(&c);
    bytes += c.glyph.size();
  }
  return out;
}
// Somewhere `text` is drawn with every cell of it in `look`.
inline bool drawn(const Screen& s, const std::string& text, const Look& look) {
  for (const auto& at : s.find(text)) {
    bool all = true;
    for (const Cell* c : cells_of(s, at, text)) all = all && looks_like(c->style, look);
    if (all) return true;
  }
  return false;
}
// Somewhere `text` is drawn in one style throughout: one run, not coloured in parts.
inline bool uniform(const Screen& s, const std::string& text) {
  for (const auto& at : s.find(text)) {
    const std::vector<const Cell*> cs = cells_of(s, at, text);
    bool one = true;
    for (const Cell* c : cs) one = one && style_eq(c->style, cs.front()->style);
    if (one) return true;
  }
  return false;
}

}  // namespace vt
