// rolltui/tests/files_test.cpp — reading a directory, which a browser and a picker both do.
//
// Its point is the DISTINCTIONS, because each one is a wrong answer if it collapses: a directory
// that cannot be read is not an empty one, a dotfile is not absent, and "sorted" has to mean the
// same thing every time or a list reorders under a person's cursor.
// ONLY the public header: reading a directory is public because a host writing its own browser
// needs it, so this suite is consumer-shaped and its reach is evidence.
#include "rolltui/rolltui.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <cstdlib>
#include <string>

#include "rolltui_test.hpp"

namespace {
namespace fs = std::filesystem;

void write_file(const fs::path& p, const std::string& text) {
  const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) { (void)!::write(fd, text.data(), text.size()); ::close(fd); }
}

std::string names_of(const RolltuiDirList& l) {
  std::string out;
  for (std::size_t i = 0; i < l.n; ++i) {
    if (i) out += ' ';
    out.append(l.v[i].name.p ? l.v[i].name.p : "", l.v[i].name.n);
  }
  return out;
}

}  // namespace

int main() {
  using rolltui_test::check;
  // A UNIQUE root per run: two concurrent invocations of this suite would otherwise write and
  // delete each other's fixtures, which is the collision three roll suites already pay a lock for.
  const std::string tmp = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
  const fs::path root = fs::path(tmp) / ("rolltui_files_test_" + std::to_string(::getpid()));
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / "zeta");
  fs::create_directories(root / "alpha");
  // NAMED SO THE SORTS DISAGREE. With `big` and `small` the size order and the name order are the
  // same, and a size assertion would pass under a name sort — which is no assertion at all.
  write_file(root / "a-tiny.txt", "hi");
  write_file(root / "z-huge.txt", std::string(500, 'x'));
  write_file(root / ".hidden", "shh");

  RolltuiDirList l{};
  RolltuiStr err{};

  // ---- 1. the entries, and what is NOT among them ------------------------------------------
  check(rolltui_dir_read(root.c_str(), std::strlen(root.c_str()), ROLLTUI_SORT_NAME, 0, &l, &err) == 1,
        "a readable directory is read");
  check(names_of(l) == "alpha zeta a-tiny.txt z-huge.txt",
        "…DIRECTORIES FIRST, then files, each by name — a person walking a tree looks for the next "
        "directory far more often than the largest file [" + names_of(l) + "]");
  check(names_of(l).find(".hidden") == std::string::npos, "…and a dotfile is hidden unless asked for");
  check(names_of(l).find(" . ") == std::string::npos && names_of(l).substr(0, 2) != ". ",
        "…and `.` and `..` are never entries: they are this directory and its parent");

  // ---- 2. asking for the hidden ones --------------------------------------------------------
  check(rolltui_dir_read(root.c_str(), std::strlen(root.c_str()), ROLLTUI_SORT_NAME, ROLLTUI_DIR_HIDDEN, &l, &err) == 1 &&
            names_of(l).find(".hidden") != std::string::npos,
        "…they appear when asked for, so hidden means hidden and not gone");

  // ---- 3. the sorts actually differ ---------------------------------------------------------
  rolltui_dir_read(root.c_str(), std::strlen(root.c_str()), ROLLTUI_SORT_SIZE, 0, &l, &err);
  const std::string by_size = names_of(l);
  check(by_size == "alpha zeta z-huge.txt a-tiny.txt",
        "…by SIZE the largest file comes first — the REVERSE of the name order, so this cannot pass "
        "under a name sort [" + by_size + "]");

  // ---- 4. THE DISTINCTION THAT MATTERS MOST -------------------------------------------------
  // An unreadable directory and an empty one both yield no entries. A caller that cannot tell
  // them apart draws "(empty)" over a permission error, which is a wrong answer wearing a success.
  fs::create_directories(root / "empty");
  check(rolltui_dir_read((root / "empty").c_str(), std::strlen((root / "empty").c_str()),
                         ROLLTUI_SORT_NAME, 0, &l, &err) == 1 && l.n == 0 && err.n == 0,
        "an EMPTY directory reads successfully with no entries and no error");
  const std::string missing = (root / "no-such-dir").string();
  check(rolltui_dir_read(missing.data(), missing.size(), ROLLTUI_SORT_NAME, 0, &l, &err) == 0 && l.n == 0 &&
            err.n != 0 && std::string(err.p, err.n).find(missing) != std::string::npos,
        "…while a MISSING one fails, empties the list, and names the path it could not open [" +
            std::string(err.p ? err.p : "", err.n) + "]");

  rolltui_str_free(&err);
  rolltui_dir_list_release(&l);
  fs::remove_all(root, ec);
  return rolltui_test::report("files_test");
}
