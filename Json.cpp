// rolltui/Json.cpp — see Json.hpp.
#include "rolltui/Json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "rolltui/Unicode.hpp"

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

struct Parser {
  std::string_view s;
  std::size_t i = 0;
  int line = 1;
  std::string error;

  bool fail(const std::string& msg) {
    if (error.empty()) error = "line " + std::to_string(line) + ": " + msg;
    return false;
  }
  void ws() {
    while (i < s.size()) {
      char c = s[i];
      if (c == '\n') { ++line; ++i; }
      else if (c == ' ' || c == '\t' || c == '\r') ++i;
      else break;
    }
  }
  bool expect(char c) {
    ws();
    if (i < s.size() && s[i] == c) { ++i; return true; }
    return fail(std::string("expected '") + c + "'" +
                (i < s.size() ? std::string(" but found '") + s[i] + "'" : " at end of input"));
  }
  bool parse_string(std::string& out) {
    if (i >= s.size() || s[i] != '"') return fail("expected a string");
    ++i;
    while (i < s.size()) {
      char c = s[i++];
      if (c == '"') return true;
      if (c == '\n') return fail("newline inside a string");
      if (c != '\\') { out.push_back(c); continue; }
      if (i >= s.size()) return fail("unterminated escape");
      char e = s[i++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          auto hex4 = [&](char32_t& v) {
            if (i + 4 > s.size()) return false;
            v = 0;
            for (int k = 0; k < 4; ++k) {
              char h = s[i++];
              int d;
              if (h >= '0' && h <= '9') d = h - '0';
              else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
              else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
              else return false;
              v = (v << 4) | static_cast<char32_t>(d);
            }
            return true;
          };
          char32_t v;
          if (!hex4(v)) return fail("bad \\u escape");
          if (v >= 0xD800 && v <= 0xDBFF) {  // surrogate pair
            char32_t lo = 0;
            if (i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
              i += 2;
              if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return fail("bad surrogate pair");
              v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              v = 0xFFFD;
            }
          } else if (v >= 0xDC00 && v <= 0xDFFF) {
            v = 0xFFFD;
          }
          unicode::append_utf8(out, v);
          break;
        }
        default: return fail(std::string("unknown escape \\") + e);
      }
    }
    return fail("unterminated string");
  }
  bool parse_value(Value& out, int depth) {
    if (depth > 200) return fail("nesting too deep");
    ws();
    if (i >= s.size()) return fail("unexpected end of input");
    char c = s[i];
    if (c == '{') {
      ++i;
      out = Value::object();
      ws();
      if (i < s.size() && s[i] == '}') { ++i; return true; }
      for (;;) {
        ws();
        std::string key;
        if (!parse_string(key)) return false;
        if (!expect(':')) return false;
        Value v;
        if (!parse_value(v, depth + 1)) return false;
        for (const auto& [k, existing] : out.obj)
          if (k == key) return fail("duplicate key \"" + key + "\"");
        out.obj.emplace_back(std::move(key), std::move(v));
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        return fail("expected ',' or '}' in object");
      }
    }
    if (c == '[') {
      ++i;
      out = Value::array();
      ws();
      if (i < s.size() && s[i] == ']') { ++i; return true; }
      for (;;) {
        Value v;
        if (!parse_value(v, depth + 1)) return false;
        out.arr.push_back(std::move(v));
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        return fail("expected ',' or ']' in array");
      }
    }
    if (c == '"') {
      std::string str;
      if (!parse_string(str)) return false;
      out = Value::string(std::move(str));
      return true;
    }
    auto literal = [&](std::string_view word) {
      if (s.substr(i, word.size()) == word) { i += word.size(); return true; }
      return false;
    };
    if (literal("true")) { out = Value::boolean(true); return true; }
    if (literal("false")) { out = Value::boolean(false); return true; }
    if (literal("null")) { out = Value::null(); return true; }
    if (c == '-' || (c >= '0' && c <= '9')) {
      std::size_t start = i;
      if (s[i] == '-') ++i;
      while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' ||
                              s[i] == 'E' || s[i] == '+' || s[i] == '-'))
        ++i;
      std::string num(s.substr(start, i - start));
      char* end = nullptr;
      double d = std::strtod(num.c_str(), &end);
      if (!end || *end != '\0' || num == "-") return fail("bad number '" + num + "'");
      out = Value::number(d);
      return true;
    }
    return fail(std::string("unexpected character '") + c + "'");
  }
};

void dump_string(std::string& o, std::string_view s) {
  o.push_back('"');
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(c)));
          o += buf;
        } else {
          o.push_back(c);
        }
    }
  }
  o.push_back('"');
}

void dump_value(std::string& o, const Value& v, int indent, int level) {
  std::string pad(static_cast<std::size_t>(indent * level), ' ');
  std::string pad1(static_cast<std::size_t>(indent * (level + 1)), ' ');
  const char* nl = indent > 0 ? "\n" : "";
  switch (v.kind) {
    case Value::Kind::Null: o += "null"; break;
    case Value::Kind::Bool: o += v.b ? "true" : "false"; break;
    case Value::Kind::Number: {
      char buf[32];
      if (std::floor(v.num) == v.num && std::fabs(v.num) < 1e15)
        std::snprintf(buf, sizeof buf, "%.0f", v.num);
      else
        std::snprintf(buf, sizeof buf, "%.17g", v.num);
      o += buf;
      break;
    }
    case Value::Kind::String: dump_string(o, v.str); break;
    case Value::Kind::Array:
      if (v.arr.empty()) { o += "[]"; break; }
      o += "[";
      o += nl;
      for (std::size_t k = 0; k < v.arr.size(); ++k) {
        o += pad1;
        dump_value(o, v.arr[k], indent, level + 1);
        if (k + 1 < v.arr.size()) o += ",";
        o += nl;
      }
      o += pad + "]";
      break;
    case Value::Kind::Object:
      if (v.obj.empty()) { o += "{}"; break; }
      o += "{";
      o += nl;
      for (std::size_t k = 0; k < v.obj.size(); ++k) {
        o += pad1;
        dump_string(o, v.obj[k].first);
        o += indent > 0 ? ": " : ":";
        dump_value(o, v.obj[k].second, indent, level + 1);
        if (k + 1 < v.obj.size()) o += ",";
        o += nl;
      }
      o += pad + "}";
      break;
  }
}

}  // namespace

Value parse(std::string_view text, std::string& error) {
  Parser p;
  p.s = text;
  Value v;
  error.clear();
  if (!p.parse_value(v, 0)) { error = p.error; return Value::null(); }
  p.ws();
  if (p.i != text.size()) {
    p.fail("trailing characters after the value");
    error = p.error;
    return Value::null();
  }
  return v;
}

std::string dump(const Value& v, int indent) {
  std::string o;
  dump_value(o, v, indent, 0);
  return o;
}

}  // namespace rolltui::json
