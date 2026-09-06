#ifndef ROLLTUI_C_FRAME_OPS_H
#define ROLLTUI_C_FRAME_OPS_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_frame_ops.h — THE THREE DRAWING LOOPS, IN C (Phase 15 m5).
 *
 * `put_text`, `fill` and `tint`: the loops every widget draws through, built out of the
 * frame's primitives (`rolltui_screen.h`) and the Unicode module's cluster walk. They were
 * three methods on `rolltui::Frame` in `Screen.cpp`, which is the SHIM and therefore C++ in
 * both configurations — fine while only C++ drew, and impossible the moment the layout and
 * the widgets became C. They are here, and were compiled into both configurations for the same
 * reason `rolltui_md_lines.c` is: one implementation of "write this text into that row",
 * not one per language.
 *
 * ---- THE SCRATCH IS A HANDLE, AND THAT IS THE POINT ------------------------------------
 *
 * `Frame::put_text` kept its cluster buffer in a `static thread_local`, which is Phase 14's
 * design lens exactly: **nobody decided the buffer's lifetime — `thread_local` made it not
 * need one, so no one was ever asked who owned it.** CLAUDE.md's strategy 3, as amended on
 * 2026-09-04, says what to do instead: *"this function needs somewhere to work" is not a new
 * strategy, it is a missing handle.* So every function here takes a `RolltuiDrawScratch*`
 * the CALLER owns, and the C++ shim owns exactly one per thread through `ThreadHandle`
 * (`rolltui/Scratch.hpp`) — one owner, named, released at thread exit and at
 * `release_thread()`.
 *
 * One buffer per ROLE inside it, so a function that calls another cannot alias its own
 * caller's scratch: the cluster array and the Unicode module's own working memory are two
 * fields and not one.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
