//
// ownership_test.cpp — Phase 13 m2: OWNERSHIP IS A STATED RULE WITH A GREP CONTROL.
//
// THE RULE, and it is true of `rolltui/` today rather than aspirational — which is the
// whole reason it is worth freezing:
//
//   OWNED     exactly one owner, and it is a `unique_ptr` or a value member. `Windows`
//             owns its widgets (`map<string, unique_ptr<Widget>>`); `WindowStack` owns
//             its layers BY VALUE; a `Frame` owns its cells.
//   BORROWED  every raw `T*` and every `string_view` in a public header. A borrow never
//             owns, never frees, and must not outlive the caller's frame — a bound
//             `const Document*` is the HOST's document, and the host outliving the
//             binding is the host's rule to keep.
//   VALUE     everything else. Layers, Styles, Contents, Marks: copied, not referenced.
//
// **THERE IS NO THIRD OWNERSHIP SHAPE, and in particular there is no shared one.** A
// `shared_ptr` makes a lifetime a runtime question, and every lifetime in this library is
// structural: a widget outlives every layout that names it, a document belongs to the
// host, a layer is a value inside a stack. The user put it as "shared_ptr webs are a no
// no" (2026-09-03); the finding that made it cheap to adopt is that the library already
// had none, so this file freezes a property rather than demanding a migration.
//
// TWO CONTROLS, because an ABSENCE check that has stopped working looks exactly like an
// absence:
//   1. the scanner is run over a string that DOES contain the pattern, and must find it;
//   2. the borrowed-pointer census is a recorded NUMBER per header, so a new raw pointer
//      fails the test until someone writes down what its lifetime is. That is the point:
//      the failure is not "you may not add a pointer", it is "say what it borrows".
//
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui_test.hpp"

using namespace rolltui_test;
namespace fs = std::filesystem;

#ifndef ROLLTUI_SOURCE_DIR
#error "ROLLTUI_SOURCE_DIR must point at rolltui/"
#endif

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Every source the library is BUILT from: its own .hpp/.cpp and its tools'. Tests,
// vendored code and the generated Unicode tables are excluded by directory — a test may
// hold a `shared_ptr` (the hosts do, legitimately), and md4c is not ours to rule on.
std::vector<std::string> library_sources(bool headers_only) {
  std::vector<std::string> out;
  for (const fs::directory_entry& e : fs::recursive_directory_iterator(ROLLTUI_SOURCE_DIR)) {
    const std::string p = e.path().string();
    const std::string rel = p.substr(std::string(ROLLTUI_SOURCE_DIR).size() + 1);
    if (rel.rfind("tests/", 0) == 0 || rel.rfind("third_party/", 0) == 0 || rel.rfind("ucd/", 0) == 0 ||
        rel.rfind("presets/", 0) == 0)
      continue;
    // The generated tables are a .h/.c pair since Phase 14 m5, so the extension filter
    // below already skips them; the name check stays as the statement of intent.
    if (rel == "unicode_tables.h" || rel == "unicode_tables.c") continue;  // generated
    const std::string ext = e.path().extension().string();
    if (headers_only ? ext != ".hpp" : (ext != ".hpp" && ext != ".cpp")) continue;
    if (!headers_only && rel.rfind("tools/", 0) == 0) continue;  // a TOOL is a host, not the library
    out.push_back(rel);
  }
  return out;
}

// A raw-pointer DECLARATION: `Foo* bar`, `const Foo* bar`, `Foo** bar`. Deliberately not
// `Foo *bar` — this library never writes it that way, and a pattern that matches both
// would also match every multiplication. Comment lines are skipped by the caller.
const std::regex& pointer_decl() {
  static const std::regex r(R"(\b[A-Za-z_][A-Za-z0-9_:]*\*+\s+[a-z_][A-Za-z0-9_]*)");
  return r;
}

bool is_comment(const std::string& line) {
  const std::size_t at = line.find_first_not_of(" \t");
  if (at == std::string::npos) return true;
  return line.compare(at, 2, "//") == 0 || line.compare(at, 2, "/*") == 0 || line[at] == '*';
}

}  // namespace

int main() {
  // ---- THE RULE: no shared ownership anywhere in the library ------------------------
  {
    const std::regex shared(R"(\bstd::(shared_ptr|weak_ptr|enable_shared_from_this)\b|\bmake_shared\b)");
    std::vector<std::string> hits;
    const std::vector<std::string> files = library_sources(/*headers_only=*/false);
    for (const std::string& rel : files) {
      const std::string text = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/" + rel);
      std::istringstream in(text);
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        if (std::regex_search(line, shared)) hits.push_back(rel + ":" + std::to_string(ln) + ":" + line);
      }
    }
    check(files.size() >= 25, "scanned the library's own sources (" + std::to_string(files.size()) + " files)");
    check(hits.empty(), "NO SHARED OWNERSHIP in rolltui: every lifetime here is structural" +
                            (hits.empty() ? "" : " — " + hits.front()));
    // CONTROL 1: the scanner finds the pattern when it is there. An absence check that has
    // silently stopped matching is indistinguishable from an absence.
    const std::string planted = "  std::shared_ptr<Widget> w = std::make_shared<Widget>();";
    check(std::regex_search(planted, shared), "…and the pattern matches a planted shared_ptr, so the control is live");
  }

  // ---- the same rule one level down: the library never news or deletes by hand -------
  {
    const std::regex manual(R"(^\s*(?!.*//).*\b(new|delete)\s+[A-Za-z_])");
    std::vector<std::string> hits;
    for (const std::string& rel : library_sources(false)) {
      const std::string text = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/" + rel);
      std::istringstream in(text);
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        if (is_comment(line)) continue;
        // `new` inside a make_unique is the sanctioned form; `operator new` is a
        // declaration, not a use.
        if (line.find("make_unique") != std::string::npos || line.find("operator new") != std::string::npos ||
            line.find("operator delete") != std::string::npos || line.find("= delete") != std::string::npos)
          continue;
        if (std::regex_search(line, manual)) hits.push_back(rel + ":" + std::to_string(ln) + ":" + line);
      }
    }
    check(hits.empty(), "…and no hand-rolled new/delete either: OWNED means unique_ptr or a value" +
                            (hits.empty() ? "" : " — " + hits.front()));
  }

  // ---- THE C SIDE'S CLOSED SET: growth has exactly one home --------------------------
  // Phase 14 m4. CLAUDE.md's rule is that every allocation is a CHOICE from a closed set and
  // never an invention, and that in C the rule can be TOTAL because every allocation is an
  // explicit call. We had the entry point (`rolltui_mem_*`) and not the set, and by the end
  // of m3 the two ported C files had invented the same growing buffer EIGHT times with two
  // different policies — Phase 13's finding reproduced exactly, one language over.
  //
  // `rolltui_mem_realloc` is how growth is spelled, so the rule is one line: only
  // `rolltui_alloc.c` may call it. `alloc` and `free` stay available everywhere, because a
  // handle and its release are not a strategy anyone can get subtly wrong. **The point is not
  // that growth is hard — it is that eight sites is eight places to have a different policy,
  // and two of them did.**
  {
    const std::regex growth(R"(\brolltui_mem_realloc\s*\()");
    std::vector<std::string> hits;
    int scanned = 0;
    for (const fs::directory_entry& e : fs::directory_iterator(std::string(ROLLTUI_SOURCE_DIR) + "/c")) {
      const std::string name = e.path().filename().string();
      const std::string ext = e.path().extension().string();
      if (ext != ".c" && ext != ".h") continue;
      if (name == "rolltui_alloc.c" || name == "rolltui_alloc.h") continue;  // the one home
      ++scanned;
      std::istringstream in(read_file(e.path().string()));
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        if (is_comment(line)) continue;
        if (std::regex_search(line, growth)) hits.push_back(name + ":" + std::to_string(ln) + ":" + line);
      }
    }
    check(scanned >= 5, "scanned the library's C sources (" + std::to_string(scanned) + " files)");
    check(hits.empty(), "GROWTH HAS ONE HOME: no C file outside rolltui_alloc.c grows a buffer itself" +
                            (hits.empty() ? "" : " — " + hits.front()));
    // CONTROL: the scanner finds the pattern when it is there. An absence check that has
    // stopped matching is indistinguishable from an absence — the same rule as the two above.
    check(std::regex_search(std::string("  f->cells = rolltui_mem_realloc(f->cells, n * 2);"), growth),
          "…and the pattern matches a planted growth call, so the control is live");
  }

  // ---- BORROWED: the census ----------------------------------------------------------
  // Every raw pointer in a public header is a BORROW, and this is the tripwire that keeps
  // it that way: the count per header is RECORDED, so adding one fails until an author
  // updates the number — at which point they have had to look at it and say what it
  // borrows. The failure is never "you may not add a pointer".
  //
  // MEASURED 2026-09-03. A count that goes DOWN fails too, for the same reason the budget
  // does: it is either a real simplification (re-record, deliberately) or the scanner
  // having stopped seeing.
  {
    struct Row {
      const char* header;
      int pointers;
    };
    // MEASURED 2026-09-03 by this test's own scanner (it prints the table it wants, so a
    // re-record is a copy-paste and never arithmetic). Terminal.hpp is 0 because m2 turned
    // its one hand-rolled owner into a `unique_ptr`.
    const Row recorded[] = {
        // Bindings.hpp 1 → 2, RE-RECORDED 2026-09-04 by Phase 15 m3, when the binding table
        // became a handle to storage the C owns. The new one is `RolltuiBindings* p` in
        // `Bindings::Handle` — the deleter of that OWNED handle, a `unique_ptr`'s deleter
        // rather than a member, the same sanctioned shape `Frame::Handle` uses. The one
        // that was already here is `std::string* moved_from`, the optional out-parameter on
        // `bind()`.
        // Bindings.hpp 2 → 3, RE-RECORDED 2026-09-04 by Phase 15 m5. The new one is
        // `const RolltuiBindings* handle() const` — a BORROW of the table this object owns,
        // handed to `rolltui_window_stack_route` for the length of one call and never
        // stored. It exists because the stack's Escape/Tab rules are behind the C boundary
        // now and have to ask the live table what the `stack` scope binds; the words those
        // three actions are called stay in `Layout.cpp` and are handed over with them.
        {"AppProfile.hpp", 0},   {"Bindings.hpp", 3},      {"Diff.hpp", 0},        {"Document.hpp", 0},
        // Effects.hpp 2 → 1, RE-RECORDED 2026-09-04 by Phase 15 m2, and this is the census
        // catching a REMOVAL, which it is meant to do just as loudly as an addition. The
        // pointer was `const EffectFn* effect_kind(std::string_view)`. A resolved kind is a
        // function and a context inside the registry now, not an object with an address, so
        // there is nothing to hand back a pointer TO — and every caller only ever asked
        // whether it resolved, which `effect_kind_resolves` answers with a bool. The one
        // left is `std::string* why`, the optional reason-out on `register_effect_kind`.
        //
        // Effects.hpp 1 → 3, RE-RECORDED 2026-09-04 by Phase 15 m3, when `EffectMap` became
        // a handle to storage the C owns (the m2 seam closing). Both new ones are that
        // handle and neither is a borrow:
        //   - `RolltuiEffectMap* p` in `EffectMap::Handle` — the deleter of the OWNED C
        //     map, a `unique_ptr`'s deleter rather than a member, the same sanctioned shape
        //     `Frame::Handle` and `KeyDecoder::Handle` already use.
        //   - `const RolltuiEffectMap* handle() const` — what `apply_effects` hands the
        //     applier. A BORROW of the map this object owns, valid for the call and never
        //     stored; it exists because the applier reads the theme's specs directly now
        //     instead of being handed views rebuilt from them every frame.
        // Keys.hpp 0 → 1, RE-RECORDED 2026-09-04 by Phase 15 m3. The one pointer is
        // `RolltuiKeyDecoder* p` in `KeyDecoder::Handle` — the deleter of the decoder's
        // OWNED C handle, the same sanctioned shape `Frame::Handle` already uses, and a
        // `unique_ptr`'s deleter rather than a member. It borrows nothing.
        // Input.hpp 0 → 1, RE-RECORDED 2026-09-04 by Phase 15 m5. The one pointer is
        // `RolltuiInput* p` in `Input::Handle` — the deleter of the widget's OWNED C handle,
        // a `unique_ptr`'s deleter rather than a member, the same sanctioned shape
        // `Frame::Handle` and four others already use. It borrows nothing. The header's
        // other change is not a pointer at all: `text()` and `editing_text()` hand back a
        // `std::string_view` where they used to hand back a `const std::string&`, which is
        // the borrow the boundary forces and states its window for.
        // Input.hpp 1 → 4 (Phase 15 m5, second pass): the three new ones all BORROW and none
        // owns. `RolltuiInput* handle()` and its const overload hand the editor's OWNED
        // handle to the one other widget that embeds one — a menu's typed field — so there
        // is one owner and not two; `const RolltuiInputActions* input_actions()` is a BORROW
        // of the thirty action names, so the menu boundary is handed a pointer to the
        // library's one table rather than a copy of it.
        {"Effects.hpp", 3},      {"Input.hpp", 4},         {"Json.hpp", 0},        {"Keys.hpp", 1},
        // Markdown.hpp 0 → 10, RE-RECORDED 2026-09-04 by Phase 15 m4, and this is the census
        // recording the milestone's whole shape change: a `Span` used to OWN a
        // `std::string` and two vectors, so the header needed no pointer to say so. Every
        // line, span and byte lives in the caller's store now, and the parsed document is a
        // handle too. The ten are what that costs a reader:
        //   - `RolltuiMdLines* p` in `Rendered::Handle` — the deleter of the OWNED store, a
        //     `unique_ptr`'s deleter rather than a member, the same sanctioned shape
        //     `Frame::Handle`, `KeyDecoder::Handle` and `EffectMap::Handle` already use.
        //   - `RolltuiMdLines* store()` and `const RolltuiMdLines* store() const` — a BORROW
        //     of the store this object owns, for the one caller that builds lines of its own
        //     behind the rendered ones (the transcript's prefix). Never stored.
        //   - `const RolltuiMdLine* p` in `lines()` and `const RolltuiMdCodeBlock* p` in
        //     `code_blocks()` — the arrays those two `std::span`s are made of, BORROWED from
        //     the store and valid until the next render into it.
        //   - `const char* p` in `clamped()` — the same, for one report string.
        //   - `char* out` on `code_block_summary` — the CALLER'S buffer, filled and not
        //     returned, which is why that function stopped handing back a `std::string`.
        //   - `RolltuiMdDoc* p` in `Document::Handle` — the third of these deleters, for the
        //     OWNED parse tree, which became a handle when the block tree left this header.
        //   - `const RolltuiMdDoc* handle() const` — a BORROW of the tree this object owns,
        //     for `Rendered::render` and nothing else. Never stored.
        //   - `const char* p` in `Document::block_code` — a BORROW of one Code block's
        //     verbatim text, valid until the document is parsed into again.
        // Marker.hpp 0 → 1, RE-RECORDED 2026-09-04 by Phase 15 m4. The one pointer is
        // `char* out` on `scroll_marker_text_into` — the CALLER'S buffer. The marker's rule
        // moved to `rolltui/c/rolltui_marker.h` (its third caller became C, and the note on
        // it has always said the rule must have exactly one definition), and this header is
        // the C++ spelling over it: the `std::string` form for the callers that want one,
        // and the buffer form for a draw path that must not build one per frame.
        // Menu.hpp 7 → 4, RE-RECORDED 2026-09-04 by Phase 15 m5, and this is the census
        // catching a REMOVAL — which it is meant to do just as loudly as an addition. The
        // three that LEFT were `const MenuItem*` and `MenuItem*` accessors returning into a
        // `std::vector<MenuItem>` the widget owned; the tree is a C tree now and those same
        // accessors hand back a node whose address is STABLE, which is the property the
        // vector could not promise. The four left are `RolltuiMenu* p` in `Menu::Handle`
        // (the deleter of the OWNED widget) and three `MenuItem*`/`const MenuItem*`
        // borrows — `find()` twice and `selected_item()`.
        {"Layout.hpp", 8},       {"Markdown.hpp", 10},     {"Marker.hpp", 1},      {"Memory.hpp", 3},       {"Menu.hpp", 4},
        // Lifetime.hpp, NEW 2026-09-04 (Phase 14 m6a). Zero raw pointers: `shutdown()` and
        // `release_thread()` take nothing and return nothing, and `on_shutdown` takes a
        // FUNCTION pointer, which the scanner's pattern does not match and which borrows
        // nothing — a releaser names a static its own module already owns.
        {"Lifetime.hpp", 0},
        // Screen.hpp 1 → 4, RE-RECORDED 2026-09-04 by Phase 14 m2, which is what this row
        // is FOR: the number moved, so somebody had to say what each new pointer is.
        //   - `RolltuiFrame* p` in `Frame::Handle` — the deleter of the frame's OWNED
        //     handle. The only one here that is not a borrow, and it is a `unique_ptr`'s
        //     deleter rather than a member, which is the sanctioned shape for OWNED.
        //   - `const char* p` twice, in `glyph()` and `link()` — BORROWS from the frame,
        //     turned into a `string_view` in the same expression and never stored. The
        //     window is stated at the C header: valid until that cell is written again.
        // Scratch.hpp 4 → 6, RE-RECORDED 2026-09-04 by Phase 14 m6a. Both new ones are the
        // per-thread release registration, and neither owns anything:
        //   - `void (*fn)(void*), void* target` on `detail::on_thread_release` — a releaser
        //     and the buffer it releases, BORROWED for the life of the thread's registry,
        //     which is cleared by `release_thread()` before any of them could dangle.
        //   - `void* p` in the captureless lambda that casts it back to the Scratch — the
        //     same borrow, one frame later.
        // Scratch.hpp 6 → 9 and Screen.hpp 4 → 6, RE-RECORDED 2026-09-04 by Phase 15 m2.
        // Scratch.hpp's three are all `ThreadHandle`, the per-thread C handle the ported
        // modules' working memory lives in (`Unicode.cpp` hand-wrote this for Phase 14 m5;
        // `Diff.cpp` and `Effects.cpp` were about to be copies two and three):
        //   - `T* p_` — the one pointer here that OWNS. It is a `unique_ptr` in spirit and
        //     not in fact because the boundary's free is a C function taken as a template
        //     parameter; the class is move-less, copy-less and its destructor is the only
        //     other way out, which is the same guarantee with the deleter named up front.
        //   - `T* get()` — a BORROW of that handle, handed to the boundary for one call.
        //   - `void* h` in the captureless release lambda — the same borrow one frame
        //     later, cast back to the handle, exactly as `Scratch`'s already is.
        // Screen.hpp's two are `RolltuiFrame* handle()` and its const overload: a BORROW of
        // the handle the Frame OWNS, so that `Effects.cpp` can hand the frame to an applier
        // written in the other language. Never stored; the window is the Frame's lifetime.
        {"Presets.hpp", 1},      // PresetStore.hpp 3 → 25, RE-RECORDED 2026-09-04 by Phase 15 m3, and this row is the
        // milestone's own measurement rather than an accounting chore. The template became
        // an ADAPTER onto `rolltui/c/rolltui_presets.h`, and a C boundary over a generic
        // container is nothing BUT pointers: twenty-two of the twenty-five are parameters
        // of the captureless lambdas that make up one `RolltuiPresetDomain` — `const char*
        // text` and `void* report` on `parse`, `const void* value` on `clone`/`equal`,
        // `void* ctx` on every `put` sink. Every one of them BORROWS for the duration of
        // its call and none is stored; the two that are not parameters are
        // `RolltuiPresetStore* p` in `Handle` (the OWNED store's deleter) and the
        // `RolltuiPresetDomain&`-returning accessors' internals. The C++ template got all
        // of this from `Value` and `PresetLoadReport&` and cost three.
        {"PresetStore.hpp", 25},   {"Scratch.hpp", 9},     {"Screen.hpp", 6},
        {"Style.hpp", 0},
        {"Terminal.hpp", 0},     {"Theme.hpp", 2},         {"ThemeAnalysis.hpp", 0}, {"ThemeGen.hpp", 0},
        {"Transcript.hpp", 3},   {"Undo.hpp", 0},          {"Widgets.hpp", 10},
        // Unicode.hpp 1 → 0, RE-RECORDED 2026-09-04 by Phase 14 m5, and this is the census
        // catching a REMOVAL — which it is meant to do just as loudly as an addition. The
        // pointer was `const Range* table` on `lookup()`, the binary search the inline
        // property accessors used. The accessors go through the boundary now, so `lookup`
        // had no callers and left with them; the header no longer hands out a pointer at all.
        {"Unicode.hpp", 0},
        // Wrap.hpp 2 → 5, RE-RECORDED 2026-09-04 by Phase 14 m3, same as Screen.hpp above:
        // the number moved, so somebody had to say what each new pointer is. The two that
        // LEFT were `const Line* begin()/end()` — a line is built on read now, so the
        // iterator is an index rather than a pointer into an array that no longer exists.
        //   - `RolltuiWrapLines* p` in `WrapLines::Handle` — the deleter of the OWNED
        //     handle, a `unique_ptr`'s deleter rather than a member, which is the
        //     sanctioned shape for OWNED and the only non-borrow here.
        //   - `const char* text` and `const WrapGrapheme* graphemes` in `operator[]` —
        //     BORROWS out of the handle, turned into a `string_view` and a `span` in the
        //     same expression and never stored. The window is stated at the C header:
        //     valid until that handle is wrapped into again, reset or destroyed.
        //   - `const WrapLines* w` / `w_` in the iterator (one declaration each, the
        //     parameter and the member) — a BORROW of the container being iterated, which
        //     by construction outlives the iterator.
        //   - `RolltuiWrapLines* owned` in the private constructor — the one pointer here
        //     whose name is the whole point: it TAKES OWNERSHIP of a handle the boundary
        //     just minted, and hands it straight to the `unique_ptr`. It is private so that
        //     the only way to get one is `clone()`, which is the only place a raw handle
        //     ever exists as a value in this header.
        //   - 6 → 7, 2026-09-04 (Phase 14 m6a): `RolltuiWrapLines* handle() const`, the
        //     make-on-first-use accessor. A BORROW of the handle this object owns, handed
        //     out only inside the class — the handle exists lazily so that a default-
        //     constructed WrapLines holds nothing, which is what lets `Scratch` release its
        //     storage to actually zero.
        {"Wrap.hpp", 7},
    };
    int total = 0, checked = 0;
    std::vector<std::string> unlisted;
    for (const std::string& rel : library_sources(/*headers_only=*/true)) {
      if (rel.find('/') != std::string::npos) continue;  // tools/ headers are a host's
      int n = 0;
      const std::string text = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/" + rel);
      std::istringstream in(text);
      std::string line;
      while (std::getline(in, line)) {
        if (is_comment(line)) continue;
        if (std::regex_search(line, pointer_decl())) ++n;
      }
      total += n;
      // The table above is PRINTED on a failure run, so re-recording is a copy-paste:
      // set ROLLTUI_CENSUS=1 to see it.
      if (std::getenv("ROLLTUI_CENSUS")) std::printf("        {\"%s\", %d},\n", rel.c_str(), n);
      const Row* row = nullptr;
      for (const Row& r : recorded)
        if (rel == r.header) row = &r;
      if (!row) {
        unlisted.push_back(rel + " (" + std::to_string(n) + ")");
        continue;
      }
      ++checked;
      check_quiet(n == row->pointers, std::string("borrowed-pointer census: ") + rel + " has " + std::to_string(n) +
                                          " raw pointers, recorded " + std::to_string(row->pointers) +
                                          " — if that is deliberate, say what the new one BORROWS and update the row");
    }
    check(unlisted.empty(), "every public header is in the census" + (unlisted.empty() ? "" : " — missing: " + unlisted.front()));
    check(checked == static_cast<int>(sizeof(recorded) / sizeof(recorded[0])),
          "…and every recorded row matched a real header (" + std::to_string(checked) + ")");
    check(total == 100, "the census counted the library's borrows (" + std::to_string(total) + " raw pointers in public headers)");
    // CONTROL 2: the pointer scanner actually matches a declaration, and does NOT match
    // arithmetic or a comment.
    check(std::regex_search(std::string("void f(const Document* doc);"), pointer_decl()), "the pointer scanner matches a declaration");
    check(!std::regex_search(std::string("  int n = a * b;"), pointer_decl()), "…and not a multiplication");
    check(is_comment("  // const Document* doc"), "…and comment lines are skipped, so prose about a pointer is not a pointer");
  }

  // ---- the rule is WRITTEN where a reader (and a model) will meet it -----------------
  // A convention not written where models read is not a convention (CLAUDE.md's own
  // standard). These are the two places this rule has to exist, and they are checked
  // rather than hoped.
  {
    const std::string claude = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/../CLAUDE.md");
    check(claude.find("OWNED") != std::string::npos && claude.find("BORROWED") != std::string::npos &&
              claude.find("shared_ptr") != std::string::npos,
          "CLAUDE.md carries the ownership rule, where every session reads it");
    const std::string widgets = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/Widgets.hpp");
    check(widgets.find("OWNED") != std::string::npos && widgets.find("BORROWED") != std::string::npos,
          "…and Widgets.hpp states it beside the type that does the owning");
  }

  return report("rolltui ownership_test");
}
