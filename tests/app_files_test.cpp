// rolltui/tests/app_files_test.cpp — where an app's own default files are found: compiled in,
// beside the binary, or in the known folder, each later rung overriding the earlier.
// ONLY the public header: `rolltui_app_file` is how a host finds its own screen.
#include "rolltui/rolltui.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "rolltui_test.hpp"

namespace {
namespace fs = std::filesystem;

void write_file(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

std::string str(const RolltuiStr& s) { return std::string(s.p ? s.p : "", s.n); }

struct Found {
  int rc = 0;
  std::string text, tried;
};

Found find(const std::string& argv0, const RolltuiEmbeddedFile* embedded, std::size_t embedded_n) {
  RolltuiStr out{}, tried{};
  Found f;
  f.rc = rolltui_app_file(argv0.c_str(), "probe", "layout", embedded, embedded_n, &out, &tried);
  f.text = str(out);
  f.tried = str(tried);
  rolltui_str_free(&out);
  rolltui_str_free(&tried);
  return f;
}

}  // namespace

int main() {
  using testkit::check;
  const std::string tmp = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
  const fs::path root = fs::path(tmp) / ("rolltui_app_files_test_" + std::to_string(::getpid()));
  std::error_code ec;
  fs::remove_all(root, ec);
  const fs::path bin = root / "bin", cfg = root / "cfg";
  fs::create_directories(bin);
  fs::create_directories(cfg);
  ::setenv("ROLL_CONFIG_DIR", cfg.c_str(), 1);
  const std::string argv0 = (bin / "probe").string();
  const std::string beside = (bin / "probe.layout.json").string();
  const std::string known = (cfg / "rolltui" / "probe" / "layout.json").string();
  const RolltuiEmbeddedFile embedded[] = {{"layout", "{\"from\":\"embedded\"}"}};

  // ---- 1. each rung, and each later one overriding the earlier -----------------------------
  Found f = find(argv0, embedded, 1);
  check(f.rc == 1 && f.text == "{\"from\":\"embedded\"}", "with no file on disk, the compiled-in copy is the answer");
  check(f.tried == "  found   (embedded)\n  missing " + beside + "\n  missing " + known,
        "`tried` names every candidate in order, hit or miss [" + f.tried + "]");

  write_file(beside, "{\"from\":\"beside\"}");
  f = find(argv0, embedded, 1);
  check(f.rc == 1 && f.text == "{\"from\":\"beside\"}", "a file beside the binary overrides the compiled-in copy");

  write_file(known, "{\"from\":\"known\"}");
  f = find(argv0, embedded, 1);
  check(f.rc == 1 && f.text == "{\"from\":\"known\"}" && f.tried.find("  found   " + beside) != std::string::npos &&
            f.tried.find("  found   " + known) != std::string::npos,
        "the known folder overrides both, and `tried` records both hits");

  fs::remove(beside, ec);
  fs::remove(known, ec);
  f = find(argv0, nullptr, 0);
  check(f.rc == 0 && f.text.empty() && f.tried.find("  missing (embedded)") == 0,
        "every rung missing: 0, nothing written, and the misses listed [" + f.tried + "]");

  // ---- 2. the known folder: ROLL_CONFIG_DIR first, then XDG_CONFIG_HOME/roll ----------------
  ::unsetenv("ROLL_CONFIG_DIR");
  ::setenv("XDG_CONFIG_HOME", (root / "xdg").c_str(), 1);
  f = find(argv0, nullptr, 0);
  check(f.tried.find("  missing " + (root / "xdg" / "roll" / "rolltui" / "probe" / "layout.json").string()) !=
            std::string::npos,
        "without ROLL_CONFIG_DIR the known folder is XDG_CONFIG_HOME/roll [" + f.tried + "]");
  ::setenv("ROLL_CONFIG_DIR", cfg.c_str(), 1);

  // ---- 3. the binary's directory, from what argv0 says ---------------------------------------
  f = find("probe", nullptr, 0);
  check(f.tried.find("  missing ./probe.layout.json") != std::string::npos,
        "argv0 with no directory in it is looked for in the working directory [" + f.tried + "]");
  f = find("/probe", nullptr, 0);
  check(f.tried.find("  missing /probe.layout.json\n") != std::string::npos,
        "a binary in `/` is looked for in `/`, not in the working directory [" + f.tried + "]");
  f = find("rel/dir/probe", nullptr, 0);
  check(f.tried.find("  missing rel/dir/probe.layout.json\n") != std::string::npos,
        "a relative argv0 keeps its directory [" + f.tried + "]");

  fs::remove_all(root, ec);
  return testkit::report("app_files_test");
}
