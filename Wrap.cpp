// rolltui/Wrap.cpp — the C++ side of the wrap engine. See Wrap.hpp for the contract and the
// rules; `WrapCpp.cpp` and `c/rolltui_wrap.c` are the two implementations of it, one CMake
// flag apart (plan/phase-14.md m3).
//
// There is very little here, which is the shape m2 arrived at for `Screen.cpp` too: the two
// entry points differ only in WHERE the answer ends up, and both are three lines.
#include "rolltui/Wrap.hpp"

namespace rolltui {

WrapLines wrap(std::string_view utf8, int width, const WrapOptions& opt) {
  // TWO SCRATCHES, NOT ONE, and the reason is a hazard rather than a preference: this
  // function must be safe to call while somebody upstream is holding a `wrap_borrow`
  // window — `Markdown` runs a HOST's highlighter with lines in hand — and one shared
  // buffer would turn that into an abort. Nothing inside the engine calls back out, so
  // this one cannot nest with itself.
  static thread_local Scratch<WrapLines> scratch("wrap build");
  auto built = scratch.lock();
  built->wrap(utf8, width, opt);
  WrapLines out;
  out.assign(*built);  // the LINES only; the warm decode and break buffers stay behind
  return out;
}

Scratch<WrapLines>::Lock wrap_borrow(std::string_view utf8, int width, const WrapOptions& opt) {
  static thread_local Scratch<WrapLines> scratch("wrap lines");
  auto lock = scratch.lock();
  lock->wrap(utf8, width, opt);
  return lock;
}

}  // namespace rolltui
