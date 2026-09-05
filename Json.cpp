// rolltui/Json.cpp — see Json.hpp. `parse`/`dump` are thin shims over
// `rolltui/c/rolltui_json.h`, which now holds the actual algorithm (Phase 17 m1); everything
// else here is unchanged C++ working directly on `Value`'s own `std::string`/`std::vector`
// members, which this module keeps for the reasons stated at the top of Json.hpp.
#include "rolltui/Json.hpp"

#include "rolltui/c/rolltui_json.h"

namespace rolltui::json {

static const Value kNull{};

const Value& Value::get(std::string_view key) const {
  if (kind != Kind::Object) return kNull;
  for (const auto& [k, v] : obj)
    if (k == key) return v;
  return kNull;
}

bool Value::has(std::string_view key) const {
  if (kind != Kind::Object) return false;
  for (const auto& [k, v] : obj)
    if (k == key) return true;
  return false;
}

Value& Value::set(std::string_view key, Value v) {
  kind = Kind::Object;
  for (auto& [k, existing] : obj)
    if (k == key) { existing = std::move(v); return existing; }
  obj.emplace_back(std::string(key), std::move(v));
  return obj.back().second;
}

namespace {

// The C tree's `kind` byte and `Value::Kind` are deliberately not `static_cast` across —
// that would be relying on the two enumerations staying numbered alike by coincidence
// (CLAUDE.md's "layout-compatible-by-fiat" failure shape, one level removed from memory
// layout). An explicit table is one more line and cannot silently drift.
Value::Kind kind_from_c(unsigned char k) {
  switch (k) {
    case ROLLTUI_JSON_BOOL: return Value::Kind::Bool;
    case ROLLTUI_JSON_NUMBER: return Value::Kind::Number;
    case ROLLTUI_JSON_STRING: return Value::Kind::String;
    case ROLLTUI_JSON_ARRAY: return Value::Kind::Array;
    case ROLLTUI_JSON_OBJECT: return Value::Kind::Object;
    default: return Value::Kind::Null;
  }
}

// Recursively converts a parsed C tree into a C++ Value tree. This is a real, full copy —
// the cost `Json.hpp`'s header comment names as the price of keeping `Value` a real
// `std::string`/`std::vector` type instead of a proxy over the C storage.
Value value_from_c(const RolltuiJsonValue* v) {
  Value out;
  if (!v) return out;
  out.kind = kind_from_c(v->kind);
  out.b = v->b != 0;
  out.num = v->num;
  out.str.assign(v->str.p ? std::string_view(v->str.p, v->str.n) : std::string_view());
  const std::size_t arr_n = rolltui_json_array_size(v);
  out.arr.reserve(arr_n);
  for (std::size_t i = 0; i < arr_n; ++i) out.arr.push_back(value_from_c(rolltui_json_array_at(v, i)));
  const std::size_t obj_n = rolltui_json_object_size(v);
  out.obj.reserve(obj_n);
  for (std::size_t i = 0; i < obj_n; ++i) {
    std::size_t klen = 0;
    const char* k = rolltui_json_object_key_at(v, i, &klen);
    out.obj.emplace_back(std::string(k, klen), value_from_c(rolltui_json_object_value_at(v, i)));
  }
  return out;
}

// The other direction: builds a fresh, owned C tree out of a C++ Value, for `dump()` to hand
// to `rolltui_json_dump`. The caller frees the result with `rolltui_json_free`.
RolltuiJsonValue* value_to_c(const Value& v) {
  switch (v.kind) {
    case Value::Kind::Null: return rolltui_json_null();
    case Value::Kind::Bool: return rolltui_json_bool(v.b ? 1 : 0);
    case Value::Kind::Number: return rolltui_json_number(v.num);
    case Value::Kind::String: return rolltui_json_string(v.str.data(), v.str.size());
    case Value::Kind::Array: {
      RolltuiJsonValue* out = rolltui_json_array();
      for (const Value& e : v.arr) rolltui_json_array_push(out, value_to_c(e));
      return out;
    }
    case Value::Kind::Object: {
      RolltuiJsonValue* out = rolltui_json_object();
      for (const auto& [k, val] : v.obj) rolltui_json_set(out, k.data(), k.size(), value_to_c(val));
      return out;
    }
  }
  return rolltui_json_null();
}

}  // namespace

Value parse(std::string_view text, std::string& error) {
  RolltuiStr err{};
  RolltuiJsonValue* v = rolltui_json_parse(text.data(), text.size(), &err);
  error.assign(err.p ? err.p : "", err.n);
  rolltui_str_free(&err);
  if (!v) return Value::null();
  Value out = value_from_c(v);
  rolltui_json_free(v);
  return out;
}

std::string dump(const Value& v, int indent) {
  RolltuiJsonValue* c = value_to_c(v);
  RolltuiStr out{};
  rolltui_json_dump(c, indent, &out);
  rolltui_json_free(c);
  std::string result(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  return result;
}

}  // namespace rolltui::json
