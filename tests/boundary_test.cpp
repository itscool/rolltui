//
// boundary_test.cpp — ROLLTUI DOES NOT DEPEND ON ROLL, as a check rather than a promise.
//
// The rule is not "rolltui depends on nothing". A shared leaf both sides depend on is not an
// upward dependency. The rule is that the library never reaches INTO the program that consumes
// it, so `rolltui/` builds and its suite passes with roll's `include/` and `src/` deleted from
// the tree — which is what makes the library extractable, and what stops the router's types
// leaking into a library that has other hosts.
//
// UNTIL THIS FILE EXISTED THAT WAS A BUILD-CONFIG PROMISE AND NOTHING CHECKED IT. It held only
// because `rolltui/CMakeLists.txt` happens not to put roll's directories on any target's include
// path. A promise like that is exactly what a refactor erodes silently: the build still works,
// the suite is still green, and the property is gone with nothing to say so.
//
// TWO ASSERTIONS, AND NEITHER SUBSUMES THE OTHER.
//
//   1. THE INCLUDE PATH, read from the BUILD SYSTEM rather than from this file's idea of it.
//      `ROLLTUI_TARGET_INCLUDES_FILE` names a file CMake writes at generate time from
//      `$<TARGET_PROPERTY:rolltui,INCLUDE_DIRECTORIES>`, one directory per line, so it is the
//      list the compiler will actually be given — not a re-reading of the CMakeLists, which
//      could agree with a stale idea of what that file says.
//
//   2. THE INCLUDE GRAPH, because the path check alone is not enough HERE for a specific
//      reason: `rolltui` exports the REPOSITORY ROOT as a public include directory (it is how
//      `#include "rolltui/rolltui.h"` resolves for every consumer). So roll's headers are
//      already reachable by a path-qualified spelling — `#include "include/Provider.hpp"` would
//      compile today — and no include-DIRECTORY check can see that. Every `#include` under
//      `rolltui/` is therefore resolved against the real search path and its target located.
//
// ARMED, NOT ASSUMED. Both assertions were seen to FAIL against a planted violation before
// either was trusted, and the planted forms are named in the checks below so the next person
// can re-run them: a `target_include_directories(rolltui PRIVATE .../include)` for the first,
// and an `#include "include/Provider.hpp"` in a library source for the second. A boundary check
// that has never seen a boundary crossed is the reports-zero shape aimed at the newest
// instrument.
//
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui_test.hpp"

namespace fs = std::filesystem;
using rolltui_test::check;

namespace {

// The repository root: rolltui's source dir with its last component removed. Derived rather
// than passed, so there is one definition of "up one" and it cannot disagree with the include
// directory the library actually exports.
fs::path repo_root() { return fs::path(ROLLTUI_SOURCE_DIR).parent_path(); }

// ROLL'S OWN DIRECTORIES, and the list is deliberately short. These are the two the library's
// Done-when names, and adding a third is a decision rather than a tidy-up: `tools/` and `tests/`
// at the root hold roll's mock servers and its suite, which the library also must not reach,
// but they are not on any search path and a rule that lists everything reads as a rule about
// nothing.
const char* kRollDirs[] = {"include", "src"};

bool under(const fs::path& p, const fs::path& dir) {
  const std::string a = fs::weakly_canonical(p).string();
  const std::string b = fs::weakly_canonical(dir).string();
  return a.size() > b.size() && a.compare(0, b.size(), b) == 0 && a[b.size()] == '/';
}

std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= s.size()) {
    const size_t n = s.find(sep, at);
    out.emplace_back(s.substr(at, n == std::string_view::npos ? s.size() - at : n - at));
    if (n == std::string_view::npos) break;
    at = n + 1;
  }
  return out;
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Every source the library ships or tests with. `third_party/` is vendored code we do not
// author and do not hold to this rule; `ucd/` is data.
std::vector<fs::path> library_sources() {
  std::vector<fs::path> out;
  for (const fs::directory_entry& e : fs::recursive_directory_iterator(ROLLTUI_SOURCE_DIR)) {
    if (!e.is_regular_file()) continue;
    const std::string p = e.path().string();
    if (p.find("/third_party/") != std::string::npos) continue;
    if (p.find("/ucd/") != std::string::npos) continue;
    const std::string ext = e.path().extension().string();
    if (ext == ".c" || ext == ".h" || ext == ".cpp" || ext == ".hpp" || ext == ".inc")
      out.push_back(e.path());
  }
  return out;
}

// The quoted or angled target of every `#include` on a line of its own. Not a preprocessor:
// a directive inside `#if 0` still counts, which is the safe direction — a boundary crossing
// that is only conditionally compiled is still a boundary crossing written down.
std::vector<std::string> includes_in(const std::string& text) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at < text.size()) {
    const size_t nl = text.find('\n', at);
    const std::string_view line(text.data() + at,
                                (nl == std::string::npos ? text.size() : nl) - at);
    at = (nl == std::string::npos) ? text.size() : nl + 1;
    const size_t h = line.find_first_not_of(" \t");
    if (h == std::string_view::npos || line[h] != '#') continue;
    const size_t k = line.find("include", h);
    if (k == std::string_view::npos || line.substr(h + 1, k - h - 1).find_first_not_of(" \t") !=
                                           std::string_view::npos)
      continue;
    const size_t o = line.find_first_of("\"<", k);
    if (o == std::string_view::npos) continue;
    const char close = line[o] == '"' ? '"' : '>';
    const size_t c = line.find(close, o + 1);
    if (c == std::string_view::npos) continue;
    out.emplace_back(line.substr(o + 1, c - o - 1));
  }
  return out;
}

}  // namespace

int main() {
  const fs::path root = repo_root();

  // ---- CONTROL: the scan reaches the library, and the roll directories it is about exist ----
  // A boundary test that walked nothing, or that was pointed at a tree with no roll in it,
  // would report a clean boundary forever.
  const std::vector<fs::path> sources = library_sources();
  bool saw_umbrella = false;
  for (const fs::path& p : sources)
    if (p.filename() == "rolltui.h") saw_umbrella = true;
  check(sources.size() > 80 && saw_umbrella,
        "the scan reaches the library: " + std::to_string(sources.size()) +
            " sources, including rolltui.h");
  bool roll_is_here = true;
  for (const char* d : kRollDirs)
    if (!fs::is_directory(root / d)) roll_is_here = false;
  check(roll_is_here,
        "roll's own include/ and src/ are present in this tree — the thing the boundary is "
        "about exists, so a pass means the library does not reach it rather than that there "
        "was nothing to reach");

  // ---- 1. THE INCLUDE PATH -----------------------------------------------------------------
  // Planted violation that must make this FAIL, for whoever re-arms it:
  //   target_include_directories(rolltui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
  const std::string dirs_text = read_file(ROLLTUI_TARGET_INCLUDES_FILE);
  check(!dirs_text.empty(),
        "the build system's include list was written and read — an empty one would make the "
        "next check pass on nothing");
  std::vector<std::string> bad_dirs;
  for (std::string d : split(dirs_text, '\n')) {
    while (!d.empty() && (d.back() == '\r' || d.back() == ' ')) d.pop_back();
    if (d.empty()) continue;
    for (const char* r : kRollDirs) {
      const fs::path rd = root / r;
      if (fs::weakly_canonical(fs::path(d)) == fs::weakly_canonical(rd) ||
          under(fs::path(d), rd))
        bad_dirs.push_back(d);
    }
  }
  check(bad_dirs.empty(),
        "no roll directory is on the library's include path" +
            (bad_dirs.empty() ? "" : " — " + bad_dirs.front()));

  // ---- 2. THE INCLUDE GRAPH ----------------------------------------------------------------
  // Planted violation that must make this FAIL:
  //   #include "include/Provider.hpp"   in any file under rolltui/
  // The search path is the real one: the file's own directory first (quoted form), then the
  // directories the build adds.
  std::vector<fs::path> search = {root, fs::path(ROLLTUI_SOURCE_DIR) / "tests",
                                  fs::path(ROLLTUI_SOURCE_DIR) / "tools"};
  std::vector<std::string> crossings;
  for (const fs::path& srcf : sources) {
    for (const std::string& inc : includes_in(read_file(srcf))) {
      std::vector<fs::path> cands = {srcf.parent_path() / inc};
      for (const fs::path& s : search) cands.push_back(s / inc);
      for (const fs::path& cand : cands) {
        if (!fs::is_regular_file(cand)) continue;
        for (const char* r : kRollDirs) {
          if (under(cand, root / r)) {
            crossings.push_back(fs::relative(srcf, root).string() + " -> " + inc);
          }
        }
        break;  // first hit wins, exactly as the preprocessor's search does
      }
    }
  }
  check(crossings.empty(),
        "no source under rolltui/ includes a file from roll's include/ or src/" +
            (crossings.empty() ? "" : " — " + crossings.front()));

  return rolltui_test::report("boundary_test");
}
