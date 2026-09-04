// rolltui/Unicode.cpp — THE C++ SHAPE of the Unicode algorithms. See Unicode.hpp for the
// contracts and rolltui/tests/ for the conformance suites that hold them; the algorithms
// themselves are behind `rolltui/c/rolltui_unicode.h`, in `UnicodeCpp.cpp` or
// `c/rolltui_unicode.c`, one CMake flag apart (plan/phase-14.md m5).
//
// Everything here is a container built around a caller-buffer call, and every one of them
// can size that buffer WITHOUT asking first — a decode yields at most one scalar per byte,
// a boundary array is n + 1, a strip only ever shrinks. That is why the boundary has no
// measure-then-fill round trip anywhere: the bounds are properties of the algorithms, and
// stating them at the header was most of the design work.
#include "rolltui/Unicode.hpp"

namespace rolltui::unicode {
namespace {

// ONE HANDLE PER THREAD, and no caller outside this file ever sees it. The six algorithms
// that need working memory take it (rolltui/c/rolltui_unicode.h); it grows to a high-water
// mark over the first few calls and never allocates again. C++ owns the lifetime the way it
// owns every other one here — a `thread_local` with a destructor — so there is nothing for a
// host to initialise and nothing left to clean up at thread exit.
struct ScratchOwner {
  RolltuiUnicodeScratch* p = rolltui_u_scratch_new();
  ScratchOwner() = default;
  ScratchOwner(const ScratchOwner&) = delete;
  ScratchOwner& operator=(const ScratchOwner&) = delete;
  ~ScratchOwner() { rolltui_u_scratch_free(p); }
};

RolltuiUnicodeScratch* scratch() {
  static thread_local ScratchOwner owner;
  return owner.p;
}

}  // namespace

// ---- UTF-8 -------------------------------------------------------------------------

DecodedChar decode_one(std::string_view s, std::size_t pos) {
  DecodedChar d{};
  rolltui_u_decode_one(s.data(), s.size(), pos, &d);
  return d;
}

void decode_utf8_into(std::string_view s, std::vector<DecodedChar>& out) {
  out.resize(s.size());  // at most one scalar per byte; shrunk to the truth below
  const std::size_t n = rolltui_u_decode_utf8_chars(s.data(), s.size(), out.data());
  out.resize(n);         // capacity kept, which is what makes the reused form free
}

std::vector<DecodedChar> decode_utf8(std::string_view s) {
  std::vector<DecodedChar> out;
  decode_utf8_into(s, out);
  return out;
}

void append_utf8(std::string& out, char32_t cp) {
  char buf[4];
  const std::size_t n = rolltui_u_append_utf8(cp, buf);
  out.append(buf, n);
}

// ---- width -------------------------------------------------------------------------

int codepoint_width(char32_t cp, bool ambiguous_wide) { return rolltui_u_codepoint_width(cp, ambiguous_wide); }

int cluster_width(std::span<const char32_t> cps, bool ambiguous_wide) {
  return rolltui_u_cluster_width(cps.data(), cps.size(), ambiguous_wide);
}

int display_width(std::string_view utf8, bool ambiguous_wide) {
  return rolltui_u_display_width(scratch(), utf8.data(), utf8.size(), ambiguous_wide);
}

// ---- UAX #29 -----------------------------------------------------------------------

// `std::vector<bool>` is a bitset and the boundary fills bytes, so this one stages. It is
// the conformance suites' shape and a cold path; the hot callers go through
// `graphemes_into`, which fills its caller's array with no staging at all.
std::vector<bool> grapheme_boundaries(std::span<const char32_t> cps) {
  std::vector<unsigned char> bytes(cps.size() + 1);
  rolltui_u_grapheme_boundaries(scratch(), cps.data(), cps.size(), bytes.data());
  return std::vector<bool>(bytes.begin(), bytes.end());
}

std::vector<bool> word_boundaries(std::span<const char32_t> cps) {
  std::vector<unsigned char> bytes(cps.size() + 1);
  rolltui_u_word_boundaries(scratch(), cps.data(), cps.size(), bytes.data());
  return std::vector<bool>(bytes.begin(), bytes.end());
}

void graphemes_into(std::string_view utf8, bool ambiguous_wide, std::vector<Grapheme>& out) {
  out.resize(utf8.size());  // there can be no more clusters than bytes
  const std::size_t n = rolltui_u_graphemes(scratch(), utf8.data(), utf8.size(), ambiguous_wide, out.data());
  out.resize(n);            // capacity kept: a reused buffer never reallocates
}

std::vector<Grapheme> graphemes(std::string_view utf8, bool ambiguous_wide) {
  std::vector<Grapheme> out;
  graphemes_into(utf8, ambiguous_wide, out);
  return out;
}

ByteRange word_range(std::string_view utf8, std::size_t offset) {
  ByteRange r;
  rolltui_u_word_range(scratch(), utf8.data(), utf8.size(), offset, &r.begin, &r.end);
  return r;
}

// ---- sanitising ---------------------------------------------------------------------

std::string strip_escape_sequences(std::string_view text) {
  std::string out(text.size(), '\0');  // stripping only ever removes
  const std::size_t n = rolltui_u_strip_escape_sequences(text.data(), text.size(), out.data());
  out.resize(n);
  return out;
}

// ---- UAX #14 -------------------------------------------------------------------------

std::vector<Break> line_break_opportunities(std::span<const char32_t> cps) {
  std::vector<Break> out(cps.size() + 1);
  // `Break` is a one-byte enum with the boundary's three values, asserted in Unicode.hpp,
  // so the array IS the byte array the boundary fills. No staging and no conversion.
  static_assert(sizeof(Break) == 1, "Break must be one byte for the boundary to fill this directly");
  rolltui_u_line_break_opportunities(scratch(), cps.data(), cps.size(),
                                     reinterpret_cast<unsigned char*>(out.data()));
  return out;
}

LineBreaks line_breaks(std::string_view utf8) {
  LineBreaks r;
  r.chars = decode_utf8(utf8);
  std::vector<char32_t> cps(r.chars.size());
  for (std::size_t i = 0; i < cps.size(); ++i) cps[i] = r.chars[i].cp;
  r.before = line_break_opportunities(cps);
  return r;
}

}  // namespace rolltui::unicode
