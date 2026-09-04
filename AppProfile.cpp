// rolltui/AppProfile.cpp — see AppProfile.hpp.
#include "rolltui/AppProfile.hpp"

#include <algorithm>

namespace rolltui {

std::string AppProfileReport::summary() const {
  if (clean()) return {};
  if (!error.empty()) return error;
  std::string s;
  auto add = [&](const std::string& x) { if (!s.empty()) s += "; "; s += x; };
  for (const std::string& x : bad_values) add("bad: " + x);
  for (const std::string& x : unknown_keys) add("unknown: " + x);
  return s;
}

namespace {

// The source rule as a word. One table, both directions, so a profile file and the
// library's own kind table spell "forbidden" the same way.
struct RuleName { SourceRule rule; const char* name; };
constexpr RuleName kRules[] = {
    {SourceRule::Required, "required"}, {SourceRule::Optional, "optional"}, {SourceRule::Forbidden, "forbidden"}};

const char* rule_name(SourceRule r) {
  for (const RuleName& n : kRules)
    if (n.rule == r) return n.name;
  return "required";
}
std::optional<SourceRule> rule_from_name(std::string_view s) {
  for (const RuleName& n : kRules)
    if (s == n.name) return n.rule;
  return std::nullopt;
}

int int_or(const json::Value& v, int def) { return v.is_number() ? static_cast<int>(v.as_number()) : def; }

}  // namespace

std::optional<AppProfile> load_app_profile(std::string_view text, AppProfileReport& report) {
  report = AppProfileReport{};
  std::string err;
  const json::Value v = json::parse(text, err);
  if (!err.empty()) { report.error = err; return std::nullopt; }
  return load_app_profile(v, report);
}

std::optional<AppProfile> load_app_profile(const json::Value& v, AppProfileReport& report) {
  report = AppProfileReport{};
  if (!v.is_object()) { report.error = "an app profile must be a JSON object"; return std::nullopt; }
  AppProfile p;
  p.app = std::string(v.get("app").as_string());
  if (p.app.empty()) { report.error = "an app profile needs an \"app\" name"; return std::nullopt; }
  p.min_width = int_or(v.get("min_width"), 0);
  p.min_height = int_or(v.get("min_height"), 0);

  for (const auto& [k, x] : v.obj)
    if (k != "app" && k != "min_width" && k != "min_height" && k != "actions" && k != "kinds" && k != "sources" &&
        k != "menus" && k != "help")
      report.unknown_keys.push_back(k);

  // actions: name → description, the same shape a layout's "actions" has.
  if (const json::Value& a = v.get("actions"); a.is_object()) {
    for (const auto& [name, d] : a.obj) {
      if (!d.is_string()) { report.bad_values.push_back("actions." + name + ": expected a string"); continue; }
      p.actions.push_back({name, std::string(d.str)});
    }
  } else if (!a.is_null()) {
    report.bad_values.push_back("actions: expected an object of name -> description");
  }

  // kinds the app registers.
  if (const json::Value& ks = v.get("kinds"); ks.is_array()) {
    for (const json::Value& k : ks.arr) {
      AppProfile::Kind kind;
      kind.name = std::string(k.get("name").as_string());
      if (kind.name.empty()) { report.bad_values.push_back("kinds[]: a kind needs a name"); continue; }
      const std::string_view rule = k.get("source").as_string("forbidden");
      if (std::optional<SourceRule> r = rule_from_name(rule)) kind.rule = *r;
      else report.bad_values.push_back("kinds." + kind.name + ".source: '" + std::string(rule) + "' is not required | optional | forbidden");
      kind.describes = std::string(k.get("describes").as_string());
      p.kinds.push_back(std::move(kind));
    }
  } else if (!ks.is_null()) {
    report.bad_values.push_back("kinds: expected an array");
  }

  if (const json::Value& s = v.get("sources"); s.is_object()) {
    for (const auto& [k, x] : s.obj)
      if (k != "documents" && k != "rows" && k != "submits" && k != "notes") report.unknown_keys.push_back("sources." + k);
    for (const json::Value& d : s.get("documents").arr) {
      AppProfile::Document doc;
      doc.name = std::string(d.get("name").as_string());
      if (doc.name.empty()) { report.bad_values.push_back("sources.documents[]: a document needs a name"); continue; }
      doc.sample = std::string(d.get("sample").as_string());
      p.documents.push_back(std::move(doc));
    }
    for (const json::Value& r : s.get("rows").arr) {
      AppProfile::RowSource rs;
      rs.name = std::string(r.get("name").as_string());
      if (rs.name.empty()) { report.bad_values.push_back("sources.rows[]: a row source needs a name"); continue; }
      for (const json::Value& row : r.get("sample").arr)
        rs.sample.emplace_back(std::string(row.get("label").as_string()), std::string(row.get("value").as_string()));
      p.rows.push_back(std::move(rs));
    }
    for (const json::Value& n : s.get("submits").arr)
      if (n.is_string()) p.submits.emplace_back(n.str);
    for (const json::Value& n : s.get("notes").arr)
      if (n.is_string()) p.notes.emplace_back(n.str);
  } else if (!s.is_null()) {
    report.bad_values.push_back("sources: expected an object");
  }

  if (const json::Value& h = v.get("help"); h.is_object()) {
    for (const auto& [k, x] : h.obj)
      if (k != "lead" && k != "note" && k != "scopes") report.unknown_keys.push_back("help." + k);
    p.help.lead = std::string(h.get("lead").as_string());
    p.help.note = std::string(h.get("note").as_string());
    for (const json::Value& s : h.get("scopes").arr)
      if (s.is_string()) p.help.scopes.emplace_back(s.str);
  } else if (!h.is_null()) {
    report.bad_values.push_back("help: expected an object");
  }

  if (const json::Value& ms = v.get("menus"); ms.is_array()) {
    for (const json::Value& m : ms.arr) {
      AppProfile::MenuFile mf;
      mf.name = std::string(m.get("name").as_string());
      if (mf.name.empty()) { report.bad_values.push_back("menus[]: a menu needs a name"); continue; }
      // The file's text VERBATIM. A profile carries the bytes rather than a parsed tree
      // so the app's own menu loader and the tool's are reading the same thing — a
      // re-serialised tree would be a second spelling that can drift.
      mf.json = std::string(m.get("json").as_string());
      p.menus.push_back(std::move(mf));
    }
  } else if (!ms.is_null()) {
    report.bad_values.push_back("menus: expected an array");
  }
  return p;
}

json::Value app_profile_to_json(const AppProfile& p) {
  json::Value root = json::Value::object();
  root.set("app", json::Value::string(p.app));
  root.set("min_width", json::Value::number(p.min_width));
  root.set("min_height", json::Value::number(p.min_height));
  json::Value actions = json::Value::object();
  for (const ActionDecl& a : p.actions) actions.set(a.name, json::Value::string(a.description));
  root.set("actions", std::move(actions));
  json::Value kinds = json::Value::array();
  for (const AppProfile::Kind& k : p.kinds) {
    json::Value o = json::Value::object();
    o.set("name", json::Value::string(k.name));
    o.set("source", json::Value::string(rule_name(k.rule)));
    o.set("describes", json::Value::string(k.describes));
    kinds.arr.push_back(std::move(o));
  }
  root.set("kinds", std::move(kinds));
  json::Value sources = json::Value::object();
  json::Value docs = json::Value::array();
  for (const AppProfile::Document& d : p.documents) {
    json::Value o = json::Value::object();
    o.set("name", json::Value::string(d.name));
    o.set("sample", json::Value::string(d.sample));
    docs.arr.push_back(std::move(o));
  }
  sources.set("documents", std::move(docs));
  json::Value rows = json::Value::array();
  for (const AppProfile::RowSource& r : p.rows) {
    json::Value o = json::Value::object();
    o.set("name", json::Value::string(r.name));
    json::Value sample = json::Value::array();
    for (const auto& [label, value] : r.sample) {
      json::Value row = json::Value::object();
      row.set("label", json::Value::string(label));
      row.set("value", json::Value::string(value));
      sample.arr.push_back(std::move(row));
    }
    o.set("sample", std::move(sample));
    rows.arr.push_back(std::move(o));
  }
  sources.set("rows", std::move(rows));
  json::Value submits = json::Value::array();
  for (const std::string& s : p.submits) submits.arr.push_back(json::Value::string(s));
  sources.set("submits", std::move(submits));
  json::Value notes = json::Value::array();
  for (const std::string& s : p.notes) notes.arr.push_back(json::Value::string(s));
  sources.set("notes", std::move(notes));
  root.set("sources", std::move(sources));
  json::Value help = json::Value::object();
  help.set("lead", json::Value::string(p.help.lead));
  help.set("note", json::Value::string(p.help.note));
  json::Value scopes = json::Value::array();
  for (const std::string& s : p.help.scopes) scopes.arr.push_back(json::Value::string(s));
  help.set("scopes", std::move(scopes));
  root.set("help", std::move(help));
  json::Value menus = json::Value::array();
  for (const AppProfile::MenuFile& m : p.menus) {
    json::Value o = json::Value::object();
    o.set("name", json::Value::string(m.name));
    o.set("json", json::Value::string(m.json));
    menus.arr.push_back(std::move(o));
  }
  root.set("menus", std::move(menus));
  return root;
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
