/* rolltui/c/rolltui_menu.c — the C side of the menu widget, the typed-field rules and (Phase
 * 17 m1) the file format. See rolltui_menu.h; the rules are rolltui/Menu.hpp's. */
#include "rolltui/c/rolltui_menu.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_layout.h"

/* A literal C string plus its length, the same one-time convenience `rolltui_bindings.c` and
 * `rolltui_app_profile.c` each name locally rather than share — a menu loads once per file,
 * never per frame. */
#define K(s) (s), strlen(s)

static int streq(const char* s, size_t slen, const char* lit) {
  const size_t litlen = strlen(lit);
  return slen == litlen && (litlen == 0 || memcmp(s, lit, litlen) == 0);
}

#define CRUMB " \xE2\x80\xBA "   /* " › " */
#define ELLIPSIS "\xE2\x80\xA6"  /* "…" */

static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static size_t zmin(size_t a, size_t b) { return a < b ? a : b; }

/* ---- small text helpers ------------------------------------------------------------------- */

static void str_add(RolltuiStr* s, const char* z) { rolltui_str_append(s, z, strlen(z)); }

/* A number as text: `precision` digits after the point, or the shortest exact form. */
static void num_text(double v, int precision, char* out, size_t cap) {
  if (precision >= 0) snprintf(out, cap, "%.*f", precision, v);
  else if (v == floor(v) && fabs(v) < 1e15) snprintf(out, cap, "%.0f", v);
  else snprintf(out, cap, "%.10g", v);
}

static int all_digits(const char* s, size_t n) {
  size_t i;
  if (n == 0) return 0;
  for (i = 0; i < n; ++i)
    if (s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

/* Could an integer whose decimal text begins with `v` (already read, `neg` signed) still land
 * in [min, max] by appending digits? ∃k ≥ 0: [v·10^k, v·10^k + 10^k − 1] meets the range. */
static int int_reachable(double v, int neg, double min, double max) {
  double scale = 1;
  int k;
  for (k = 0; k <= 18; ++k, scale *= 10) {
    const double lo = v * scale, hi = v * scale + (scale - 1);
    const double a = neg ? -hi : lo, b = neg ? -lo : hi;
    if (b >= min && a <= max) return 1;
    if (lo > (fabs(min) > fabs(max) ? fabs(min) : fabs(max))) break;
  }
  return 0;
}

/* Reads a signed decimal prefix: sign, integer digits, optional '.', fraction digits. The
 * two digit runs are BORROWS into the caller's text, which is what the C++'s two
 * `std::string`s were hiding — a `split_number` allocated twice per keystroke. */
typedef struct NumParts {
  int neg, dot;
  const char* ip;
  size_t ip_n;
  const char* fp;
  size_t fp_n;
} NumParts;

static int split_number(const char* s, size_t n, NumParts* out) {
  size_t i = 0;
  memset(out, 0, sizeof *out);
  out->ip = s;
  out->fp = s;
  if (i < n && s[i] == '-') {
    out->neg = 1;
    ++i;
  }
  out->ip = s + i;
  for (; i < n; ++i) {
    const char c = s[i];
    if (c >= '0' && c <= '9') {
      if (out->dot) ++out->fp_n;
      else ++out->ip_n;
      continue;
    }
    if (c == '.' && !out->dot) {
      out->dot = 1;
      out->fp = s + i + 1;
      continue;
    }
    return 0;
  }
  return 1;
}

static double span_to_double(const char* s, size_t n) {
  char buf[64];
  if (n == 0) return 0;
  if (n >= sizeof buf) n = sizeof buf - 1;
  memcpy(buf, s, n);
  buf[n] = '\0';
  return strtod(buf, NULL);
}

void rolltui_input_check_release(RolltuiInputCheck* c) {
  rolltui_str_free(&c->reason);
  rolltui_str_free(&c->canonical);
}

static void check_begin(RolltuiInputCheck* c) {
  c->prefix_ok = 0;
  c->valid = 0;
  rolltui_str_clear(&c->reason);
  rolltui_str_clear(&c->canonical);
}

/* ---- the hint --------------------------------------------------------------------------------- */

static void hint_bound(const RolltuiInputSpec* spec, double v, int is_min, RolltuiStr* out) {
  char buf[64];
  if ((is_min && v <= -1e15) || (!is_min && v >= 1e15)) {
    str_add(out, "any");
    return;
  }
  num_text(v, spec->type == ROLLTUI_INPUT_TYPE_INT ? 0 : (spec->precision >= 0 ? spec->precision : 1), buf,
           sizeof buf);
  str_add(out, buf);
}

void rolltui_input_hint(const RolltuiInputSpec* spec, RolltuiStr* out) {
  rolltui_str_clear(out);
  if (spec->hint.n) {
    rolltui_str_set(out, spec->hint.p, spec->hint.n);
    return;
  }
  switch (spec->type) {
    case ROLLTUI_INPUT_TYPE_INT:
      hint_bound(spec, spec->min, 1, out);
      str_add(out, "..");
      hint_bound(spec, spec->max, 0, out);
      return;
    case ROLLTUI_INPUT_TYPE_FLOAT:
      hint_bound(spec, spec->min, 1, out);
      str_add(out, "..");
      hint_bound(spec, spec->max, 0, out);
      if (spec->precision >= 0) {
        char buf[32];
        snprintf(buf, sizeof buf, " (%d digits)", spec->precision);
        str_add(out, buf);
      }
      return;
    case ROLLTUI_INPUT_TYPE_COLOR: str_add(out, "#rrggbb | 0-255 | none"); return;
    case ROLLTUI_INPUT_TYPE_SIZE: str_add(out, "fill | fill N | N% | N% \xC2\xB1 cells | cells"); return;
    case ROLLTUI_INPUT_TYPE_DIM: str_add(out, "cells | N% | N% \xC2\xB1 cells"); return;
    case ROLLTUI_INPUT_TYPE_NAME: str_add(out, "a name: letters, digits, - _ ."); return;
    default:
      if (spec->max_len) {
        char buf[48];
        snprintf(buf, sizeof buf, "up to %zu characters", spec->max_len);
        str_add(out, buf);
      }
      return;
  }
}

static const char* const kTypeNames[ROLLTUI_INPUT_TYPE_COUNT] = {"text", "int",  "float", "color",
                                                                "size", "dim",  "name"};

const char* rolltui_input_type_name(unsigned char type, size_t* len) {
  const char* s = type < ROLLTUI_INPUT_TYPE_COUNT ? kTypeNames[type] : "";
  if (len) *len = strlen(s);
  return s;
}

int rolltui_input_type_from_name(const char* name, size_t len, unsigned char* out) {
  unsigned char i;
  for (i = 0; i < ROLLTUI_INPUT_TYPE_COUNT; ++i)
    if (strlen(kTypeNames[i]) == len && memcmp(kTypeNames[i], name, len) == 0) {
      *out = i;
      return 1;
    }
  return 0;
}

/* ---- the seven checks --------------------------------------------------------------------------- */

/* Every check builds its reason out of the hint, so the hint is computed once here and lent
 * to whichever branch needs it. */
static void empty_rule(const RolltuiInputSpec* spec, const RolltuiStr* range, const char* noun,
                       RolltuiInputCheck* c) {
  c->valid = spec->optional;
  if (!spec->optional) {
    str_add(&c->reason, noun);
    if (range->n) {
      str_add(&c->reason, " (");
      rolltui_str_append_str(&c->reason, range);
      str_add(&c->reason, ")");
    }
  }
}

static void check_int(const RolltuiInputSpec* spec, const char* text, size_t n, RolltuiInputCheck* c) {
  RolltuiStr range;
  NumParts p;
  double v, signed_v;
  char buf[64];
  memset(&range, 0, sizeof range);
  rolltui_input_hint(spec, &range);
  if (!split_number(text, n, &p) || p.dot) {
    str_add(&c->reason, "only digits");
    if (spec->min < 0) str_add(&c->reason, " and a leading '-'");
    str_add(&c->reason, " (");
    rolltui_str_append_str(&c->reason, &range);
    str_add(&c->reason, ")");
    goto done;
  }
  if (p.neg && spec->min >= 0) {
    str_add(&c->reason, "no negatives (");
    rolltui_str_append_str(&c->reason, &range);
    str_add(&c->reason, ")");
    goto done;
  }
  v = p.ip_n ? span_to_double(p.ip, p.ip_n) : 0;
  if (p.ip_n && !int_reachable(v, p.neg, spec->min, spec->max)) {
    str_add(&c->reason, "nothing starting with '");
    rolltui_str_append(&c->reason, text, n);
    str_add(&c->reason, "' fits ");
    rolltui_str_append_str(&c->reason, &range);
    goto done;
  }
  if (p.ip_n == 0 && p.neg && !int_reachable(0, 1, spec->min, spec->max) && !(spec->min < 0)) {
    str_add(&c->reason, "no negatives (");
    rolltui_str_append_str(&c->reason, &range);
    str_add(&c->reason, ")");
    goto done;
  }
  c->prefix_ok = 1;
  if (n == 0) {
    empty_rule(spec, &range, "a value is needed", c);
    goto done;
  }
  if (p.ip_n == 0) {
    str_add(&c->reason, "a whole number (");
    rolltui_str_append_str(&c->reason, &range);
    str_add(&c->reason, ")");
    goto done;
  }
  signed_v = p.neg ? -v : v;
  if (signed_v < spec->min || signed_v > spec->max) {
    str_add(&c->reason, "a whole number ");
    rolltui_str_append_str(&c->reason, &range);
    goto done;
  }
  c->valid = 1;
  num_text(signed_v, 0, buf, sizeof buf);
  rolltui_str_set(&c->canonical, buf, strlen(buf));
done:
  rolltui_str_free(&range);
}

static void check_float(const RolltuiInputSpec* spec, const char* text, size_t n, RolltuiInputCheck* c) {
  RolltuiStr range;
  NumParts p;
  int reachable;
  double v;
  char buf[64];
  memset(&range, 0, sizeof range);
  rolltui_input_hint(spec, &range);
  if (!split_number(text, n, &p)) {
    str_add(&c->reason, "only digits, one '.'");
    if (spec->min < 0) str_add(&c->reason, " and a leading '-'");
    str_add(&c->reason, " (");
    rolltui_str_append_str(&c->reason, &range);
    str_add(&c->reason, ")");
    goto done;
  }
  if (p.neg && spec->min >= 0) {
    str_add(&c->reason, "no negatives (");
    rolltui_str_append_str(&c->reason, &range);
    str_add(&c->reason, ")");
    goto done;
  }
  if (spec->precision >= 0 && (int)p.fp_n > spec->precision) {
    snprintf(buf, sizeof buf, "at most %d digits after the point", spec->precision);
    str_add(&c->reason, buf);
    goto done;
  }
  {
    const double ip = p.ip_n ? span_to_double(p.ip, p.ip_n) : 0;
    /* Reachable values: without a point, any integer continuation plus a fraction; with a
     * point and d fraction digits, [v, v + 10^-d). */
    if (!p.dot) {
      const double lo = ip, hi = ip + 1;
      const double a = p.neg ? -hi : lo, b = p.neg ? -lo : hi;
      reachable = p.ip_n == 0 ? (p.neg ? spec->min < 0 : 1)
                              : (int_reachable(ip, p.neg, spec->min, spec->max) ||
                                 (b >= spec->min && a <= spec->max));
    } else {
      char num[80];
      const size_t in = p.ip_n ? p.ip_n : 1;
      const size_t fn = p.fp_n ? p.fp_n : 1;
      double lo, hi, a, b, width;
      if (in + fn + 2 < sizeof num) {
        memcpy(num, p.ip_n ? p.ip : "0", in);
        num[in] = '.';
        memcpy(num + in + 1, p.fp_n ? p.fp : "0", fn);
        num[in + 1 + fn] = '\0';
      } else {
        num[0] = '0';
        num[1] = '\0';
      }
      lo = strtod(num, NULL);
      width = pow(10.0, -(double)p.fp_n);
      hi = lo + width;
      a = p.neg ? -hi : lo;
      b = p.neg ? -lo : hi;
      reachable = b >= spec->min && a <= spec->max;
    }
  }
  if (!reachable) {
    str_add(&c->reason, "nothing starting with '");
    rolltui_str_append(&c->reason, text, n);
    str_add(&c->reason, "' fits ");
    rolltui_str_append_str(&c->reason, &range);
    goto done;
  }
  c->prefix_ok = 1;
  if (n == 0) {
    empty_rule(spec, &range, "a value is needed", c);
    goto done;
  }
  if (p.ip_n == 0 && p.fp_n == 0) {
    str_add(&c->reason, "a number (");
    rolltui_str_append_str(&c->reason, &range);
    str_add(&c->reason, ")");
    goto done;
  }
  v = span_to_double(text, n);
  if (v < spec->min || v > spec->max) {
    str_add(&c->reason, "a number ");
    rolltui_str_append_str(&c->reason, &range);
    goto done;
  }
  c->valid = 1;
  num_text(v, spec->precision, buf, sizeof buf);
  rolltui_str_set(&c->canonical, buf, strlen(buf));
done:
  rolltui_str_free(&range);
}

static int is_prefix_ci(const char* text, size_t n, const char* word) {
  size_t i;
  if (n > strlen(word)) return 0;
  for (i = 0; i < n; ++i)
    if (tolower((unsigned char)text[i]) != word[i]) return 0;
  return 1;
}

static void check_color(const RolltuiInputSpec* spec, const char* text, size_t n, RolltuiInputCheck* c) {
  static const char* kHint = "#rrggbb | 0-255 | none";
  int prefix = n == 0 || is_prefix_ci(text, n, "none");
  RolltuiStyleColor col;
  if (!prefix && text[0] == '#') {
    size_t i;
    prefix = n <= 7;
    for (i = 1; prefix && i < n; ++i) prefix = isxdigit((unsigned char)text[i]) != 0;
  } else if (!prefix && all_digits(text, n)) {
    prefix = n <= 3 && int_reachable(span_to_double(text, n), 0, 0, 255);
  }
  if (!prefix) {
    str_add(&c->reason, "not the start of a colour (");
    str_add(&c->reason, kHint);
    str_add(&c->reason, ")");
    return;
  }
  c->prefix_ok = 1;
  if (n == 0) {
    c->valid = spec->optional;
    if (!spec->optional) {
      str_add(&c->reason, "a colour is needed (");
      str_add(&c->reason, kHint);
      str_add(&c->reason, ")");
    }
    return;
  }
  {
    /* Lower-cased into a small buffer: a colour is at most seven characters and this is the
     * one place that needs the fold. */
    char lower[16];
    size_t i;
    const size_t ln = zmin(n, sizeof lower - 1);
    for (i = 0; i < ln; ++i) lower[i] = (char)tolower((unsigned char)text[i]);
    lower[ln] = '\0';
    if (ln == n && rolltui_color_parse(lower, ln, &col)) {
      char out[ROLLTUI_COLOR_STRING_MAX];
      const size_t cn = rolltui_color_to_string(col, out, sizeof out);
      c->valid = 1;
      rolltui_str_set(&c->canonical, out, cn);
      return;
    }
  }
  str_add(&c->reason, "not a colour yet (");
  str_add(&c->reason, kHint);
  str_add(&c->reason, ")");
}

/* N | N% | N% ± M — the shapes a Dim can be typed in; `size` adds fill / fill N. */
static int dim_prefix(const char* t, size_t n, int size) {
  size_t i = 0;
  if (size && n && t[0] == 'f') {
    if (is_prefix_ci(t, n, "fill")) return 1;
    if (n < 4 || memcmp(t, "fill", 4) != 0) return 0;
    i = 4;
    if (i < n && t[i] != ' ') return 0;
    if (i < n) ++i;
    for (; i < n; ++i)
      if (t[i] < '0' || t[i] > '9') return 0;
    return 1;
  }
  while (i < n && t[i] >= '0' && t[i] <= '9') ++i;
  if (i == n) return 1;
  if (i == 0 || t[i] != '%') return 0;
  ++i;
  if (i == n) return 1;
  if (t[i] == ' ') ++i;
  if (i == n) return 1;
  if (t[i] != '+' && t[i] != '-') return 0;
  ++i;
  if (i < n && t[i] == ' ') ++i;
  for (; i < n; ++i)
    if (t[i] < '0' || t[i] > '9') return 0;
  return 1;
}

static void check_size(const RolltuiInputSpec* spec, const char* text, size_t n, RolltuiInputCheck* c) {
  static const char* kHint = "fill | fill N | N% | N% \xC2\xB1 cells | cells";
  RolltuiSplitSize s;
  if (!dim_prefix(text, n, 1)) {
    str_add(&c->reason, "not the start of a size (");
    str_add(&c->reason, kHint);
    str_add(&c->reason, ")");
    return;
  }
  c->prefix_ok = 1;
  if (n == 0) {
    c->valid = spec->optional;
    if (!spec->optional) {
      str_add(&c->reason, "a size is needed (");
      str_add(&c->reason, kHint);
      str_add(&c->reason, ")");
    }
    return;
  }
  if (rolltui_parse_size_text(text, n, &s)) {
    char out[ROLLTUI_DIM_STRING_MAX];
    const size_t on = rolltui_split_size_to_string(s, out, sizeof out);
    c->valid = 1;
    rolltui_str_set(&c->canonical, out, on);
    return;
  }
  str_add(&c->reason, "not a size yet (");
  str_add(&c->reason, kHint);
  str_add(&c->reason, ")");
}

static void check_dim(const RolltuiInputSpec* spec, const char* text, size_t n, RolltuiInputCheck* c) {
  static const char* kHint = "cells | N% | N% \xC2\xB1 cells";
  RolltuiDim d;
  int have = 0;
  if (!dim_prefix(text, n, 0)) {
    str_add(&c->reason, "not the start of a dim (");
    str_add(&c->reason, kHint);
    str_add(&c->reason, ")");
    return;
  }
  c->prefix_ok = 1;
  if (n == 0) {
    c->valid = spec->optional;
    if (!spec->optional) {
      str_add(&c->reason, "a dim is needed (");
      str_add(&c->reason, kHint);
      str_add(&c->reason, ")");
    }
    return;
  }
  have = rolltui_parse_dim(text, n, &d);
  if (!have && all_digits(text, n)) {
    d.fraction = 0;
    d.cells = (int)span_to_double(text, n);
    have = 1;
  }
  if (have) {
    char out[ROLLTUI_DIM_STRING_MAX];
    const size_t on = rolltui_dim_to_string(d, out, sizeof out);
    c->valid = 1;
    rolltui_str_set(&c->canonical, out, on);
    return;
  }
  str_add(&c->reason, "not a dim yet (");
  str_add(&c->reason, kHint);
  str_add(&c->reason, ")");
}

static void check_name(const RolltuiInputSpec* spec, const char* text, size_t n, RolltuiInputCheck* c) {
  static const char* kHint = "letters, digits, - _ . (no leading dot)";
  const size_t cap = spec->max_len ? spec->max_len : 64;
  size_t i;
  char buf[64];
  if (n > cap) {
    snprintf(buf, sizeof buf, "at most %zu characters", cap);
    str_add(&c->reason, buf);
    return;
  }
  if (n && text[0] == '.') {
    str_add(&c->reason, "a name cannot start with a dot (");
    str_add(&c->reason, kHint);
    str_add(&c->reason, ")");
    return;
  }
  for (i = 0; i < n; ++i) {
    const unsigned char ch = (unsigned char)text[i];
    if (!(isalnum(ch) || ch == '-' || ch == '_' || ch == '.')) {
      str_add(&c->reason, "only ");
      str_add(&c->reason, kHint);
      return;
    }
  }
  c->prefix_ok = 1;
  if (n == 0) {
    c->valid = spec->optional;
    if (!spec->optional) str_add(&c->reason, "a name is needed");
    return;
  }
  c->valid = 1;
  rolltui_str_set(&c->canonical, text, n);
}

static void check_text(const RolltuiInputSpec* spec, const char* text, size_t n, RolltuiUnicodeScratch* u,
                       RolltuiInputCheck* c) {
  size_t len = 0;
  char buf[64];
  if (spec->max_len || spec->min_len) {
    /* The grapheme COUNT, which is the only thing here that needs the cluster walk — hence
     * the scratch parameter the header explains. */
    RolltuiUnicodeGrapheme* g = (RolltuiUnicodeGrapheme*)rolltui_mem_alloc((n ? n : 1) * sizeof *g);
    len = rolltui_u_graphemes(u, text, n, 0, g);
    rolltui_mem_free(g);
  }
  if (spec->max_len && len > spec->max_len) {
    snprintf(buf, sizeof buf, "at most %zu characters", spec->max_len);
    str_add(&c->reason, buf);
    return;
  }
  c->prefix_ok = 1;
  /* The same empty rule as every other type. `min_len` is a length rule, never the
   * emptiness rule (Menu.hpp, and the Phase 10 m5 defect behind it). */
  if (n == 0) {
    c->valid = spec->optional;
    if (!spec->optional) str_add(&c->reason, "a value is needed");
    return;
  }
  if (spec->min_len && len < spec->min_len) {
    snprintf(buf, sizeof buf, "at least %zu characters", spec->min_len);
    str_add(&c->reason, buf);
    return;
  }
  c->valid = 1;
  rolltui_str_set(&c->canonical, text, n);
}

void rolltui_check_input(const RolltuiInputSpec* spec, const char* text, size_t len,
                         RolltuiUnicodeScratch* u, RolltuiInputCheck* out) {
  check_begin(out);
  switch (spec->type) {
    case ROLLTUI_INPUT_TYPE_INT: check_int(spec, text, len, out); return;
    case ROLLTUI_INPUT_TYPE_FLOAT: check_float(spec, text, len, out); return;
    case ROLLTUI_INPUT_TYPE_COLOR: check_color(spec, text, len, out); return;
    case ROLLTUI_INPUT_TYPE_SIZE: check_size(spec, text, len, out); return;
    case ROLLTUI_INPUT_TYPE_DIM: check_dim(spec, text, len, out); return;
    case ROLLTUI_INPUT_TYPE_NAME: check_name(spec, text, len, out); return;
    default: check_text(spec, text, len, u, out); return;
  }
}

/* ---- the widget ----------------------------------------------------------------------------------- */

void rolltui_menu_event_release(RolltuiMenuEvent* e) {
  rolltui_str_free(&e->id);
  rolltui_str_free(&e->value);
}

/* One entry of the flattened palette list. */
typedef struct FlatEntry {
  size_t* path; /* OWNED */
  size_t path_n, path_cap;
  RolltuiStr label;
} FlatEntry;

struct RolltuiMenu {
  RolltuiMenuItem root;
  size_t* path; /* the level, as indices from the root */
  size_t path_n, path_cap;
  size_t sel;
  RolltuiStr filter;
  int editing;
  RolltuiInput* edit; /* BORROWED: `rolltui::Menu` owns it */
  RolltuiStr edit_reason;
  int palette;
  FlatEntry* flat;
  size_t flat_n, flat_cap;
  RolltuiMenuOptions opt;
  RolltuiRect area;
  int top;

  /* the visible list, rebuilt on demand and LENT to a caller */
  size_t* vis;
  size_t vis_n, vis_cap;

  RolltuiStr probe;         /* what `try_insert` judges a keystroke in */
  RolltuiValidatorFn vfn;
  void* vctx;
  RolltuiUnicodeScratch* u; /* WORKING MEMORY, one role: the checks' cluster walk */
};

/* ---- paths and levels -------------------------------------------------------------------------------- */

static void path_push(RolltuiMenu* m, size_t i) {
  m->path = (size_t*)rolltui_grow(m->path, &m->path_cap, m->path_n + 1, sizeof *m->path);
  m->path[m->path_n++] = i;
}

static RolltuiMenuItem* by_path(RolltuiMenu* m, const size_t* p, size_t n) {
  RolltuiMenuItem* it = &m->root;
  size_t k;
  for (k = 0; k < n; ++k) {
    if (p[k] >= it->children.n) return NULL;
    it = it->children.v[p[k]];
  }
  return it;
}

static RolltuiMenuItem* level_mut(RolltuiMenu* m) {
  RolltuiMenuItem* it = by_path(m, m->path, m->path_n);
  return it ? it : &m->root;
}

const RolltuiMenuItem* rolltui_menu_level(const RolltuiMenu* m) { return level_mut((RolltuiMenu*)m); }

size_t rolltui_menu_path(const RolltuiMenu* m, const size_t** out) {
  if (out) *out = m->path;
  return m->path_n;
}

/* ---- the flat list ------------------------------------------------------------------------------------ */

/* EMPTIES, KEEPING EVERY BUFFER — and the first cut of this FREED them while leaving the
 * pointers in place, so the next `flat_add` reused a slot holding a dangling `path` and
 * handed it to `rolltui_fit`. All fifty tests passed in the C++ configuration and the C one
 * died inside malloc with no output; ASan named it a double free with both stacks on the
 * first run. It is the same shape CLAUDE.md's design lens keeps finding — a `clear()` that
 * throws away exactly the storage being reused — with the C's twist that a freed pointer
 * left in a struct is a real defect rather than a wasted allocation. */
static void flat_clear(RolltuiMenu* m) {
  size_t i;
  for (i = 0; i < m->flat_n; ++i) {
    m->flat[i].path_n = 0;
    rolltui_str_clear(&m->flat[i].label);
  }
  m->flat_n = 0;
}

/* …and this is the one that actually hands the buffers back. */
static void flat_release(RolltuiMenu* m) {
  size_t i;
  for (i = 0; i < m->flat_cap; ++i) {
    rolltui_mem_free(m->flat[i].path);
    rolltui_str_free(&m->flat[i].label);
  }
  rolltui_mem_free(m->flat);
  m->flat = NULL;
  m->flat_n = 0;
  m->flat_cap = 0;
}

static FlatEntry* flat_add(RolltuiMenu* m, const size_t* path, size_t n) {
  FlatEntry* e;
  m->flat = (FlatEntry*)rolltui_grow_zeroed(m->flat, &m->flat_cap, m->flat_n + 1, sizeof *m->flat);
  e = &m->flat[m->flat_n++];
  e->path = (size_t*)rolltui_fit(e->path, &e->path_cap, n ? n : 1, sizeof *e->path);
  memcpy(e->path, path, n * sizeof *path);
  e->path_n = n;
  return e;
}

/* The recursive walk, with the index path and the label prefix carried explicitly — the C++
 * did it with a capturing lambda over a `std::vector` and a `std::string`. */
static void flat_walk(RolltuiMenu* m, const RolltuiMenuItem* it, const RolltuiStr* prefix, size_t* p,
                      size_t* p_n, size_t p_cap) {
  size_t i;
  for (i = 0; i < it->children.n; ++i) {
    const RolltuiMenuItem* c = it->children.v[i];
    RolltuiStr label;
    memset(&label, 0, sizeof label);
    if (*p_n < p_cap) p[(*p_n)++] = i;
    if (prefix->n) {
      rolltui_str_append_str(&label, prefix);
      str_add(&label, CRUMB);
    }
    rolltui_str_append_str(&label, &c->label);
    if (c->kind == ROLLTUI_MENU_SUBMENU) {
      flat_walk(m, c, &label, p, p_n, p_cap);
    } else if (c->kind == ROLLTUI_MENU_CHOICE) {
      size_t j;
      for (j = 0; j < c->children.n; ++j) {
        FlatEntry* e;
        if (*p_n < p_cap) p[(*p_n)++] = j;
        e = flat_add(m, p, *p_n);
        rolltui_str_set(&e->label, label.p, label.n);
        str_add(&e->label, CRUMB);
        rolltui_str_append_str(&e->label, &c->children.v[j]->label);
        if (*p_n) --(*p_n);
      }
    } else {
      FlatEntry* e = flat_add(m, p, *p_n);
      rolltui_str_set(&e->label, label.p, label.n);
    }
    rolltui_str_free(&label);
    if (*p_n) --(*p_n);
  }
}

/* How deep the tree goes, so the walk's index path can be one exact allocation. */
static size_t tree_depth(const RolltuiMenuItem* it) {
  size_t best = 0, i;
  for (i = 0; i < it->children.n; ++i) {
    const size_t d = tree_depth(it->children.v[i]);
    if (d > best) best = d;
  }
  return best + 1;
}

static void rebuild_flat(RolltuiMenu* m) {
  const size_t depth = tree_depth(&m->root) + 1;
  size_t* p = (size_t*)rolltui_mem_alloc(depth * sizeof *p);
  size_t p_n = 0;
  RolltuiStr empty;
  memset(&empty, 0, sizeof empty);
  flat_clear(m);
  flat_walk(m, &m->root, &empty, p, &p_n, depth);
  rolltui_mem_free(p);
}

size_t rolltui_menu_flat_count(const RolltuiMenu* m) { return m->flat_n; }

const char* rolltui_menu_flat_label(const RolltuiMenu* m, size_t i, size_t* len) {
  if (i >= m->flat_n) {
    if (len) *len = 0;
    return "";
  }
  return rolltui_str_get(&m->flat[i].label, len);
}

size_t rolltui_menu_flat_path(const RolltuiMenu* m, size_t i, const size_t** out) {
  if (i >= m->flat_n) {
    if (out) *out = NULL;
    return 0;
  }
  if (out) *out = m->flat[i].path;
  return m->flat[i].path_n;
}

/* ---- the visible list ------------------------------------------------------------------------------- */

static int contains_ci(const char* hay, size_t hn, const char* needle, size_t nn) {
  size_t i, j;
  if (nn == 0) return 1;
  if (nn > hn) return 0;
  for (i = 0; i + nn <= hn; ++i) {
    for (j = 0; j < nn; ++j)
      if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j])) break;
    if (j == nn) return 1;
  }
  return 0;
}

static void vis_push(RolltuiMenu* m, size_t i) {
  m->vis = (size_t*)rolltui_grow(m->vis, &m->vis_cap, m->vis_n + 1, sizeof *m->vis);
  m->vis[m->vis_n++] = i;
}

/* THE VISIBLE LIST IS A BUFFER THIS MENU OWNS AND LENDS. The C++ returned a fresh
 * `std::vector<std::size_t>` from a `const` accessor called four times per key press and
 * twice per drawn row — which is the shape CLAUDE.md's per-frame-API rule names. */
static size_t build_visible(RolltuiMenu* m) {
  size_t i;
  m->vis_n = 0;
  if (m->palette) {
    for (i = 0; i < m->flat_n; ++i)
      if (contains_ci(m->flat[i].label.p, m->flat[i].label.n, m->filter.p, m->filter.n)) vis_push(m, i);
    return m->vis_n;
  }
  {
    const RolltuiMenuItem* lv = rolltui_menu_level(m);
    for (i = 0; i < lv->children.n; ++i)
      if (contains_ci(lv->children.v[i]->label.p, lv->children.v[i]->label.n, m->filter.p, m->filter.n))
        vis_push(m, i);
  }
  return m->vis_n;
}

size_t rolltui_menu_visible(const RolltuiMenu* m, const size_t** out) {
  RolltuiMenu* mm = (RolltuiMenu*)m;
  const size_t n = build_visible(mm);
  if (out) *out = mm->vis;
  return n;
}

static RolltuiMenuItem* item_at(RolltuiMenu* m, size_t vis_index) {
  const size_t n = build_visible(m);
  if (vis_index >= n) return NULL;
  if (m->palette) {
    const FlatEntry* e = &m->flat[m->vis[vis_index]];
    return by_path(m, e->path, e->path_n);
  }
  return level_mut(m)->children.v[m->vis[vis_index]];
}

const RolltuiMenuItem* rolltui_menu_selected_item(const RolltuiMenu* m) {
  return item_at((RolltuiMenu*)m, m->sel);
}

size_t rolltui_menu_selected(const RolltuiMenu* m) { return m->sel; }

static int item_rows(const RolltuiMenu* m) { return m->area.h >= 2 ? m->area.h - 1 : m->area.h; }

static void ensure_visible(RolltuiMenu* m) {
  const int rows = item_rows(m);
  int n;
  if (rows <= 0) {
    m->top = 0;
    return;
  }
  if ((int)m->sel < m->top) m->top = (int)m->sel;
  if ((int)m->sel >= m->top + rows) m->top = (int)m->sel - rows + 1;
  n = (int)build_visible(m);
  m->top = iclamp(m->top, 0, imax(0, n - rows));
}

static void clamp_selection(RolltuiMenu* m) {
  const size_t n = build_visible(m);
  if (n == 0) m->sel = 0;
  else if (m->sel >= n) m->sel = n - 1;
  ensure_visible(m);
}

int rolltui_menu_scroll_first(const RolltuiMenu* m) { return m->top < 0 ? 0 : m->top; }
int rolltui_menu_scroll_visible(const RolltuiMenu* m) {
  const int r = item_rows(m);
  return r < 0 ? 0 : r;
}

const char* rolltui_menu_filter(const RolltuiMenu* m, size_t* len) { return rolltui_str_get(&m->filter, len); }
int rolltui_menu_editing(const RolltuiMenu* m) { return m->editing; }
const char* rolltui_menu_edit_reason(const RolltuiMenu* m, size_t* len) {
  return rolltui_str_get(&m->edit_reason, len);
}
int rolltui_menu_palette(const RolltuiMenu* m) { return m->palette; }

void rolltui_menu_breadcrumb(const RolltuiMenu* m, RolltuiStr* out) {
  const RolltuiMenuItem* it = &m->root;
  size_t k;
  rolltui_str_clear(out);
  if (m->palette) {
    if (m->root.label.n == 0) {
      str_add(out, "\xE2\x80\xBA");
      return;
    }
    rolltui_str_append_str(out, &m->root.label);
    str_add(out, CRUMB);
    str_add(out, ELLIPSIS);
    return;
  }
  rolltui_str_append_str(out, &m->root.label);
  for (k = 0; k < m->path_n; ++k) {
    if (m->path[k] >= it->children.n) break;
    it = it->children.v[m->path[k]];
    if (out->n) str_add(out, CRUMB);
    rolltui_str_append_str(out, &it->label);
  }
}

/* ---- lifetime ------------------------------------------------------------------------------------------ */

RolltuiMenu* rolltui_menu_new(void) {
  RolltuiMenu* m = (RolltuiMenu*)rolltui_mem_alloc(sizeof *m);
  memset(m, 0, sizeof *m);
  rolltui_menu_item_init(&m->root);
  m->root.kind = ROLLTUI_MENU_SUBMENU;
  /* OWNED (Phase 17). This used to be a borrowed parameter, and BOTH callers in the tree
   * created an input for it and passed it — the two-consumers-one-wrapper tell. A menu with no
   * editor cannot edit a typed field, so there was never a menu that wanted a different one. */
  m->edit = rolltui_input_new();
  m->u = rolltui_u_scratch_new();
  return m;
}

void rolltui_menu_free(RolltuiMenu* m) {
  if (!m) return;
  rolltui_menu_item_release(&m->root);
  rolltui_mem_free(m->path);
  rolltui_str_free(&m->filter);
  rolltui_str_free(&m->edit_reason);
  rolltui_str_free(&m->probe);
  flat_release(m);
  rolltui_mem_free(m->vis);
  rolltui_u_scratch_free(m->u);
  rolltui_input_free(m->edit); /* OWNED since Phase 17 */
  rolltui_mem_free(m);
}

void rolltui_menu_reset(RolltuiMenu* m) {
  m->path_n = 0;
  m->sel = 0;
  m->top = 0;
  rolltui_str_clear(&m->filter);
  m->editing = 0;
  rolltui_str_clear(&m->edit_reason);
  m->palette = 0;
  rebuild_flat(m);
}

void rolltui_menu_set_root(RolltuiMenu* m, const RolltuiMenuItem* root) {
  rolltui_menu_item_copy(&m->root, root);
  m->root.kind = ROLLTUI_MENU_SUBMENU;
  rolltui_menu_reset(m);
}

RolltuiMenuItem* rolltui_menu_root(RolltuiMenu* m) { return &m->root; }

static RolltuiMenuItem* find_in(RolltuiMenuItem* it, const char* id, size_t len) {
  size_t i;
  if (rolltui_str_eq(&it->id, id, len)) return it;
  for (i = 0; i < it->children.n; ++i) {
    RolltuiMenuItem* f = find_in(it->children.v[i], id, len);
    if (f) return f;
  }
  return NULL;
}

RolltuiMenuItem* rolltui_menu_find(RolltuiMenu* m, const char* id, size_t len) {
  return find_in(&m->root, id, len);
}

int rolltui_menu_set_options(RolltuiMenu* m, const char* id, size_t len, const RolltuiMenuItemList* options) {
  RolltuiMenuItem* it = rolltui_menu_find(m, id, len);
  if (!it) return 0;
  rolltui_menu_list_copy(&it->children, options);
  rebuild_flat(m);
  clamp_selection(m);
  return 1;
}

void rolltui_menu_set_palette(RolltuiMenu* m, int on) {
  if (m->palette == (on != 0)) return;
  m->palette = on != 0;
  m->path_n = 0;
  m->sel = 0;
  m->top = 0;
  rolltui_str_clear(&m->filter);
  m->editing = 0;
  rebuild_flat(m);
}

void rolltui_menu_set_validator_fn(RolltuiMenu* m, RolltuiValidatorFn fn, void* ctx) {
  m->vfn = fn;
  m->vctx = ctx;
}

/* ---- navigation ---------------------------------------------------------------------------------------- */

static void descend(RolltuiMenu* m, size_t child) {
  const RolltuiMenuItem* lv;
  size_t i;
  path_push(m, child);
  rolltui_str_clear(&m->filter);
  m->sel = 0;
  m->top = 0;
  lv = rolltui_menu_level(m);
  if (lv->kind == ROLLTUI_MENU_CHOICE)
    for (i = 0; i < lv->children.n; ++i)
      if (rolltui_str_eq(&lv->children.v[i]->id, lv->value.p, lv->value.n)) {
        m->sel = i;
        break;
      }
  ensure_visible(m);
}

static int ascend(RolltuiMenu* m) {
  size_t was;
  const RolltuiMenuItem* lv;
  if (m->path_n == 0) return 0;
  was = m->path[--m->path_n];
  rolltui_str_clear(&m->filter);
  lv = rolltui_menu_level(m);
  m->sel = lv->children.n == 0 ? 0 : zmin(was, lv->children.n - 1);
  m->top = 0;
  ensure_visible(m);
  return 1;
}

/* ---- editing a typed field ------------------------------------------------------------------------------ */

static void refresh_reason(RolltuiMenu* m) {
  RolltuiMenuItem* it = item_at(m, m->sel);
  RolltuiInputCheck c;
  size_t n = 0;
  const char* t;
  if (!it) return;
  memset(&c, 0, sizeof c);
  t = rolltui_input_text(m->edit, &n);
  rolltui_check_input(&it->spec, t, n, m->u, &c);
  if (c.valid) rolltui_str_clear(&m->edit_reason);
  else rolltui_str_set(&m->edit_reason, c.reason.p, c.reason.n);
  rolltui_input_check_release(&c);
}

static void begin_edit(RolltuiMenu* m, RolltuiMenuItem* it) {
  m->editing = 1;
  rolltui_str_clear(&m->edit_reason);
  rolltui_input_set_text(m->edit, it->value.p, it->value.n);
  rolltui_input_select_all(m->edit); /* typing replaces; a first arrow key places the caret */
}

static int try_insert(RolltuiMenu* m, const char* text, size_t len) {
  RolltuiMenuItem* it = item_at(m, m->sel);
  RolltuiInputCheck c;
  if (!it) return 0;
  memset(&c, 0, sizeof c);
  /* What the text would be after the insertion, checked as a prefix of some valid value
   * before it lands. Never a coercion: refused or inserted. */
  rolltui_input_preview_insert(m->edit, text, len, &m->probe);
  rolltui_check_input(&it->spec, m->probe.p, m->probe.n, m->u, &c);
  if (!c.prefix_ok) {
    rolltui_str_set(&m->edit_reason, c.reason.p, c.reason.n);
    rolltui_input_check_release(&c);
    return 0;
  }
  rolltui_input_check_release(&c);
  rolltui_input_insert(m->edit, text, len);
  refresh_reason(m);
  return 1;
}

static void step(RolltuiMenu* m, int direction) {
  RolltuiMenuItem* it = item_at(m, m->sel);
  RolltuiInputCheck now;
  size_t tn = 0;
  const char* t;
  double v;
  char buf[64];
  int prec;
  if (!it || (it->spec.type != ROLLTUI_INPUT_TYPE_INT && it->spec.type != ROLLTUI_INPUT_TYPE_FLOAT)) return;
  prec = it->spec.type == ROLLTUI_INPUT_TYPE_INT ? 0 : it->spec.precision;
  memset(&now, 0, sizeof now);
  t = rolltui_input_text(m->edit, &tn);
  rolltui_check_input(&it->spec, t, tn, m->u, &now);
  if (now.valid && tn) {
    v = span_to_double(t, tn);
  } else {
    RolltuiInputCheck committed;
    memset(&committed, 0, sizeof committed);
    rolltui_check_input(&it->spec, it->value.p, it->value.n, m->u, &committed);
    v = (committed.valid && it->value.n) ? span_to_double(it->value.p, it->value.n)
                                         : (it->spec.min > -1e15 ? it->spec.min : 0);
    rolltui_input_check_release(&committed);
    num_text(v, prec, buf, sizeof buf);
    rolltui_input_set_text(m->edit, buf, strlen(buf));
    refresh_reason(m);
    rolltui_input_check_release(&now);
    return;
  }
  rolltui_input_check_release(&now);
  v = v + direction * it->spec.step;
  if (v < it->spec.min) v = it->spec.min;
  if (v > it->spec.max) v = it->spec.max;
  num_text(v, prec, buf, sizeof buf);
  rolltui_input_set_text(m->edit, buf, strlen(buf));
  refresh_reason(m);
}

static int action_is(const char* a, size_t n, const char* name) {
  return name && strlen(name) == n && memcmp(a, name, n) == 0;
}

static void handle_edit(RolltuiMenu* m, const RolltuiEvent* e, const RolltuiBindings* b,
                        const RolltuiMenuActions* A, RolltuiMenuEvent* out) {
  RolltuiMenuItem* it = item_at(m, m->sel);
  const char* a;
  size_t alen = 0;
  if (!it) {
    m->editing = 0;
    return;
  }
  if (e->kind == ROLLTUI_EVENT_PASTE) {
    try_insert(m, e->text, e->text_len);
    return;
  }
  if (e->kind != ROLLTUI_EVENT_KEY) return;
  a = rolltui_bindings_action_for(b, &e->key, "edit", 4, &alen);
  if (a && action_is(a, alen, A->commit)) {
    RolltuiInputCheck c;
    size_t tn = 0;
    const char* t = rolltui_input_text(m->edit, &tn);
    memset(&c, 0, sizeof c);
    rolltui_check_input(&it->spec, t, tn, m->u, &c);
    if (c.valid && it->spec.type == ROLLTUI_INPUT_TYPE_TEXT && it->spec.validator.n) {
      RolltuiStr why;
      memset(&why, 0, sizeof why);
      if (m->vfn && m->vfn(m->vctx, it->spec.validator.p, it->spec.validator.n, t, tn, &why)) {
        if (why.n) {
          c.valid = 0;
          rolltui_str_set(&c.reason, why.p, why.n);
        }
      } else {
        c.valid = 0;
        rolltui_str_clear(&c.reason);
        str_add(&c.reason, "no validator named '");
        rolltui_str_append_str(&c.reason, &it->spec.validator);
        str_add(&c.reason, "' is registered");
      }
      rolltui_str_free(&why);
    }
    if (!c.valid) {
      rolltui_str_set(&m->edit_reason, c.reason.p, c.reason.n);
      rolltui_input_check_release(&c);
      return;
    }
    rolltui_str_set(&it->value, c.canonical.p, c.canonical.n);
    m->editing = 0;
    rolltui_str_clear(&m->edit_reason);
    out->kind = ROLLTUI_MENU_EVENT_INPUT;
    rolltui_str_set(&out->id, it->id.p, it->id.n);
    rolltui_str_set(&out->value, it->value.p, it->value.n);
    rolltui_input_check_release(&c);
    return;
  }
  if (a && action_is(a, alen, A->cancel)) {
    m->editing = 0;
    rolltui_str_clear(&m->edit_reason);
    return;
  }
  if (a && action_is(a, alen, A->step_up)) {
    step(m, +1);
    return;
  }
  if (a && action_is(a, alen, A->step_down)) {
    step(m, -1);
    return;
  }
  if (e->key.key == ROLLTUI_KEY_CHAR && !e->key.ctrl && !e->key.alt && e->key.ch >= 0x20 &&
      e->key.ch != 0x7F) {
    char buf[4];
    const size_t n = rolltui_u_append_utf8(e->key.ch, buf);
    try_insert(m, buf, n);
    return;
  }
  /* Everything else is the input widget's: the caret, selection, deletions, kills. Its
   * action table travels in `A->input`, so there is one table of those names in the library. */
  {
    const unsigned char did = rolltui_input_handle(m->edit, e, b, A->input, 0);
    if (did == ROLLTUI_INPUT_HANDLED) refresh_reason(m);
  }
}

/* ---- acting -------------------------------------------------------------------------------------------- */

static void act(RolltuiMenu* m, size_t vis_index, RolltuiMenuEvent* out) {
  RolltuiMenuItem* it = item_at(m, vis_index);
  if (!it || !it->enabled) return;
  if (m->palette) {
    const FlatEntry* fe = &m->flat[m->vis[vis_index]];
    if (fe->path_n >= 2) {
      RolltuiMenuItem* p = by_path(m, fe->path, fe->path_n - 1);
      if (p && p->kind == ROLLTUI_MENU_CHOICE) {
        rolltui_str_set(&p->value, it->id.p, it->id.n);
        out->kind = ROLLTUI_MENU_EVENT_CHOOSE;
        rolltui_str_set(&out->id, p->id.p, p->id.n);
        rolltui_str_set(&out->value, it->id.p, it->id.n);
        return;
      }
    }
  } else if (rolltui_menu_level(m)->kind == ROLLTUI_MENU_CHOICE) {
    RolltuiMenuItem* choice = level_mut(m);
    rolltui_str_set(&choice->value, it->id.p, it->id.n);
    out->kind = ROLLTUI_MENU_EVENT_CHOOSE;
    rolltui_str_set(&out->id, choice->id.p, choice->id.n);
    rolltui_str_set(&out->value, it->id.p, it->id.n);
    ascend(m);
    return;
  }
  switch (it->kind) {
    case ROLLTUI_MENU_ACTION:
      out->kind = ROLLTUI_MENU_EVENT_ACTIVATE;
      rolltui_str_set(&out->id, it->id.p, it->id.n);
      return;
    case ROLLTUI_MENU_TOGGLE:
      it->checked = !it->checked;
      out->kind = ROLLTUI_MENU_EVENT_TOGGLE;
      rolltui_str_set(&out->id, it->id.p, it->id.n);
      out->checked = it->checked;
      return;
    case ROLLTUI_MENU_SUBMENU:
    case ROLLTUI_MENU_CHOICE:
      if (m->palette) return;
      build_visible(m);
      descend(m, m->vis[vis_index]);
      return;
    case ROLLTUI_MENU_INPUT:
      m->sel = vis_index;
      begin_edit(m, it);
      return;
    default:
      return;
  }
}

/* ---- keys and the mouse --------------------------------------------------------------------------------- */

static void move_to(RolltuiMenu* m, size_t i, size_t n) {
  if (n == 0) {
    m->sel = 0;
    return;
  }
  m->sel = zmin(i, n - 1);
  ensure_visible(m);
}

static void handle_key(RolltuiMenu* m, const RolltuiChord* k, const RolltuiBindings* b,
                       const RolltuiMenuActions* A, RolltuiMenuEvent* out) {
  const int text = k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt && k->ch >= 0x20 && k->ch != 0x7F;
  const char* a = NULL;
  size_t alen = 0, n;
  if (text) {
    char buf[4];
    const size_t bn = rolltui_u_append_utf8(k->ch, buf);
    rolltui_str_append(&m->filter, buf, bn);
    m->sel = 0;
    m->top = 0;
    ensure_visible(m);
    return;
  }
  a = rolltui_bindings_action_for(b, k, "menu", 4, &alen);
  if (!a) return;
  n = build_visible(m);
  if (action_is(a, alen, A->up)) {
    move_to(m, m->sel == 0 ? 0 : m->sel - 1, n);
    return;
  }
  if (action_is(a, alen, A->down)) {
    move_to(m, m->sel + 1, n);
    return;
  }
  if (action_is(a, alen, A->page_up)) {
    const size_t s = (size_t)imax(item_rows(m), 1);
    move_to(m, m->sel < s ? 0 : m->sel - s, n);
    return;
  }
  if (action_is(a, alen, A->page_down)) {
    move_to(m, m->sel + (size_t)imax(item_rows(m), 1), n);
    return;
  }
  if (action_is(a, alen, A->first)) {
    move_to(m, 0, n);
    return;
  }
  if (action_is(a, alen, A->last)) {
    move_to(m, n == 0 ? 0 : n - 1, n);
    return;
  }
  if (action_is(a, alen, A->activate)) {
    act(m, m->sel, out);
    return;
  }
  if (action_is(a, alen, A->descend)) {
    const RolltuiMenuItem* it = item_at(m, m->sel);
    if (it && it->enabled && !m->palette &&
        (it->kind == ROLLTUI_MENU_SUBMENU || it->kind == ROLLTUI_MENU_CHOICE)) {
      build_visible(m);
      descend(m, m->vis[m->sel]);
    }
    return;
  }
  if (action_is(a, alen, A->ascend)) {
    if (m->filter.n) {
      rolltui_str_clear(&m->filter);
      clamp_selection(m);
      return;
    }
    ascend(m);
    return;
  }
  if (action_is(a, alen, A->back)) {
    if (m->filter.n) {
      rolltui_str_clear(&m->filter);
      clamp_selection(m);
      return;
    }
    if (ascend(m)) return;
    out->kind = ROLLTUI_MENU_EVENT_CLOSED;
    return;
  }
  if (action_is(a, alen, A->erase)) {
    if (m->filter.n) {
      /* One grapheme off the end. The cluster walk needs the array, and the filter is short. */
      RolltuiUnicodeGrapheme* g = (RolltuiUnicodeGrapheme*)rolltui_mem_alloc(m->filter.n * sizeof *g);
      const size_t gn = rolltui_u_graphemes(m->u, m->filter.p, m->filter.n, 0, g);
      if (gn) {
        m->filter.n = g[gn - 1].offset;
        m->filter.p[m->filter.n] = '\0';
      }
      rolltui_mem_free(g);
      clamp_selection(m);
    }
    return;
  }
}

static void handle_mouse(RolltuiMenu* m, const RolltuiMouseEvent* e, RolltuiMenuEvent* out) {
  size_t n, idx;
  int first_item_row;
  if (e->kind == 4 /* WheelUp */) {
    if (m->sel > 0) {
      --m->sel;
      ensure_visible(m);
    }
    return;
  }
  if (e->kind == 5 /* WheelDown */) {
    n = build_visible(m);
    if (n && m->sel + 1 < n) {
      ++m->sel;
      ensure_visible(m);
    }
    return;
  }
  if (e->kind != 0 /* Press */ || e->button != 1) return;
  if (!(e->x >= m->area.x && e->y >= m->area.y && e->x < m->area.x + m->area.w &&
        e->y < m->area.y + m->area.h))
    return;
  first_item_row = m->area.h >= 2 ? m->area.y + 1 : m->area.y;
  if (e->y < first_item_row) return;
  idx = (size_t)m->top + (size_t)(e->y - first_item_row);
  if (idx >= build_visible(m)) return;
  m->sel = idx;
  act(m, m->sel, out);
}

void rolltui_menu_handle(RolltuiMenu* m, const RolltuiEvent* e, const RolltuiBindings* b,
                         const RolltuiMenuActions* A, RolltuiMenuEvent* out) {
  out->kind = ROLLTUI_MENU_EVENT_NONE;
  out->checked = 0;
  rolltui_str_clear(&out->id);
  rolltui_str_clear(&out->value);
  if (m->editing) {
    handle_edit(m, e, b, A, out);
    return;
  }
  if (e->kind == ROLLTUI_EVENT_KEY) {
    handle_key(m, &e->key, b, A, out);
    return;
  }
  if (e->kind == ROLLTUI_EVENT_MOUSE) {
    handle_mouse(m, &e->mouse, out);
    return;
  }
  if (e->kind == ROLLTUI_EVENT_PASTE) {
    /* Pasted text goes where typed text would: the filter. */
    size_t i;
    for (i = 0; i < e->text_len; ++i) {
      const unsigned char c = (unsigned char)e->text[i];
      if (c >= 0x20 && c != 0x7F) {
        RolltuiChord k;
        memset(&k, 0, sizeof k);
        k.key = ROLLTUI_KEY_CHAR;
        k.ch = c;
        handle_key(m, &k, b, A, out);
      }
    }
  }
}

/* ---- layout and drawing ----------------------------------------------------------------------------------- */

void rolltui_menu_set_options_struct(RolltuiMenu* m, const RolltuiMenuOptions* o) { m->opt = *o; }
const RolltuiMenuOptions* rolltui_menu_options(const RolltuiMenu* m) { return &m->opt; }
void rolltui_menu_area(const RolltuiMenu* m, RolltuiRect* out) { *out = m->area; }

void rolltui_menu_layout(RolltuiMenu* m, RolltuiRect area) {
  m->area = area;
  ensure_visible(m);
}

int rolltui_menu_rows_for(const RolltuiMenu* m) {
  const size_t n = build_visible((RolltuiMenu*)m);
  return 1 + imax(1, (int)n);
}

/* The row's text, into a caller's string. */
static void row_text(const RolltuiMenu* m, const RolltuiMenuItem* it, int in_palette, size_t vis_index,
                     RolltuiStr* out) {
  rolltui_str_clear(out);
  if (in_palette) {
    if (it->kind == ROLLTUI_MENU_TOGGLE) str_add(out, it->checked ? "[x] " : "[ ] ");
    rolltui_str_append_str(out, &m->flat[m->vis[vis_index]].label);
    return;
  }
  switch (it->kind) {
    case ROLLTUI_MENU_TOGGLE:
      str_add(out, it->checked ? "[x] " : "[ ] ");
      rolltui_str_append_str(out, &it->label);
      break;
    case ROLLTUI_MENU_INPUT:
      rolltui_str_append_str(out, &it->label);
      str_add(out, ": ");
      rolltui_str_append_str(out, &it->value);
      break;
    default:
      rolltui_str_append_str(out, &it->label);
  }
  {
    const RolltuiMenuItem* lv = rolltui_menu_level(m);
    if (lv->kind == ROLLTUI_MENU_CHOICE && rolltui_str_eq(&it->id, lv->value.p, lv->value.n)) {
      /* • the current option — prepended, so the buffer is built once and shifted once. */
      RolltuiStr t;
      memset(&t, 0, sizeof t);
      str_add(&t, "\xE2\x80\xA2 ");
      rolltui_str_append_str(&t, out);
      rolltui_str_move(out, &t);
    }
  }
}

void rolltui_menu_draw(const RolltuiMenu* m, RolltuiFrame* f, RolltuiDrawScratch* draw,
                       const RolltuiStyle* styles, const RolltuiMenuRoles* roles,
                       const RolltuiInputRoles* input_roles, int focused) {
  RolltuiMenu* mm = (RolltuiMenu*)m;
  const RolltuiRect a = m->area;
  const int aw = m->opt.ambiguous_wide;
  const int x0 = a.x + m->opt.inset, w = a.w - 2 * m->opt.inset;
  size_t vis_n;
  int y = a.y, rows, r;
  RolltuiStr line;
  if (a.w <= 0 || a.h <= 0 || w <= 0) return;
  memset(&line, 0, sizeof line);
  vis_n = build_visible(mm);
  if (a.h >= 2) {
    if (m->editing) {
      /* The breadcrumb yields to the field's guidance: the reason a key or a commit was
       * refused when there is one, else the constraint. */
      const RolltuiMenuItem* it = item_at(mm, m->sel);
      if (m->edit_reason.n) {
        str_add(&line, "\xE2\x9C\x97 ");
        rolltui_str_append_str(&line, &m->edit_reason);
      } else {
        RolltuiStr hint;
        memset(&hint, 0, sizeof hint);
        if (it) rolltui_input_hint(&it->spec, &hint);
        str_add(&line, "editing \xE2\x80\x94 ");
        if (hint.n) rolltui_str_append_str(&line, &hint);
        else str_add(&line, "Enter commits, Esc cancels");
        rolltui_str_free(&hint);
      }
      rolltui_frame_put_text(f, draw, x0, y, line.p, line.n,
                             styles[m->edit_reason.n ? roles->warning : roles->shortcut], w, aw, 0);
    } else {
      int used;
      rolltui_menu_breadcrumb(m, &line);
      used = rolltui_frame_put_text(f, draw, x0, y, line.p, line.n, styles[roles->breadcrumb], w, aw, 0);
      if (m->filter.n) {
        rolltui_str_clear(&line);
        str_add(&line, "  /");
        rolltui_str_append_str(&line, &m->filter);
        rolltui_frame_put_text(f, draw, x0 + used, y, line.p, line.n, styles[roles->shortcut],
                               imax(w - used, 0), aw, 0);
      }
    }
    ++y;
  }
  rows = item_rows(m);
  if (vis_n == 0) {
    if (rows > 0) {
      rolltui_str_clear(&line);
      if (m->filter.n) {
        str_add(&line, "(no match for /");
        rolltui_str_append_str(&line, &m->filter);
        str_add(&line, ")");
      } else {
        str_add(&line, "(empty)");
      }
      rolltui_frame_put_text(f, draw, x0, y, line.p, line.n, styles[roles->text_muted], w, aw, 0);
    }
    rolltui_str_free(&line);
    return;
  }
  for (r = 0; r < rows; ++r) {
    const size_t i = (size_t)(m->top + r);
    const RolltuiMenuItem* it;
    int is_sel;
    RolltuiStyle base;
    RolltuiRect row_rect;
    if (i >= vis_n) break;
    it = item_at(mm, i);
    if (!it) break;
    is_sel = i == m->sel;
    base = styles[is_sel ? roles->selected : (it->enabled ? roles->item : roles->text_muted)];
    row_rect.x = x0;
    row_rect.y = y + r;
    row_rect.w = w;
    row_rect.h = 1;
    rolltui_frame_fill(f, draw, row_rect, base, NULL, 0);
    if (is_sel && m->editing) {
      /* The field: its label, then the input widget's own drawing (caret, selection). */
      int used;
      RolltuiRect field;
      rolltui_str_clear(&line);
      rolltui_str_append_str(&line, &it->label);
      str_add(&line, ": ");
      used = rolltui_frame_put_text(f, draw, x0, y + r, line.p, line.n, base, w, aw, 0);
      field.x = x0 + used;
      field.y = y + r;
      field.w = imax(w - used, 0);
      field.h = 1;
      if (field.w > 0) {
        /* THE COMPARISON, NOT A COPY — and a stack `RolltuiInputOptions o;` is GARBAGE in C,
         * where the C++'s default member initializers made the same line safe. The first cut
         * copied into one and ASan caught the write through its uninitialised prompt pointer.
         * Both halves of the fix are the same one: build the copy only when something
         * actually differs, which on a steady frame is never. */
        if (rolltui_input_options(m->edit)->ambiguous_wide != (unsigned char)(aw != 0)) {
          RolltuiInputOptions o;
          memset(&o, 0, sizeof o);
          rolltui_input_options_copy(&o, rolltui_input_options(m->edit));
          o.ambiguous_wide = (unsigned char)(aw != 0);
          rolltui_input_set_options(m->edit, &o);
          rolltui_input_options_release(&o);
        }
        rolltui_input_layout(m->edit, field);
        rolltui_input_draw(m->edit, f, draw, styles, input_roles, focused);
      }
      continue;
    }
    {
      RolltuiStr right;
      int rw, left_max, used, rx;
      memset(&right, 0, sizeof right);
      row_text(m, it, m->palette, i, &line);
      if (!m->palette) {
        if (it->kind == ROLLTUI_MENU_CHOICE) {
          rolltui_str_append_str(&right, &it->value);
          str_add(&right, " \xE2\x96\xB8");
        } else if (it->kind == ROLLTUI_MENU_SUBMENU) {
          str_add(&right, "\xE2\x96\xB8");
        } else if (it->shortcut.n) {
          rolltui_str_set(&right, it->shortcut.p, it->shortcut.n);
        }
      }
      rw = right.n ? rolltui_u_display_width(mm->u, right.p, right.n, aw) : 0;
      left_max = right.n == 0 ? w : imax(w - rw - 1, 0);
      used = rolltui_frame_put_text(f, draw, x0, y + r, line.p, line.n, base, left_max, aw, 0);
      if (rw > 0 && rw <= w) {
        const RolltuiStyle rs =
            is_sel ? base
                   : (it->kind == ROLLTUI_MENU_CHOICE || it->kind == ROLLTUI_MENU_SUBMENU
                          ? base
                          : styles[roles->shortcut]);
        rx = imax(w - rw, used + 1);
        rolltui_frame_put_text(f, draw, x0 + rx, y + r, right.p, right.n, rs, imax(w - rx, 0), aw, 0);
      }
      rolltui_str_free(&right);
    }
  }
  if (w >= 1 && rows >= 1) {
    if (m->top > 0) rolltui_frame_put(f, x0 + w - 1, y, "\xE2\x96\xB2", 3, 1, styles[roles->scroll_marker], 0);
    if ((size_t)(m->top + rows) < vis_n)
      rolltui_frame_put(f, x0 + w - 1, y + rows - 1, "\xE2\x96\xBC", 3, 1, styles[roles->scroll_marker], 0);
  }
  rolltui_str_free(&line);
}

/* ---- the file format (Phase 17 m1) --------------------------------------------------------- */

/* ---- the report: mirrors `MenuLoadReport` field-for-field, the same shape
 * `rolltui_app_profile.c`'s and `rolltui_bindings.c`'s reports use. --------------------------- */

void rolltui_menu_load_report_release(RolltuiMenuLoadReport* r) {
  size_t i;
  if (!r) return;
  rolltui_str_free(&r->error);
  for (i = 0; i < r->unknown_keys_n; ++i) rolltui_str_free(&r->unknown_keys[i]);
  rolltui_mem_free(r->unknown_keys);
  for (i = 0; i < r->bad_values_n; ++i) rolltui_str_free(&r->bad_values[i]);
  rolltui_mem_free(r->bad_values);
  memset(r, 0, sizeof *r);
}

void rolltui_menu_load_report_set_error(RolltuiMenuLoadReport* r, const char* s, size_t len) {
  rolltui_str_set(&r->error, s, len);
}

void rolltui_menu_load_report_add_unknown_key(RolltuiMenuLoadReport* r, const char* s, size_t len) {
  r->unknown_keys =
      (RolltuiStr*)rolltui_grow_zeroed(r->unknown_keys, &r->unknown_keys_cap, r->unknown_keys_n + 1, sizeof *r->unknown_keys);
  rolltui_str_set(&r->unknown_keys[r->unknown_keys_n++], s, len);
}

void rolltui_menu_load_report_add_bad_value(RolltuiMenuLoadReport* r, const char* s, size_t len) {
  r->bad_values =
      (RolltuiStr*)rolltui_grow_zeroed(r->bad_values, &r->bad_values_cap, r->bad_values_n + 1, sizeof *r->bad_values);
  rolltui_str_set(&r->bad_values[r->bad_values_n++], s, len);
}

int rolltui_menu_load_report_clean(const RolltuiMenuLoadReport* r) {
  return r->error.n == 0 && r->unknown_keys_n == 0 && r->bad_values_n == 0;
}

/* `where + suffix` into a fresh report entry; the one shape every per-item message shares. */
static void bad_value_at(RolltuiMenuLoadReport* r, const char* where, size_t wlen, const char* suffix, size_t slen) {
  RolltuiStr msg;
  memset(&msg, 0, sizeof msg);
  rolltui_str_set(&msg, where, wlen);
  rolltui_str_append(&msg, suffix, slen);
  rolltui_menu_load_report_add_bad_value(r, msg.p, msg.n);
  rolltui_str_free(&msg);
}

static void unknown_key_at(RolltuiMenuLoadReport* r, const char* where, size_t wlen, const char* suffix, size_t slen) {
  RolltuiStr msg;
  memset(&msg, 0, sizeof msg);
  rolltui_str_set(&msg, where, wlen);
  rolltui_str_append(&msg, suffix, slen);
  rolltui_menu_load_report_add_unknown_key(r, msg.p, msg.n);
  rolltui_str_free(&msg);
}

/* THE TREE-WIDE ID SET, and a Choice's own fresh one for its options (Menu.hpp: "a Choice's
 * OPTIONS are values, unique only within their choice"). GROWING AMORTISED, the same shape a
 * report's own lists use: ids are only ever added, across one parse. */
typedef struct {
  RolltuiStr* v;
  size_t n, cap;
} IdSet;

static int idset_has(const IdSet* s, const char* p, size_t n) {
  size_t i;
  for (i = 0; i < s->n; ++i)
    if (rolltui_str_eq(&s->v[i], p, n)) return 1;
  return 0;
}

static void idset_add(IdSet* s, const char* p, size_t n) {
  s->v = (RolltuiStr*)rolltui_grow_zeroed(s->v, &s->cap, s->n + 1, sizeof *s->v);
  rolltui_str_set(&s->v[s->n++], p, n);
}

static void idset_free(IdSet* s) {
  size_t i;
  for (i = 0; i < s->n; ++i) rolltui_str_free(&s->v[i]);
  rolltui_mem_free(s->v);
  s->v = NULL;
  s->n = s->cap = 0;
}

static const char* kind_name(unsigned char k) {
  switch (k) {
    case ROLLTUI_MENU_ACTION: return "action";
    case ROLLTUI_MENU_SUBMENU: return "submenu";
    case ROLLTUI_MENU_TOGGLE: return "toggle";
    case ROLLTUI_MENU_CHOICE: return "choice";
    case ROLLTUI_MENU_INPUT: return "input";
    default: return "action";
  }
}

static int kind_from_name(const char* s, size_t len, unsigned char* out) {
  if (streq(s, len, "action")) { *out = ROLLTUI_MENU_ACTION; return 1; }
  if (streq(s, len, "submenu")) { *out = ROLLTUI_MENU_SUBMENU; return 1; }
  if (streq(s, len, "toggle")) { *out = ROLLTUI_MENU_TOGGLE; return 1; }
  if (streq(s, len, "choice")) { *out = ROLLTUI_MENU_CHOICE; return 1; }
  if (streq(s, len, "input")) { *out = ROLLTUI_MENU_INPUT; return 1; }
  return 0;
}

/* A leading '.' from `where + "." + key`, when `where` is the root's empty string — the same
 * one-character fix-up `menu_from_json` applied to the whole report afterward rather than
 * threading a "am I the root" flag through every recursive call. Shifts in place; no
 * realloc, because the result is never longer than what is already held. */
static void strip_leading_dot(RolltuiStr* s) {
  if (s->n > 0 && s->p[0] == '.') {
    memmove(s->p, s->p + 1, s->n - 1);
    s->n -= 1;
    s->p[s->n] = '\0';
  }
}

/* ONE ITEM, recursively — a direct port of `Menu.cpp`'s `item_from_json`, preserving every
 * message and every ordering rule: `where` is this item's own path ("", "items[1]",
 * "items[1].items[0]", ...), spec keys are deferred to a second pass so an item's `kind` is
 * settled (by the FIRST pass, wherever "kind" fell in file order) before any of them are
 * checked against it, and `ids` is the CALLER's set — the tree-wide one, or a fresh one for a
 * Choice's own options, chosen by the CALLER exactly as `item_from_json`'s did. */
static void item_from_json(const RolltuiJsonValue* v, const char* where, size_t wlen, RolltuiMenuItem* it,
                           RolltuiMenuLoadReport* rep, IdSet* ids) {
  size_t i, n;
  int kind_given = 0;
  size_t* spec_idx = NULL;
  size_t spec_cap = 0, spec_n = 0;

  if (!rolltui_json_is_object(v)) {
    bad_value_at(rep, where, wlen, K(": expected an item object"));
    return;
  }
  n = rolltui_json_object_size(v);
  for (i = 0; i < n; ++i) {
    size_t klen = 0;
    const char* k = rolltui_json_object_key_at(v, i, &klen);
    const RolltuiJsonValue* x = rolltui_json_object_value_at(v, i);
    RolltuiStr at;
    memset(&at, 0, sizeof at);
    rolltui_str_set(&at, where, wlen);
    rolltui_str_append(&at, K("."));
    rolltui_str_append(&at, k, klen);

    if (streq(k, klen, "id") || streq(k, klen, "label") || streq(k, klen, "shortcut") || streq(k, klen, "value") ||
        streq(k, klen, "action")) {
      if (!rolltui_json_is_string(x)) {
        bad_value_at(rep, at.p, at.n, K(": expected a string"));
      } else {
        size_t slen = 0;
        const char* s = rolltui_json_as_string(x, "", 0, &slen);
        if (streq(k, klen, "id")) rolltui_str_set(&it->id, s, slen);
        else if (streq(k, klen, "label")) rolltui_str_set(&it->label, s, slen);
        else if (streq(k, klen, "shortcut")) rolltui_str_set(&it->shortcut, s, slen);
        else if (streq(k, klen, "action")) rolltui_str_set(&it->action_name, s, slen);
        else rolltui_str_set(&it->value, s, slen);
      }
    } else if (streq(k, klen, "kind")) {
      size_t slen = 0;
      const char* s = rolltui_json_is_string(x) ? rolltui_json_as_string(x, "", 0, &slen) : NULL;
      unsigned char kd = 0;
      if (!s || !kind_from_name(s, slen, &kd)) {
        bad_value_at(rep, at.p, at.n, K(": expected action | submenu | toggle | choice | input"));
      } else {
        it->kind = kd;
        kind_given = 1;
      }
    } else if (streq(k, klen, "enabled") || streq(k, klen, "checked")) {
      if (!rolltui_json_is_bool(x)) {
        bad_value_at(rep, at.p, at.n, K(": expected true or false"));
      } else {
        const unsigned char bv = (unsigned char)rolltui_json_as_bool(x, 0);
        if (streq(k, klen, "enabled")) it->enabled = bv;
        else it->checked = bv;
      }
    } else if (streq(k, klen, "items")) {
      if (!rolltui_json_is_array(x)) {
        bad_value_at(rep, at.p, at.n, K(": expected an array of items"));
      } else {
        /* "kind" read straight from the JSON, not from `it->kind`/`kind_given` — "items" may
         * be visited before "kind" in file order, and object LOOKUP is order-independent even
         * though this loop over members is not (the C++'s `v.get("kind")` is the same read). */
        size_t kindlen = 0;
        const char* kinds_ = rolltui_json_as_string(rolltui_json_get(v, K("kind")), "", 0, &kindlen);
        const int choice = streq(kinds_, kindlen, "choice");
        const size_t an = rolltui_json_array_size(x);
        size_t ai;
        IdSet option_ids;
        memset(&option_ids, 0, sizeof option_ids);
        for (ai = 0; ai < an; ++ai) {
          RolltuiMenuItem* child = rolltui_menu_list_add(&it->children);
          RolltuiStr childat;
          char idxbuf[32];
          const int idxlen = snprintf(idxbuf, sizeof idxbuf, "[%zu]", ai);
          memset(&childat, 0, sizeof childat);
          rolltui_str_set(&childat, at.p, at.n);
          rolltui_str_append(&childat, idxbuf, idxlen > 0 ? (size_t)idxlen : 0);
          item_from_json(rolltui_json_array_at(x, ai), childat.p, childat.n, child, rep, choice ? &option_ids : ids);
          rolltui_str_free(&childat);
        }
        idset_free(&option_ids);
      }
    } else if (streq(k, klen, "type") || streq(k, klen, "min") || streq(k, klen, "max") || streq(k, klen, "step") ||
               streq(k, klen, "precision") || streq(k, klen, "max_len") || streq(k, klen, "min_len") ||
               streq(k, klen, "optional") || streq(k, klen, "validator") || streq(k, klen, "hint")) {
      /* Deferred to a second pass, below, exactly as the C++'s `spec_keys` were — so an
       * item's `kind` (settled by the first pass, wherever "kind" itself fell) is known
       * before any of them are checked against it. */
      spec_idx = (size_t*)rolltui_grow(spec_idx, &spec_cap, spec_n + 1, sizeof *spec_idx);
      spec_idx[spec_n++] = i;
    } else {
      rolltui_menu_load_report_add_unknown_key(rep, at.p, at.n);
    }
    rolltui_str_free(&at);
  }
  if (!kind_given) it->kind = rolltui_json_has(v, K("items")) ? ROLLTUI_MENU_SUBMENU : ROLLTUI_MENU_ACTION;

  /* An action's shortcut is the bindings' to say (Menu.hpp): a file that also spells one out
   * is stating the same fact twice, and the second copy is what goes stale. */
  if (it->action_name.n != 0 && it->shortcut.n != 0) {
    bad_value_at(rep, where, wlen, K(".shortcut: an item with an \"action\" takes its shortcut from the bindings (ignored)"));
    rolltui_str_clear(&it->shortcut);
  }

  for (i = 0; i < spec_n; ++i) {
    size_t klen = 0;
    const char* k = rolltui_json_object_key_at(v, spec_idx[i], &klen);
    const RolltuiJsonValue* x = rolltui_json_object_value_at(v, spec_idx[i]);
    RolltuiStr at;
    memset(&at, 0, sizeof at);
    rolltui_str_set(&at, where, wlen);
    rolltui_str_append(&at, K("."));
    rolltui_str_append(&at, k, klen);

    if (it->kind != ROLLTUI_MENU_INPUT) {
      unknown_key_at(rep, at.p, at.n, K(" (only an input has it)"));
      rolltui_str_free(&at);
      continue;
    }
    if (streq(k, klen, "type")) {
      size_t tlen = 0;
      const char* ts = rolltui_json_is_string(x) ? rolltui_json_as_string(x, "", 0, &tlen) : NULL;
      unsigned char t = 0;
      if (!ts || !rolltui_input_type_from_name(ts, tlen, &t))
        bad_value_at(rep, at.p, at.n, K(": expected text | int | float | color | size | dim | name"));
      else it->spec.type = t;
    } else if (streq(k, klen, "min") || streq(k, klen, "max") || streq(k, klen, "step")) {
      if (!rolltui_json_is_number(x)) {
        bad_value_at(rep, at.p, at.n, K(": expected a number"));
      } else {
        const double num = rolltui_json_as_number(x, 0);
        if (streq(k, klen, "min")) it->spec.min = num;
        else if (streq(k, klen, "max")) it->spec.max = num;
        else it->spec.step = num;
      }
    } else if (streq(k, klen, "precision") || streq(k, klen, "max_len") || streq(k, klen, "min_len")) {
      const double num = rolltui_json_as_number(x, -1);
      if (!rolltui_json_is_number(x) || num < 0 || num != floor(num)) {
        bad_value_at(rep, at.p, at.n, K(": expected a whole number \xE2\x89\xA5 0"));
      } else if (streq(k, klen, "precision")) {
        it->spec.precision = (int)num;
      } else if (streq(k, klen, "max_len")) {
        it->spec.max_len = (size_t)num;
      } else {
        it->spec.min_len = (size_t)num;
      }
    } else if (streq(k, klen, "optional")) {
      if (!rolltui_json_is_bool(x)) bad_value_at(rep, at.p, at.n, K(": expected true or false"));
      else it->spec.optional = (unsigned char)rolltui_json_as_bool(x, 0);
    } else /* "validator" or "hint" */ {
      if (!rolltui_json_is_string(x)) {
        bad_value_at(rep, at.p, at.n, K(": expected a string"));
      } else {
        size_t slen = 0;
        const char* s = rolltui_json_as_string(x, "", 0, &slen);
        if (streq(k, klen, "validator")) rolltui_str_set(&it->spec.validator, s, slen);
        else rolltui_str_set(&it->spec.hint, s, slen);
      }
    }
    rolltui_str_free(&at);
  }
  rolltui_mem_free(spec_idx);

  if (it->kind == ROLLTUI_MENU_INPUT && it->spec.min > it->spec.max)
    bad_value_at(rep, where, wlen, K(": min is above max"));
  if (it->kind == ROLLTUI_MENU_INPUT && it->spec.validator.n != 0 && it->spec.type != ROLLTUI_INPUT_TYPE_TEXT)
    bad_value_at(rep, where, wlen, K(".validator: only a text input takes a validator (a typed input validates itself)"));
  if (it->id.n == 0) {
    bad_value_at(rep, where, wlen, K(": an item needs an \"id\""));
  } else if (idset_has(ids, it->id.p, it->id.n)) {
    RolltuiStr msg;
    memset(&msg, 0, sizeof msg);
    rolltui_str_set(&msg, where, wlen);
    rolltui_str_append(&msg, K(".id: duplicate id '"));
    rolltui_str_append(&msg, it->id.p, it->id.n);
    rolltui_str_append(&msg, K("'"));
    rolltui_menu_load_report_add_bad_value(rep, msg.p, msg.n);
    rolltui_str_free(&msg);
  } else {
    idset_add(ids, it->id.p, it->id.n);
  }
  if (it->label.n == 0) rolltui_str_set(&it->label, it->id.p, it->id.n);
}

int rolltui_menu_parse_json(const char* text, size_t len, RolltuiMenuItem* out, RolltuiMenuLoadReport* report) {
  RolltuiJsonValue* root;
  RolltuiStr jerr;
  IdSet ids;
  size_t i;
  memset(&jerr, 0, sizeof jerr);
  memset(&ids, 0, sizeof ids);
  rolltui_menu_load_report_release(report);
  rolltui_menu_item_release(out); /* leaves `out` freshly empty (rolltui_menu_item_init'd) */
  root = rolltui_json_parse(text, len, &jerr);
  if (!root) {
    rolltui_menu_load_report_set_error(report, jerr.p ? jerr.p : "", jerr.n);
    rolltui_str_free(&jerr);
    return 0;
  }
  rolltui_str_free(&jerr);
  if (!rolltui_json_is_object(root)) {
    rolltui_menu_load_report_set_error(report, K("menu file must be a JSON object"));
    rolltui_json_free(root);
    return 0;
  }
  item_from_json(root, "", 0, out, report, &ids);
  idset_free(&ids);
  rolltui_json_free(root);
  /* The root's own path is "", so its direct children's messages read ".items[i]...": strip
   * the one leading dot, matching `menu_from_json`'s post-process exactly (unknown_keys and
   * bad_values only — never `error`, which is not path-prefixed). */
  for (i = 0; i < report->unknown_keys_n; ++i) strip_leading_dot(&report->unknown_keys[i]);
  for (i = 0; i < report->bad_values_n; ++i) strip_leading_dot(&report->bad_values[i]);
  if (out->kind != ROLLTUI_MENU_SUBMENU)
    rolltui_menu_load_report_add_bad_value(report, K("kind: the root must be a submenu (it holds the top level)"));
  return 1;
}

/* ONE ITEM, recursively, the other direction — a direct port of `Menu.cpp`'s `item_to_json`. */
static RolltuiJsonValue* item_to_json(const RolltuiMenuItem* it) {
  RolltuiJsonValue* o = rolltui_json_object();
  const int implied = (it->kind == ROLLTUI_MENU_SUBMENU && it->children.n != 0) ||
                      (it->kind == ROLLTUI_MENU_ACTION && it->children.n == 0);
  const int label_is_id =
      it->label.n == it->id.n && (it->label.n == 0 || memcmp(it->label.p, it->id.p, it->label.n) == 0);
  size_t i;

  rolltui_json_set(o, K("id"), rolltui_json_string(it->id.p, it->id.n));
  if (!label_is_id) rolltui_json_set(o, K("label"), rolltui_json_string(it->label.p, it->label.n));
  if (!implied) {
    const char* kn = kind_name(it->kind);
    rolltui_json_set(o, K("kind"), rolltui_json_string(kn, strlen(kn)));
  }
  if (it->action_name.n != 0) rolltui_json_set(o, K("action"), rolltui_json_string(it->action_name.p, it->action_name.n));
  /* An action's shortcut is DERIVED and never written back (apply_shortcuts fills it from the
   * live chords), so a round trip cannot bake one moment's keys into a file. */
  if (it->action_name.n == 0 && it->shortcut.n != 0)
    rolltui_json_set(o, K("shortcut"), rolltui_json_string(it->shortcut.p, it->shortcut.n));
  if (!it->enabled) rolltui_json_set(o, K("enabled"), rolltui_json_bool(0));
  if (it->checked) rolltui_json_set(o, K("checked"), rolltui_json_bool(1));
  if (it->value.n != 0) rolltui_json_set(o, K("value"), rolltui_json_string(it->value.p, it->value.n));

  if (it->kind == ROLLTUI_MENU_INPUT) {
    RolltuiInputSpec d;
    const RolltuiInputSpec* s = &it->spec;
    rolltui_input_spec_init(&d);
    if (s->type != d.type) {
      size_t tlen = 0;
      const char* tn = rolltui_input_type_name(s->type, &tlen);
      rolltui_json_set(o, K("type"), rolltui_json_string(tn, tlen));
    }
    if (s->min != d.min) rolltui_json_set(o, K("min"), rolltui_json_number(s->min));
    if (s->max != d.max) rolltui_json_set(o, K("max"), rolltui_json_number(s->max));
    if (s->step != d.step) rolltui_json_set(o, K("step"), rolltui_json_number(s->step));
    if (s->precision != d.precision) rolltui_json_set(o, K("precision"), rolltui_json_number(s->precision));
    if (s->max_len != d.max_len) rolltui_json_set(o, K("max_len"), rolltui_json_number((double)s->max_len));
    if (s->min_len != d.min_len) rolltui_json_set(o, K("min_len"), rolltui_json_number((double)s->min_len));
    if (s->optional) rolltui_json_set(o, K("optional"), rolltui_json_bool(1));
    if (s->validator.n != 0) rolltui_json_set(o, K("validator"), rolltui_json_string(s->validator.p, s->validator.n));
    if (s->hint.n != 0) rolltui_json_set(o, K("hint"), rolltui_json_string(s->hint.p, s->hint.n));
    rolltui_input_spec_release(&d);
  }

  if (it->children.n != 0) {
    RolltuiJsonValue* arr = rolltui_json_array();
    for (i = 0; i < it->children.n; ++i) rolltui_json_array_push(arr, item_to_json(it->children.v[i]));
    rolltui_json_set(o, K("items"), arr);
  }
  return o;
}

void rolltui_menu_dump_json(const RolltuiMenuItem* root, RolltuiStr* out) {
  RolltuiJsonValue* v = item_to_json(root);
  rolltui_json_dump(v, 2, out);
  rolltui_str_append(out, "\n", 1);
  rolltui_json_free(v);
}

/* The menu's own editor, BORROWED — valid for the menu's life. What `Menu::editor()` returns,
 * and the reason owning the input costs a caller nothing: the three states a typed field has
 * (committed value, editing text, preview) are still readable. */
RolltuiInput* rolltui_menu_editor(const RolltuiMenu* m) { return m->edit; }
