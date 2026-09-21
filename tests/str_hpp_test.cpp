// str_hpp_test.cpp — `rolltui/str.hpp`: the view and the few string operations a C++ host reaches for beside RolltuiStr.
//
// `std::string` is the ORACLE here and only here: every read operation of the view is compared with its `std::string`
// counterpart over a set of texts and every position that matters (the ends, one either side, past the end), so a difference
// is a bug in the view and not a matter of taste. The builders are checked for content and for what they leave behind: a
// chain of `+`, a long printf and a pile of moves must free everything they took.
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/str.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using testkit::check;

namespace {
std::string sv(StrView v) { return std::string(v.data(), v.size()); }
std::size_t live_bytes() {
  std::size_t b = 0;
  rolltui_mem_stats(nullptr, nullptr, nullptr, &b, nullptr, nullptr);
  return b;
}
}  // namespace

int main() {
  const std::vector<std::string> texts = {"", "a", "/", "abc", "a/b/c.txt", "/usr/local/bin", "aaaa", "abab", "hello world", "x/", "//", "\xE6\x97\xA5/\xE6\x9C\xAC"};
  const std::vector<std::string> needles = {"", "a", "/", "ab", "b/c", "aa", "world", "zz", "abcd", "c.txt", "/"};
  const std::size_t npos = std::string::npos;

  // ---- the view reads exactly as std::string does -------------------------------------------------------------------------
  {
    int bad = 0, total = 0;
    std::string first_bad;
    auto same = [&](bool ok, const std::string& what) {
      ++total;
      if (!ok) { ++bad; if (first_bad.empty()) first_bad = what; }
    };
    for (const std::string& t : texts) {
      const StrView v(t.data(), t.size());
      same(v.size() == t.size() && v.empty() == t.empty(), "size/empty of [" + t + "]");
      same(v.data() != nullptr, "data() is never NULL");
      for (std::size_t pos : {std::size_t(0), std::size_t(1), t.size() > 0 ? t.size() - 1 : 0, t.size(), t.size() + 1, npos}) {
        for (std::size_t len : {std::size_t(0), std::size_t(1), std::size_t(3), npos}) {
          std::string want = pos > t.size() ? "" : t.substr(pos, len);
          same(sv(v.substr(pos, len)) == want, "substr(" + std::to_string(pos) + ", " + std::to_string(len) + ") of [" + t + "]");
        }
      }
      for (const std::string& nd : needles) {
        const StrView n(nd.data(), nd.size());
        for (std::size_t from : {std::size_t(0), std::size_t(1), t.size(), t.size() + 1, npos}) {
          same(v.find(n, from) == t.find(nd, from), "find([" + nd + "], " + std::to_string(from) + ") in [" + t + "]");
          same(v.rfind(n, from) == t.rfind(nd, from), "rfind([" + nd + "], " + std::to_string(from) + ") in [" + t + "]");
        }
        same(v.starts_with(n) == (t.compare(0, nd.size(), nd) == 0 && nd.size() <= t.size()), "starts_with([" + nd + "]) of [" + t + "]");
        same(v.ends_with(n) == (nd.size() <= t.size() && t.compare(t.size() - nd.size(), nd.size(), nd) == 0), "ends_with([" + nd + "]) of [" + t + "]");
        same(v.contains(n) == (t.find(nd) != npos), "contains([" + nd + "]) in [" + t + "]");
        const int c = v.compare(n), w = t.compare(nd);
        same((c < 0) == (w < 0) && (c > 0) == (w > 0), "compare([" + nd + "]) with [" + t + "]");
        same((v == n) == (t == nd) && (v != n) == (t != nd) && (v < n) == (t < nd), "==, != and < of [" + t + "] and [" + nd + "]");
      }
      for (char ch : {'a', '/', 'z', '.'}) {
        for (std::size_t from : {std::size_t(0), std::size_t(2), t.size(), npos}) {
          same(v.find(ch, from) == t.find(ch, from), std::string("find('") + ch + "', " + std::to_string(from) + ") in [" + t + "]");
          same(v.rfind(ch, from) == t.rfind(ch, from), std::string("rfind('") + ch + "', " + std::to_string(from) + ") in [" + t + "]");
        }
      }
      if (!t.empty()) same(v.front() == t.front() && v.back() == t.back() && v[0] == t[0], "front/back/[] of [" + t + "]");
      for (std::size_t k : {std::size_t(0), std::size_t(1), std::size_t(2), t.size(), t.size() + 3}) {
        same(sv(v.first(k)) == t.substr(0, k) && sv(v.last(k)) == (k >= t.size() ? t : t.substr(t.size() - k)), "first/last(" + std::to_string(k) + ") of [" + t + "]");
        same(sv(v.drop_front(k)) == (k >= t.size() ? "" : t.substr(k)) && sv(v.drop_back(k)) == (k >= t.size() ? "" : t.substr(0, t.size() - k)),
             "drop_front/drop_back(" + std::to_string(k) + ") of [" + t + "]");
      }
    }
    check(bad == 0, "the view answers as std::string does across " + std::to_string(total) + " comparisons" + (bad ? " — first difference: " + first_bad : ""));
  }

  // ---- a view of nothing is the empty string, and a NUL byte inside is text -------------------------------------------------
  {
    const StrView none, from_null(static_cast<const char*>(nullptr)), from_str(static_cast<const RolltuiStr&>(RolltuiStr()));
    check(none.empty() && from_null.empty() && from_str.empty() && std::strlen(none.data()) == 0 && none == from_null && none == "", "NULL is the empty string, and its data() is a real empty C string");
    const char raw[] = {'a', '\0', 'b'};
    const StrView v(raw, 3);
    check(v.size() == 3 && v.find('b') == 2 && v.find(StrView("\0b", 2)) == 1 && v != StrView("a", 1), "a view is a length, not a NUL: bytes past a NUL are still its text");
  }

  // ---- conversions: a RolltuiStr, a literal and a const char* all read as a view ----------------------------------------------
  {
    RolltuiStr s;
    s = "hello";
    const char* c = "world";
    auto take = [](StrView v) { return std::string(v.data(), v.size()); };
    check(take(s) == "hello" && take("lit") == "lit" && take(c) == "world" && take(StrView(s).substr(1, 3)) == "ell", "a function that reads takes a StrView and accepts a RolltuiStr, a literal and a const char*");
    check(s == "hello" && StrView(s) == "hello" && StrView("hello") == s, "and equality reads the same from every side");
  }

  // ---- + builds ONE string, and leaves nothing behind -------------------------------------------------------------------------
  {
    const std::size_t before = live_bytes();
    {
      RolltuiStr dir, name;
      dir = "/usr/local";
      name = "bin";
      RolltuiStr joined = dir + "/" + name + ".d" + "/" + StrView("x.json").substr(0, 1);
      check(joined == "/usr/local/bin.d/x", "a chain of + is the concatenation [" + std::string(joined.c_str()) + "]");
      check(dir == "/usr/local" && name == "bin", "…and the operands are untouched");
      RolltuiStr lit = "a" + name;
      RolltuiStr both = StrView("<") + name + ">";
      RolltuiStr moved = std::move(both) + "!";
      check(lit == "abin" && moved == "<bin>!" && both.empty(), "a literal on the left works, and a temporary on the left is used, not copied");
      RolltuiStr acc;
      for (int i = 0; i < 200; ++i) acc += StrView("ab");
      acc += 'z';
      acc += "!";
      check(acc.size() == 402 && acc.c_str()[401] == '!' && acc.c_str()[402] == '\0', "+= appends a view, a char and a literal, and stays NUL-terminated");
      RolltuiStr copy = own(StrView(acc).substr(398, 4));
      check(copy == "abz!", "own() makes an owned copy of part of a string");
    }
    check(live_bytes() == before, "everything built above was freed (" + std::to_string(live_bytes()) + " vs " + std::to_string(before) + ")");
  }

  // ---- appendf / format / to_str ----------------------------------------------------------------------------------------------
  {
    const std::size_t before = live_bytes();
    {
      RolltuiStr s;
      appendf(s, "%d items, %s, %5.1f%%", 3, "ok", 12.34);
      check(s == "3 items, ok,  12.3%", "appendf formats and appends [" + std::string(s.c_str()) + "]");
      appendf(s, " %c", 'x');
      check(s == "3 items, ok,  12.3% x", "…and appends again");
      std::string big(2000, 'q');
      RolltuiStr longer = format("<%s|%d>", big.c_str(), 7);
      check(longer.size() == 2004 && longer.c_str()[0] == '<' && longer.c_str()[2001] == '|' && longer.c_str()[2002] == '7' && longer.c_str()[2003] == '>', "a result past the stack scratch is not truncated");
      check(to_str(0) == "0" && to_str(-42) == "-42" && to_str(9007199254740993LL) == "9007199254740993", "to_str writes any long long exactly");
      RolltuiStr none = format("%s", "");
      check(none.empty() && none.c_str()[0] == '\0', "formatting nothing gives the empty string");
    }
    check(live_bytes() == before, "formatting freed everything (" + std::to_string(live_bytes()) + " vs " + std::to_string(before) + ")");
  }

  // ---- pop_back / trim_trailing ---------------------------------------------------------------------------------------------------
  {
    RolltuiStr s;
    s = "a/b///";
    trim_trailing(s, '/');
    check(s == "a/b" && s.c_str()[3] == '\0', "trim_trailing removes every trailing char and keeps the NUL");
    pop_back(s);
    check(s == "a/" && s.size() == 2, "pop_back drops the last byte");
    RolltuiStr empty;
    pop_back(empty);
    trim_trailing(empty, '/');
    check(empty.empty(), "…and both are no-ops on the empty string");
  }

  // ---- paths --------------------------------------------------------------------------------------------------------------------------
  {
    const struct { const char* path; const char* base; const char* dir; } cases[] = {
        {"a/b.txt", "b.txt", "a"}, {"b.txt", "b.txt", ""}, {"/x", "x", "/"}, {"/", "/", "/"}, {"/usr/local/bin", "bin", "/usr/local"}, {"", "", ""}, {"a/", "", "a"}};
    int bad = 0;
    for (const auto& c : cases)
      if (path_base(c.path) != StrView(c.base) || path_dir(c.path) != StrView(c.dir)) {
        ++bad;
        std::printf("    path [%s]: base [%s] dir [%s]\n", c.path, sv(path_base(c.path)).c_str(), sv(path_dir(c.path)).c_str());
      }
    check(bad == 0, "path_base and path_dir split a path into its last component and everything before it");
  }

  // ---- any string-like type is viewed in place ------------------------------------------------------------------------------------
  {
    const std::string held = "held in a std::string";
    const StrView v = held;
    check(v.size() == held.size() && v.data() == held.data() && v == "held in a std::string", "a std::string is viewed in place, not copied");
    const std::string_view sv = std::string_view(held).substr(5);
    const StrView w = sv;
    check(w == "in a std::string" && w.data() == sv.data(), "a std::string_view is viewed in place");
    const RolltuiStr built = RolltuiStr("a ") + held + " and " + sv;
    check(built == "a held in a std::string and in a std::string", "a string-like type appends onto a RolltuiStr with +");
    check(StrView(std::string()).empty(), "an empty std::string is the empty view");
    check(std::is_convertible_v<const char*, StrView> && std::is_convertible_v<const RolltuiStr&, StrView> && !std::is_convertible_v<int, StrView>,
          "a pointer and a RolltuiStr still convert, and something without data() and size() does not");
  }

  // ---- StrVec: the growing list ---------------------------------------------------------------------------------------------------
  {
    const std::size_t before = live_bytes();
    {
      StrVec v;
      check(v.empty() && v.size() == 0 && !v.contains("x"), "a new list is empty");
      for (int i = 0; i < 100; ++i) v.add(StrView(("item" + std::to_string(i)).c_str()));
      RolltuiStr adopted;
      adopted = "adopted";
      v.add(std::move(adopted));
      check(v.size() == 101 && v[0] == "item0" && v[99] == "item99" && v.back() == "adopted" && adopted.empty(),
            "add copies a view and adopts a moved string, across several growths of the array");
      check(v.contains("item57") && v.contains(StrView("adopted")) && !v.contains("item100") && !v.contains(""), "contains looks for a whole element");
      std::size_t total = 0;
      for (const RolltuiStr& s : v) total += s.size();
      check(total == 10 * 5 + 90 * 6 + 7, "a range-for walks every element (" + std::to_string(total) + ")");
      StrVec moved = std::move(v);
      check(v.empty() && moved.size() == 101 && moved[50] == "item50", "a list moves, and the source is left empty");
      moved.clear();
      moved.add("again");
      check(moved.size() == 1 && moved[0] == "again", "clear() empties it and it fills again");
      StrVec assigned;
      assigned.add("x");
      assigned = std::move(moved);
      check(assigned.size() == 1 && assigned[0] == "again", "move-assigning frees what the target held and takes the source's");
      const StrVec listed = {"default", "vim-ish", ""};
      check(listed.size() == 3 && listed[0] == "default" && listed[1] == "vim-ish" && listed[2].empty() && listed.contains("vim-ish"),
            "a braced list of literals builds a list, each one copied in");
      const StrVec none = {};
      check(none.empty(), "an empty brace is the empty list");
      StrVec copy;
      copy.add("stale");
      copy.assign(listed);
      copy.assign(copy);
      check(copy.size() == 3 && copy[0] == "default" && copy[1] == "vim-ish" && listed.size() == 3, "assign replaces a list's contents with copies, and assigning it to itself changes nothing");
      RolltuiStr word;
      word = "abcdef";
      rolltui::assign(word, StrView(word).substr(2, 3));
      check(word == "cde", "assign of a view into the same string copies before it replaces");
      rolltui::assign(word, "x");
      check(word == "x", "assign of a literal replaces the text");
    }
    check(live_bytes() == before, "the list freed every string and its array (" + std::to_string(live_bytes()) + " vs " + std::to_string(before) + ")");
  }

  return testkit::report("rolltui_str_hpp_test");
}
