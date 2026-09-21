// rolltui/tests/regex_test.cpp — THE SMALL MATCHER a syntax definition is written in.
//
// What is held to account: every construct it takes does what its name says, every construct it does not take is
// REFUSED WITH A REASON (a language file with a typo must say so, not colour nothing), and it cannot be made to hang or
// crash — by a pattern that loops on nothing, by one that backtracks without end, by random bytes.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_regex.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"

namespace {
using testkit::check;

std::size_t live_bytes() {
  std::size_t live = 0;
  rolltui_mem_stats(nullptr, nullptr, nullptr, &live, nullptr, nullptr);
  return live;
}

struct Hit {
  bool ok = false;
  int start = -1, end = -1;
  int g[10][2];
  Hit() { for (auto& x : g) { x[0] = x[1] = -1; } }
  std::string group(const std::string& text, int i) const { return g[i][0] < 0 ? std::string("<none>") : text.substr(static_cast<std::size_t>(g[i][0]), static_cast<std::size_t>(g[i][1] - g[i][0])); }
};

// Matches `pat` at `at` in `text`; a pattern that does not compile is a failed match with `why` filled.
Hit match(const std::string& pat, const std::string& text, std::size_t at = 0, bool icase = false, const RolltuiRegexRefs* refs = nullptr, std::string* why = nullptr) {
  Hit h;
  RolltuiStr err{};
  RolltuiRegex* re = rolltui_regex_compile(pat.data(), pat.size(), icase ? 1 : 0, &err);
  if (why) *why = std::string(err.p ? err.p : "", err.n);
  rolltui_str_free(&err);
  if (!re) return h;
  RolltuiRegexMatch m{};
  h.ok = rolltui_regex_match_at(re, text.data(), text.size(), at, refs, &m) != 0;
  if (h.ok) {
    h.start = m.start[0];
    h.end = m.end[0];
    for (int i = 0; i < 10; ++i) { h.g[i][0] = m.start[i]; h.g[i][1] = m.end[i]; }
  }
  rolltui_regex_free(re);
  return h;
}

// The text the pattern matched at `at`, or "<no>".
std::string got(const std::string& pat, const std::string& text, std::size_t at = 0, bool icase = false) {
  const Hit h = match(pat, text, at, icase);
  return h.ok ? text.substr(static_cast<std::size_t>(h.start), static_cast<std::size_t>(h.end - h.start)) : std::string("<no>");
}

bool refused(const std::string& pat, std::string* why = nullptr) {
  RolltuiStr err{};
  RolltuiRegex* re = rolltui_regex_compile(pat.data(), pat.size(), 0, &err);
  if (why) *why = std::string(err.p ? err.p : "", err.n);
  rolltui_str_free(&err);
  const bool bad = re == nullptr;
  rolltui_regex_free(re);
  return bad;
}
}  // namespace

int main() {
  const std::size_t base = live_bytes();

  // ---- LITERALS, ANCHORED: a match is asked for AT a position, never searched for -----------------------------------
  check(got("abc", "abc") == "abc", "a literal matches");
  check(got("abc", "xabc") == "<no>", "…and only where it is asked: at 0 it is not there");
  check(got("abc", "xabc", 1) == "abc", "…and at 1 it is");
  check(got("", "abc") == "", "the empty pattern matches nothing, at once");
  check(got("a.c", "abc") == "abc" && got("a.c", "a\xC3\xA9" "c") == "<no>", ".: any one byte (a two-byte letter is two)");
  check(got("caf\xC3\xA9", "caf\xC3\xA9!") == "caf\xC3\xA9", "a literal with a non-ASCII letter is its bytes");

  // ---- CLASSES --------------------------------------------------------------------------------------------------------
  check(got("[abc]+", "cabbage") == "cabba", "a class and a plus");
  check(got("[a-f0-9]+", "3fz") == "3f", "ranges");
  check(got("[^0-9]+", "ab1") == "ab", "a negated class");
  check(got("[^\"]*", "abc\"def") == "abc", "…the usual one: everything up to a quote");
  check(got("[\\]x]+", "]x]y") == "]x]", "an escaped ] inside a class");
  check(got("[]x]+", "]x]y") == "]x]", "a ] first in a class is a member");
  check(got("[a\\-z]+", "a-z") == "a-z", "an escaped hyphen");
  check(got("[\\d_]+", "1_2x") == "1_2", "\\d inside a class");
  check(got("\\d+", "123abc") == "123" && got("\\D+", "ab1") == "ab", "\\d and \\D");
  check(got("\\w+", "foo_bar1 x") == "foo_bar1" && got("\\W+", "  a") == "  ", "\\w and \\W");
  check(got("\\w+", "caf\xC3\xA9 ok") == "caf\xC3\xA9", "a byte of 0x80 or more is a word character");
  check(got("\\s+", " \tx") == " \t" && got("\\S+", "ab c") == "ab", "\\s and \\S");
  check(got("[^x]", "\n") == "<no>", "a line has no newline for a negated class to match");

  // ---- QUANTIFIERS ----------------------------------------------------------------------------------------------------------
  check(got("a*", "aaab") == "aaa" && got("a*", "b") == "", "*");
  check(got("a+", "aaab") == "aaa" && got("a+", "b") == "<no>", "+");
  check(got("ab?c", "ac") == "ac" && got("ab?c", "abc") == "abc", "?");
  check(got("a{3}", "aaaa") == "aaa" && got("a{3}", "aa") == "<no>", "{n}");
  check(got("a{2,}", "aaaa") == "aaaa" && got("a{2,}", "a") == "<no>", "{n,}");
  check(got("a{1,3}", "aaaaa") == "aaa", "{n,m}");
  check(got("a{,3}", "a{,3}") == "a{,3}", "a { that is no quantifier is a literal");
  check(got("<.*>", "<a><b>") == "<a><b>", "greedy takes as much as it can");
  check(got("<.*?>", "<a><b>") == "<a>", "lazy takes as little");
  check(got("a+?", "aaa") == "a" && got("a??", "a") == "", "lazy + and ?");
  check(got("(?:ab)+", "ababx") == "abab", "a repeated group");
  check(got("(ab){2}", "ababab") == "abab", "a counted group");

  // ---- ALTERNATION AND GROUPS ---------------------------------------------------------------------------------------------
  check(got("cat|dog", "dogma") == "dog" && got("cat|dog", "bird") == "<no>", "alternation");
  check(got("a|ab", "ab") == "a", "the first alternative that matches wins (not the longest)");
  check(got("(?:a|b)+", "abba!") == "abba", "a non-capturing group");
  {
    const Hit h = match("(\\w+)\\s*=\\s*(\\d+)", "width = 42;");
    check(h.ok && h.group("width = 42;", 1) == "width" && h.group("width = 42;", 2) == "42", "captures: the text of each group");
  }
  {
    const Hit h = match("(a)|(b)", "b");
    check(h.ok && h.g[1][0] == -1 && h.g[2][0] == 0, "a group that took no part is -1");
  }
  {
    const Hit h = match("(a)+", "aaa");
    check(h.ok && h.g[1][0] == 2 && h.g[1][1] == 3, "a repeated group holds its last turn");
  }

  // ---- ANCHORS AND WORD BOUNDARIES ---------------------------------------------------------------------------------------------
  check(got("^#", "#include") == "#" && got("^#", "  #x", 2) == "<no>", "^ is the start of the LINE, not of the try");
  check(got("x$", "ax", 1) == "x" && got("x$", "xa") == "<no>", "$ is the end of the line");
  check(got("$", "abc", 3) == "" && got("$", "abc", 1) == "<no>", "…and an empty match there is a match");
  check(got("\\bfoo\\b", "foo bar") == "foo" && got("\\bfoo\\b", "foobar") == "<no>", "\\b: a whole word");
  check(got("\\bfoo", "a foo", 2) == "foo" && got("\\bfoo", "afoo", 1) == "<no>", "\\b at a word's start");
  check(got("\\Boo", "foo", 1) == "oo" && got("\\Boo", " oo", 1) == "<no>", "\\B: not at a boundary");
  check(got("\\bcaf\xC3\xA9\\b", "caf\xC3\xA9 ") == "caf\xC3\xA9", "a word boundary holds at a two-byte letter");

  // ---- LOOK-AHEAD --------------------------------------------------------------------------------------------------------------------
  check(got("\"[^\"]*\"(?=\\s*:)", "\"key\": 1") == "\"key\"", "positive look-ahead: a quoted string that is followed by a colon");
  check(got("\"[^\"]*\"(?=\\s*:)", "\"val\", 1") == "<no>", "…and one that is not is refused");
  check(got("foo(?!bar)", "foobaz") == "foo" && got("foo(?!bar)", "foobar") == "<no>", "negative look-ahead");
  check(got("(?=a)a", "ab") == "a", "look-ahead consumes nothing");
  {
    const Hit h = match("(?=(a))a", "a");
    check(h.ok && h.g[1][0] == -1, "what a look-ahead captured is not exported");
  }
  check(got("(?!)", "a") == "<no>", "an empty negative look-ahead never matches: nothing always follows");

  // ---- BACK-REFERENCES: what a region's BEGIN matched (a here-document's terminator, a Lua long bracket's level) ---------------------
  {
    const std::string begin = "==";
    RolltuiRegexRefs refs{};
    refs.p[1] = begin.data();
    refs.n[1] = begin.size();
    check(match("\\]\\1\\]", "]==]", 0, false, &refs).ok, "\\1 is the text the begin matched in group 1");
    check(!match("\\]\\1\\]", "]=]", 0, false, &refs).ok, "…and a different level is not the end");
    check(match("\\]\\1\\]", "]]", 0, false, nullptr).ok, "with no refs given, \\1 is empty");
    RolltuiRegexRefs upper{};
    const std::string e = "EOF";
    upper.p[1] = e.data();
    upper.n[1] = e.size();
    check(match("^\\1$", "eof", 0, true, &upper).ok, "a back-reference folds case with the pattern");
    check(!match("^\\1$", "eof", 0, false, &upper).ok, "…and only then");
  }

  // ---- CASE ------------------------------------------------------------------------------------------------------------------------------
  check(got("select", "SELECT", 0, true) == "SELECT" && got("select", "SELECT") == "<no>", "case-insensitive when asked, exact otherwise");
  check(got("[a-c]+", "ABCd", 0, true) == "ABC", "…and in a class");
  {
    // a letter that ignores case is one instruction, so a long list of words does not run out of character classes
    std::string many = "\\b(?:";
    for (int i = 0; i < 200; ++i) many += (i ? "|" : "") + std::string("Word") + std::to_string(i) + "x";
    many += ")\\b";
    check(got(many, "a WORD199X b", 2, true) == "WORD199X", "two hundred case-insensitive words compile and match: " + got(many, "a WORD199X b", 2, true));
    check(got(many, "a WORD199X b", 2, false) == "<no>", "…and case still counts when it is asked to");
    // the fold must not turn punctuation into a letter: `@` is one bit away from `` ` `` and `[` from `{`
    check(got("a", "A", 0, true) == "A" && got("a", "@", 0, true) == "<no>" && got("a", "`", 0, true) == "<no>" && got("z", "{", 0, true) == "<no>" && got("z", "[", 0, true) == "<no>", "a folded letter matches its two cases and nothing near them");
    check(got("k", "\xC0", 0, true) == "<no>" && got("k", "\xEB", 0, true) == "<no>", "…nor a byte above 0x7F that shares low bits (a UTF-8 lead)");
    RolltuiStr err{};
    RolltuiRegex* re = rolltui_regex_compile("Select", 6, 1, &err);
    check(re && rolltui_regex_first_byte(re, 's') && rolltui_regex_first_byte(re, 'S') && !rolltui_regex_first_byte(re, 'x'), "first_byte knows both cases of a folded first letter");
    rolltui_regex_free(re);
    rolltui_str_free(&err);
  }

  // ---- ESCAPES -----------------------------------------------------------------------------------------------------------------------------
  check(got("a\\.b", "a.b") == "a.b" && got("a\\.b", "axb") == "<no>", "an escaped dot is a dot");
  check(got("\\/\\/", "//x") == "//" && got("\\\"", "\"") == "\"", "escaped punctuation is itself");
  check(got("\\x41\\t", "A\t") == "A\t", "\\xHH and \\t");
  check(got("\\(\\)\\[\\]\\{\\}\\*\\+\\?\\|\\^\\$", "()[]{}*+?|^$") == "()[]{}*+?|^$", "every metacharacter, escaped");

  // ---- WHAT IT REFUSES, AND SAYS WHY ---------------------------------------------------------------------------------------------------------
  std::string why;
  check(refused("(abc", &why) && !why.empty(), "an unclosed group is refused, with a reason [" + why + "]");
  check(refused("abc)", &why) && !why.empty(), "an unmatched ) [" + why + "]");
  check(refused("[abc", &why) && !why.empty(), "an unclosed class [" + why + "]");
  check(refused("*a", &why) && !why.empty(), "a quantifier with nothing to repeat [" + why + "]");
  check(refused("a\\", &why) && !why.empty(), "a trailing backslash [" + why + "]");
  check(refused("\\q", &why) && !why.empty(), "an unknown escape [" + why + "]");
  check(refused("(?<=a)b", &why) && !why.empty(), "look-behind is not taken, and says so [" + why + "]");
  check(refused("(?i)a", &why) && !why.empty(), "inline flags are not taken [" + why + "]");
  check(refused("[z-a]", &why) && !why.empty(), "a range that runs backwards [" + why + "]");
  check(refused("a{5,2}", &why) && !why.empty(), "a repeat that runs backwards [" + why + "]");
  check(refused("a{999}", &why) && !why.empty(), "a repeat count over the limit [" + why + "]");
  check(refused("(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)", &why) && !why.empty(), "more than nine groups [" + why + "]");
  check(refused("\\xZZ", &why) && !why.empty(), "a bad \\x [" + why + "]");
  {
    std::string deep;
    for (int i = 0; i < 60; ++i) deep += "(?:";
    deep += "a";
    for (int i = 0; i < 60; ++i) deep += ")";
    check(refused(deep, &why) && !why.empty(), "groups nested too deep [" + why + "]");
    std::string big = "(?:";
    for (int i = 0; i < 40; ++i) big += "[a-z]{60}";
    big += ")";
    (void)refused("(?:a{64}){64}(?:b{64}){64}", &why);
    check(true, "a pattern that compiles to a great deal is either taken or refused, and either way it returned");
    check(refused("((((a{64}){64}){64}){64})", &why) && !why.empty(), "a pattern that would compile to far too much is refused [" + why + "]");
  }

  // ---- IT CANNOT RUN AWAY ---------------------------------------------------------------------------------------------------------------------------
  {
    const Hit h = match("(a*)*b", std::string(40, 'a'));
    check(!h.ok, "a loop over something that can match nothing terminates: (a*)*b on aaaa… is no match, not a hang");
    check(got("(a*)*", "aaab") == "aaa", "…and where it can match, it does");
    check(got("(a|b*)*c", "abbbc") == "abbbc", "a loop whose body can be empty, and a way through");
    check(!match("(a+)+$", std::string(30, 'a') + "b").ok, "the classic catastrophe, (a+)+$ over aaa…ab, gives up rather than take years");
    (void)match("(.*a){12}", std::string(30, 'a') + "b");
    check(true, "a nested-loop blow-up, (.*a){12} over aaa…ab, returned");
    check(!match("(?:x+x+)+y", std::string(28, 'x')).ok, "x+x+ under a loop, likewise");
  }

  // ---- WHAT CAN BEGIN A MATCH (a rule that cannot start here is skipped without running) --------------------------------------------------------------
  {
    RolltuiRegex* re = rolltui_regex_compile("[a-c]x|\\d", std::strlen("[a-c]x|\\d"), 0, nullptr);
    check(re && rolltui_regex_first_byte(re, 'a') && rolltui_regex_first_byte(re, '7') && !rolltui_regex_first_byte(re, 'z') && !rolltui_regex_first_byte(re, ' '),
          "first_byte: an alternation can begin with a class member or a digit, and with nothing else");
    rolltui_regex_free(re);
    re = rolltui_regex_compile("a*", std::strlen("a*"), 0, nullptr);
    check(re && rolltui_regex_can_be_empty(re) && rolltui_regex_first_byte(re, 'z'), "a pattern that can match nothing can begin with anything: the answer is conservative");
    rolltui_regex_free(re);
    re = rolltui_regex_compile("(?=x)y", std::strlen("(?=x)y"), 0, nullptr);
    check(re && rolltui_regex_first_byte(re, 'y'), "a look-ahead first does not hide what follows it");
    rolltui_regex_free(re);
    re = rolltui_regex_compile("//", std::strlen("//"), 0, nullptr);
    check(re && rolltui_regex_first_byte(re, '/') && !rolltui_regex_first_byte(re, 'a') && rolltui_regex_groups(re) == 0 && !rolltui_regex_uses_refs(re), "a comment opener can begin only with a slash");
    rolltui_regex_free(re);
    re = rolltui_regex_compile("(a)(b)\\1", std::strlen("(a)(b)\\1"), 0, nullptr);
    check(re && rolltui_regex_groups(re) == 2 && rolltui_regex_uses_refs(re), "groups are counted and a back-reference is noticed");
    rolltui_regex_free(re);
  }

  // ---- RANDOM PATTERNS AND RANDOM TEXT: nothing crashes and nothing hangs -----------------------------------------------------------------------------------
  {
    std::uint32_t seed = 12345;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    const char alphabet[] = "ab(|)*+?[]^$.\\{},-:=!dwsbBx1 ";
    int compiled = 0, matched = 0;
    for (int round = 0; round < 4000; ++round) {
      std::string pat;
      const int n = 1 + static_cast<int>(rnd() % 14);
      for (int i = 0; i < n; ++i) pat.push_back(alphabet[rnd() % (sizeof alphabet - 1)]);
      std::string text;
      const int tn = static_cast<int>(rnd() % 24);
      for (int i = 0; i < tn; ++i) text.push_back("ab x1=,-:"[rnd() % 9]);
      RolltuiRegex* re = rolltui_regex_compile(pat.data(), pat.size(), (rnd() & 1) != 0, nullptr);
      if (!re) continue;
      ++compiled;
      for (std::size_t at = 0; at <= text.size(); ++at) {
        RolltuiRegexMatch m{};
        if (rolltui_regex_match_at(re, text.data(), text.size(), at, nullptr, &m)) {
          ++matched;
          if (m.start[0] != static_cast<int>(at) || m.end[0] < m.start[0] || m.end[0] > static_cast<int>(text.size())) check(false, "a match lies inside the text and begins where it was asked: /" + pat + "/ on '" + text + "'");
        }
      }
      rolltui_regex_free(re);
    }
    check(compiled > 500 && matched > 500, "random patterns: " + std::to_string(compiled) + " compiled, " + std::to_string(matched) + " matches, none out of bounds, none hung");
    // and bytes that are not text at all
    RolltuiRegex* re = rolltui_regex_compile("(\\w+|[^a]|\\s)*x", std::strlen("(\\w+|[^a]|\\s)*x"), 0, nullptr);
    std::string junk;
    for (int i = 0; i < 4000; ++i) junk.push_back(static_cast<char>(rnd() & 0xFF));
    for (std::size_t at = 0; at < junk.size(); at += 97) {
      RolltuiRegexMatch m{};
      (void)rolltui_regex_match_at(re, junk.data(), junk.size(), at, nullptr, &m);
    }
    rolltui_regex_free(re);
    check(true, "four thousand random bytes through a loopy pattern: it returned");
  }

  check(live_bytes() == base, "every pattern compiled and freed: the library holds what it held before (" + std::to_string(live_bytes()) + " vs " + std::to_string(base) + ")");
  return testkit::report("rolltui_regex_test");
}
