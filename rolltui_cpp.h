#ifndef ROLLTUI_CPP_H
#define ROLLTUI_CPP_H
/*
 * rolltui/rolltui_cpp.h — the C++ half of every type `rolltui.h` declares as plain C, so
 * `rolltui.h` itself can be just types, data and functions.
 *
 * C++ cannot add a method to a struct from a second header — a method, including a
 * destructor, has to be declared inside the struct's own original definition; there is no
 * reopening a class later. So a type's C++ half moves here in one of two shapes:
 *
 *   a free function      for anything that needs no membership at all: every `operator==`
 *                         here is one, in the global namespace so argument-dependent lookup
 *                         finds it — the same reason `str.hpp` puts RolltuiStr's `operator+=`/
 *                         `operator+` there rather than in `namespace rolltui`.
 *   rename + derive       for anything that DOES need member syntax (a static factory, an
 *                         instance method, a destructor). The plain-data struct keeps living in
 *                         `rolltui.h` under a `Raw` suffix (`RolltuiFoo` -> `RolltuiFooRaw`);
 *                         this file declares `class RolltuiFoo : public RolltuiFooRaw { ... };`
 *                         under the ORIGINAL name, in the global namespace, so it reclaims the
 *                         identifier every existing call site already uses. Public single
 *                         inheritance over a POD base with no virtual functions costs nothing —
 *                         identical `sizeof`, identical layout — and a `RolltuiFoo*` converts
 *                         implicitly to `RolltuiFooRaw*`, so a C function's existing signature
 *                         (`void rolltui_thing(..., RolltuiFooRaw* out)`) keeps compiling at
 *                         every call site with no change beyond this file being `#include`d.
 *
 * NOT `RolltuiStr` ITSELF, and not `str.hpp`'s job. `RolltuiStr`'s own minimal RAII stays
 * directly on the struct in `rolltui.h`, the same way it already did before this file existed —
 * it is the one type with far too much reach (hundreds of call sites) to move under this same
 * pass, and `str.hpp` already sits beside it as a separate, established convenience layer
 * (`StrView`, `StrVec`) rather than a replacement for its own destructor. This file complements
 * `str.hpp`; it does not merge with it or touch it.
 *
 * NOT the enum-class-vs-byte fields either (`RolltuiMouseEvent::kind` and its seven siblings):
 * those are a DATA FIELD typed differently per language, not a bolted-on method, and no
 * mechanism here moves a field's declared type without either duplicating it or turning a
 * direct field read into a method call everywhere for no real gain. They stay exactly as
 * `rolltui.h` already declares them.
 */

#include "rolltui/rolltui.h"

#endif /* ROLLTUI_CPP_H */
