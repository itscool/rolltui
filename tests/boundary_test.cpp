//
// boundary_test.cpp — THE LIBRARY INCLUDES ONLY ITSELF, as a check rather than a promise.
//
// rolltui is compiled inside other programs' builds: copied into a host's own `rolltui/` folder, or its sources listed in a host's
// own build file. Each resolves the library's includes against the HOST's include path, and the one spelling that resolves in every
// one of them is `rolltui/...`, because the folder the library sits in is named rolltui in all of them. An include spelled any other
// way resolves against whatever the host happens to keep — a `testkit/` of its own, an `include/` of its own — or against nothing.
// That is how the library's sources once included `testkit/testctl.h`: it resolved only because the first host kept a testkit at its
// root, and a host without one could not compile the library until its build file added a line to work round it.
//
// TWO ASSERTIONS, AND NEITHER SUBSUMES THE OTHER.
//
//   1. THE INCLUDE PATH, read from the BUILD SYSTEM rather than from this file's idea of it: `ROLLTUI_TARGET_INCLUDES_FILE` is written
//      at generate time from `$<TARGET_PROPERTY:rolltui,INCLUDE_DIRECTORIES>`, so it is the list the compiler is given. Every
//      directory on it is inside this repository; one outside would let an include resolve somewhere this repository does not control.
//
//   2. THE INCLUDE GRAPH, because a path check alone cannot see a spelling: the repository root is on the path (it is how
//      `rolltui/rolltui.h` resolves), so `#include "testkit/testctl.h"` compiles here and nowhere a host lacks a testkit. Every quoted
//      include in the library's own sources is held to `include_problem` below. Angled includes are the system's, not this file's.
//
// ARMED, NOT ASSUMED: each check's rule is run on planted violations every run, so a pass means a violation would have been seen.
//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui_test.hpp"

namespace fs = std::filesystem;
using testkit::check;

namespace {

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

// THE LIBRARY'S OWN SOURCES: what a host compiles when it builds rolltui — `c/` and the sources and headers at the repository
// root. `third_party/` is vendored code we do not author; the tests, tools and examples are hosts of the library, not the library.
std::vector<fs::path> library_sources() {
  const fs::path root = ROLLTUI_SOURCE_DIR;
  std::vector<fs::path> out;
  auto take = [&](const fs::path& dir) {
    for (const fs::directory_entry& e : fs::directory_iterator(dir)) {
      if (!e.is_regular_file()) continue;
      const std::string ext = e.path().extension().string();
      if (ext == ".c" || ext == ".h" || ext == ".cpp" || ext == ".hpp") out.push_back(e.path());
    }
  };
  take(root);
  take(root / "c");
  return out;
}

// What is wrong with one quoted include in a library source, or "" when nothing is: it must be spelled `rolltui/...` and name a
// file in this repository — the one spelling that resolves in every host, because the folder the library sits in is named rolltui
// in all of them. nsplit is the exception, and only for its own header: a two-file library meant to be dropped into any project on
// its own includes its header by plain name, which resolves beside the file in every build.
std::string include_problem(const std::string& file_name, const std::string& inc) {
  if ((file_name == "nsplit.c" || file_name == "nsplit.h") && inc == "nsplit.h") return "";
  if (inc.rfind("rolltui/", 0) != 0) return "not spelled rolltui/...";
  if (!fs::is_regular_file(fs::path(ROLLTUI_SOURCE_DIR) / inc.substr(8))) return "names no file in this repository";
  return "";
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
  const fs::path root = ROLLTUI_SOURCE_DIR;

  // ---- CONTROL: the scan reaches the library ---------------------------------------------------------------------------------------
  const std::vector<fs::path> sources = library_sources();
  bool saw_umbrella = false, saw_c = false;
  for (const fs::path& p : sources) {
    if (p.filename() == "rolltui.h") saw_umbrella = true;
    if (p.filename() == "rolltui_presets.c") saw_c = true;
  }
  check(sources.size() > 80 && saw_umbrella && saw_c,
        "the scan reaches the library: " + std::to_string(sources.size()) + " sources, rolltui.h and c/ among them");

  // ---- 1. THE INCLUDE PATH -----------------------------------------------------------------------------------------------------------
  auto inside = [&](const fs::path& d) { return fs::weakly_canonical(d) == fs::weakly_canonical(root) || under(d, root); };
  // The standalone build's include root: a directory whose one entry is a link named `rolltui` to this repository.
  auto prefix_dir = [&](const fs::path& d) {
    std::error_code ec;
    int entries = 0;
    for (const fs::directory_entry& e : fs::directory_iterator(d, ec)) {
      (void)e;
      ++entries;
    }
    const fs::path link = d / "rolltui";
    return !ec && entries == 1 && fs::is_symlink(fs::symlink_status(link, ec)) &&
           fs::weakly_canonical(link) == fs::weakly_canonical(root);
  };
  const std::string dirs_text = read_file(ROLLTUI_TARGET_INCLUDES_FILE);
  check(!dirs_text.empty(), "the build system's include list was written and read — an empty one would make the next check pass on nothing");
  std::vector<std::string> outside;
  for (std::string d : split(dirs_text, '\n')) {
    while (!d.empty() && (d.back() == '\r' || d.back() == ' ')) d.pop_back();
    if (!d.empty() && !inside(fs::path(d)) && !prefix_dir(fs::path(d))) outside.push_back(d);
  }
  check(outside.empty(), "every directory on the library's include path is inside this repository, or is the `rolltui/` prefix that links to it" +
                             (outside.empty() ? "" : " — " + outside.front()));
  check(!inside(root.parent_path() / "include") && inside(root / "c"),
        "…and the rule can tell: a host's include/ beside this repository is outside it, the library's c/ is inside");
  check(!prefix_dir(root.parent_path()) && !prefix_dir(root / "c"),
        "…and only a directory holding nothing but the `rolltui` link counts as the prefix");

  // ---- 2. THE INCLUDE GRAPH ----------------------------------------------------------------------------------------------------------
  std::vector<std::string> bad;
  int quoted = 0;
  for (const fs::path& srcf : sources) {
    const std::string text = read_file(srcf);
    for (const std::string& inc : includes_in(text)) {
      if (text.find("#include \"" + inc + "\"") == std::string::npos) continue; /* angled: the system's */
      ++quoted;
      const std::string why = include_problem(srcf.filename().string(), inc);
      if (!why.empty()) bad.push_back(fs::relative(srcf, root).string() + " -> " + inc + " (" + why + ")");
    }
  }
  check(quoted > 200, "the graph scan reads the library's quoted includes (" + std::to_string(quoted) + ")");
  check(bad.empty(), "every quoted include in the library is spelled rolltui/... and names a file in this repository" +
                         (bad.empty() ? "" : " — " + bad.front()));
  check(!include_problem("rolltui_marker.c", "testkit/testctl.h").empty() &&
            !include_problem("rolltui_x.c", "include/Provider.hpp").empty() &&
            !include_problem("rolltui_x.c", "rolltui/no_such_header.h").empty() &&
            !include_problem("rolltui_x.c", "nsplit.h").empty() &&
            include_problem("rolltui_marker.c", "rolltui/testkit/testctl.h").empty() &&
            include_problem("nsplit.c", "nsplit.h").empty(),
        "…and the rule refuses what it is for: testkit/ and include/ spellings, a rolltui/ name with no file, nsplit's header outside "
        "nsplit — and keeps rolltui/testkit/testctl.h and nsplit's own include");

  return testkit::report("boundary_test");
}
