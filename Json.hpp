#pragma once
//
// rolltui/Json.hpp — a small JSON value and parser of our own, so the library's file
// formats (themes, layouts, menus, config) need no dependency: "two headers and go"
// for a consumer, and one parser to hold to one error-reporting standard. Not a
// general-purpose JSON library: no streaming, no comments, numbers are doubles,
// objects keep insertion order (so a theme's role list round-trips in the order the
// author wrote it).
//
//   json::Value v = json::parse(text, error)   // error gets "line N: what" on failure
//   v.is_object(), v["key"], v.get("key")      // lookups never throw; a missing key is
//                                              // a Null value you can keep querying
//   json::dump(v, indent)                      // deterministic output, for round trips
//
// PHASE 17 m1: the parsing and serialising ALGORITHMS now live in C
// (`rolltui/c/rolltui_json.h`); `parse`/`dump` below are thin shims that convert to and
// from that file's tree and otherwise change nothing. `Value` itself keeps this exact
// `std::string`/`std::vector` shape — unlike `DocEntry`/`MenuItem`, it is NOT made "the same
// struct" as its C counterpart, because roughly 40 call sites across six other C++ modules
// (`Theme.cpp`, `Layout.cpp`, `Menu.cpp`, `Bindings.cpp`, `Presets.cpp`, `ThemeGen.cpp`,
// `ThemeAnalysis.cpp`, `PresetStore.hpp`) read and write `.str`/`.arr`/`.obj` with real
// `std::vector`/`std::string` operations a C-backed proxy cannot honestly offer (an
// erase-remove over `.obj` in `Presets.cpp`, `.str` handed into a `vector<string>::push_back`
// in `Theme.cpp`, a whole-vector assignment and aggregate-init `push_back`s in
// `TuiFrontend.cpp`/`paint.cpp`). None of those six are ported by this task, so this header's
// public shape is UNCHANGED and every one of those call sites keeps compiling exactly as
// written. See `rolltui/c/rolltui_json.h`'s header comment for the full reasoning — it is the
// one place `json::Value` still crosses this library's boundary as a tree rather than as
// text, and it is a deliberate, scoped exception rather than the pattern to copy.
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rolltui::json {

struct Value {
  enum class Kind { Null, Bool, Number, String, Array, Object };
  Kind kind = Kind::Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<Value> arr;
  std::vector<std::pair<std::string, Value>> obj;  // insertion order kept

  static Value null() { return {}; }
  static Value boolean(bool v) { Value x; x.kind = Kind::Bool; x.b = v; return x; }
  static Value number(double v) { Value x; x.kind = Kind::Number; x.num = v; return x; }
  static Value string(std::string v) { Value x; x.kind = Kind::String; x.str = std::move(v); return x; }
  static Value array() { Value x; x.kind = Kind::Array; return x; }
  static Value object() { Value x; x.kind = Kind::Object; return x; }

  bool is_null() const { return kind == Kind::Null; }
  bool is_bool() const { return kind == Kind::Bool; }
  bool is_number() const { return kind == Kind::Number; }
  bool is_string() const { return kind == Kind::String; }
  bool is_array() const { return kind == Kind::Array; }
  bool is_object() const { return kind == Kind::Object; }

  // Object lookup; Null when absent or when this is not an object.
  const Value& get(std::string_view key) const;
  const Value& operator[](std::string_view key) const { return get(key); }
  bool has(std::string_view key) const;
  // Object insert-or-replace.
  Value& set(std::string_view key, Value v);

  // Typed reads with defaults; never throw.
  std::string_view as_string(std::string_view def = "") const { return is_string() ? str : def; }
  double as_number(double def = 0) const { return is_number() ? num : def; }
  bool as_bool(bool def = false) const { return is_bool() ? b : def; }

  // Structural equality (objects compare in insertion order, as they dump).
  bool operator==(const Value&) const = default;
};

// Parses `text`. On failure returns Null and sets `error` to "line N: message".
Value parse(std::string_view text, std::string& error);

// Serialises deterministically. indent = 0 → single line.
std::string dump(const Value& v, int indent = 2);

}  // namespace rolltui::json
