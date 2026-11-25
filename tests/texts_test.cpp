// texts_test.cpp — the words the library draws, in the person's language: a texts file loaded, an action's description and a
// library note in it, what a note names put in its place, English for what a file does not give, the template a translator starts
// from, and the language a locale names.
#include <cstdlib>
#include <cstring>
#include <string>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h"   /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to read a frame back */
#include "rolltui/c/rolltui_bindings.h" /* …and an action's description as the help reads it */
#include "rolltui_test.hpp"

using testkit::check;

namespace {

std::string description(const RolltuiBindings* b, const char* action) {
  size_t n = 0;
  const char* d = rolltui_bindings_description(b, action, std::strlen(action), &n);
  return d ? std::string(d, n) : std::string("(none)");
}

// The screen of one window showing `content`, as text.
std::string screen_of(RolltuiContext* ctx, const char* content) {
  const std::string layout = std::string(R"({ "resizable": false, "name": "t", "root": { "id": "w", "content": ")") + content +
                             R"(", "border": "rounded" } })";
  RolltuiLayoutReport rep{};
  RolltuiLayout* l = rolltui_load_layout_text(layout.data(), layout.size(), nullptr, 0, nullptr, &rep);
  rolltui_layout_report_release(&rep);
  RolltuiWindows* w = rolltui_windows_new(ctx);
  RolltuiWindowStack* st = rolltui_window_stack_new();
  rolltui_window_stack_set_base(st, rolltui_layout_base(l));
  const RolltuiRect area{0, 0, 50, 5};
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  rolltui_windows_sync(w, st);
  rolltui_windows_layout(w, st, area);
  RolltuiFrame* f = rolltui_frame_new(50, 5, styles[0]);
  RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
  struct Slot {
    RolltuiWindows* w;
    RolltuiStyle* s;
  } slot{w, styles};
  rolltui_window_stack_compose(
      st, f, area, styles, rolltui_layout_default_roles(),
      [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
        Slot* sl = static_cast<Slot*>(c);
        rolltui_windows_draw(sl->w, rn, fr, sl->s, rolltui_windows_default_roles());
      },
      &slot, 0, cs);
  RolltuiStr t{};
  rolltui_frame_to_text(f, &t);
  const std::string out = str_of(t);
  rolltui_str_free(&t);
  rolltui_compose_scratch_free(cs);
  rolltui_frame_free(f);
  rolltui_window_stack_free(st);
  rolltui_windows_free(w);
  rolltui_layout_free(l);
  return out;
}

void test_template() {
  RolltuiContext* ctx = rolltui_context_new();
  rolltui_context_set_library_defaults(ctx);
  RolltuiStr t{};
  rolltui_texts_template(rolltui_bindings_default(ctx), &t);
  const std::string tpl = str_of(t);
  RolltuiStr err{};
  RolltuiJsonValue* root = rolltui_json_parse(t.p, t.n, &err);
  const RolltuiJsonValue* texts = root ? rolltui_json_get(root, "texts", 5) : nullptr;
  size_t n = 0;
  const char* empty = texts ? rolltui_json_as_string(rolltui_json_get(texts, "text.list.empty", 15), "", 0, &n) : "";
  check(texts != nullptr && std::string(empty, n) == "(empty)", "the template is a texts file, the library's notes in it in English [" + str_of(err) + "]");
  check(tpl.find("\"input.complete\": \"complete the word, or the next candidate in the list\"") != std::string::npos,
        "…and every action the table declares, by its name, with its description");
  check(tpl.find("\"text.window.nothing_bound\": \"nothing is bound to '{1}'\"") != std::string::npos, "…a note's {1} kept for the translator");
  check(rolltui_texts_load(t.p, t.n, &err) == 1, "the template loads as it is");
  rolltui_texts_clear();
  rolltui_json_free(root);
  rolltui_str_free(&t);
  rolltui_context_free(ctx);
}

void test_load() {
  RolltuiContext* ctx = rolltui_context_new();
  rolltui_context_set_library_defaults(ctx);
  const RolltuiBindings* b = rolltui_bindings_default(ctx);
  check(description(b, "input.complete") == "complete the word, or the next candidate in the list", "CONTROL: a description is English with no texts file");
  const std::string left_en = description(b, "input.left");
  const std::string fr = R"J({ "language": "fr", "texts": {
      "input.complete": "compléter le mot",
      "text.window.nothing_bound": "rien n'est lié à « {1} »",
      "text.keys.unbound": "(sans touche)",
      "not.an.id": "ignored",
      "text.list.empty": 3 } })J";
  RolltuiStr err{};
  check(rolltui_texts_load(fr.data(), fr.size(), &err) == 1, "a texts file loads [" + str_of(err) + "]");
  check(description(b, "input.complete") == "compléter le mot", "an action's description is the file's, by the action's name");
  check(description(b, "input.left") == left_en && left_en != "(none)", "…and an action the file does not give stays English");
  check(description(b, "not.an.id") == "(none)", "…a text the file gives for no declared action describes nothing");
  // THE HELP DOCUMENT is the live table's: in the file's language
  const char* scopes[] = {"input"};
  const size_t lens[] = {5};
  RolltuiStr doc{};
  rolltui_help_document(b, "", 0, scopes, lens, 1, "", 0, &doc);
  check(str_of(doc).find("compléter le mot") != std::string::npos, "the help document is in the file's language");
  rolltui_str_free(&doc);
  // A NOTE A WIDGET DRAWS, with what it names put in
  const std::string shown = screen_of(ctx, "chart:cpu");
  check(shown.find("rien n'est lié à « cpu »") != std::string::npos, "a window's note is in the file's language, its source put in [" + shown.substr(0, 80) + "]");
  // A FILE THAT IS NOT ONE leaves the language as it was
  const std::string bad = "[1, 2]";
  check(rolltui_texts_load(bad.data(), bad.size(), &err) == 0 && !err.empty(), "a file that is not a texts file is refused, and says why [" + str_of(err) + "]");
  check(description(b, "input.complete") == "compléter le mot", "…and the language loaded before stays");
  const std::string garbled = "{ \"texts\": ";
  check(rolltui_texts_load(garbled.data(), garbled.size(), &err) == 0 && description(b, "input.complete") == "compléter le mot",
        "…as it does for one that does not parse");
  // A SECOND LOAD REPLACES THE FIRST
  const std::string de = R"({ "texts": { "text.window.nothing_bound": "nichts an '{1}' gebunden" } })";
  rolltui_texts_load(de.data(), de.size(), &err);
  check(description(b, "input.complete") == "complete the word, or the next candidate in the list" &&
            screen_of(ctx, "chart:cpu").find("nichts an 'cpu' gebunden") != std::string::npos,
        "a second file replaces the first: what it does not give is English again");
  rolltui_texts_clear();
  check(screen_of(ctx, "chart:cpu").find("nothing is bound to 'cpu'") != std::string::npos, "clearing puts the English back");
  rolltui_str_free(&err);
  rolltui_context_free(ctx);
}

void test_language() {
  char lang[16];
  for (const char* v : {"LC_ALL", "LC_MESSAGES", "LANG"}) ::unsetenv(v);
  check(rolltui_locale_language(lang, sizeof lang) == 2 && std::string(lang) == "en", "with no locale the language is English");
  ::setenv("LANG", "fr_CA.UTF-8", 1);
  check(rolltui_locale_language(lang, sizeof lang) == 2 && std::string(lang) == "fr", "fr_CA.UTF-8 is fr");
  ::setenv("LC_MESSAGES", "de_DE", 1);
  check(rolltui_locale_language(lang, sizeof lang) == 2 && std::string(lang) == "de", "…LC_MESSAGES before LANG");
  ::setenv("LC_ALL", "PT_br.utf8", 1);
  check(rolltui_locale_language(lang, sizeof lang) == 2 && std::string(lang) == "pt", "…LC_ALL before both, in lower case");
  ::setenv("LC_ALL", "C.UTF-8", 1);
  check(rolltui_locale_language(lang, sizeof lang) == 2 && std::string(lang) == "en", "…and C.UTF-8 is POSIX's, English");
  ::setenv("LC_ALL", "sr@latin", 1);
  check(rolltui_locale_language(lang, sizeof lang) == 2 && std::string(lang) == "sr", "…a modifier is not the language");
  for (const char* v : {"LC_ALL", "LC_MESSAGES"}) ::unsetenv(v);
  ::setenv("LANG", "en_US.UTF-8", 1);
}

}  // namespace

int main() {
  test_template();
  test_load();
  test_language();
  return testkit::report("texts_test");
}
