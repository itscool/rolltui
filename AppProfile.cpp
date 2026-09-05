// rolltui/AppProfile.cpp — see AppProfile.hpp. `load_app_profile`/`app_profile_to_json` are
// thin shims over `rolltui/c/rolltui_app_profile.h`, which now holds the actual parsing and
// serialising algorithm (Phase 17 m1); `profile_contents`/`mount_app_profile` are unchanged
// C++ working directly on the `AppProfile` struct's own members, which this module keeps for
// the reasons stated at the top of AppProfile.hpp.
#include "rolltui/AppProfile.hpp"

#include <algorithm>

#include "rolltui/c/rolltui_app_profile.h"

namespace rolltui {

// PHASE 17 m2a: `rolltui_app_profile_report_summary` says at its own declaration that it
// "mirrors `AppProfileReport::summary()` exactly" — and this function composed the identical
// English beside it. Same shape as `BindingsLoadReport::summary()` one file over: a mirror is
// a copy with a promise attached, and the promise is the part nothing checks.
std::string AppProfileReport::summary() const {
  if (clean()) return {};
  if (!error.empty()) return error;
  RolltuiAppProfileReport r{};
  for (const std::string& x : bad_values) rolltui_app_profile_report_add_bad_value(&r, x.data(), x.size());
  for (const std::string& x : unknown_keys) rolltui_app_profile_report_add_unknown_key(&r, x.data(), x.size());
  RolltuiStr out{};
  rolltui_app_profile_report_summary(&r, &out);
  std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  rolltui_app_profile_report_release(&r);
  return s;
}

namespace {

// `AppProfile::Kind::rule`'s `SourceRule` and `rolltui_app_profile.h`'s
// `ROLLTUI_APP_PROFILE_SOURCE_*` share numeric values as a DOCUMENTED fact (that header's
// own comment states it), but the conversion is still an explicit switch rather than a
// `static_cast` relying on it — the same reasoning `Json.cpp`'s `kind_from_c` gives, and the
// same failure shape CLAUDE.md names: two things numbered alike by coincidence is not the
// same as two things declared to agree.
SourceRule rule_from_c(int rule) {
  switch (rule) {
    case ROLLTUI_APP_PROFILE_SOURCE_REQUIRED: return SourceRule::Required;
    case ROLLTUI_APP_PROFILE_SOURCE_OPTIONAL: return SourceRule::Optional;
    default: return SourceRule::Forbidden;
  }
}
int rule_to_c(SourceRule rule) {
  switch (rule) {
    case SourceRule::Required: return ROLLTUI_APP_PROFILE_SOURCE_REQUIRED;
    case SourceRule::Optional: return ROLLTUI_APP_PROFILE_SOURCE_OPTIONAL;
    default: return ROLLTUI_APP_PROFILE_SOURCE_FORBIDDEN;
  }
}

AppProfileReport report_from_c(const RolltuiAppProfileReport& r) {
  AppProfileReport out;
  out.error.assign(r.error.p ? r.error.p : "", r.error.n);
  out.unknown_keys.reserve(r.unknown_keys_n);
  for (std::size_t i = 0; i < r.unknown_keys_n; ++i)
    out.unknown_keys.emplace_back(r.unknown_keys[i].p ? r.unknown_keys[i].p : "", r.unknown_keys[i].n);
  out.bad_values.reserve(r.bad_values_n);
  for (std::size_t i = 0; i < r.bad_values_n; ++i)
    out.bad_values.emplace_back(r.bad_values[i].p ? r.bad_values[i].p : "", r.bad_values[i].n);
  return out;
}

// Recursively converts a parsed C profile into the C++ struct roll's `TuiFrontend.cpp` and
// `rolltui-paint` build by hand — a real, full copy, the same cost `Json.hpp`'s conversion
// pays and for the same reason: the C++ shape stays a real `std::vector`/`std::string` type
// rather than a proxy over the C storage, because those two files assign and aggregate-init
// it directly (see `AppProfile.hpp`'s header comment).
AppProfile app_profile_from_c(const RolltuiAppProfile* c) {
  AppProfile p;
  std::size_t len = 0;
  p.app.assign(rolltui_app_profile_app(c, &len), len);
  p.min_width = rolltui_app_profile_min_width(c);
  p.min_height = rolltui_app_profile_min_height(c);

  const std::size_t an = rolltui_app_profile_action_count(c);
  p.actions.reserve(an);
  for (std::size_t i = 0; i < an; ++i) {
    std::size_t nlen = 0, dlen = 0;
    const char* n = rolltui_app_profile_action_name(c, i, &nlen);
    const char* d = rolltui_app_profile_action_description(c, i, &dlen);
    p.actions.push_back({std::string(n, nlen), std::string(d, dlen)});
  }

  const std::size_t kn = rolltui_app_profile_kind_count(c);
  p.kinds.reserve(kn);
  for (std::size_t i = 0; i < kn; ++i) {
    AppProfile::Kind k;
    std::size_t nlen = 0, dlen = 0;
    const char* n = rolltui_app_profile_kind_name(c, i, &nlen);
    const char* d = rolltui_app_profile_kind_describes(c, i, &dlen);
    k.name = std::string(n, nlen);
    k.rule = rule_from_c(rolltui_app_profile_kind_rule(c, i));
    k.describes = std::string(d, dlen);
    p.kinds.push_back(std::move(k));
  }

  const std::size_t docn = rolltui_app_profile_document_count(c);
  p.documents.reserve(docn);
  for (std::size_t i = 0; i < docn; ++i) {
    std::size_t nlen = 0, slen = 0;
    const char* n = rolltui_app_profile_document_name(c, i, &nlen);
    const char* s = rolltui_app_profile_document_sample(c, i, &slen);
    p.documents.push_back({std::string(n, nlen), std::string(s, slen)});
  }

  const std::size_t rn = rolltui_app_profile_row_count(c);
  p.rows.reserve(rn);
  for (std::size_t i = 0; i < rn; ++i) {
    AppProfile::RowSource rs;
    std::size_t nlen = 0;
    const char* n = rolltui_app_profile_row_name(c, i, &nlen);
    rs.name = std::string(n, nlen);
    const std::size_t sn = rolltui_app_profile_row_sample_count(c, i);
    rs.sample.reserve(sn);
    for (std::size_t j = 0; j < sn; ++j) {
      std::size_t llen = 0, vlen = 0;
      const char* l = rolltui_app_profile_row_sample_label(c, i, j, &llen);
      const char* v = rolltui_app_profile_row_sample_value(c, i, j, &vlen);
      rs.sample.emplace_back(std::string(l, llen), std::string(v, vlen));
    }
    p.rows.push_back(std::move(rs));
  }

  const std::size_t sun = rolltui_app_profile_submit_count(c);
  p.submits.reserve(sun);
  for (std::size_t i = 0; i < sun; ++i) {
    std::size_t slen = 0;
    const char* s = rolltui_app_profile_submit_at(c, i, &slen);
    p.submits.emplace_back(s, slen);
  }

  const std::size_t non = rolltui_app_profile_note_count(c);
  p.notes.reserve(non);
  for (std::size_t i = 0; i < non; ++i) {
    std::size_t slen = 0;
    const char* s = rolltui_app_profile_note_at(c, i, &slen);
    p.notes.emplace_back(s, slen);
  }

  const std::size_t mn = rolltui_app_profile_menu_count(c);
  p.menus.reserve(mn);
  for (std::size_t i = 0; i < mn; ++i) {
    std::size_t nlen = 0, jlen = 0;
    const char* n = rolltui_app_profile_menu_name(c, i, &nlen);
    const char* j = rolltui_app_profile_menu_json(c, i, &jlen);
    p.menus.push_back({std::string(n, nlen), std::string(j, jlen)});
  }

  std::size_t leadlen = 0, notelen = 0;
  const char* lead = rolltui_app_profile_help_lead(c, &leadlen);
  const char* note = rolltui_app_profile_help_note(c, &notelen);
  p.help.lead = std::string(lead, leadlen);
  p.help.note = std::string(note, notelen);
  const std::size_t scn = rolltui_app_profile_help_scope_count(c);
  p.help.scopes.reserve(scn);
  for (std::size_t i = 0; i < scn; ++i) {
    std::size_t slen = 0;
    const char* s = rolltui_app_profile_help_scope_at(c, i, &slen);
    p.help.scopes.emplace_back(s, slen);
  }
  return p;
}

// The other direction: builds a fresh, owned C profile out of a hand-built C++ AppProfile,
// for `app_profile_to_json` to hand to `rolltui_app_profile_dump`. The caller frees the
// result with `rolltui_app_profile_free`. Built out of the SAME `_new`/`add_*`/`set_*`
// primitives `rolltui_app_profile_parse` itself uses while walking parsed JSON, so there is
// exactly one way a profile's fields get set (`rolltui_app_profile.h`'s header comment).
RolltuiAppProfile* app_profile_to_c(const AppProfile& p) {
  RolltuiAppProfile* c = rolltui_app_profile_new();
  rolltui_app_profile_set_app(c, p.app.data(), p.app.size());
  rolltui_app_profile_set_min_size(c, p.min_width, p.min_height);
  for (const ActionDecl& a : p.actions)
    rolltui_app_profile_add_action(c, a.name.data(), a.name.size(), a.description.data(), a.description.size());
  for (const AppProfile::Kind& k : p.kinds)
    rolltui_app_profile_add_kind(c, k.name.data(), k.name.size(), rule_to_c(k.rule), k.describes.data(),
                                 k.describes.size());
  for (const AppProfile::Document& d : p.documents)
    rolltui_app_profile_add_document(c, d.name.data(), d.name.size(), d.sample.data(), d.sample.size());
  for (const AppProfile::RowSource& r : p.rows) {
    const std::size_t idx = rolltui_app_profile_add_row(c, r.name.data(), r.name.size());
    for (const auto& [label, value] : r.sample)
      rolltui_app_profile_row_add_sample(c, idx, label.data(), label.size(), value.data(), value.size());
  }
  for (const std::string& s : p.submits) rolltui_app_profile_add_submit(c, s.data(), s.size());
  for (const std::string& s : p.notes) rolltui_app_profile_add_note(c, s.data(), s.size());
  for (const AppProfile::MenuFile& m : p.menus)
    rolltui_app_profile_add_menu(c, m.name.data(), m.name.size(), m.json.data(), m.json.size());
  rolltui_app_profile_set_help(c, p.help.lead.data(), p.help.lead.size(), p.help.note.data(), p.help.note.size());
  for (const std::string& s : p.help.scopes) rolltui_app_profile_add_help_scope(c, s.data(), s.size());
  return c;
}

}  // namespace

std::optional<AppProfile> load_app_profile(std::string_view text, AppProfileReport& report) {
  RolltuiAppProfileReport crep{};
  RolltuiAppProfile* c = rolltui_app_profile_parse(text.data(), text.size(), &crep);
  report = report_from_c(crep);
  rolltui_app_profile_report_release(&crep);
  if (!c) return std::nullopt;
  AppProfile p = app_profile_from_c(c);
  rolltui_app_profile_free(c);
  return p;
}

// A tree still crosses here — see this header's own note on why the shim keeps this
// overload's signature. Implemented in terms of the text overload above (dump then parse)
// rather than as a second, independent tree walk, so there remains exactly one parsing
// implementation (`rolltui_app_profile.c`) rather than two that could disagree.
std::optional<AppProfile> load_app_profile(const json::Value& v, AppProfileReport& report) {
  return load_app_profile(json::dump(v, 0), report);
}

// Also still returns a tree, for the same reason. Builds the C profile, dumps it to TEXT
// (the fix `rolltui_app_profile.h` makes at its own boundary — see its header comment) and
// parses that text back into the tree this signature promises, rather than hand-building a
// second tree-construction implementation alongside the C one.
json::Value app_profile_to_json(const AppProfile& p) {
  RolltuiAppProfile* c = app_profile_to_c(p);
  RolltuiStr text{};
  rolltui_app_profile_dump(c, /*indent=*/0, &text);
  rolltui_app_profile_free(c);
  std::string err;
  json::Value v = json::parse(std::string_view(text.p ? text.p : "", text.n), err);
  rolltui_str_free(&text);
  return v;
}

std::vector<std::string> profile_contents(const AppProfile& p) {
  std::vector<std::string> out;
  for (const AppProfile::Document& d : p.documents) out.push_back("transcript:" + d.name);
  for (const std::string& s : p.submits) out.push_back("input:" + s);
  for (const AppProfile::RowSource& r : p.rows) out.push_back("rows:" + r.name);
  for (const AppProfile::MenuFile& m : p.menus) out.push_back("menu:" + m.name);
  out.emplace_back("help");
  for (const std::string& s : p.help.scopes) out.push_back("help:" + s);
  for (const AppProfile::Kind& k : p.kinds)
    out.push_back(k.rule == SourceRule::Forbidden ? k.name : k.name + ":");
  return out;
}

void mount_app_profile(Windows& windows, const AppProfile& p) {
  for (const AppProfile::Kind& k : p.kinds) {
    // A PLACEHOLDER, never an error panel. The window is correct — this tool simply is
    // not the app that can build it — and drawing an error for a correct window is the
    // finding this milestone exists to fix. The label names the kind so the author can
    // see which of the app's windows they are looking at.
    const std::string label = k.name;
    windows.register_kind(
        k.name,
        [label] {
          return std::make_unique<CallbackWidget>(
              [label](const ResolvedNode& rn, Frame& f, const Theme& theme) {
                const Rect r = content_rect(rn);
                if (r.w <= 0 || r.h <= 0) return;
                const std::string text = "[" + label + "]";
                f.put_text(r.x, r.y + r.h / 2, text, theme.style(Role::text_muted), r.w);
              },
              nullptr);
        },
        k.rule, k.describes);
  }
  for (const AppProfile::Document& d : p.documents) {
    // The sample is owned by the Windows for as long as the profile is mounted: a
    // Document the tool holds, not a pointer into the profile the caller may drop.
    windows.bind_sample_document(d.name, d.sample);
  }
  for (const AppProfile::RowSource& r : p.rows) {
    // m5b: the sample is captured BY VALUE once and refilled into the caller's buffer each
    // frame, rather than a fresh vector being built per frame from it.
    windows.bind_rows(r.name, [sample = r.sample](Rows& out) {
      for (const auto& [label, value] : sample) out.add(label, value);
    });
  }
  for (const std::string& s : p.submits) windows.bind_submit(s, [](const std::string&) {});
  for (const std::string& s : p.notes) windows.bind_note(s, [](Note&) {});
  for (const AppProfile::MenuFile& m : p.menus) windows.add_menu(m.name, m.json);
  // The app's help, not the tool's — including the scope LIST, which is what a
  // `help:<scope>` window is judged against (Phase 11 m5b). A profile that names none
  // leaves the tool's own, which is the honest answer for an app that published nothing.
  if (!p.help.scopes.empty()) windows.set_help(p.help.lead, p.help.scopes, p.help.note);
}

}  // namespace rolltui
