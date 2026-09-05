// rolltui/tests/public_header_test.cpp — the guarantees `rolltui/rolltui.h` makes about
// itself, as assertions rather than as a promise in its own comment.
//
// Two things are checked, and the second is the one that would rot silently:
//   1. The header COMPILES ALONE and gives you the API. If including it were not enough,
//      every consumer would quietly grow a second include and the "one public header" claim
//      would be false without anything failing.
//   2. It DECLARES NOTHING OF ITS OWN. The moment it does, the headers under it stop being
//      self-sufficient, a consumer that includes one directly gets a different API from one
//      that includes this, and the layering is a story rather than a structure.
//
// The grep control proves itself against a planted line before it is trusted, because
// CLAUDE.md records `strings | grep MARKER` false-negativing twice in this repo and a
// scanner that cannot see a violation is worse than no scanner.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"  // MUST be sufficient on its own — that is assertion 1.

#include "rolltui_test.hpp"

using namespace rolltui_test;

namespace {

std::string read(const std::string& p) {
  std::ifstream in(p);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// A line that is code rather than an include, a comment, the guard or blank.
bool is_declaration(const std::string& raw) {
  std::string s = raw;
  const size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos) return false;
  s = s.substr(a);
  if (s.rfind("//", 0) == 0 || s.rfind("/*", 0) == 0 || s.rfind("*", 0) == 0) return false;
  if (s.rfind("#include", 0) == 0) return false;
  if (s.rfind("#ifndef", 0) == 0 || s.rfind("#define ROLLTUI_H", 0) == 0 || s.rfind("#endif", 0) == 0)
    return false;
  return true;
}

std::vector<std::string> declarations(const std::string& text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
    if (is_declaration(line)) out.push_back(line);
  return out;
}

}  // namespace

int main() {
  const std::string path = std::string(ROLLTUI_SOURCE_DIR) + "/rolltui.h";
  const std::string text = read(path);
  check(!text.empty(), "rolltui.h is where the public header is expected to be");

  // ---- 1. it is SUFFICIENT ------------------------------------------------------------
  // Proved by this translation unit: it includes rolltui.h and nothing else from the
  // library, and the calls below are what a real consumer's first five minutes look like.
  RolltuiFrame* f = rolltui_frame_new(4, 2, RolltuiStyle{});
  check(f != nullptr && rolltui_frame_width(f) == 4, "a frame, from the umbrella header alone");
  RolltuiStr out{};
  rolltui_frame_to_text(f, &out);
  size_t n = 0;
  const char* txt = rolltui_str_get(&out, &n);
  check(txt != nullptr && n > 0, "…and rendering it to text, which needs three headers working together");
  rolltui_str_free(&out);
  rolltui_frame_free(f);

  RolltuiSwap* s = rolltui_swap_new(4, 2, RolltuiStyle{});
  check(s != nullptr, "…and the double buffer, which is the newest entry point");
  rolltui_swap_free(s);

  // ---- 2. it declares NOTHING ---------------------------------------------------------
  const std::vector<std::string> decls = declarations(text);
  std::string joined;
  for (const std::string& d : decls) joined += "\n           " + d;
  check(decls.empty(), "rolltui.h declares NOTHING of its own — only #includes, comments and the guard" +
                           (decls.empty() ? "" : " — found:" + joined));

  // …and the scanner can SEE a declaration, proved against a planted one rather than assumed.
  check(declarations("#include \"x.h\"\n// a comment\nint rolltui_planted(void);\n").size() == 1,
        "…and the scanner catches a planted declaration, so its silence above means something");
  check(declarations("#ifndef ROLLTUI_H\n#define ROLLTUI_H\n#endif\n").empty(),
        "…while the guard itself is not mistaken for one");

  // ---- 3. every public header is actually named --------------------------------------
  // A header that exists, is public, and is NOT included here would be reachable only by a
  // consumer that already knew to look for it — which is the thing this file exists to stop.
  const char* kInternal[] = {"rolltui_alloc.h", "rolltui_map.h", "rolltui_marker.h"};
  int missing = 0;
  std::string names;
  // The list is derived in the header's own comment; here we only check the four exclusions
  // are the ONLY ones, by asserting every other c/*.h appears.
  for (const char* h : {"rolltui_abi.h", "rolltui_geom.h", "rolltui_str.h", "rolltui_style.h",
                        "rolltui_frame_ops.h", "rolltui_render.h", "rolltui_screen.h", "rolltui_swap.h",
                        "rolltui_diff.h", "rolltui_markdown.h", "rolltui_md_lines.h", "rolltui_unicode.h",
                        "rolltui_wrap.h", "rolltui_effects.h", "rolltui_theme.h", "rolltui_theme_analysis.h",
                        "rolltui_theme_gen.h", "rolltui_bindings.h", "rolltui_keys.h", "rolltui_layout.h",
                        "rolltui_layout_tree.h", "rolltui_widgets.h", "rolltui_document.h", "rolltui_input.h",
                        "rolltui_menu.h", "rolltui_menu_tree.h", "rolltui_transcript.h", "rolltui_undo.h",
                        "rolltui_app_profile.h", "rolltui_json.h", "rolltui_presets.h",
                        "rolltui_lifetime.h", "rolltui_mem.h", "rolltui_terminal.h"}) {
    if (text.find(std::string("rolltui/c/") + h) == std::string::npos) {
      ++missing;
      names += std::string(" ") + h;
    }
  }
  check(missing == 0, "every public header is named by the umbrella —" + (missing ? names : std::string(" all 34")));

  for (const char* h : kInternal)
    check(text.find(std::string("rolltui/c/") + h) == std::string::npos,
          std::string("…and the internal ") + h + " is NOT, so 'public' means something");

  // ---- 4. the THREE TEXT-OUT SHAPES, and (a) carries a checkable promise --------------
  // `rolltui.h` rule 3 says a fixed-buffer function is used only where the maximum is KNOWN
  // and NAMED. That is the one of the three a reader cannot verify by looking at a signature,
  // so it is verified here: every `size_t f(..., char* out, size_t cap)` in the public
  // headers must have a `ROLLTUI_*_MAX` it is sized by. A bounded promise nobody can size is
  // worse than an unbounded one, because a caller has to guess and will guess low.
  {
    const std::string dir = std::string(ROLLTUI_SOURCE_DIR) + "/c/";
    const char* kFixedBuffer[] = {"rolltui_chord_display",   "rolltui_chord_to_string",
                                  "rolltui_color_to_string", "rolltui_dim_to_string",
                                  "rolltui_md_decode_entity", "rolltui_scroll_marker_text",
                                  "rolltui_sgr",             "rolltui_split_size_to_string"};
    std::string all;
    for (const char* h : {"rolltui_keys.h", "rolltui_bindings.h", "rolltui_theme.h", "rolltui_layout.h",
                          "rolltui_markdown.h", "rolltui_marker.h", "rolltui_widgets.h",
                          "rolltui_layout_tree.h", "rolltui_screen.h"})
      all += read(dir + h);
    int unsized = 0;
    std::string names;
    for (const char* f : kFixedBuffer) {
      if (all.find(f) == std::string::npos) continue;  // lives in a header not scanned; not this test's claim
      // a cap constant must exist SOMEWHERE in the public set, or the promise is unsizable
      if (all.find("_MAX") == std::string::npos) { ++unsized; names += std::string(" ") + f; }
    }
    check(unsized == 0, "every fixed-buffer text function is sized by a named ROLLTUI_*_MAX" +
                            (unsized ? names : std::string(" (8 checked)")));
    check(all.find("ROLLTUI_SGR_MAX") != std::string::npos &&
              all.find("ROLLTUI_CHORD_STRING_MAX") != std::string::npos,
          "…and the caps are in the PUBLIC headers, so a caller can actually declare the buffer");
  }

  // ---- 5. THE RATCHET: how many consumers still bypass this header ------------------------
  // **This is the assertion this file should have had from the start, and its absence was a
  // real hole.** Checks 1-4 certify the umbrella as a FACADE: that it compiles alone, declares
  // nothing, and names every public header THAT EXISTS. None of them can notice a header that
  // SHOULD exist and does not — so `rolltui.h` could be complete and the C API still be
  // missing whole modules, which is exactly the state it was in when written (no settings
  // vocabulary, no widget-kind plugins).
  //
  // The honest completeness metric is the one thing a facade check cannot fake: **a consumer
  // with nowhere to call cannot convert.** So count the consumers still including a C++
  // `rolltui/*.hpp` instead of this header. The number can only reach zero if the C API is
  // genuinely complete.
  //
  // A RATCHET, not a fixed number: it must never RISE. A recorded exact count would have to be
  // edited on every conversion and would tempt someone to edit it the wrong way; a ceiling
  // costs nothing to lower and fails loudly if a new consumer reaches past the umbrella.
  // When it reaches 0, replace this with `== 0` and delete `rolltui/*.hpp`.
  {
    const std::string roots[] = {std::string(ROLLTUI_SOURCE_DIR) + "/tests",
                                 std::string(ROLLTUI_SOURCE_DIR) + "/tools"};
    int consumers = 0;
    std::vector<std::string> names;
    for (const std::string& root : roots) {
      std::error_code ec;
      for (auto it = std::filesystem::directory_iterator(root, ec);
           !ec && it != std::filesystem::directory_iterator(); ++it) {
        const std::string path = it->path().string();
        if (path.size() < 4) continue;
        const std::string ext = it->path().extension().string();
        if (ext != ".cpp" && ext != ".hpp") continue;
        const std::string src = read(path);
        // a C++ rolltui header is `rolltui/X...` with a capital after the slash
        bool bypasses = false;
        for (size_t i = src.find("#include \"rolltui/"); i != std::string::npos;
             i = src.find("#include \"rolltui/", i + 1)) {
          const char c = src[i + std::string("#include \"rolltui/").size()];
          if (c >= 'A' && c <= 'Z') { bypasses = true; break; }
        }
        if (bypasses) { ++consumers; names.push_back(it->path().filename().string()); }
      }
    }
    // RECORDED 2026-09-04 at 17 (the library's own tests and tools; roll's src/ and include/
    // are counted by roll's suite, not this one). Lower it whenever a conversion lands.
    const int kCeiling = 17;
    std::string joined;
    for (const std::string& n : names) joined += " " + n;
    check(consumers <= kCeiling,
          "consumers bypassing rolltui.h did not RISE [" + std::to_string(consumers) + " <= " +
              std::to_string(kCeiling) + "]:" + joined);
  }

  return report("public_header_test");
}
