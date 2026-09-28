//
// gen_unicode_tables.cpp — generates rolltui/unicode_tables.hpp (enums + extern table
// declarations) and rolltui/unicode_tables.cpp (the tables) from the Unicode Character
// Database files checked in under rolltui/ucd/ (fetched by tools/fetch_ucd.sh, which
// pins the Unicode version and every file's sha256).
//
//   gen_unicode_tables <ucd-dir> --out   <rolltui-dir>   write both files
//   gen_unicode_tables <ucd-dir> --check <rolltui-dir>   exit 1 if either is stale
//   gen_unicode_tables <ucd-dir>                         write the .cpp to stdout
//   --commit <text>   record this instead of `git rev-parse HEAD` (+ "-dirty" when
//                     this generator source has uncommitted changes)
//
// the ACQUIRE tier for Unicode knowledge is "generate thin glue and validate
// it": the two rejected data libraries (utf8proc, libunibreak — see the plan)
// carry the same few thousand code point ranges this tool emits, and Unicode publishes
// both the data and a conformance suite for it. The suites run in ctest against the
// algorithms in rolltui/Unicode.cpp; this tool only produces the tables they consult.
//
// What is generated (one binary-searched range table each, plus the enums they index):
//   LineBreak            DerivedLineBreak.txt          UAX #14 line breaking class
//   EastAsianWidth       DerivedEastAsianWidth.txt     UAX #11 width
//   GraphemeBreak        GraphemeBreakProperty.txt     UAX #29 Grapheme_Cluster_Break
//   WordBreak            WordBreakProperty.txt         UAX #29 Word_Break (word selection)
//   IndicConjunctBreak   DerivedCoreProperties.txt     InCB, for UAX #29 rule GB9c
//   GeneralCategory      DerivedGeneralCategory.txt    gc, for LB1 (SA→CM needs Mn/Mc),
//                                                      LB15a/b (Pi/Pf) and LB30b (Cn),
//                                                      and for the width function's
//                                                      zero-width classes
//   ExtendedPictographic emoji-data.txt                UAX #29 GB11, UAX #14 LB30b
//   DefaultIgnorable     DerivedCoreProperties.txt     width 0 (renders nothing)
//
// `# @missing:` lines give the value of every code point a file does not list, and
// MUST be applied: a file has one for the whole codespace and may have more for
// sub-ranges, applied in file order with later lines overriding earlier ones. That
// is how UNASSIGNED code points in the CJK blocks and Planes 2-3 get ID / W, and the
// Currency Symbols block PR — without it an unassigned ideograph would wrap like a
// Latin letter and measure one cell wide. The two properties are read from the
// extracted/Derived* files precisely because those carry the sub-range defaults as
// data; LineBreak.txt only describes them in prose, and the prose is imprecise
// (it says 1F000..1FAFF where the data says 1F000..1F7FF and 1F900..1FAFF — the
// Unicode 17 conformance suite follows the data; see tools/fetch_ucd.sh).
//
// A value name the generator does not know (a new line-break class, a new gc) is an
// error, never an "other" bucket: the enum in the header is the whole list, and the
// algorithm that consumes it has to say what the new class means.
//
// No dependencies beyond the standard library; macOS-only only in that it shells out
// to `git` for the commit line.
//
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr char32_t kMaxCp = 0x10FFFF;

// ---- the value vocabularies: fixed here, checked against the data -----------------

// UAX #14 classes, alphabetical. The header's enum is emitted in this order.
const std::vector<std::string> kLineBreakValues = {
    "AI", "AK", "AL", "AP", "AS", "B2", "BA", "BB", "BK", "CB", "CJ", "CL", "CM",
    "CP", "CR", "EB", "EM", "EX", "GL", "H2", "H3", "HH", "HL", "HY", "ID", "IN",
    "IS", "JL", "JT", "JV", "LF", "NL", "NS", "NU", "OP", "PO", "PR", "QU", "RI",
    "SA", "SG", "SP", "SY", "VF", "VI", "WJ", "XX", "ZW", "ZWJ"};
const std::vector<std::string> kEastAsianWidthValues = {"N", "A", "F", "H", "Na", "W"};
const std::vector<std::string> kGraphemeBreakValues = {
    "Other", "CR", "LF", "Control", "Extend", "ZWJ", "Regional_Indicator", "Prepend",
    "SpacingMark", "L", "V", "T", "LV", "LVT"};
// UAX #29 Word_Break, in the order tr29 lists them.
const std::vector<std::string> kWordBreakValues = {
    "Other", "CR", "LF", "Newline", "Extend", "ZWJ", "Regional_Indicator", "Format",
    "Katakana", "Hebrew_Letter", "ALetter", "Single_Quote", "Double_Quote", "MidNumLet",
    "MidLetter", "MidNum", "Numeric", "ExtendNumLet", "WSegSpace"};
const std::vector<std::string> kIndicConjunctBreakValues = {"None", "Consonant", "Extend",
                                                            "Linker"};
const std::vector<std::string> kGeneralCategoryValues = {
    "Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No", "Pc", "Pd", "Ps",
    "Pe", "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs",
    "Co", "Cn"};

// The derived files' @missing lines use long property-value names where their data
// lines use the short ones; both spell the same value.
const std::map<std::string, std::string> kValueAliases = {
    {"Unknown", "XX"},     {"Ideographic", "ID"}, {"Prefix_Numeric", "PR"},
    {"Neutral", "N"},      {"Ambiguous", "A"},    {"Fullwidth", "F"},
    {"Halfwidth", "H"},    {"Narrow", "Na"},      {"Wide", "W"},
};

// ---- small helpers -----------------------------------------------------------------

[[noreturn]] void die(const std::string& msg) {
  std::fprintf(stderr, "gen_unicode_tables: %s\n", msg.c_str());
  std::exit(2);
}

std::string trim(std::string_view s) {
  size_t a = 0, b = s.size();
  auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (a < b && ws(s[a])) ++a;
  while (b > a && ws(s[b - 1])) --b;
  return std::string(s.substr(a, b - a));
}

std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    size_t p = s.find(sep, start);
    out.push_back(trim(s.substr(start, p == std::string_view::npos ? std::string_view::npos
                                                                     : p - start)));
    if (p == std::string_view::npos) break;
    start = p + 1;
  }
  return out;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) die("cannot read " + path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool parse_range(const std::string& field, char32_t& lo, char32_t& hi) {
  size_t dots = field.find("..");
  auto hex = [](const std::string& s, char32_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    unsigned long v = std::strtoul(s.c_str(), &end, 16);
    if (*end != '\0' || v > kMaxCp) return false;
    out = static_cast<char32_t>(v);
    return true;
  };
  if (dots == std::string::npos) {
    if (!hex(field, lo)) return false;
    hi = lo;
    return true;
  }
  return hex(field.substr(0, dots), lo) && hex(field.substr(dots + 2), hi) && lo <= hi;
}

// One parsed UCD file: the data lines, the @missing lines, and the header comment.
struct UcdFile {
  std::string name;      // e.g. "DerivedLineBreak-17.0.0.txt" (first line, minus "# ")
  std::string date;      // e.g. "2025-07-29, 13:52:18 GMT"
  std::string version;   // "17.0.0" parsed from `name`, or from a "# Version:" line
  std::string header;    // every comment line before the first data line
  struct Line {
    char32_t lo, hi;
    std::vector<std::string> fields;  // fields[1..] — field 0 was the range
    bool missing;                     // from a `# @missing:` line
  };
  std::vector<Line> lines;
};

UcdFile parse_ucd(const std::string& path) {
  UcdFile f;
  std::string text = read_file(path);
  std::istringstream in(text);
  std::string raw;
  bool first = true, seen_data = false;
  while (std::getline(in, raw)) {
    std::string line = trim(raw);
    if (first) {
      first = false;
      if (line.rfind("# ", 0) == 0) f.name = line.substr(2);
    }
    if (line.rfind("# Date:", 0) == 0) f.date = trim(line.substr(7));
    if (line.rfind("# Version:", 0) == 0 && f.version.empty())
      f.version = trim(line.substr(10));
    if (line.rfind("# @missing:", 0) == 0) {
      auto fields = split(line.substr(11), ';');
      UcdFile::Line l;
      if (fields.empty() || !parse_range(fields[0], l.lo, l.hi))
        die(path + ": bad @missing line: " + line);
      l.fields = fields;
      l.missing = true;
      f.lines.push_back(l);
      continue;
    }
    if (line.empty() || line[0] == '#') {
      if (!seen_data) f.header += line + '\n';
      continue;
    }
    seen_data = true;
    size_t hash = line.find('#');
    std::string data = trim(hash == std::string::npos ? line : line.substr(0, hash));
    auto fields = split(data, ';');
    UcdFile::Line l;
    if (fields.size() < 2 || !parse_range(fields[0], l.lo, l.hi))
      die(path + ": bad data line: " + line);
    l.fields = fields;
    l.missing = false;
    f.lines.push_back(l);
  }
  // "DerivedLineBreak-17.0.0.txt" → "17.0.0" (but not "emoji-data.txt" → "data")
  size_t dash = f.name.find('-'), dot = f.name.rfind(".txt");
  if (dash != std::string::npos && dot != std::string::npos && dot > dash + 1 &&
      std::isdigit(static_cast<unsigned char>(f.name[dash + 1])))
    f.version = f.name.substr(dash + 1, dot - dash - 1);
  return f;
}

// A dense map over every code point, later run-length encoded.
// The C spellings of a table's names. `kLineBreak` is the C++ one and stays; C gets
// `rolltui_u_line_break` for the array and `ROLLTUI_LINEBREAK_*` for the values, because a
// C constant lives in one flat namespace and has to say who it belongs to.
struct Table;
std::string c_prefix(const Table& t);
std::string c_array(const Table& t);
std::string c_count(const Table& t);

struct Table {
  std::string name;                      // C++ identifier (kLineBreak)
  std::string enum_name;                 // "LineBreak"
  std::vector<std::string> values;       // enum vocabulary (index = value byte)
  std::vector<uint8_t> map;              // per code point
  uint8_t def = 0;                       // the @missing default
  std::vector<std::string> sources;      // file names, for the header comment
  bool flag = false;                     // boolean property: emitted without an enum
  Table() : map(kMaxCp + 1, 0) {}

  uint8_t index_of(const std::string& raw, const std::string& where) const {
    auto alias = kValueAliases.find(raw);
    const std::string& v = alias == kValueAliases.end() ? raw : alias->second;
    auto it = std::find(values.begin(), values.end(), v);
    if (it == values.end())
      die(where + ": unknown " + enum_name + " value '" + v +
          "' — a new Unicode value needs an enum entry AND a decision in Unicode.cpp");
    return static_cast<uint8_t>(it - values.begin());
  }
  void fill(char32_t lo, char32_t hi, uint8_t v) {
    for (char32_t c = lo; c <= hi; ++c) map[c] = v;
  }
};

struct Range {
  char32_t lo, hi;
  uint8_t v;
};
std::vector<Range> rle(const Table& t) {
  std::vector<Range> out;
  char32_t c = 0;
  while (c <= kMaxCp) {
    char32_t start = c;
    uint8_t v = t.map[c];
    while (c + 1 <= kMaxCp && t.map[c + 1] == v) ++c;
    if (v != t.def) out.push_back({start, c, v});
    ++c;
  }
  return out;
}

// Apply a property file's @missing lines in file order (the first must cover the
// whole codespace and becomes the table default; later ones override sub-ranges),
// then the explicit lines. `pick` selects the value field for a line, or returns ""
// to skip it (multi-property files).
void load_property(Table& t, const UcdFile& f,
                   const std::function<std::string(const UcdFile::Line&)>& pick) {
  t.sources.push_back(f.name + "  (" + f.date + ")");
  bool have_default = false;
  for (const auto& l : f.lines) {
    if (!l.missing) continue;
    std::string v = pick(l);
    if (v.empty()) continue;
    if (!have_default) {
      if (l.lo != 0 || l.hi != kMaxCp)
        die(f.name + ": the first @missing line must cover 0000..10FFFF, got " + v);
      t.def = t.index_of(v, f.name + " @missing");
      std::fill(t.map.begin(), t.map.end(), t.def);
      have_default = true;
      continue;
    }
    t.fill(l.lo, l.hi, t.index_of(v, f.name + " @missing sub-range"));
  }
  if (!have_default) die(f.name + ": no @missing default line for " + t.enum_name);
  for (const auto& l : f.lines) {
    if (l.missing) continue;
    std::string v = pick(l);
    if (v.empty()) continue;
    t.fill(l.lo, l.hi, t.index_of(v, f.name));
  }
}

// A boolean property drawn from a multi-property file (emoji-data.txt,
// DerivedCoreProperties.txt): rows whose named field equals `property`.
void load_flag(Table& t, const UcdFile& f, const std::string& property) {
  t.sources.push_back(f.name + "  (" + f.date + ")");
  t.flag = true;
  t.values = {"No", "Yes"};
  t.def = 0;
  std::fill(t.map.begin(), t.map.end(), 0);
  size_t rows = 0;
  for (const auto& l : f.lines) {
    if (l.missing || l.fields.size() < 2 || l.fields[1] != property) continue;
    t.fill(l.lo, l.hi, 1);
    ++rows;
  }
  if (rows == 0) die(f.name + ": no rows for property " + property);
}

std::string hex(char32_t c) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "0x%04X", static_cast<unsigned>(c));
  return buf;
}

std::string shell(const std::string& cmd) {
  std::string out;
  if (FILE* p = popen(cmd.c_str(), "r")) {
    char buf[256];
    while (fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
  }
  return trim(out);
}

std::vector<std::string> lines_of(const std::string& s) {
  std::vector<std::string> v;
  std::istringstream in(s);
  std::string l;
  while (std::getline(in, l)) v.push_back(l);
  return v;
}

// Compare a generated text with a file on disk, ignoring the generator-commit line.
// Returns true when current.
// `LineBreak` -> `ROLLTUI_LINEBREAK_`; `kLineBreak` -> `rolltui_u_line_break` and
// `ROLLTUI_U_LINE_BREAK_COUNT`. C has one flat namespace, so a constant has to say who it
// belongs to; C++ keeps the short names it already uses inside `rolltui::unicode`.
std::string c_prefix(const Table& t) {
  std::string out = "ROLLTUI_";
  for (char c : t.enum_name) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out + "_";
}

// `kLineBreak` -> `line_break`: drop the leading `k`, then camelCase to snake_case.
std::string snake_of(const Table& t) {
  std::string out;
  for (size_t i = (t.name.rfind('k', 0) == 0 ? 1 : 0); i < t.name.size(); ++i) {
    const char c = t.name[i];
    if (std::isupper(static_cast<unsigned char>(c)) && !out.empty()) out += '_';
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// `rolltui_u_table_` and not `rolltui_u_`: the boundary in rolltui/c/rolltui_unicode.h
// already has FUNCTIONS called `rolltui_u_east_asian_width` and friends, and C has one
// flat namespace — the data and the function that reads it cannot share a name.
std::string c_array(const Table& t) { return "rolltui_u_table_" + snake_of(t); }

std::string c_count(const Table& t) {
  std::string out = "ROLLTUI_U_TABLE_";
  for (char c : snake_of(t)) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out + "_COUNT";
}

bool check_file(const std::string& path, const std::string& text) {
  std::vector<std::string> a = lines_of(read_file(path)), b = lines_of(text);
  const std::string skip = "// generator commit:";
  size_t n = std::max(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    const std::string* x = i < a.size() ? &a[i] : nullptr;
    const std::string* y = i < b.size() ? &b[i] : nullptr;
    if (x && y && x->rfind(skip, 0) == 0 && y->rfind(skip, 0) == 0) continue;
    if (!x || !y || *x != *y) {
      std::fprintf(stderr,
                   "gen_unicode_tables: %s is STALE — first difference at line %zu\n"
                   "  checked in: %s\n  regenerated: %s\n",
                   path.c_str(), i + 1, x ? x->c_str() : "<end of file>",
                   y ? y->c_str() : "<end of file>");
      return false;
    }
  }
  std::printf("gen_unicode_tables: %s is current (%zu lines)\n", path.c_str(), a.size());
  return true;
}

void write_file(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  if (!out) die("cannot write " + path);
  out << text;
  std::fprintf(stderr, "gen_unicode_tables: wrote %s (%zu bytes)\n", path.c_str(), text.size());
}

}  // namespace

int main(int argc, char** argv) {
  std::string ucd_dir, out_dir, check_dir, commit;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* flag) -> std::string {
      if (i + 1 >= argc) die(std::string(flag) + " needs a value");
      return argv[++i];
    };
    if (a == "--out") out_dir = next("--out");
    else if (a == "--check") check_dir = next("--check");
    else if (a == "--commit") commit = next("--commit");
    else if (a.rfind("--", 0) == 0) die("unknown flag " + a);
    else if (ucd_dir.empty()) ucd_dir = a;
    else die("unexpected argument " + a);
  }
  if (ucd_dir.empty()) die("usage: gen_unicode_tables <ucd-dir> [--out DIR | --check DIR]");
  if (ucd_dir.back() != '/') ucd_dir += '/';

  // ---- read ------------------------------------------------------------------------
  UcdFile lb = parse_ucd(ucd_dir + "DerivedLineBreak.txt");
  UcdFile eaw = parse_ucd(ucd_dir + "DerivedEastAsianWidth.txt");
  UcdFile gcb = parse_ucd(ucd_dir + "GraphemeBreakProperty.txt");
  UcdFile wb = parse_ucd(ucd_dir + "WordBreakProperty.txt");
  UcdFile dcp = parse_ucd(ucd_dir + "DerivedCoreProperties.txt");
  UcdFile dgc = parse_ucd(ucd_dir + "DerivedGeneralCategory.txt");
  UcdFile emoji = parse_ucd(ucd_dir + "emoji-data.txt");

  // Every versioned file must agree on the Unicode version; emoji-data.txt carries
  // "# Version: 17.0" (major.minor only), so it is checked as a prefix.
  std::string version = lb.version;
  for (const UcdFile* f : {&eaw, &gcb, &wb, &dcp, &dgc})
    if (f->version != version)
      die("Unicode version mismatch: " + lb.name + " is " + version + " but " + f->name +
          " is " + f->version);
  if (version.rfind(emoji.version, 0) != 0)
    die("emoji-data.txt Version: " + emoji.version + " does not match " + version);

  // ---- build -----------------------------------------------------------------------
  auto field = [](size_t n) {
    return [n](const UcdFile::Line& l) -> std::string {
      return l.fields.size() > n ? l.fields[n] : "";
    };
  };
  Table t_lb;
  t_lb.name = "kLineBreak"; t_lb.enum_name = "LineBreak";
  t_lb.values = kLineBreakValues;
  load_property(t_lb, lb, field(1));

  Table t_eaw;
  t_eaw.name = "kEastAsianWidth"; t_eaw.enum_name = "EastAsianWidth";
  t_eaw.values = kEastAsianWidthValues;
  load_property(t_eaw, eaw, field(1));

  Table t_gcb;
  t_gcb.name = "kGraphemeBreak"; t_gcb.enum_name = "GraphemeBreak";
  t_gcb.values = kGraphemeBreakValues;
  load_property(t_gcb, gcb, field(1));

  Table t_wb;
  t_wb.name = "kWordBreak"; t_wb.enum_name = "WordBreak";
  t_wb.values = kWordBreakValues;
  load_property(t_wb, wb, field(1));

  // DerivedCoreProperties.txt: "094D ; InCB; Linker" (and "# @missing: ...; InCB; None").
  Table t_incb;
  t_incb.name = "kIndicConjunctBreak"; t_incb.enum_name = "IndicConjunctBreak";
  t_incb.values = kIndicConjunctBreakValues;
  load_property(t_incb, dcp, [](const UcdFile::Line& l) -> std::string {
    return (l.fields.size() > 2 && l.fields[1] == "InCB") ? l.fields[2] : "";
  });

  // DerivedGeneralCategory.txt has no @missing line in 17.0.0 (it lists Cn ranges
  // explicitly); synthesise the documented default so load_property's contract holds.
  Table t_gc;
  t_gc.name = "kGeneralCategory"; t_gc.enum_name = "GeneralCategory";
  t_gc.values = kGeneralCategoryValues;
  {
    bool has_missing = false;
    for (const auto& l : dgc.lines) has_missing |= l.missing;
    if (!has_missing) {
      std::vector<std::string> f = {"0000..10FFFF", "Cn"};
      dgc.lines.insert(dgc.lines.begin(), UcdFile::Line{0, kMaxCp, f, true});
    }
  }
  load_property(t_gc, dgc, field(1));

  Table t_ep;
  t_ep.name = "kExtendedPictographic"; t_ep.enum_name = "ExtendedPictographic";
  load_flag(t_ep, emoji, "Extended_Pictographic");

  Table t_di;
  t_di.name = "kDefaultIgnorable"; t_di.enum_name = "DefaultIgnorable";
  load_flag(t_di, dcp, "Default_Ignorable_Code_Point");

  // ---- the commit line -------------------------------------------------------------
  if (commit.empty()) {
    std::string git = "git -C '" + ucd_dir + "' ";
    commit = shell(git + "rev-parse --short=12 HEAD 2>/dev/null");
    if (commit.empty()) commit = "unknown";
    std::string self = shell(git + "rev-parse --show-toplevel 2>/dev/null");
    if (!self.empty() &&
        !shell(git + "status --porcelain -- '" + self + "/tools/gen_unicode_tables.cpp'")
             .empty())
      commit += "-dirty";
  }

  // ---- emit ------------------------------------------------------------------------
  std::vector<const Table*> tables = {&t_lb, &t_eaw, &t_gcb, &t_wb, &t_incb, &t_gc, &t_ep, &t_di};
  std::string banner;
  {
    std::ostringstream o;
    o << "// Unicode version: " << version << "\n"
         "// Sources (the first line and the Date: line of each UCD file read):\n";
    std::vector<std::string> seen;
    for (const Table* t : tables)
      for (const auto& s : t->sources)
        if (std::find(seen.begin(), seen.end(), s) == seen.end()) {
          seen.push_back(s);
          o << "//   " << s << "\n";
        }
    o << "// generator commit: " << commit << "\n"
         "//\n"
         "// Regenerate:  gen_unicode_tables rolltui/ucd --out rolltui\n"
         "// Verify:      gen_unicode_tables rolltui/ucd --check rolltui\n"
         "//              (ctest runs the check; the commit line above is the one line it\n"
         "//              ignores, since it names the commit the tables were made from)\n";
    banner = o.str();
  }

  std::string header;
  {
    std::ostringstream o;
    o << "// unicode_tables.h — GENERATED by tools/gen_unicode_tables.cpp. DO NOT EDIT.\n"
         "//\n" << banner <<
         "//\n"
         "// Each table (defined in unicode_tables.c) is a sorted, non-overlapping list of\n"
         "// {first, last, value} ranges covering only the code points whose value differs\n"
         "// from the table's default; look a code point up by binary search on `last`.\n"
         "// Unassigned code points inside the CJK blocks and Planes 2-3 carry ID / W, and\n"
         "// the Currency Symbols block PR, from the @missing sub-range lines of the\n"
         "// Derived* files.\n"
         "//\n"
         "// ONE TABLE FILE, COMPILED BY BOTH LANGUAGES. The data is C and the\n"
         "// arrays have C linkage, so the two implementations of the Unicode algorithms\n"
         "// share them rather than each carrying a copy: 9,800 lines duplicated would be\n"
         "// 9,800 lines that could disagree, and the whole point of the flag is that the two\n"
         "// sides answer the SAME tests from the SAME facts.\n"
         "//\n"
         "// Each property has two spellings, both generated HERE from one list, because each\n"
         "// implementation should read naturally in its own language: plain\n"
         "// constants for C, an `enum class` for C++. A `static_assert` per value is emitted\n"
         "// below, so a generator that ever emitted them differently would fail to compile\n"
         "// rather than silently classify a code point wrong.\n"
         "#ifndef ROLLTUI_UNICODE_TABLES_H\n"
         "#define ROLLTUI_UNICODE_TABLES_H\n"
         "#include <stddef.h>\n"
         "\n"
         "#include \"rolltui/rolltui.h\"\n"
         "\n"
         "#ifdef __cplusplus\n"
         "extern \"C\" {\n"
         "#endif\n"
         "\n"
         "#define ROLLTUI_UNICODE_VERSION \"" << version << "\"\n"
         "\n"
         "typedef struct RolltuiUnicodeRange {\n"
         "  RolltuiCodepoint first;\n"
         "  RolltuiCodepoint last;\n"
         "  unsigned char value;\n"
         "} RolltuiUnicodeRange;\n";
    for (const Table* t : tables) {
      if (t->flag) continue;
      o << "\n/* " << t->enum_name << " */\n";
      for (size_t i = 0; i < t->values.size(); ++i)
        o << "#define " << c_prefix(*t) << t->values[i] << " " << i << "\n";
      o << "#define " << c_prefix(*t) << "DEFAULT " << c_prefix(*t) << t->values[t->def] << "\n";
    }
    o << "\n";
    for (const Table* t : tables) {
      std::vector<Range> ranges = rle(*t);
      o << "extern const RolltuiUnicodeRange " << c_array(*t) << "[" << ranges.size() << "];  /* "
        << t->enum_name << (t->flag ? " (value 1 = Yes; absent = No)" : "") << " */\n";
      o << "#define " << c_count(*t) << " " << ranges.size() << "\n";
    }
    o << "\n"
         "#ifdef __cplusplus\n"
         "}  /* extern \"C\" */\n"
         "\n"
         "// The C++ spelling of the same facts. The arrays are the C arrays under their old\n"
         "// names, so `UnicodeCpp.cpp` reads exactly as it always did.\n"
         "#include <cstddef>\n"
         "\n"
         "namespace rolltui::unicode {\n"
         "\n"
         "inline constexpr char kUnicodeVersion[] = ROLLTUI_UNICODE_VERSION;\n"
         "using Range = RolltuiUnicodeRange;\n";
    for (const Table* t : tables) {
      if (t->flag) continue;
      o << "\nenum class " << t->enum_name << " : unsigned char {";
      for (size_t i = 0; i < t->values.size(); ++i) o << (i ? ", " : " ") << t->values[i];
      o << " };\n";
      for (size_t i = 0; i < t->values.size(); ++i)
        o << "static_assert(static_cast<unsigned>(" << t->enum_name << "::" << t->values[i]
          << ") == " << c_prefix(*t) << t->values[i] << ", \"" << t->enum_name << "::"
          << t->values[i] << " and " << c_prefix(*t) << t->values[i] << " disagree\");\n";
      o << "inline constexpr " << t->enum_name << " " << t->name << "Default = "
        << t->enum_name << "::" << t->values[t->def] << ";\n";
    }
    o << "\n";
    for (const Table* t : tables) {
      std::vector<Range> ranges = rle(*t);
      o << "inline const Range* const " << t->name << " = " << c_array(*t) << ";\n";
      o << "inline constexpr std::size_t " << t->name << "Count = " << c_count(*t) << ";\n";
    }
    o << "\n}  // namespace rolltui::unicode\n"
         "#endif  // __cplusplus\n"
         "\n"
         "#endif  /* ROLLTUI_UNICODE_TABLES_H */\n";
    header = o.str();
  }

  std::string source;
  {
    std::ostringstream o;
    o << "/* unicode_tables.c — GENERATED by tools/gen_unicode_tables.cpp. DO NOT EDIT. */\n"
         "/*\n" << banner <<
         "*/\n"
         "#include \"rolltui/unicode_tables.h\"\n";
    for (const Table* t : tables) {
      std::vector<Range> ranges = rle(*t);
      o << "\n/* " << t->enum_name << ": " << ranges.size() << " ranges";
      if (t->flag) o << " (value is always 1; absent means No) */\n";
      else o << ", default " << t->enum_name << "::" << t->values[t->def] << " */\n";
      o << "const RolltuiUnicodeRange " << c_array(*t) << "[" << ranges.size() << "] = {\n";
      for (const Range& r : ranges) {
        o << "  {" << hex(r.lo) << ", " << hex(r.hi) << ", " << unsigned(r.v) << "},";
        // Rows carry the enum's integer value; the trailing comment names it so a
        // diff of the file on a Unicode bump reads as classes, not numbers.
        if (!t->flag) o << "  /* " << t->values[r.v] << " */";
        o << "\n";
      }
      o << "};\n";
    }
    source = o.str();
  }

  // ---- write / check ---------------------------------------------------------------
  if (!check_dir.empty()) {
    if (check_dir.back() != '/') check_dir += '/';
    bool ok = check_file(check_dir + "unicode_tables.h", header);
    ok = check_file(check_dir + "unicode_tables.c", source) && ok;
    return ok ? 0 : 1;
  }
  if (out_dir.empty()) {
    std::fwrite(source.data(), 1, source.size(), stdout);
    return 0;
  }
  if (out_dir.back() != '/') out_dir += '/';
  write_file(out_dir + "unicode_tables.h", header);
  write_file(out_dir + "unicode_tables.c", source);
  return 0;
}
