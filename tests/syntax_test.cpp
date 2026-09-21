// rolltui/tests/syntax_test.cpp — LANGUAGES AS DATA, AND THE ENGINE THAT RUNS THEM.
//
// The shipped languages are held to what they colour in `syntax_langs_test.cpp`. This holds the ENGINE to what it promises,
// with small languages written here so that each kind of rule is proved on its own: a word set, a pattern with captures, a
// region (its escape, its end, its unterminated form, its span over lines), a here-document's back-reference, an include of
// another context and of another language, a stack of contexts. And what a person writing their own language file meets: a
// mistake is refused WHOLE, with the reason and the rule; a good file replaces the one it names; and nothing in a file, or
// a text, can hang or crash it.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_syntax.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"
#include "syntax_test_helpers.hpp"

namespace {
using testkit::check;
using syntest::add;
using syntest::lang_of;
using syntest::live_bytes;
using syntest::marked;

// a general-purpose little C-like language most sections use
const char* kT1 = R"J({
  "name": "T1", "aliases": ["tee"], "extensions": ["t1"],
  "not_after": ["."],
  "contexts": { "main": [
    { "region": { "begin": "//", "class": "comment" } },
    { "region": { "begin": "/\\*", "end": "\\*/", "class": "comment" } },
    { "region": { "begin": "\"", "end": "\"", "class": "string", "escape": "\\\\.", "single_line": true } },
    { "words": ["if", "else", "return"], "class": "keyword" },
    { "words": ["true", "false", "null"], "class": "constant" },
    { "match": "\\b\\d+(?:\\.\\d+)?\\b", "class": "number" },
    { "match": "\\b([A-Za-z_]\\w*)\\s*(?=\\()", "captures": { "1": "function" } }
  ] }
})J";
}  // namespace

int main() {
  const std::size_t base = live_bytes();
  {
    RolltuiSyntax* s = rolltui_syntax_new();
    std::string why;
    check(add(s, kT1, &why), "a language loads [" + why + "]");
    const int t1 = lang_of(s, "T1");
    check(t1 >= 0, "…and is found by its name");

    // ---- WORDS, NUMBERS, STRINGS, COMMENTS, CAPTURES ---------------------------------------------------------------------
    check(marked(s, t1, "if (x == 42) return \"a\\\"b\"; // done") ==
              "{keyword|if} (x == {number|42}) {keyword|return} {string|\"a}{escape|\\\"}{string|b\"}; {comment|// done}",
          "keywords, a number, a string with an escape, a line comment: " + marked(s, t1, "if (x == 42) return \"a\\\"b\"; // done"));
    check(marked(s, t1, "foo(1) bar (2)") == "{function|foo}({number|1}) {function|bar} ({number|2})", "a capture colours part of a match: " + marked(s, t1, "foo(1) bar (2)"));
    check(marked(s, t1, "iffy elsewhere returned") == "iffy elsewhere returned", "a word is WHOLE: `iffy` is not `if`");
    check(marked(s, t1, "x.if y.return") == "x.if y.return", "a word after a `.` is a member, not a keyword (not_after)");
    check(marked(s, t1, "true false null nully") == "{constant|true} {constant|false} {constant|null} nully", "a second word set, another class");
    check(marked(s, t1, "3.14 9x x9 2") == "{number|3.14} 9x x9 {number|2}", "a number is a whole word: `9x` and `x9` are not numbers: " + marked(s, t1, "3.14 9x x9 2"));
    check(marked(s, t1, "if\tx  ") == "{keyword|if}\tx  ", "whitespace is left alone");
    check(marked(s, t1, "caf\xC3\xA9 if") == "caf\xC3\xA9 {keyword|if}", "a two-byte letter is part of a word, and the run after it is right");

    // ---- A REGION OVER LINES, AND ONE THAT ENDS AT ITS LINE -------------------------------------------------------------------
    check(marked(s, t1, "a /* one\ntwo */ if\nreturn") == "a {comment|/* one}\n{comment|two */} {keyword|if}\n{keyword|return}",
          "a block comment spans lines, and the code after it is code again: " + marked(s, t1, "a /* one\ntwo */ if\nreturn"));
    check(marked(s, t1, "\"open\nif") == "{string|\"open}\n{keyword|if}", "a string that is not closed ends with its line and does not swallow the file");
    check(marked(s, t1, "/* never closed\nif return") == "{comment|/* never closed}\n{comment|if return}", "a block comment that is not closed runs to the end, as it should");
    check(marked(s, t1, "\"a\\\\\" if") == "{string|\"a}{escape|\\\\}{string|\"} {keyword|if}", "an escaped backslash does not escape the quote after it");
    check(marked(s, t1, "// c\nif") == "{comment|// c}\n{keyword|if}", "a line comment ends at the line");

    // ---- LINES ----------------------------------------------------------------------------------------------------------------
    {
      RolltuiHighlight* h = rolltui_highlight_new();
      check(rolltui_highlight_run(h, s, t1, "", 0) == 0, "no text is no lines");
      check(rolltui_highlight_run(h, s, t1, "if\n", 3) == 1, "a final newline does not start another line");
      check(rolltui_highlight_run(h, s, t1, "if\n\n", 4) == 2, "…but a blank line is one");
      check(rolltui_highlight_run(h, s, t1, "if", 2) == 1, "a last line with no newline is a line");
      check(rolltui_highlight_run(h, s, t1, "\n", 1) == 1, "a lone newline is one empty line");
      check(rolltui_highlight_run(h, s, -1, "x", 1) < 0 && rolltui_highlight_run(h, s, 99, "x", 1) < 0, "a language that is not there is refused");
      check(marked(s, t1, "if\r\nreturn\r\n") == "{keyword|if}\n{keyword|return}", "a CRLF is a line end and the \\r is not part of the line: " + marked(s, t1, "if\r\nreturn\r\n"));
      rolltui_highlight_run(h, s, t1, "a if b", 6);
      const RolltuiSyntaxRun* runs = nullptr;
      check(rolltui_highlight_line(h, 0, &runs) == 1 && runs[0].begin == 2 && runs[0].end == 4 && runs[0].cls == ROLLTUI_SYN_KEYWORD, "a run is bytes of the LINE: [2, 4) keyword");
      check(rolltui_highlight_line(h, 5, &runs) == 0 && runs == nullptr, "a line past the end has no runs");
      rolltui_highlight_free(h);
    }

    // ---- A LONG LINE is coloured as far as the limit and no further, and does not slow the rest ------------------------------------------
    {
      std::string longline;
      for (int i = 0; i < 3000; ++i) longline += "if 12 ";
      RolltuiHighlight* h = rolltui_highlight_new();
      check(rolltui_highlight_run(h, s, t1, longline.data(), longline.size()) == 1, "a line of eighteen thousand bytes");
      const RolltuiSyntaxRun* runs = nullptr;
      const std::size_t n = rolltui_highlight_line(h, 0, &runs);
      check(n > 100 && runs[n - 1].end <= 4000, "…is coloured up to a few thousand bytes and plain after them [" + std::to_string(n) + " runs, last ends at " + std::to_string(runs[n - 1].end) + "]");
      rolltui_highlight_free(h);
    }

    // ---- A RUN NEVER ENDS INSIDE A CHARACTER: a pattern matches BYTES, a terminal draws letters ----------------------------------------------------------------------------
    check(marked(s, t1, "\"a\\\xC3\xA9\" if") == "{string|\"a}{escape|\\\xC3\xA9}{string|\"} {keyword|if}", "`\\.` before a two-byte letter escapes the whole letter: " + marked(s, t1, "\"a\\\xC3\xA9\" if"));
    check(marked(s, t1, "\"\\\xE2\x82\xAC\"") == "{string|\"}{escape|\\\xE2\x82\xAC}{string|\"}", "…and a three-byte one");
    {
      std::string longs = "\"";
      for (int i = 0; i < 3000; ++i) longs += "\xC3\xA9";
      longs += "\"";
      RolltuiHighlight* h = rolltui_highlight_new();
      rolltui_highlight_run(h, s, t1, longs.data(), longs.size());
      const RolltuiSyntaxRun* runs = nullptr;
      const std::size_t n = rolltui_highlight_line(h, 0, &runs);
      check(n == 1 && runs[0].end < longs.size() && (static_cast<unsigned char>(longs[runs[0].end]) & 0xC0) != 0x80 && runs[0].end >= 3990,
            "a line cut at the limit is cut between letters (ends at " + std::to_string(n ? runs[n - 1].end : 0) + ")");
      rolltui_highlight_free(h);
    }
    {
      // every edge of every run of a text full of two- and three-byte letters is on a boundary, whatever the rules did
      std::string text;
      for (int i = 0; i < 40; ++i) text += "if \"\xC3\xA9\\\xC3\xA9\\\xE2\x82\xAC\xC3\xA9\" // \xE2\x82\xAC foo(\xC3\xA9) 4\xC3\xA9 /* \xC3\xA9 */\n";
      RolltuiHighlight* h = rolltui_highlight_new();
      const long lines = rolltui_highlight_run(h, s, t1, text.data(), text.size());
      bool onboundary = lines == 40;
      std::size_t at = 0;
      for (long li = 0; li < lines; ++li) {
        const std::size_t nl = text.find('\n', at);
        const RolltuiSyntaxRun* runs = nullptr;
        const std::size_t rn = rolltui_highlight_line(h, static_cast<std::size_t>(li), &runs);
        for (std::size_t i = 0; i < rn; ++i) {
          const std::size_t ll = nl - at;
          if (runs[i].begin < ll && (static_cast<unsigned char>(text[at + runs[i].begin]) & 0xC0) == 0x80) onboundary = false;
          if (runs[i].end < ll && (static_cast<unsigned char>(text[at + runs[i].end]) & 0xC0) == 0x80) onboundary = false;
        }
        at = nl + 1;
      }
      check(onboundary, "no run begins or ends on a continuation byte");
      rolltui_highlight_free(h);
    }

    // ---- ANOTHER CONTEXT, A STACK OF THEM ------------------------------------------------------------------------------------------
    const char* kKv = R"J({
      "name": "KV", "extensions": ["kv"],
      "contexts": {
        "main": [
          { "match": "^\\s*#.*$", "class": "comment" },
          { "match": "^\\s*\\[[^\\]]*\\]", "class": "section" },
          { "match": "^\\s*([\\w.-]+)\\s*(=)", "captures": { "1": "property", "2": "operator" }, "push": "value" }
        ],
        "value": [
          { "match": "$", "pop": true },
          { "match": "\"[^\"]*\"", "class": "string" },
          { "match": "\\b(?:true|false)\\b", "class": "constant" },
          { "match": "\\b\\d+\\b", "class": "number" },
          { "match": ";.*$", "class": "comment", "pop": true }
        ]
      }
    })J";
    check(add(s, kKv, &why), "a language with a second context loads [" + why + "]");
    const int kv = lang_of(s, "kv");
    check(marked(s, kv, "[core]\nname = \"a b\" ; note\nsize = 12\nok = true\n# x") ==
              "{section|[core]}\n{property|name} {operator|=} {string|\"a b\"} {comment|; note}\n{property|size} {operator|=} {number|12}\n{property|ok} {operator|=} {constant|true}\n{comment|# x}",
          "push a context on `=`, pop it at the end of the line: " + marked(s, kv, "[core]\nname = \"a b\" ; note\nsize = 12\nok = true\n# x"));
    check(marked(s, kv, "  = 3\njust words") == "  = 3\njust words", "a line with no key is plain and never enters the value context");
    check(marked(s, kv, "a = 1\nb = 2\nc = 3\n[s]") == "{property|a} {operator|=} {number|1}\n{property|b} {operator|=} {number|2}\n{property|c} {operator|=} {number|3}\n{section|[s]}",
          "`$` pops the value at the end of EVERY line, so the next line starts in main again: " + marked(s, kv, "a = 1\nb = 2\nc = 3\n[s]"));

    // ---- `$` AT THE END OF A LINE: a region may end there, and so may a context ---------------------------------------------------------------------------------------
    check(add(s, R"J({ "name": "Eol", "extensions": ["eol"], "contexts": { "main": [
        { "region": { "begin": "#", "end": "$", "class": "comment" } },
        { "match": "<", "push": "angle", "class": "punct" },
        { "words": ["a"], "class": "keyword" } ],
      "angle": [ { "match": "(?=\\s*$)", "pop": true }, { "match": ">", "class": "punct", "pop": true }, { "words": ["b"], "class": "type" } ] } })J", &why), "a region that ends by `$` loads [" + why + "]");
    const int eol = lang_of(s, "Eol");
    check(marked(s, eol, "a # c\na") == "{keyword|a} {comment|# c}\n{keyword|a}", "a region whose end is `$` ends with its line: " + marked(s, eol, "a # c\na"));
    check(marked(s, eol, "< b\na < b >\na") == "{punct|<} {type|b}\n{keyword|a} {punct|<} {type|b} {punct|>}\n{keyword|a}",
          "a context left open at the end of a line is popped by a rule that looks ahead to it: " + marked(s, eol, "< b\na < b >\na"));

    // ---- A NAMED CONTEXT WITH A CLASS OF ITS OWN: nesting block comments, without a catch-all rule -----------------------------------------------------------------------
    check(add(s, R"J({ "name": "Nest", "extensions": ["nst"], "contexts": {
        "main": [ { "match": "/\\*", "class": "comment", "push": "block" }, { "words": ["a"], "class": "keyword" } ],
        "block": { "class": "comment", "rules": [ { "match": "/\\*", "push": "block" }, { "match": "\\*/", "pop": true } ] } } })J", &why), "a context given as { class, rules } loads [" + why + "]");
    const int nst = lang_of(s, "Nest");
    check(marked(s, nst, "a /* x /* y */ a */ a\n/* z\nb */ a") == "{keyword|a} {comment|/* x /* y */ a */} {keyword|a}\n{comment|/* z}\n{comment|b */} {keyword|a}",
          "block comments nest, over lines, and a rule with no class draws in its context's: " + marked(s, nst, "a /* x /* y */ a */ a\n/* z\nb */ a"));
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [], "b": { "class": "colour", "rules": [] } } })J", &why) && why.find("no such class") != std::string::npos, "a context's class that is not one is refused [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [], "b": { "class": "comment" } } })J", &why) && why.find("rules must be a list") != std::string::npos, "a context object with no rules is refused [" + why + "]");

    // ---- WHAT A WORD IS: letters, digits, _, and what the language adds -----------------------------------------------------------------------------------------------------
    check(add(s, R"J({ "name": "Dash", "extensions": ["dsh"], "word_chars": "-", "contexts": { "main": [ { "words": ["set", "set-url"], "class": "keyword" } ] } })J", &why), "a language whose words may hold `-` loads [" + why + "]");
    const int dash = lang_of(s, "Dash");
    check(marked(s, dash, "set set-url set-x --set x-set") == "{keyword|set} {keyword|set-url} set-x --set x-set", "`set-url` is one word, `set-x` is another that is not a keyword, and neither `--set` nor `x-set` is `set`: " + marked(s, dash, "set set-url set-x --set x-set"));
    check(!add(s, R"J({ "name": "X", "word_chars": "a", "contexts": { "main": [] } })J", &why) && why.find("punctuation") != std::string::npos, "word_chars of a letter is refused [" + why + "]");
    check(!add(s, R"J({ "name": "X", "word_chars": " ", "contexts": { "main": [] } })J", &why) && why.find("punctuation") != std::string::npos, "…and of a space [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "words": ["a-b"], "class": "keyword" } ] } })J", &why) && why.find("whole word") != std::string::npos, "…and without it a word with a `-` is still refused [" + why + "]");

    // ---- BACK-REFERENCES: a here-document, a long bracket ---------------------------------------------------------------------------------------
    const char* kHere = R"J({
      "name": "Here", "extensions": ["here"],
      "contexts": { "main": [
        { "region": { "begin": "<<-?(\\w+)", "end": "^\\s*\\1$", "class": "string" } },
        { "region": { "begin": "\\[(=*)\\[", "end": "\\]\\1\\]", "class": "string" } },
        { "words": ["cat", "echo"], "class": "keyword" }
      ] }
    })J";
    check(add(s, kHere, &why), "a language whose region ends by a back-reference loads [" + why + "]");
    const int here = lang_of(s, "Here");
    check(marked(s, here, "cat <<EOF\nbody EOF x\nEOF\necho") == "{keyword|cat} {string|<<EOF}\n{string|body EOF x}\n{string|EOF}\n{keyword|echo}",
          "a here-document ends at ITS terminator, not at the word inside the body: " + marked(s, here, "cat <<EOF\nbody EOF x\nEOF\necho"));
    check(marked(s, here, "[==[ a ]] b ]==] cat") == "{string|[==[ a ]] b ]==]} {keyword|cat}", "a long bracket ends at its own level: " + marked(s, here, "[==[ a ]] b ]==] cat"));
    check(marked(s, here, "[[ a\nb ]] cat") == "{string|[[ a}\n{string|b ]]} {keyword|cat}", "…and a level-0 one, over two lines");

    // ---- NESTED RULES, and an include of the language's own main context (an interpolation inside a string) -----------------------------------
    const char* kInterp = R"J({
      "name": "Interp", "extensions": ["itp"],
      "contexts": { "main": [
        { "region": { "begin": "`", "end": "`", "class": "string", "escape": "\\\\.",
                      "rules": [ { "region": { "begin": "\\$\\{", "end": "\\}", "class": "plain", "begin_class": "punct", "end_class": "punct",
                                               "rules": [ { "include": "main" } ] } } ] } },
        { "words": ["let", "if"], "class": "keyword" },
        { "match": "\\b\\d+\\b", "class": "number" }
      ] }
    })J";
    check(add(s, kInterp, &why), "a language whose region holds a region that includes main loads [" + why + "]");
    const int itp = lang_of(s, "Interp");
    check(marked(s, itp, "let `a ${if 42} b` 7") == "{keyword|let} {string|`a }{punct|${}{keyword|if} {number|42}{punct|}}{string| b`} {number|7}",
          "code inside `${ }` inside a string is code: " + marked(s, itp, "let `a ${if 42} b` 7"));

    // ---- ANOTHER LANGUAGE'S RULES (a page that holds a script) ---------------------------------------------------------------------------------------
    const char* kPage = R"J({
      "name": "Page", "extensions": ["pg"],
      "contexts": { "main": [
        { "region": { "begin": "<script>", "end": "(?=</script>)", "class": "plain", "begin_class": "tag", "rules": [ { "include": "lang:T1" } ] } },
        { "match": "</script>", "class": "tag" },
        { "words": ["hello"], "class": "type" }
      ] }
    })J";
    check(add(s, kPage, &why), "a language that includes another by name loads [" + why + "]");
    const int pg = lang_of(s, "page");
    check(marked(s, pg, "hello <script>if (1) return</script> hello") == "{type|hello} {tag|<script>}{keyword|if} ({number|1}) {keyword|return}{tag|</script>} {type|hello}",
          "inside the script, the other language's rules apply; outside, they do not: " + marked(s, pg, "hello <script>if (1) return</script> hello"));
    check(marked(s, pg, "if return") == "if return", "…and `if` on a page is not a keyword");

    // ---- CASE ----------------------------------------------------------------------------------------------------------------------------------------
    check(add(s, R"J({ "name": "Sql", "extensions": ["sqlx"], "ignore_case": true,
        "contexts": { "main": [ { "words": ["select", "from"], "class": "keyword" }, { "match": "--.*$", "class": "comment" } ] } })J", &why), "a case-insensitive language loads");
    const int sql = lang_of(s, "SQL");
    check(marked(s, sql, "SELECT a FROM b -- x") == "{keyword|SELECT} a {keyword|FROM} b {comment|-- x}", "…and keywords match in any case");

    // ---- WHAT CANNOT MAKE A RULE HANG ----------------------------------------------------------------------------------------------------------------
    check(add(s, R"J({ "name": "Empty", "extensions": ["emp"],
        "contexts": { "main": [ { "match": "x*", "class": "keyword" }, { "words": ["a"], "class": "type" } ] } })J", &why), "a rule that can match nothing loads");
    check(marked(s, lang_of(s, "Empty"), "a xx b") == "{type|a} {keyword|xx} b",
          "…and a match of nothing that does nothing is skipped, so the line still ends: " + marked(s, lang_of(s, "Empty"), "a xx b"));
    check(add(s, R"J({ "name": "Loop", "extensions": ["lp"],
        "contexts": { "main": [ { "region": { "begin": "(?=a)", "end": "(?=b)", "class": "string" } } ] } })J", &why), "a region that begins by looking ahead loads");
    check(marked(s, lang_of(s, "Loop"), "aaaa\naaaa") == "{string|aaaa}\n{string|aaaa}", "…and a begin that consumes nothing cannot loop forever: the region opens, holds the text, and the line ends: " + marked(s, lang_of(s, "Loop"), "aaaa\naaaa"));
    {
      std::string deep = R"J({ "name": "Deep", "extensions": ["dp"], "contexts": { "main": [ { "match": "\\(", "class": "punct", "push": "main" }, { "match": "\\)", "class": "punct", "pop": true } ] } })J";
      check(add(s, deep, &why), "a language that pushes itself loads");
      const std::string many(500, '(');
      check(marked(s, lang_of(s, "Deep"), many + "))))\n(") == "{punct|" + many + "))))}\n{punct|(}", "…and five hundred pushes do not overflow: the stack is capped, the line and the next are right");
    }

    // ---- WHAT A PERSON WRITING THEIR OWN FILE MEETS: a mistake is refused, whole, with the reason -------------------------------------------------------
    const std::size_t langs_before = rolltui_syntax_language_count(s);
    check(!add(s, "not json", &why) && why.find("JSON") != std::string::npos, "not JSON: refused and said so [" + why + "]");
    // THE MISTAKE EVERYONE MAKES: a pattern's backslash written once, which JSON does not read as one
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "match": "\d+", "class": "number" } ] } })J", &why) && why.find("unknown escape \\d") != std::string::npos && why.find("written twice") != std::string::npos,
          "a single backslash in a pattern is refused with what it was and what to write instead [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "match": "\%" } ] } })J", &why) && why.find("unknown escape \\%") != std::string::npos, "…for any character, not only the usual ones [" + why + "]");
    check(!add(s, R"J({ "contexts": { "main": [] } })J", &why) && why.find("name") != std::string::npos, "no name [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "other": [] } })J", &why) && why.find("main") != std::string::npos, "no main context [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "match": "a", "class": "keyword" }, { "match": "(b", "class": "keyword" } ] } })J", &why) &&
              why.find("X") != std::string::npos && why.find("rule 2") != std::string::npos && why.find("not closed") != std::string::npos,
          "a bad pattern names the language, the rule and the reason [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "match": "a", "class": "colour" } ] } })J", &why) && why.find("no such class") != std::string::npos, "an unknown class [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "match": "a", "push": "nowhere" } ] } })J", &why) && why.find("does not exist") != std::string::npos, "a push to a context that is not there [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "include": "nowhere" } ] } })J", &why) && why.find("no such context") != std::string::npos, "an include of a context that is not there [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "match": "(a)", "captures": { "2": "keyword" } } ] } })J", &why) && why.find("group") != std::string::npos, "a capture of a group the pattern has not got [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "match": "a", "push": "main", "pop": true } ] } })J", &why) && why.find("not more than one") != std::string::npos, "two actions in one rule [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "words": ["a-b"], "class": "keyword" } ] } })J", &why) && why.find("whole word") != std::string::npos, "a word that could never match [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "words": ["a"] } ] } })J", &why) && why.find("class") != std::string::npos, "a words rule with no class [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "nothing": 1 } ] } })J", &why) && why.find("needs one of") != std::string::npos, "a rule of no kind [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "region": { "class": "string" } } ] } })J", &why) && why.find("begin") != std::string::npos, "a region with no begin [" + why + "]");
    check(!add(s, R"J({ "name": "X", "contexts": { "main": [ { "region": { "begin": "\"", "end": "(", "class": "string" } } ] } })J", &why), "a region whose end is a bad pattern");
    check(!add(s, R"J({ "name": "X", "first_line": "(", "contexts": { "main": [] } })J", &why), "a first_line that is a bad pattern");
    check(rolltui_syntax_language_count(s) == langs_before && lang_of(s, "X") < 0, "…and NONE of the refused files left anything behind: the set is as it was");
    check(add(s, R"J({ "name": "Good", "extensions": ["gd"], "contexts": { "main": [ { "words": ["a"], "class": "keyword" } ] } })J", &why) && marked(s, lang_of(s, "Good"), "a b") == "{keyword|a} b",
          "…and a good file after them loads and works");

    // ---- A LANGUAGE OF THE SAME NAME REPLACES THE ONE THERE (how a person changes a shipped one) ---------------------------------------------------------
    check(add(s, R"J({ "name": "good", "extensions": ["gd"], "contexts": { "main": [ { "words": ["b"], "class": "type" } ] } })J", &why), "a language with the name of one already there, in another case, loads");
    check(marked(s, lang_of(s, "Good"), "a b") == "a {type|b}", "…and REPLACES it: the old rules are gone");
    check(std::string(rolltui_syntax_language_name(s, lang_of(s, "good"))) == "good", "…under its own spelling");

    // ---- FINDING A LANGUAGE -------------------------------------------------------------------------------------------------------------------------------
    check(lang_of(s, "tee") == t1 && lang_of(s, "TEE") == t1 && lang_of(s, "T1") == t1, "a fence finds a language by name or alias, case ignored");
    check(lang_of(s, "cobol") < 0 && lang_of(s, "") < 0, "…and finds nothing for a name nobody has");
    check(add(s, R"J({ "name": "Ts", "extensions": ["ts"], "filenames": ["Buildfile"], "first_line": "^#!.*\\bnode\\b",
        "contexts": { "main": [ { "words": ["let"], "class": "keyword" } ] } })J", &why), "(a language with an extension, a filename and a shebang)");
    check(add(s, R"J({ "name": "Dts", "extensions": ["d.ts"], "contexts": { "main": [ { "words": ["declare"], "class": "keyword" } ] } })J", &why), "(and one with a longer extension)");
    const int ts = lang_of(s, "ts"), dts = lang_of(s, "dts");
    auto file = [&](const std::string& p, const std::string& first = "") { return rolltui_syntax_find_file(s, p.data(), p.size(), first.empty() ? nullptr : first.data(), first.size()); };
    check(file("/a/b/x.ts") == ts && file("x.TS") == ts, "a file is found by its extension, case ignored");
    check(file("/a/lib.d.ts") == dts, "…the LONGEST extension wins: `.d.ts` is not `.ts`");
    check(file("/a/Buildfile") == ts && file("Buildfile") == ts, "…a whole file name is found by that");
    check(file("/a/x.unknown") < 0 && file("/a/noext") < 0 && file("/a/.ts") < 0, "…and one nobody claims is not");
    check(file("/a/run", "#!/usr/bin/env node") == ts, "a file with no extension is found by what its first line says");
    check(file("/a/run", "#!/bin/sh") < 0, "…and a first line that says something else is not");

    // ---- THE MARKDOWN SEAM: once per line, in order, through a sink --------------------------------------------------------------------------------------------
    {
      RolltuiSyntaxMd* md = rolltui_syntax_md_new(s, ROLLTUI_ROLE_MD_CODE_BLOCK);
      std::vector<std::string> lines = {"if (1) {", "  return 2 // done", "}"};
      std::vector<RolltuiMdCodeLine> cl;
      for (const auto& l : lines) cl.push_back(RolltuiMdCodeLine{l.data(), l.size()});
      struct Sink { std::vector<std::array<int, 3>> got; } sink;
      auto emit = [](void* p, std::size_t b, std::size_t e, unsigned char role) { static_cast<Sink*>(p)->got.push_back({static_cast<int>(b), static_cast<int>(e), role}); };
      for (std::size_t i = 0; i < cl.size(); ++i) rolltui_syntax_md_highlight(md, "t1", 2, cl.data(), cl.size(), i, emit, &sink);
      const unsigned char kw = rolltui_syntax_role(ROLLTUI_SYN_KEYWORD, ROLLTUI_ROLE_MD_CODE_BLOCK), num = rolltui_syntax_role(ROLLTUI_SYN_NUMBER, ROLLTUI_ROLE_MD_CODE_BLOCK);
      const int kw_bold = kw | ROLLTUI_MD_ROLE_BOLD, cm_italic = rolltui_syntax_role(ROLLTUI_SYN_COMMENT, ROLLTUI_ROLE_MD_CODE_BLOCK) | ROLLTUI_MD_ROLE_ITALIC;
      check(sink.got.size() == 5 && sink.got[0] == std::array<int, 3>{0, 2, kw_bold} && sink.got[1] == std::array<int, 3>{4, 5, num},
            "the seam emits each line's runs as theme roles, offsets within the line (" + std::to_string(sink.got.size()) + " spans)");
      check(sink.got[4] == std::array<int, 3>{11, 18, cm_italic}, "…a keyword asks for bold and a comment for italic, in the top two bits of the role byte");
      Sink none;
      rolltui_syntax_md_highlight(md, "cobol", 5, cl.data(), cl.size(), 0, emit, &none);
      check(none.got.empty(), "…and a language nobody has is called and emits nothing");
      Sink second;
      std::vector<std::string> other = {"return"};
      std::vector<RolltuiMdCodeLine> ol = {RolltuiMdCodeLine{other[0].data(), other[0].size()}};
      rolltui_syntax_md_highlight(md, "t1", 2, ol.data(), ol.size(), 0, emit, &second);
      check(second.got.size() == 1, "…and a second block, at its first line, is run afresh, not read from the first block's cache");
      rolltui_syntax_md_free(md);
    }

    // ---- THE ROLE TABLE -----------------------------------------------------------------------------------------------------------------------------------------
    check(rolltui_syntax_role(ROLLTUI_SYN_PLAIN, 7) == 7 && rolltui_syntax_role(ROLLTUI_SYN_OPERATOR, 7) == 7, "a plain class, and an operator, are drawn as the block's own text");
    check(rolltui_syntax_role(ROLLTUI_SYN_KEYWORD, 7) != rolltui_syntax_role(ROLLTUI_SYN_STRING, 7) && rolltui_syntax_role(ROLLTUI_SYN_STRING, 7) != rolltui_syntax_role(ROLLTUI_SYN_COMMENT, 7),
          "a keyword, a string and a comment are three different roles");
    // what a class asks for on top of its role: weight for a keyword, slant for a comment, nothing for the rest
    check(rolltui_syntax_flags(ROLLTUI_SYN_KEYWORD) == ROLLTUI_SYN_BOLD && rolltui_syntax_flags(ROLLTUI_SYN_PREPROC) == ROLLTUI_SYN_BOLD, "a keyword and a preprocessor line are bold");
    check(rolltui_syntax_flags(ROLLTUI_SYN_COMMENT) == ROLLTUI_SYN_ITALIC && rolltui_syntax_flags(ROLLTUI_SYN_DOC) == ROLLTUI_SYN_ITALIC && rolltui_syntax_flags(ROLLTUI_SYN_ATTRIBUTE) == ROLLTUI_SYN_ITALIC,
          "a comment, a doc comment and an annotation are italic");
    {
      bool none = true;
      for (unsigned char c : {ROLLTUI_SYN_PLAIN, ROLLTUI_SYN_TYPE, ROLLTUI_SYN_FUNCTION, ROLLTUI_SYN_STRING, ROLLTUI_SYN_ESCAPE, ROLLTUI_SYN_NUMBER, ROLLTUI_SYN_CONSTANT, ROLLTUI_SYN_OPERATOR, ROLLTUI_SYN_PUNCT,
                              ROLLTUI_SYN_VARIABLE, ROLLTUI_SYN_TAG, ROLLTUI_SYN_PROPERTY, ROLLTUI_SYN_SECTION})
        none = none && rolltui_syntax_flags(c) == 0;
      check(none && rolltui_syntax_flags(200) == 0, "…and no other class asks for either (a section is bold through its own role)");
    }
    check(rolltui_syntax_role(ROLLTUI_SYN_PUNCT, 7) == ROLLTUI_ROLE_TEXT_MUTED, "punctuation is drawn in the muted text role: a `label` has a ground of its own that would show as a patch behind it");
    check(ROLLTUI_ROLE_COUNT <= ROLLTUI_MD_ROLE_MASK && (ROLLTUI_MD_ROLE_BOLD & ROLLTUI_MD_ROLE_MASK) == 0 && (ROLLTUI_MD_ROLE_ITALIC & ROLLTUI_MD_ROLE_MASK) == 0, "every Role fits under the two attribute bits the seam borrows");
    check(std::string(rolltui_syntax_class_name(ROLLTUI_SYN_KEYWORD)) == "keyword" && rolltui_syntax_class_name(200) == nullptr, "class names are what a file spells");

    // ---- RANDOM TEXT THROUGH EVERY LANGUAGE HERE: nothing crashes, nothing hangs, no run is out of bounds -------------------------------------------------------
    {
      std::uint32_t seed = 99;
      auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
      const char alphabet[] = "abif\"'`/*<>{}()[]$#;=- \t\n\\0189.,:";
      RolltuiHighlight* h = rolltui_highlight_new();
      bool bounded = true;
      for (int round = 0; round < 600; ++round) {
        std::string text;
        const int n = static_cast<int>(rnd() % 200);
        for (int i = 0; i < n; ++i) text.push_back(alphabet[rnd() % (sizeof alphabet - 1)]);
        for (std::size_t l = 0; l < rolltui_syntax_language_count(s); ++l) {
          if (!rolltui_syntax_language_name(s, static_cast<int>(l))) continue;
          const long lines = rolltui_highlight_run(h, s, static_cast<int>(l), text.data(), text.size());
          std::size_t at = 0;
          for (long li = 0; li < lines; ++li) {
            const std::size_t nl = text.find('\n', at);
            const std::size_t len = (nl == std::string::npos ? text.size() : nl) - at;
            const RolltuiSyntaxRun* runs = nullptr;
            const std::size_t rn = rolltui_highlight_line(h, static_cast<std::size_t>(li), &runs);
            unsigned prev = 0;
            for (std::size_t i = 0; i < rn; ++i) {
              if (runs[i].begin < prev || runs[i].end <= runs[i].begin || runs[i].end > len || runs[i].cls >= ROLLTUI_SYN_COUNT) bounded = false;
              prev = runs[i].end;
            }
            at = nl == std::string::npos ? text.size() : nl + 1;
          }
        }
      }
      check(bounded, "random text through every language: runs are sorted, non-empty, inside the line, and a class that exists");
      rolltui_highlight_free(h);
    }

    // ---- RANDOM DAMAGE TO A LANGUAGE FILE: it loads or it is refused, and either way nothing is left broken ------------------------------------------------------------
    {
      std::uint32_t seed = 4242;
      auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
      int loaded = 0, refused = 0;
      for (int round = 0; round < 1500; ++round) {
        std::string doc = kT1;
        const int edits = 1 + static_cast<int>(rnd() % 3);
        for (int e = 0; e < edits && !doc.empty(); ++e) {
          const std::size_t at = rnd() % doc.size();
          switch (rnd() % 3) {
            case 0: doc[at] = static_cast<char>("{}[]\",:\\()*+?^$|.a1 "[rnd() % 20]); break;
            case 1: doc.erase(at, 1 + rnd() % 3); break;
            default: doc.insert(at, 1, "{}[]\",:\\()*+?"[rnd() % 14]); break;
          }
        }
        RolltuiSyntax* fresh = rolltui_syntax_new();
        if (add(fresh, doc)) {
          ++loaded;
          const int l = lang_of(fresh, "T1") >= 0 ? lang_of(fresh, "T1") : 0;
          (void)marked(fresh, l, "if (x == 42) return \"a\\\"b\"; // done\n/* c */\nfoo(1)");
        } else {
          ++refused;
        }
        rolltui_syntax_free(fresh);
      }
      check(loaded > 20 && refused > 20, "1,500 damaged copies of a language file: " + std::to_string(loaded) + " loaded, " + std::to_string(refused) + " refused, none crashed, none hung");
    }
    rolltui_syntax_free(s);
  }

  // ---- SPEED: a quarter of a megabyte of code through a language --------------------------------------------------------------------------------------------------------
  {
    RolltuiSyntax* s = rolltui_syntax_new();
    add(s, kT1);
    std::string big;
    while (big.size() < 262144) big += "if (count == 42) { return foo(\"text\", 3.14); } // trailing comment here\n/* a block\n comment */ let x = 7;\n";
    RolltuiHighlight* h = rolltui_highlight_new();
    const auto t0 = std::chrono::steady_clock::now();
    const long lines = rolltui_highlight_run(h, s, lang_of(s, "T1"), big.data(), big.size());
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    check(lines > 2000 && ms < 1500.0, "a quarter of a megabyte (" + std::to_string(lines) + " lines) is coloured in " + std::to_string(ms) + " ms");
    rolltui_highlight_free(h);
    rolltui_syntax_free(s);
  }

  check(live_bytes() == base, "after every language loaded, refused, replaced, run and freed, the library holds what it held before (" + std::to_string(live_bytes()) + " vs " + std::to_string(base) + ")");
  return testkit::report("rolltui_syntax_test");
}
