//
// shipped_artifact_test.cpp — NO CONTROL NAME REACHES THE SHIPPED LIBRARY.
//
// The control points live at the guarantees they break, in the library's own C. In the build
// every host, app and ordinary suite links, `testkit_ctl_on` is a MACRO that discards its
// argument and the table is not compiled at all — so the name is never an argument to anything
// and cannot appear in the archive. That is a claim about a FILE ON DISK, and this checks it.
//
// TWO ARCHIVES, AND THAT IS THE WHOLE DESIGN. `strings | grep MARKER` has false-negatived twice
// in this repository — once with the marker in a comment, once with the literal folded by the
// compiler — so "the name is not in the shipped archive" is worthless on its own: a scanner that
// can find nothing reports exactly the same thing. So every name is looked for in BOTH builds
// and must be PRESENT in the control one and ABSENT from the shipped one. The positive half is
// not a separate arming ritual performed once; it runs on every invocation, against the same
// bytes, with the same code.
//
// THE NAMES COME FROM THE SOURCE, not from a list kept here. A list would go stale the first
// time a control was added, and would go stale SILENTLY, which is the shape this whole phase is
// about.
//
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "rolltui_test.hpp"

using namespace testkit;
namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Every `testkit_ctl_on("…")` name in the library's own C, read from the files.
std::vector<std::string> control_names() {
  std::vector<std::string> out;
  const fs::path c = fs::path(ROLLTUI_SOURCE_DIR) / "c";
  if (!fs::is_directory(c)) return out;
  for (const fs::directory_entry& e : fs::directory_iterator(c)) {
    const std::string ext = e.path().extension().string();
    if (ext != ".c" && ext != ".h") continue;
    const std::string text = read_file(e.path());
    const std::string open = "testkit_ctl_on(\"";
    for (size_t i = text.find(open); i != std::string::npos; i = text.find(open, i + 1)) {
      const size_t b = i + open.size();
      const size_t q = text.find('"', b);
      if (q == std::string::npos) continue;
      out.push_back(text.substr(b, q - b));
    }
  }
  return out;
}

}  // namespace

TESTKIT_TEST(no_control_name_is_in_the_shipped_library_and_every_one_is_in_the_control_build) {
  const std::vector<std::string> names = control_names();
  check(!names.empty(),
        "the library has control points to look for: " + std::to_string(names.size()) +
            " name(s) read from rolltui/c/ — an empty list would make everything below vacuous");

  const std::string shipped = read_file(ROLLTUI_SHIPPED_ARCHIVE);
  const std::string with_controls = read_file(ROLLTUI_CONTROL_ARCHIVE);
  check(shipped.size() > 100000 && with_controls.size() > 100000,
        "both archives were read: shipped " + std::to_string(shipped.size()) + " B, control " +
            std::to_string(with_controls.size()) + " B");

  for (const std::string& n : names) {
    // THE POSITIVE HALF, and it is what makes the negative half mean anything: the same scanner,
    // the same bytes, finds this exact name in the build that has the control points.
    check(with_controls.find(n) != std::string::npos,
          "`" + n + "` IS in the control build — so the scanner can find a name when one is "
                    "there, and the absence below is a fact rather than a broken search");
    check(shipped.find(n) == std::string::npos,
          "`" + n + "` is NOT in the shipped library");
  }
}

int main() { return testkit::report("rolltui_shipped_artifact_test"); }
