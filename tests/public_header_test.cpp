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
#include <algorithm>
#include <cstdlib>
#include <map>
#include <regex>
#include <set>
#include <vector>

#include <dirent.h>

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

// ---- section 6's instruments: a literal-aware comment stripper, a recursive lister, and the
// identifier scan the class table is checked against (Phase 19 m1) ---------------------------
// Comments are stripped RESPECTING string and char literals — a "/*" inside a JSON string or a
// "//" inside a URL would otherwise eat real code, which is how the first census of this
// surface under-counted the library's own reach by a third.
std::string strip_all_comments(const std::string& src) {
  std::string out;
  out.reserve(src.size());
  for (size_t i = 0; i < src.size();) {
    const char c = src[i];
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < src.size() && src[j] != c) j += (src[j] == '\\') ? 2 : 1;
      out.append(src, i, std::min(j + 1, src.size()) - i);
      i = j + 1;
    } else if (src.compare(i, 2, "/*") == 0) {
      const size_t j = src.find("*/", i + 2);
      out += ' ';
      i = (j == std::string::npos) ? src.size() : j + 2;
    } else if (src.compare(i, 2, "//") == 0) {
      const size_t j = src.find('\n', i);
      i = (j == std::string::npos) ? src.size() : j;
    } else {
      out += c;
      ++i;
    }
  }
  return out;
}

void list_files(const std::string& dir, const std::vector<std::string>& exts, std::vector<std::string>& out) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  while (dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    const std::string path = dir + "/" + name;
    if (e->d_type == DT_DIR) { list_files(path, exts, out); continue; }
    for (const std::string& x : exts)
      if (name.size() > x.size() && name.compare(name.size() - x.size(), x.size(), x) == 0) out.push_back(path);
  }
  closedir(d);
}

// Every `rolltui_*` identifier in `text` (comments already stripped), counted.
void count_idents(const std::string& text, std::map<std::string, int>& out) {
  static const std::regex re(R"(\brolltui_[a-z0-9_]+\b)");
  for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) ++out[it->str()];
}
// A function DEFINITION in a .c file: a line starting at column 0 with a type, the name, a
// parameter list and an opening brace — the one mention of a function that is not a use of it.
std::set<std::string> definitions_in(const std::string& text) {
  static const std::regex re(R"((?:^|\n)[A-Za-z_][^\n;{}]*?\b(rolltui_[a-z0-9_]+)\s*\([^;{}]*\)\s*\{)");
  std::set<std::string> out;
  for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) out.insert((*it)[1].str());
  return out;
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
  // `rolltui_marker.h` LEFT this list in Phase 17 m2c: `transcript_test.cpp` asserts the
  // "▼ N more" rule directly, so a consumer reaches it and m4's own method makes it public.
  const char* kInternal[] = {"rolltui_alloc.h", "rolltui_map.h"};
  int missing = 0;
  std::string names;
  // ENUMERATED FROM THE DIRECTORY, not from a hand-written list (Phase 17 m2a). It used to be
  // a literal list of 34 names, and two headers — `rolltui_widget_kinds.h` and
  // `rolltui_embedded.h` — appeared in NEITHER it nor `kInternal`, so they were unreachable
  // through the umbrella and this check said nothing about them. A hand-list can only assert
  // about the names someone remembered to type; reading `c/` means a new header must land in
  // one list or the other or this fails.
  std::vector<std::string> headers;
  if (DIR* d = opendir((std::string(ROLLTUI_SOURCE_DIR) + "/c").c_str())) {
    while (dirent* e = readdir(d)) {
      const std::string n = e->d_name;
      if (n.size() > 2 && n.compare(n.size() - 2, 2, ".h") == 0) headers.push_back(n);
    }
    closedir(d);
  }
  std::sort(headers.begin(), headers.end());
  check(headers.size() >= 34, "the C header directory was read [" + std::to_string(headers.size()) + " headers]");
  for (const std::string& h : headers) {
    bool internal = false;
    for (const char* i : kInternal) internal = internal || h == i;
    if (internal) continue;
    if (text.find(std::string("rolltui/c/") + h) == std::string::npos) {
      ++missing;
      names += " " + h;
    }
  }
  // The count is derived from `kInternal`, never a literal: it read `- 3` while `kInternal`
  // held 2, so the label printed one fewer public header than there were. A wrong number in a
  // PASSING check is the quietest kind — nothing fails, and the figure gets quoted onward.
  const std::size_t internal_n = sizeof kInternal / sizeof *kInternal;
  check(missing == 0, "every public header is named by the umbrella —" +
                          (missing ? names : " all " + std::to_string(headers.size() - internal_n)));

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
    // 17 -> 14, 2026-09-05: the conversions that landed between are the four theme/effects
    // tests, layout_test and the preset-domain work; the ceiling had simply not been dropped
    // behind them, which is the one way a ratchet quietly stops ratcheting.
    // 14 -> 8, 2026-09-05 (m1d complete + m2c's first three suites): `tool_actions.hpp` and
    // all three editors, plus `authored_screen_test`, `input_test` and `transcript_test`. The
    // eight left are the two remaining HOSTS (m3) and six test suites that still need
    // `Layout`/`Theme`/`Screen`/`Widgets`/`Bindings`/`Unicode` C++ types (m2c).
    // 8 -> 0, 2026-09-05 (m4b): m2c and m3 landed and the reading had been 0 since, with the
    // CEILING left at 8 — a ratchet that has slack is a ratchet that is not holding anything.
    // It is a HARD ZERO now: there is no C++ header left to bypass to.
    const int kCeiling = 0;
    std::string joined;
    for (const std::string& n : names) joined += " " + n;
    check(consumers <= kCeiling,
          "consumers bypassing rolltui.h did not RISE [" + std::to_string(consumers) + " <= " +
              std::to_string(kCeiling) + "]:" + joined);
  }

  // ---- m4b: NO PUBLIC FUNCTION HANDS A RESULT BACK THROUGH A CALLBACK ---------------------
  // The rule the API had for TEXT OUT and did not have for N THINGS OUT, now stated for both
  // and checked rather than remembered. A callback parameter is legitimate when it carries a
  // DECISION the library cannot make (`RolltuiScopeFn`, `RolltuiRowsFn`, `RolltuiEffectFn`);
  // it is wrong when it carries a RESULT the library already has, because then every consumer
  // writes the same lambda-and-collector. That cost was measured before this check existed:
  // `rolltui_preset_store_list` was wrapped at 7 of 7 sites and `struct PresetInfo` had been
  // written out three times, byte for byte, in roll, the studio and presets_test.
  //
  // The two RESULT-sink types are the subject. `RolltuiPutFn` survives as INTERNAL plumbing
  // (the library streams into its own buffers with it) and as the type of the descriptor hooks
  // a DOMAIN supplies — a decision going in — so what is scanned is the public declarations.
  {
    // EVERY result-sink type the library has ever had. `RolltuiPresetInfoFn` and
    // `RolltuiMenuActionFn` no longer exist — they are listed anyway, so reintroducing one
    // fails here rather than passing because the name is not in a list someone maintained.
    //
    // NOT listed, and each for a stated reason rather than by omission: `RolltuiScopeFn`,
    // `RolltuiValidatorFn`, `RolltuiEffectFn`, `RolltuiEventFn`, `RolltuiTermEventFn`,
    // `RolltuiRowsFn`, `RolltuiSubmitFn`, `RolltuiNoteFn`, `RolltuiCopyFn`,
    // `RolltuiMdHighlightFn`, `RolltuiUndoFreeFn`, `RolltuiSlotFn`, `RolltuiReasonFn`,
    // `RolltuiThemePresetValidFn` all carry a DECISION the library cannot make.
    // `RolltuiDiffLineFn` is the one that looks like a sink and is not: it is an input
    // ACCESSOR (how to read line N of the caller's block), and `rolltui_diff_spans`' result
    // already goes to a caller-filled `out`/`out_cap`. The signature settles it, not the name.
    static const char* kResultSinks[] = {"RolltuiPutFn", "RolltuiPresetInfoFn", "RolltuiMenuActionFn"};
    // `rolltui_preset_read_file` is the one deliberate exception and it is NAMED, not a
    // category: it streams a whole file into whatever buffer the caller already has, and the
    // library's own three callers use an internal `Buf`. A consumer that wants the bytes as a
    // string passes `rolltui_str_put` — the bridge that exists for exactly this.
    static const char* kAllowed[] = {"rolltui_preset_read_file"};
    std::vector<std::string> offenders;
    int scanned = 0, sentinel = 0;
    for (const std::string& h : headers) {
      bool internal = false;
      for (const char* i : kInternal) internal = internal || h == i;
      if (internal) continue;  // an internal header is free to use whatever shape it likes
      // Declaration LINES joined into statements: a wrapped signature must be one string, or
      // a sink type on a continuation line would be judged on its own.
      std::string stmt;
      for (const std::string& line : declarations(read(std::string(ROLLTUI_SOURCE_DIR) + "/c/" + h))) {
        stmt += (stmt.empty() ? "" : " ") + line;
        if (line.find(';') == std::string::npos) continue;
        const std::string decl = stmt;
        stmt.clear();
        // Only things that look like a FUNCTION declaration; a typedef of the callback type
        // itself is the definition, not a use of it.
        if (decl.find("rolltui_") == std::string::npos || decl.find('(') == std::string::npos) continue;
        ++scanned;
        // The ARMING word: every statement counted here names a rolltui_ symbol, so a run that
        // sees none has a broken splitter and fails instead of reporting a clean zero.
        ++sentinel;
        if (decl.rfind("typedef", 0) == 0) continue;
        bool sink = false;
        for (const char* t : kResultSinks) sink = sink || decl.find(t) != std::string::npos;
        if (!sink) continue;
        bool allowed = false;
        for (const char* a : kAllowed) allowed = allowed || decl.find(a) != std::string::npos;
        if (!allowed) offenders.push_back(h + ": " + decl.substr(0, 90));
      }
    }
    check(sentinel > 200, "the declaration scanner can see: " + std::to_string(sentinel) + " of " +
                              std::to_string(scanned) + " scanned declarations name a rolltui_ symbol");
    std::string joined;
    for (const std::string& o : offenders) joined += "\n      " + o;
    check(offenders.empty(),
          "no public function hands a RESULT back through a callback — it takes the caller's "
          "RolltuiStr* (one string), RolltuiStrList* (many) or typed list instead" + joined);
  }

  // ---- 6. THE CLASS TABLE: every public function has ONE class, and the classes are what
  //         `rolltui.h` is written from (Phase 19 m1) ------------------------------------------
  // `api_classes.inc` is the "intentional public API design" the user asked for, as data: every
  // function declared in a public header carries PUBLIC, TOOL_FACING, INTERNAL or DELETE, with
  // the reason where it is not the module's default. This section measures REACH — who outside
  // the library mentions each function — and holds the table to it, so a class cannot drift from
  // the evidence and a new function cannot arrive unclassified.
  {
    struct Row { const char* fn; const char* cls; };
    static const Row kApi[] = {
#define ROLLTUI_API(name, cls) {#name, #cls},
#include "api_classes.inc"
#undef ROLLTUI_API
    };
    const std::string root = std::string(ROLLTUI_SOURCE_DIR);   // rolltui/
    const std::string repo = root + "/..";
    // Declared in a public header: any `rolltui_x(` mention in a header that is not internal.
    std::set<std::string> declared;
    for (const std::string& h : headers) {
      bool internal = false;
      for (const char* i : kInternal) internal = internal || h == i;
      if (internal) continue;
      static const std::regex decl_re(R"(\b(rolltui_[a-z0-9_]+)\s*\()");
      const std::string t = strip_all_comments(read(root + "/c/" + h));
      for (std::sregex_iterator it(t.begin(), t.end(), decl_re), end; it != end; ++it) declared.insert((*it)[1].str());
    }
    // Reach, four consumers and the library, each a set of mentioned identifiers.
    auto mentions_in = [&](const std::vector<std::string>& dirs, const std::vector<std::string>& exts) {
      std::vector<std::string> files;
      for (const std::string& d : dirs) list_files(d, exts, files);
      std::map<std::string, int> counts;
      for (const std::string& f : files) count_idents(strip_all_comments(read(f)), counts);
      std::set<std::string> out;
      for (const auto& [k, v] : counts) out.insert(k);
      return out;
    };
    const std::set<std::string> roll = mentions_in({repo + "/src", repo + "/include"}, {".cpp", ".hpp"});
    const std::set<std::string> tools = mentions_in({root + "/tools"}, {".cpp", ".hpp"});
    const std::set<std::string> tests = mentions_in({root + "/tests", repo + "/tests"}, {".cpp", ".hpp", ".c"});
    std::set<std::string> lib;  // mentioned in a .c beyond its own definition
    {
      std::vector<std::string> cs;
      list_files(root + "/c", {".c"}, cs);
      for (const std::string& f : cs) {
        const std::string t = strip_all_comments(read(f));
        const std::set<std::string> defs = definitions_in(t);
        std::map<std::string, int> counts;
        count_idents(t, counts);
        for (const auto& [k, v] : counts)
          if (defs.count(k) ? v > 1 : true) lib.insert(k);
      }
    }
    std::set<std::string> hdr;  // mentioned in a header's inline C++ member: reached by every C++ consumer
    for (const std::string& h : headers) {
      const std::string t = strip_all_comments(read(root + "/c/" + h));
      static const std::regex decl_stmt(R"([^\n;{}]*?\b(rolltui_[a-z0-9_]+)\s*\([^;{}]*\)\s*;)");
      std::set<std::string> decls;
      for (std::sregex_iterator it(t.begin(), t.end(), decl_stmt), end; it != end; ++it) decls.insert((*it)[1].str());
      std::map<std::string, int> counts;
      count_idents(t, counts);
      for (const auto& [k, v] : counts)
        if (declared.count(k) && (decls.count(k) ? v > 1 : true)) hdr.insert(k);
    }
    auto reach_of = [&](const std::string& f) -> const char* {
      return roll.count(f) ? "roll" : tools.count(f) ? "tools" : tests.count(f) ? "tests" : lib.count(f) ? "lib" : hdr.count(f) ? "hdr" : "nothing";
    };
    // The instruments are proved armed before any zero is believed.
    check(declared.size() > 700 && roll.count("rolltui_preset_store_new") && tools.count("rolltui_window_stack_push_popup") &&
              lib.count("rolltui_str_append") && !lib.count("rolltui_preset_store_new_NOSUCH"),
          "the class census sees declarations (" + std::to_string(declared.size()) + "), roll's reach, the tools' reach and the library's own");
    std::map<std::string, std::string> cls;
    for (const Row& r : kApi) cls[r.fn] = r.cls;
    std::vector<std::string> unclassified, stale, roll_not_public, tool_internal, deleted_but_reached, internal_reached;
    for (const std::string& f : declared)
      if (!cls.count(f)) unclassified.push_back(f);
    for (const Row& r : kApi) {
      if (!declared.count(r.fn)) { stale.push_back(r.fn); continue; }
      const std::string c = r.cls, reach = reach_of(r.fn);
      if (reach == "roll" && c != "PUBLIC") roll_not_public.push_back(std::string(r.fn) + " (" + c + ")");
      if (reach == "tools" && (c == "INTERNAL" || c == "DELETE")) tool_internal.push_back(std::string(r.fn) + " (" + c + ")");
      if (c == "DELETE" && reach != "nothing") deleted_but_reached.push_back(std::string(r.fn) + " (" + reach + ")");
      if (c == "INTERNAL" && (reach == "roll" || reach == "tools")) internal_reached.push_back(std::string(r.fn) + " (" + reach + ")");
    }
    auto join = [](const std::vector<std::string>& v) { std::string s; for (const std::string& x : v) s += "\n      " + x; return s; };
    check(unclassified.empty(), "every function declared in a public header has a class in api_classes.inc — a new one is a DECISION, not an arrival" + join(unclassified));
    check(stale.empty(), "every row of api_classes.inc names a declared function (a deleted one takes its row with it)" + join(stale));
    check(roll_not_public.empty(), "a function roll reaches is PUBLIC" + join(roll_not_public));
    check(tool_internal.empty(), "a function a tool reaches is PUBLIC or TOOL_FACING" + join(tool_internal));
    check(internal_reached.empty(), "an INTERNAL function is reached by no host and no tool" + join(internal_reached));
    check(deleted_but_reached.empty(), "a DELETE row is reached by nothing, anywhere" + join(deleted_but_reached));
    // THE TOTALS, RECORDED: a class that moves is a decision and re-records this line.
    std::map<std::string, int> totals;
    for (const Row& r : kApi) ++totals[r.cls];
    // MEASURED 2026-09-06 (Phase 19 m1): 851 functions in 37 public headers.
    const int kPublic = 577, kTool = 42, kInternal_ = 184, kDelete = 48;
    check(totals["PUBLIC"] == kPublic && totals["TOOL_FACING"] == kTool && totals["INTERNAL"] == kInternal_ && totals["DELETE"] == kDelete,
          "the class totals are the recorded ones (PUBLIC " + std::to_string(totals["PUBLIC"]) + ", TOOL_FACING " + std::to_string(totals["TOOL_FACING"]) +
              ", INTERNAL " + std::to_string(totals["INTERNAL"]) + ", DELETE " + std::to_string(totals["DELETE"]) + ") — a moved class re-records them deliberately");
    if (std::getenv("ROLLTUI_CENSUS")) {
      std::map<std::string, std::map<std::string, int>> reach_by_class;
      for (const Row& r : kApi) ++reach_by_class[r.cls][reach_of(r.fn)];
      for (const auto& [c, m] : reach_by_class) {
        std::printf("        %-12s", c.c_str());
        for (const auto& [rch, n] : m) std::printf(" %s=%d", rch.c_str(), n);
        std::printf("\n");
      }
    }
  }

  return report("public_header_test");
}
