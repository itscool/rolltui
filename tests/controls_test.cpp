//
// controls_test.cpp — the library's negative controls, as PERMANENT tests.
//
// WHAT THIS REPLACES. Twenty of these were run once each during the C port: applied to a source
// file by hand, built, observed to fail by name, and reverted. They are prose in a journal
// entry, and NOTHING RE-RUNS ANY OF THEM — so nothing would notice if one of the assertions they
// proved became vacuous tomorrow. That is the same shape as the defect they were built to catch,
// one level up: a check that has stopped checking looks exactly like one that passes.
//
// HOW A CONTROL BECOMES A TEST. The defect state lives at the guarantee's own site, behind a
// named flag (`testkit_ctl_on("area.what_it_breaks")`, testkit/testctl.h), and this suite flips
// it. `check_controlled` then asserts three things in one binary: the guarantee HOLDS with the
// control off, is GONE with it on, and holds again once restored. The middle one is the half the
// hand method could not do — a stale binary and a control that does not reach its guarantee look
// identical from outside, and this repository has seven-plus false greens from exactly that.
//
// THE FLAG NEVER REACHES THE SHIPPED LIBRARY. This binary links `rolltui_ctl`; every host, app
// and other suite links `rolltui`, where `testkit_ctl_on` is a macro that discards its argument
// and the table is not compiled at all. `rolltui-shipped-artifact-test` asserts that, and proves
// its own scanner can find a string before believing it found none.
//
// THE TWO SHAPES EVERY CONTROL HERE TAKES: a classification inverted (a wrong answer that is
// still a legal answer) and a reduction made a no-op (a step skipped where the output stays well
// formed). Neither can fail loudly, which is why each earns a control rather than a comment.
//
// WRITING THE CONTROL POINT IS TRANSCRIPTION; WRITING THE PREDICATE IS NOT. A predicate has to
// read the guarantee itself and nothing beside it, and three here did not on the first attempt —
// each caught by the middle or the third assertion rather than by review. Two rules came out of
// that and both are worth having in hand before adding one:
//   * BUILD THE STATE INSIDE THE PREDICATE. Anything laid out, flowed or cached in front of the
//     control answers from bytes the flag never reached, and the middle assertion passes.
//   * ASSERT THE GUARANTEE, NOT A CONSEQUENCE OF IT. `rows_for(20) > 1` is satisfied by a
//     SECOND rule in the same function (a full last column opens a row), so it stayed true with
//     the break rule gone. The coordinate a character is drawn at is the rule itself.
// A defect state that mutates state OUTLIVING the control window fails the third assertion
// instead — which is the same fix: own the state inside the lambda.
//
// Each control below is followed by a NON-CONTROLLED sibling asserting the other side of its
// rule, so "the guarantee holds" cannot be satisfied by the thing never happening at all.
//
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// INTERNAL headers, BY NAME. A control asserts a guarantee at the place the library MAKES it,
// and several of those places are internal by design. Routing a control through a public call
// it does not belong to would make it assert something adjacent to the guarantee instead of the
// guarantee, which is the one failure this mechanism exists to rule out.
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_widget_input.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_markdown.h"
#include "rolltui/c/rolltui_widget_menu.h"
#include "rolltui/c/rolltui_presets.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme.h"  // rolltui_sgr: an internal header, the way the
                                      // library's own suites reach what no host needs.
#include "rolltui/c/rolltui_widget_transcript.h"
#include "rolltui/c/rolltui_widgets.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;
using namespace testkit;

namespace {

// The library's own diff classifier, reached the way a host reaches it. A local
// reimplementation would leave the control flipping a flag nothing under test reads.
const char* line_at(const void* block, size_t i, size_t* len) {
  const auto& v = *static_cast<const std::vector<std::string>*>(block);
  *len = v[i].size();
  return v[i].data();
}

RolltuiRole first_role_of(const char* line) {
  RolltuiDiffScratch* s = rolltui_diff_scratch_new();
  const std::vector<std::string> block{line};
  RolltuiDiffSpan out[ROLLTUI_DIFF_MAX_SPANS];
  const size_t n = rolltui_diff_spans(s, "diff", 4, &block, block.size(), &line_at, 0,
                                      rolltui_diff_default_roles(), out, ROLLTUI_DIFF_MAX_SPANS);
  const RolltuiRole r = (RolltuiRole)(n == 0 ? (int)ROLLTUI_ROLE_COUNT : (int)out[0].role);
  rolltui_diff_scratch_free(s);
  return r;
}

std::string sgr_of(const RolltuiStyle& style, unsigned char depth) {
  char buf[ROLLTUI_SGR_MAX];
  const size_t n = rolltui_sgr(&style, depth, buf, sizeof buf);
  return std::string(buf, n);
}

std::string marker_text(size_t below, int max_width) {
  char buf[ROLLTUI_MARKER_MAX];
  const size_t n = rolltui_scroll_marker_text(below, max_width, 0, buf, sizeof buf);
  return std::string(buf, n);
}

// The thumb for a view `first` lines down a `total`-line document in a `track`-cell bar.
// -1 for "no bar", which is a different answer from "offset 0" and must not be confused with it.
int thumb_offset(size_t first, size_t visible, size_t total, int track) {
  RolltuiScrollExtent e{};
  e.first = first;
  e.visible = visible;
  e.total = total;
  RolltuiScrollThumb t{};
  return rolltui_scroll_thumb(&e, track, &t) ? t.offset : -1;
}

// A chord by its parts, the way a bindings file spells one.
RolltuiChord chord_char(RolltuiCodepoint ch, bool ctrl, bool alt, bool shift) {
  RolltuiChord k{};
  k.key = ROLLTUI_KEY_CHAR;
  k.ch = ch;
  k.ctrl = ctrl ? 1 : 0;
  k.alt = alt ? 1 : 0;
  k.shift = shift ? 1 : 0;
  return k;
}

// A rendered document, owned for the length of one predicate. Rendering is what the controls
// below flip a rule inside, so the render happens INSIDE the predicate rather than once in
// front of it — a store built before the flag was set carries bytes the flag never reached.
struct Rendered {
  RolltuiMdLines* store = nullptr;
  explicit Rendered(const std::string& src, int width, int fold_over_lines = 0) {
    RolltuiMdRenderOptions o{};
    o.width = width;
    o.fold_over_lines = fold_over_lines;
    o.roles = *rolltui_md_roles();
    RolltuiMdDoc* doc = rolltui_md_doc_new();
    rolltui_md_parse(doc, src.data(), src.size());
    store = rolltui_md_lines_new();
    rolltui_md_render(store, doc, &o);
    rolltui_md_doc_free(doc);
  }
  Rendered(const Rendered&) = delete;
  Rendered& operator=(const Rendered&) = delete;
  ~Rendered() { rolltui_md_lines_free(store); }

  // Is there a span whose role is `role` and whose text is EXACTLY `text`? "Exactly" is the
  // load-bearing word: a span store that never merges renders the same characters in the same
  // order and every substring is still findable, so a `contains` check could not tell the two
  // apart.
  bool has_span(unsigned char role, const std::string& text) const {
    const RolltuiMdLine* all = rolltui_md_lines_all(store);
    for (size_t i = 0; i < rolltui_md_lines_count(store); ++i)
      for (size_t j = 0; j < all[i].span_n; ++j) {
        const RolltuiMdSpan& s = all[i].span_p[j];
        if (s.role == role && std::string(s.text_p, s.text_n) == text) return true;
      }
    return false;
  }
  std::string plain() const {
    std::string out;
    const RolltuiMdLine* all = rolltui_md_lines_all(store);
    for (size_t i = 0; i < rolltui_md_lines_count(store); ++i) {
      if (i) out += '\n';
      for (size_t j = 0; j < all[i].span_n; ++j)
        out.append(all[i].span_p[j].text_p, all[i].span_p[j].text_n);
    }
    return out;
  }
  bool any_block_folded() const {
    const RolltuiMdCodeBlock* b = rolltui_md_lines_code_blocks(store);
    for (size_t i = 0; i < rolltui_md_lines_code_block_count(store); ++i)
      if (b[i].folded) return true;
    return false;
  }
};

// A layout parsed from its file text, plus a stack standing on it — the shape every host
// starts from, so a control here asserts what a host would actually see.
struct Screen {
  RolltuiLayout layout;
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  bool loaded = false;
  explicit Screen(const std::string& json) {
    rolltui_layout_init(&layout);
    RolltuiLoadedLayout parsed;
    rolltui_loaded_layout_init(&parsed);
    size_t defaults_n = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(test_context(), &defaults_n);
    // VALUE-initialised: the report's array fields have no default member initializer, so a
    // plain `RolltuiLayoutReport report;` in C++ hands the loader indeterminate pointers and
    // its first growth writes through one.
    RolltuiLayoutReport report{};
    if (rolltui_load_layout_text_into(json.data(), json.size(), &parsed, defaults, defaults_n,
                                      rolltui_layout_default_hooks(), &report)) {
      rolltui_loaded_layout_to_layout(&parsed, &layout);
      rolltui_window_stack_set_base(stack, &layout.base);
      loaded = true;
    }
    rolltui_layout_report_release(&report);
    rolltui_loaded_layout_release(&parsed);
  }
  Screen(const Screen&) = delete;
  Screen& operator=(const Screen&) = delete;
  ~Screen() {
    rolltui_window_stack_free(stack);
    rolltui_layout_release(&layout);
  }
  // The resolved rect of the window with this id, or a zeroed rect when there is none —
  // never an unchecked index, so a regression fails an assertion instead of the process.
  RolltuiRect rect_of(const std::string& id, RolltuiRect screen) const {
    struct Find {
      std::string id;
      RolltuiRect out{};
      bool found = false;
    } f{id, {}, false};
    rolltui_window_stack_resolve(
        stack, screen,
        [](void* ctx, const RolltuiResolvedNode* rn) {
          Find& g = *static_cast<Find*>(ctx);
          size_t n = 0;
          const char* p = rolltui_layout_node_id(rn->node, &n);
          if (!g.found && p && std::string(p, n) == g.id) {
            g.out = rn->outer;
            g.found = true;
          }
        },
        &f);
    return f.out;
  }
};

// Does the type's own rule accept `text` as a committable value? A Text field's `max_len` counts
// GRAPHEMES, so the check needs a scratch to walk clusters in.
bool input_commits(const RolltuiInputSpec& spec, const std::string& text) {
  RolltuiUnicodeScratch* u = rolltui_u_scratch_new();
  RolltuiInputCheck c{};
  rolltui_check_input(&spec, text.data(), text.size(), u, &c);
  const bool ok = c.valid != 0;
  rolltui_input_check_release(&c);
  rolltui_u_scratch_free(u);
  return ok;
}

}  // namespace

TESTKIT_TEST(a_diff_line_is_classified_by_its_marker) {
  // The guarantee `markdown_test` asserts as "'+' is an added line", with the control that makes
  // it fail. The defect state is an INVERSION rather than a crash on purpose: every downstream
  // step still runs, every span is a legal span, and the only symptom is that the colours are
  // the other way round. Nothing in the library can notice.
  check_controlled("diff.added_and_removed_are_swapped",
                   "a '+' line is diff_added and a '-' line is diff_removed", [] {
                     return first_role_of("+added") == ROLLTUI_ROLE_DIFF_ADDED &&
                            first_role_of("-gone") == ROLLTUI_ROLE_DIFF_REMOVED;
                   });
  // The guarantee that is NOT controlled, kept beside it because it is the reason the classifier
  // tests headers first: "+++ b/x" starts with '+' and is not an added line.
  check(first_role_of("+++ b/x") != ROLLTUI_ROLE_DIFF_ADDED,
        "a file header is not an added line, whatever it starts with");
}

TESTKIT_TEST(a_mono_terminal_is_sent_no_colour) {
  // `theme_test` asserts this as "mono SGR keeps attributes only". The defect state is a step
  // SKIPPED: the reduction returns without doing anything, the colour survives, and the SGR
  // string that comes out is still perfectly well formed — so the only place it is visible is a
  // terminal that reported no colour and got some, which is the one machine nobody tests on.
  RolltuiStyle s = {};
  s.fg = RolltuiStyleColor::rgb(1, 2, 3);
  s.bg = RolltuiStyleColor::indexed(21);
  s.bold = 1;
  s.underline = 1;
  check_controlled("theme.mono_keeps_colour", "a mono downgrade keeps the attributes and drops the colour",
                   [&s] { return sgr_of(s, ROLLTUI_DEPTH_MONO) == "\x1b[0;1;4m"; });
  check(sgr_of(s, ROLLTUI_DEPTH_TRUECOLOR) != sgr_of(s, ROLLTUI_DEPTH_MONO),
        "…and the same style at full depth is a different string, so the control above is not "
        "asserting that the two depths agree");
}

TESTKIT_TEST(a_narrow_row_gets_the_count_alone_not_the_full_marker) {
  // `transcript_test` asserts this as "at 18 cells the full form would eat the row". The defect
  // state is a THRESHOLD dropped: the full form is taken whenever it merely fits, so it never
  // overflows and never writes a byte it should not — it just takes most of a narrow row to say
  // what four cells already said.
  check_controlled("marker.full_form_ignores_the_half_width_rule",
                   "the full '\xE2\x96\xBC N more ' form is taken only when it costs at most half the width",
                   [] { return marker_text(198, 18) == "\xE2\x96\xBC" "198"; });
  // Not controlled, kept beside it because it is what stops the control above asserting merely
  // that SOMETHING is emitted: with room, the full form IS the answer.
  check(marker_text(57, 78) == "\xE2\x96\xBC 57 more ",
        "…and with room the full form is still what comes out, so the rule is a threshold "
        "rather than a refusal");
}

TESTKIT_TEST(a_scrollbar_thumb_touches_an_end_only_at_that_end) {
  // `layout_test`'s end-reservation rule. The defect state is the RESERVATION gone, leaving the
  // rounded position to stand: one line down a 100-line document rounds to offset 0, so the
  // thumb sits flush against the top while the document is not at the top. The thumb is still
  // inside the track and still the right length, which is why nothing else can notice.
  check_controlled("widgets.scrollbar_ends_are_not_reserved",
                   "a view one line down does not draw its thumb against the top", [] {
                     return thumb_offset(1, 10, 100, 10) >= 1;
                   });
  check(thumb_offset(0, 10, 100, 10) == 0 && thumb_offset(90, 10, 100, 10) > 0,
        "…while a view that IS at the top gets offset 0, so the guarantee is 'iff at the end' "
        "and not 'never at the end'");
}

TESTKIT_TEST(a_chord_the_terminal_cannot_deliver_is_refused) {
  // `deliverability_test` asserts this as "deliverable('alt+[', legacy) = yes but ESC[ decodes
  // to something else". The defect state is the sequence-introducer test always answering no:
  // `alt+[` transmits ESC '[', which a terminal reads as the start of a CSI sequence, so the
  // chord never arrives. Calling it deliverable hands a bindings file a key that silently does
  // something else — no error, no refused row.
  const RolltuiChord alt_bracket = chord_char('[', false, true, false);
  check_controlled("keys.alt_csi_introducer_claims_deliverable",
                   "alt+[ is refused under the legacy protocol", [&alt_bracket] {
                     return rolltui_key_deliverable(&alt_bracket, ROLLTUI_PROTOCOL_LEGACY) == 0;
                   });
  const RolltuiChord alt_k = chord_char('k', false, true, false);
  check(rolltui_key_deliverable(&alt_k, ROLLTUI_PROTOCOL_LEGACY) != 0,
        "…while alt+k, which introduces nothing, is deliverable — so the control above is not "
        "asserting that legacy refuses every alt chord");
}

TESTKIT_TEST(an_empty_text_field_commits_only_when_the_spec_says_optional) {
  // `menu_test` asserts this twice: "text: empty is refused when the spec is not optional" and
  // "optional text: empty commits as empty". The defect state is the empty rule made
  // unconditional — Text accepted empty whatever the spec said. A `file:` source is a Text
  // field, so the commit succeeded and handed the host an empty path: a well-formed event
  // carrying a value nothing can open.
  RolltuiInputSpec required{};
  required.type = rolltui::InputType::Text;
  check_controlled("menu.empty_text_is_always_valid",
                   "an empty value is refused by a Text field that is not optional",
                   [&required] { return !input_commits(required, ""); });
  RolltuiInputSpec optional{};
  optional.type = rolltui::InputType::Text;
  optional.optional = 1;
  check(input_commits(optional, ""),
        "…and an OPTIONAL Text field still commits empty, so the control above is not asserting "
        "that empty is always refused");
}

TESTKIT_TEST(a_row_for_an_undeclared_action_claims_no_key) {
  // `bindings_test` asserts the kept-and-inert rule three ways. `declare()` is AUTHORITATIVE,
  // so loading another screen leaves the last one's rows in the table with their chords intact —
  // the file is the whole domain and every row must round-trip. The defect state is the guard
  // gone, so a chord belonging to a screen that is not running answers anyway: a legal row, a
  // legal file, and a key that quietly does something no menu can show.
  RolltuiBindings* b = rolltui_bindings_new();
  RolltuiLayoutAction alpha{};
  set_str(alpha.name, "app.alpha");
  set_str(alpha.description, "the screen that is running");
  rolltui_bindings_declare(b, &alpha, 1, nullptr, 0);
  RolltuiChord f5{};
  f5.key = ROLLTUI_KEY_F5;
  check(rolltui_bindings_bind(b, "app.alpha", 9, &f5, nullptr, nullptr) != 0,
        "the chord binds while its action is declared — so the row below really exists");
  size_t n = 0;
  check(rolltui_bindings_action_for(b, &f5, "app", 3, &n) != nullptr,
        "…and answers for the declared action");

  RolltuiLayoutAction beta{};
  set_str(beta.name, "app.beta");
  set_str(beta.description, "another screen entirely");
  rolltui_bindings_declare(b, &beta, 1, nullptr, 0);  // app.alpha is now undeclared; its row stays
  check_controlled("bindings.undeclared_action_claims_a_key",
                   "a kept row whose action nothing declares answers for no key", [b, &f5] {
                     size_t len = 0;
                     return rolltui_bindings_action_for(b, &f5, "app", 3, &len) == nullptr;
                   });
  // The row is KEPT, not dropped — which is the other half of the rule and what makes the
  // control above about answering rather than about deletion.
  RolltuiStr dumped{};
  rolltui_bindings_dump_json(b, "t", 1, &dumped);
  check(str_of(dumped).find("app.alpha") != std::string::npos,
        "…and the undeclared row still round-trips through the file, so 'inert' is not 'gone'");
  rolltui_str_free(&dumped);
  rolltui_str_free(&alpha.name);
  rolltui_str_free(&alpha.description);
  rolltui_str_free(&beta.name);
  rolltui_str_free(&beta.description);
  rolltui_bindings_free(b);
}

TESTKIT_TEST(a_preset_listing_offers_default_first) {
  // `presets_test`'s ordering rule: the shipped presets come first and "default" ahead of the
  // rest, because that is the order a chooser offers them in. The defect state is the two
  // passes swapped — every name is still listed exactly once and every entry is well formed,
  // so nothing but the order is wrong and nothing downstream can tell.
  RolltuiPresetStore* s = rolltui_preset_store_new(rolltui_preset_domain_theme(test_context()),
                                                  ROLLTUI_PRESETS_DIR, std::strlen(ROLLTUI_PRESETS_DIR), 0,
                                                  nullptr, 0);
  RolltuiPresetList list;
  rolltui_preset_store_list(s, &list);
  check(list.n > 1, "the shipped themes are there to order: " + std::to_string(list.n) +
                        " — one entry would make the check below vacuous");
  check_controlled("presets.default_is_not_offered_first", "'default' is the first preset listed",
                   [s] {
                     RolltuiPresetList l;
                     rolltui_preset_store_list(s, &l);
                     return l.n > 0 && view_of(l.v[0].name) == "default";
                   });
  rolltui_preset_store_free(s);
}

TESTKIT_TEST(a_progress_bar_colours_exactly_its_fraction_of_the_span) {
  // `effects_test` asserts this as "0.5 of an 8-cell span is 4 cells". A bar is a PICTURE OF A
  // NUMBER, so the one thing it owes is that the picture and the number agree. The defect state
  // is the fill boundary off by one — every cell written is a legal cell, the span keeps its
  // width, and the bar simply reads one cell fuller than the fraction it was given.
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT] = {};
  RolltuiEffectMap* map = rolltui_effect_map_new(ROLLTUI_EFFECT_STATE_COUNT, ROLLTUI_ROLE_TEXT);
  const size_t i = rolltui_effect_map_add(map, ROLLTUI_EFFECT_STATE_PROGRESS, "bar", 3, /*period_ms=*/0,
                                          /*width=*/0, /*steps=*/0, /*backward=*/0);
  rolltui_effect_map_add_role(map, ROLLTUI_EFFECT_STATE_PROGRESS, i, ROLLTUI_ROLE_ACCENT_1);
  RolltuiEffectScratch* scratch = rolltui_effect_scratch_new();

  // The predicate builds its own frame each time: a mark is frame state, and a control that
  // re-read one frame would be asserting about bytes written before the flag was flipped.
  const auto cells_touched = [&](double fraction) {
    RolltuiStyle fill = {};
    RolltuiFrame* f = rolltui_frame_new(20, 3, fill);
    rolltui_frame_mark(f, 6, 1, 8, ROLLTUI_EFFECT_STATE_PROGRESS, 0, fraction);
    RolltuiEffectReport rep{};
    rolltui_effects_apply(test_context(), f, scratch, styles, nullptr, map, 0, 0, &rep, nullptr, nullptr);
    rolltui_frame_free(f);
    return rep.cells_touched;
  };
  check(cells_touched(1.0) == 8, "a full bar colours the whole 8-cell span: " +
                                     std::to_string(cells_touched(1.0)) +
                                     " — the span and the kind both resolve, so the check below "
                                     "is about the boundary rather than about nothing happening");
  check_controlled("effects.bar_fills_one_cell_too_many",
                   "half of an 8-cell span is 4 cells", [&cells_touched] { return cells_touched(0.5) == 4; });
  check(cells_touched(0.0) == 0,
        "…and an empty bar colours nothing, so the rule is arithmetic over the fraction rather "
        "than a constant");
  rolltui_effect_scratch_free(scratch);
  rolltui_effect_map_free(map);
}

TESTKIT_TEST(a_links_cells_arrive_as_one_span) {
  // `markdown_test` asserts this as "link text in md_link". A link's cells reach the store ONE
  // GRAPHEME AT A TIME, so the tail-merge rule is the only thing that makes them a span. The
  // defect state is that rule made a no-op: four one-cell spans instead of one four-cell span.
  // The frame renders identically character for character, and only a consumer reading SPANS —
  // for a role, an href, a click target — can see the difference.
  const std::string src = "A [link](https://x.y/z) here.\n";
  const unsigned char link = rolltui_md_roles()->link;
  check_controlled("md.spans_never_merge_into_the_pool_tail",
                   "a four-character link arrives as ONE span carrying all four characters",
                   [&src, link] { return Rendered(src, 80).has_span(link, "link"); });
  // The characters are there either way, which is exactly why the control above asks about the
  // span rather than about the text.
  check(Rendered(src, 80).plain().find("link") != std::string::npos,
        "…and the text renders the same whether or not the spans merged, so this guarantee is "
        "about structure and nothing downstream of it reports the difference");
}

TESTKIT_TEST(a_links_destination_is_shown_after_its_text) {
  // `markdown_test` asserts this as "URL shown after it in md_link_url". A terminal cannot be
  // hovered, so a link whose text differs from its target has nowhere else to say where it
  // goes. The defect state is the appended run suppressed: the document is still well formed,
  // the link text still renders, and the destination is simply invisible.
  const std::string src = "A [link](https://x.y/z) here.\n";
  const unsigned char url = rolltui_md_roles()->link_url;
  check_controlled("md.link_url_is_dropped", "the URL is rendered after the link text",
                   [&src, url] { return Rendered(src, 80).has_span(url, " (https://x.y/z)"); });
  // An AUTOLINK carries no separate destination and must NOT gain one — the rule is "show a
  // target the text does not already say", not "append a URL to every link".
  check(Rendered("An autolink https://example.com/p here.\n", 80).plain().find("(https://example.com/p)") ==
            std::string::npos,
        "…while an autolink gains no '(url)' run, so the control above is not asserting that "
        "every link grows a suffix");
}

TESTKIT_TEST(a_code_block_folds_only_when_it_is_over_the_threshold) {
  // `markdown_test`'s fold threshold. `fold_over_lines` means OVER, so a block of exactly that
  // many lines shows. The defect state is the comparison off by one: a block short enough to
  // read is hidden behind a header row, which renders perfectly and is simply the wrong answer.
  std::string at_the_limit = "```\n";
  for (int i = 0; i < 4; ++i) at_the_limit += "line " + std::to_string(i) + "\n";
  at_the_limit += "```\n";
  check_controlled("md.fold_threshold_folds_at_the_limit",
                   "a 4-line block with fold_over_lines=4 is NOT folded",
                   [&at_the_limit] { return !Rendered(at_the_limit, 40, 4).any_block_folded(); });
  std::string over_the_limit = "```\n";
  for (int i = 0; i < 5; ++i) over_the_limit += "line " + std::to_string(i) + "\n";
  over_the_limit += "```\n";
  check(Rendered(over_the_limit, 40, 4).any_block_folded(),
        "…and a 5-line block IS folded, so the control above is not asserting that folding "
        "never happens");
}

TESTKIT_TEST(two_bordered_siblings_share_the_edge_between_them) {
  // `layout_test` asserts this as "bordered b starts on a's right border and reaches the edge".
  // Two bordered neighbours draw ONE column between them, so the pair spans the extent exactly.
  // The defect state is the rule inverted: every rect still lands inside its parent and the
  // frame still composes — the columns are simply one cell wrong each, which is the kind of
  // thing a screenshot shows and no assertion downstream does.
  const std::string shared =
      R"({"name":"t","root":{"row":[{"id":"a","content":"text:a","size":32,"border":"single"},)"
      R"({"id":"b","content":"text:b","border":"single"}]}})";
  Screen s(shared);
  check(s.loaded, "the two-window screen parsed — an unparsed layout would make every rect below zero");
  check_controlled("layout.shared_edges_are_inverted",
                   "the second of two bordered siblings starts ON the first's right border", [&s] {
                     const RolltuiRect r = s.rect_of("b", {0, 0, 80, 10});
                     return r.x == 31 && r.w == 49;
                   });
  // With one side UNBORDERED there is nothing to share, and the neighbour starts past the
  // first — so the control above is about a condition rather than about a constant offset.
  const std::string unshared =
      R"({"name":"t","root":{"row":[{"id":"a","content":"text:a","size":32,"border":"none"},)"
      R"({"id":"b","content":"text:b","border":"single"}]}})";
  Screen u(unshared);
  const RolltuiRect r = u.rect_of("b", {0, 0, 80, 10});
  check(r.x == 32 && r.w == 48, "…while an unbordered neighbour is not shared with: x=" +
                                    std::to_string(r.x) + " w=" + std::to_string(r.w));
}

TESTKIT_TEST(a_modal_popup_keeps_focus_off_the_layers_under_it) {
  // A modal is the one layer that owns the keyboard while it is up. The defect state is the
  // confinement deleted: the modal is still drawn and still tints the screen, and keys go to
  // whatever layer behind it happens to hold focus — which reads as the modal ignoring input
  // rather than as a bug, and is why this is a control rather than a comment.
  // The popup's own window is NOT focusable, which is the real shape (roll's approval preview
  // is exactly that): with the guarantee, nothing takes keys; without it, the input behind does.
  const std::string json =
      R"({"name":"t","focus":"prompt","root":{"id":"prompt","content":"input:prompt","focusable":true},)"
      R"("popups":[{"id":"note","dismiss":false,"x":"50%","y":"50%","w":20,"h":5,"anchor":"center","modal":true,)"
      R"("root":{"id":"note","content":"text:note","border":"rounded"}}]})";
  Screen s(json);
  check(s.loaded && rolltui_window_stack_focused(s.stack) != nullptr,
        "with no popup up, the base's focusable window holds focus");
  check(rolltui_window_stack_push_popup(s.stack, &s.layout, "note", 4) != 0,
        "…and the modal popup pushes");
  check_controlled("layout.a_modal_does_not_confine_focus",
                   "no window under a modal holds focus while it is up",
                   [&s] { return rolltui_window_stack_focused(s.stack) == nullptr; });
  rolltui_window_stack_pop(s.stack);
  check(rolltui_window_stack_focused(s.stack) != nullptr,
        "…and popping it hands focus back, so the control above is not asserting that this "
        "screen never has a focused window");
}

TESTKIT_TEST(a_pause_in_typing_closes_the_undo_group) {
  // `input_test` asserts this as "a timeout closes the group even between two edits that would
  // otherwise merge". The defect state is the timeout made infinite: undo still works and the
  // stack stays consistent, so nothing can report a problem — one ctrl-Z simply throws away
  // every ordinary edit of the session instead of the last run.
  const auto type_at = [](RolltuiInput* in, char32_t c, unsigned long long at_ms) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = ROLLTUI_KEY_CHAR;
    e.key.ch = c;
    rolltui_input_handle(in, &e, rolltui_bindings_default(test_context()), rolltui_input_default_actions(), at_ms);
  };
  // Two keystrokes five seconds apart, then ONE undo: with the group closed, the first
  // keystroke survives.
  const auto after_one_undo = [&type_at](unsigned long long second_ms) {
    RolltuiInput* in = rolltui_input_new();
    type_at(in, 'a', 1000);
    type_at(in, 'b', second_ms);
    rolltui_input_undo(in);
    size_t n = 0;
    const char* p = rolltui_input_text(in, &n);
    const std::string out(p, n);
    rolltui_input_free(in);
    return out;
  };
  check_controlled("input.undo_group_never_closes",
                   "one undo after a five-second pause removes only the second keystroke",
                   [&after_one_undo] { return after_one_undo(6000) == "a"; });
  check(after_one_undo(1200) == "",
        "…while two keystrokes 200 ms apart are ONE group, so the control above is about the "
        "pause rather than about every keystroke being its own step");
}

TESTKIT_TEST(text_wider_than_the_input_wraps_onto_another_row) {
  // The cell-wrap rule: a run with no break opportunity still wraps at the edge, because the
  // caret arithmetic and the drawn rows have to agree about where a character is. The defect
  // state is the break made a no-op — nothing overflows a buffer and no byte is lost; the flow
  // just reports one row for a line of any length, and every caret below the first row lands
  // somewhere the text is not.
  // WHERE a character is drawn, not how many rows exist. `rows_for` alone cannot carry this
  // control: `build_flow` also opens a row when the last column FILLS the width, so a flow
  // that never breaks still reports two rows for a 60-character line and the middle assertion
  // passes — which `check_controlled` correctly calls a failure. The 40th character is at
  // column 0 of row 2 when the run wraps and at column 40 of row 0 when it does not, and
  // a column past the input's own width is the defect stated as a coordinate. The "> " prompt
  // indents every row by two, so row 0 holds offsets 0-17 and row 2 begins at offset 36.
  // The input is built INSIDE the predicate. A flow is cached until the text or the width
  // changes, so an input laid out in front of the control answers from bytes the flag never
  // reached — and the middle assertion passes, which `check_controlled` correctly calls a
  // failure.
  const auto cell = [](size_t offset) {
    RolltuiInput* in = rolltui_input_new();
    const std::string long_run(60, 'x');
    rolltui_input_set_text(in, long_run.data(), long_run.size());
    rolltui_input_layout(in, RolltuiRect{0, 0, 20, 6});
    int row = 0, col = 0;
    rolltui_input_cell_of(in, offset, &row, &col);
    rolltui_input_free(in);
    return std::pair<int, int>(row, col);
  };
  const auto spell = [&cell](size_t offset) {
    return "row " + std::to_string(cell(offset).first) + " col " + std::to_string(cell(offset).second);
  };
  check_controlled("input.cell_wrap_never_breaks",
                   "the 40th character of an unbroken run is drawn on row 2 of a 20-cell input",
                   [&cell] { return cell(40) == std::pair<int, int>(2, 6); });
  check(cell(5) == std::pair<int, int>(0, 7),
        "…while the 5th is still on row 0, so the control above is not asserting that every "
        "character is pushed down a row [" + spell(5) + "]");
}

TESTKIT_TEST(choosing_a_value_in_the_palette_reaches_the_choice_that_owns_it) {
  // `menu_test` asserts this as "Enter on a palette row acts on the leaf as navigation would (a
  // choice option chooses)". The palette is FLAT, so the only thing that says which Choice a
  // row's value belongs to is its index path. The defect state is that path resolved one level
  // short: the row's "parent" is its grandparent, which is not a Choice, so the branch falls
  // through and Enter emits an Activate for the option id instead of a Choose for the choice.
  // Both are legal events; a host switching on `kind` simply takes the wrong branch.
  const auto choose_on_first_palette_row = [] {
    std::vector<RolltuiMenuItem> options;
    options.push_back(RolltuiMenuItem::action("mono", "mono"));
    options.push_back(RolltuiMenuItem::action("dark", "dark"));
    RolltuiMenuItem root = submenu_of("root", "Root", [&options] {
      std::vector<RolltuiMenuItem> top;
      top.push_back(choice_of("theme", "Theme", std::move(options), "dark"));
      return top;
    }());
    RolltuiMenu* m = rolltui_menu_new();
    rolltui_menu_set_root(m, &root);
    rolltui_menu_set_palette(m, 1);
    RolltuiEvent enter{};
    enter.kind = ROLLTUI_EVENT_KEY;
    enter.key.key = ROLLTUI_KEY_ENTER;
    RolltuiMenuEvent out{};
    rolltui_menu_handle(m, &enter, rolltui_bindings_default(test_context()), rolltui_menu_default_actions(), &out);
    const std::string kind_and_id = std::to_string((int)out.kind) + ":" + str_of(out.id) + ":" + str_of(out.value);
    rolltui_menu_event_release(&out);
    rolltui_menu_free(m);
    return kind_and_id;
  };
  const std::string expected = std::to_string((int)ROLLTUI_MENU_EVENT_CHOOSE) + ":theme:mono";
  check_controlled("menu.palette_choice_resolves_one_level_short",
                   "Enter on a palette option emits a Choose naming the choice it belongs to",
                   [&] { return choose_on_first_palette_row() == expected; });
  check(!expected.empty() && choose_on_first_palette_row() == expected,
        "…and the event really is " + expected + ", so the control above compares against a "
        "value this menu actually produces rather than against an empty string");
}

TESTKIT_TEST(a_window_keeps_the_same_widget_between_frames) {
  // A window's widget is owned by the content table and looked up by its content string, so the
  // scroll position, the selection and a half-typed line survive a frame. The defect state is
  // the lookup always missing: a fresh widget is built every time a window is synced. Nothing
  // crashes and every widget drawn is a valid widget of the right kind — it is a NEW one, so
  // everything the last frame put into it is gone.
  // The table is built INSIDE the predicate. With the control ON the second call REPLACES the
  // table's row, so a widget captured in front of the control is no longer the one the table
  // holds once the control is restored — and the third assertion fails, which is the mechanism
  // reporting that the defect state outlived its own window rather than a broken guarantee.
  const auto same_widget_twice = [](const char* content, size_t len) {
    RolltuiWindows* w = rolltui_windows_new(test_context());
    RolltuiWidget* a = rolltui_windows_widget_for(w, content, len);
    RolltuiWidget* b = rolltui_windows_widget_for(w, content, len);
    const bool same = a != nullptr && a == b;
    rolltui_windows_free(w);
    return same;
  };
  check(same_widget_twice("input:prompt", 12),
        "the window table builds a widget for input:prompt and hands it back");
  check_controlled("widgets.widget_lookup_always_misses",
                   "asking twice for one content string hands back the same widget",
                   [&same_widget_twice] { return same_widget_twice("input:prompt", 12); });
  RolltuiWindows* w = rolltui_windows_new(test_context());
  check(rolltui_windows_widget_for(w, "input:prompt", 12) != rolltui_windows_widget_for(w, "transcript:session", 18),
        "…while a DIFFERENT content string is a different widget, so the control above is not "
        "asserting that this table only ever holds one thing");
  rolltui_windows_free(w);
}

TESTKIT_TEST(find_reaches_text_inside_a_folded_entry) {
  // `transcript_test` asserts this as "ONE INSIDE THE FOLDED BLOCK" and "…and revealing it
  // UNFOLDED the block". A folded entry draws its summary row and nothing else, so find has to
  // search the entry's text AS IF UNFOLDED or the words in it do not exist. The defect state is
  // that cache never consulted: the search reports fewer matches, which reads exactly like a
  // document that does not contain the word — no error, nothing to notice.
  const auto matches_for = [](const char* query) {
    RolltuiDocument doc;
    RolltuiDocEntry open;
    open.id = "e0";
    set_str(open.text, "an open line");
    open.markdown = false;
    doc.push_back(std::move(open));
    RolltuiDocEntry folded;
    folded.id = "e1";
    set_str(folded.text, "buried needle in the body");
    folded.markdown = false;
    folded.foldable = true;
    set_str(folded.summary, "a tool call");
    folded.folded = true;
    doc.push_back(std::move(folded));

    RolltuiTranscript* t = rolltui_transcript_new();
    RolltuiTranscriptOptions opt;
    opt.gap = 0;
    rolltui_transcript_layout(t, &doc, RolltuiRect{0, 0, 40, 10}, &opt);
    rolltui_transcript_set_query(t, query, std::strlen(query));
    rolltui_transcript_layout(t, &doc, RolltuiRect{0, 0, 40, 10}, &opt);
    const size_t n = rolltui_transcript_match_count(t);
    rolltui_transcript_free(t);
    return n;
  };
  check_controlled("transcript.find_searches_the_drawn_text",
                   "a word inside a FOLDED entry is found", [&matches_for] { return matches_for("needle") == 1; });
  check(matches_for("open") == 1,
        "…and a word on a visible line is found either way, so the control above is about the "
        "folded entry rather than about find working at all");
}

TESTKIT_TEST(revealing_a_match_already_on_screen_moves_nothing) {
  // The reveal's minimal-movement rule: a match already in the viewport is already revealed, so
  // nothing scrolls. The defect state is an unconditional scroll — every match is still found
  // and still revealed, and the view simply jumps under the reader on every next-match. No
  // assertion about the MATCH can see it; only one about the view's top line can.
  const auto top_after_find = [] {
    RolltuiDocument doc;
    for (int i = 0; i < 12; ++i) {
      RolltuiDocEntry e;
      const std::string id = "e" + std::to_string(i);
      e.id = id.c_str();
      set_str(e.text, i == 2 ? "the needle here" : ("line " + std::to_string(i)));
      e.markdown = false;
      doc.push_back(std::move(e));
    }
    RolltuiTranscript* t = rolltui_transcript_new();
    RolltuiTranscriptOptions opt;
    opt.gap = 0;
    const RolltuiRect area{0, 0, 40, 10};
    rolltui_transcript_layout(t, &doc, area, &opt);
    rolltui_transcript_scroll_by(t, -1000);  // to the top: the match on line 2 is in view
    rolltui_transcript_layout(t, &doc, area, &opt);
    rolltui_transcript_set_query(t, "needle", 6);
    rolltui_transcript_layout(t, &doc, area, &opt);
    rolltui_transcript_find_next(t);
    rolltui_transcript_layout(t, &doc, area, &opt);
    const size_t top = rolltui_transcript_top_line(t);
    const size_t matches = rolltui_transcript_match_count(t);
    rolltui_transcript_free(t);
    return std::pair<size_t, size_t>(top, matches);
  };
  check(top_after_find().second == 1,
        "the query matches exactly once, on a line the viewport already shows — without a match "
        "there would be nothing to reveal and the check below would be vacuous");
  check_controlled("transcript.reveal_always_scrolls",
                   "revealing a match that is already on screen leaves the top line where it was",
                   [&top_after_find] { return top_after_find().first == 0; });
}

// LAST, because it reads what every test above did. Registered after them by file order, which
// is the order `TESTKIT_TEST` runs them in.
TESTKIT_TEST(every_control_point_the_library_reached_was_flipped_and_nothing_else_was) {
  // `known` is every name the code under test ASKED about; `flipped` is every name a test turned
  // on. A known-but-unflipped name is a control point with no negative side — a defect state
  // planted in the library that nothing ever exercises, which is the hand method's failure with
  // the plant left in. A flipped-but-unknown name is a typo: a test believing it flipped
  // something the code never consulted, so its middle assertion measured nothing.
  //
  // IT IS NOT THE SAME CHECK AS `rolltui-shipped-artifact-test`'s, and the pair is the point.
  // That one reads the SOURCES and the two archives — it sees a name that is compiled in. This
  // one sees only what this process actually EXECUTED, so a control point on a line no test
  // reaches is invisible to it and visible to the other; a control point in a file the scanner
  // does not walk is the reverse.
  std::vector<std::string> known = testkit::ctl::known();
  std::vector<std::string> flipped = testkit::ctl::flipped();
  std::sort(known.begin(), known.end());
  std::sort(flipped.begin(), flipped.end());
  check(known.size() >= 20, "the library reached at least the twenty ported control points: " +
                                std::to_string(known.size()) +
                                " — a small number here means the suite above stopped running");
  for (const std::string& n : known)
    check(std::binary_search(flipped.begin(), flipped.end(), n),
          "control point `" + n + "` reached by the library was flipped by a test");
  for (const std::string& n : flipped)
    check(std::binary_search(known.begin(), known.end(), n),
          "control `" + n + "` flipped by a test is a name the library asked about");
}

int main() { return testkit::report("rolltui_controls_test"); }
