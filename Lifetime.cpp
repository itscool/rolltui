// rolltui/Lifetime.cpp — see Lifetime.hpp. Phase 17 m1 moved this module's real logic into
// `rolltui/c/rolltui_lifetime.c`; everything that could become a header-only inline forward
// did (Lifetime.hpp). This one function could not:
//
// `rolltui::detail::on_thread_release` is DECLARED in `rolltui/Scratch.hpp`, on purpose
// without the `inline` specifier — that header's own comment says it stays "a template and
// nothing else" and deliberately does not include Lifetime.hpp. A function declared plainly
// in one header needs an ordinary, out-of-line, single-definition home if it is going to be
// *defined* rather than merely forwarded inline; that is what this translation unit is, and
// it is the one piece of this module that a header-only shim cannot absorb without either
// duplicating the definition (an ODR violation the moment two .cpp files pull it in) or
// reaching into Scratch.hpp to change what it promises about itself — out of scope here.
//
// The function itself is a one-line forward, same as everything in Lifetime.hpp: the actual
// per-thread registry (the growable arrays, the reverse-order release) lives in
// rolltui/c/rolltui_lifetime.c now, reachable as `rolltui_thread_on_release`.
#include "rolltui/Lifetime.hpp"

#include "rolltui/c/rolltui_lifetime.h"

namespace rolltui::detail {
void on_thread_release(void (*fn)(void*), void* target) { rolltui_thread_on_release(fn, target); }
}  // namespace rolltui::detail
