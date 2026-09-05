/* rolltui/c/rolltui_theme_analysis.c — the colour maths. See rolltui_theme_analysis.h for
 * the boundary's rules and what deliberately stayed in `rolltui/ThemeAnalysis.cpp` (the
 * report and the auto-fix, both still C++ because `Theme`/`Role`/`json::Value` are). The
 * constants below are the published ones and `rolltui/tests/theme_analysis_test.cpp` holds
 * them to reference values; nothing here is tuned by eye. Nothing allocates. */
#include "rolltui/c/rolltui_theme_analysis.h"

#include <math.h>
#include <string.h>

#include "rolltui/c/rolltui_theme.h"

/* std::clamp/min/max have no C equivalent; three small helpers stand in for them
 * everywhere below (Phase 17 m1's own instance of the rule `rolltui_alloc.h` states for
 * allocation: a closed set of named helpers, not one invented per call site). */
static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static double maxd(double a, double b) { return a > b ? a : b; }
static double mind(double a, double b) { return a < b ? a : b; }

/* ---- sRGB <-> linear (IEC 61966-2-1) ---------------------------------------------------- */

double rolltui_srgb_channel_to_linear(double c) {
  c = clampd(c, 0.0, 1.0);
  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

double rolltui_linear_channel_to_srgb(double v) {
  v = clampd(v, 0.0, 1.0);
  return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

int rolltui_to_linear(RolltuiStyleColor c, RolltuiLin* out) {
  if (c.kind == 0) return 0; /* None: the terminal's own colour, unknown rather than assumed */
  if (c.kind == 1) rolltui_ansi_index_rgb(c.index, &c); /* Indexed -> the Rgb it resolves to */
  out->r = rolltui_srgb_channel_to_linear(c.r / 255.0);
  out->g = rolltui_srgb_channel_to_linear(c.g / 255.0);
  out->b = rolltui_srgb_channel_to_linear(c.b / 255.0);
  return 1;
}

static unsigned char encode_channel(double v) {
  return (unsigned char)lround(rolltui_linear_channel_to_srgb(v) * 255.0);
}

void rolltui_from_linear(RolltuiLin l, RolltuiStyleColor* out) {
  out->kind = 2; /* Rgb */
  out->index = 0;
  out->r = encode_channel(l.r);
  out->g = encode_channel(l.g);
  out->b = encode_channel(l.b);
}

/* ---- linear <-> OKLab <-> OKLCH (Björn Ottosson, 2020) ---------------------------------- */

void rolltui_linear_to_oklab(RolltuiLin c, RolltuiOkLab* out) {
  const double l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
  const double m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
  const double s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;
  const double l_ = cbrt(l), m_ = cbrt(m), s_ = cbrt(s);
  out->L = 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_;
  out->a = 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_;
  out->b = 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_;
}

void rolltui_oklab_to_linear(RolltuiOkLab lab, RolltuiLin* out) {
  const double l_ = lab.L + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
  const double m_ = lab.L - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
  const double s_ = lab.L - 0.0894841775 * lab.a - 1.2914855480 * lab.b;
  const double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
  out->r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
  out->g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
  out->b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
}

void rolltui_oklab_to_oklch(RolltuiOkLab lab, RolltuiOkLch* out) {
  double h = atan2(lab.b, lab.a) * 180.0 / M_PI;
  if (h < 0) h += 360.0;
  out->L = lab.L;
  out->C = sqrt(lab.a * lab.a + lab.b * lab.b);
  out->h = h;
}

void rolltui_oklch_to_oklab(RolltuiOkLch lch, RolltuiOkLab* out) {
  const double rad = lch.h * M_PI / 180.0;
  out->L = lch.L;
  out->a = lch.C * cos(rad);
  out->b = lch.C * sin(rad);
}

/* ---- contrast and distance --------------------------------------------------------------- */

double rolltui_relative_luminance(RolltuiLin l) { return 0.2126 * l.r + 0.7152 * l.g + 0.0722 * l.b; }

double rolltui_wcag_contrast(RolltuiLin a, RolltuiLin b) {
  const double la = rolltui_relative_luminance(a), lb = rolltui_relative_luminance(b);
  const double hi = maxd(la, lb), lo = mind(la, lb);
  return (hi + 0.05) / (lo + 0.05);
}

/* SAPC-4g (APCA 0.1.9): luminance with the APCA coefficients and exponent, a soft black
 * clamp, then the polarity-specific powers. */
static double apca_channel(double v) { return pow(clampd(rolltui_linear_channel_to_srgb(v), 0.0, 1.0), 2.4); }

static double apca_y(RolltuiLin c) {
  return 0.2126729 * apca_channel(c.r) + 0.7151522 * apca_channel(c.g) + 0.0721750 * apca_channel(c.b);
}

static double apca_clamp_black(double Y) { return Y < 0.022 ? Y + pow(0.022 - Y, 1.414) : Y; }

double rolltui_apca_contrast(RolltuiLin text, RolltuiLin bg) {
  const double ytxt = apca_clamp_black(apca_y(text));
  const double ybg = apca_clamp_black(apca_y(bg));
  double sapc;
  if (fabs(ybg - ytxt) < 0.0005) return 0.0;
  if (ybg > ytxt) {
    sapc = (pow(ybg, 0.56) - pow(ytxt, 0.57)) * 1.14;
    return sapc < 0.1 ? 0.0 : (sapc - 0.027) * 100.0;
  }
  sapc = (pow(ybg, 0.65) - pow(ytxt, 0.62)) * 1.14;
  return sapc > -0.1 ? 0.0 : (sapc + 0.027) * 100.0;
}

double rolltui_delta_e(RolltuiOkLab a, RolltuiOkLab b) {
  const double dl = a.L - b.L, da = a.a - b.a, db = a.b - b.b;
  return sqrt(dl * dl + da * da + db * db);
}

static const char* const kCvdNames[ROLLTUI_CVD_COUNT] = {"protanopia", "deuteranopia", "tritanopia"};

const char* rolltui_cvd_name(unsigned char type, size_t* len) {
  const char* s = type < ROLLTUI_CVD_COUNT ? kCvdNames[type] : "";
  if (len) *len = strlen(s);
  return s;
}

/* Machado, Oliveira & Fernandes 2009, severity 1.0, in linear RGB. Rows sum to 1, so a grey
 * stays that grey (asserted in theme_analysis_test.cpp). An out-of-range `type` falls
 * through to the tritanopia matrix, exactly as the original C++'s `?:` chain did (it tested
 * Protanopia, then Deuteranopia, and used Tritanopia's matrix for anything else — including
 * the enum's third and only remaining legal value, but a byte crossing a C boundary has no
 * enum to enforce that, so the fallthrough is now the whole of the guarantee). */
void rolltui_simulate_cvd(RolltuiLin l, unsigned char type, RolltuiLin* out) {
  static const double kProtan[3][3] = {
      {0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}};
  static const double kDeutan[3][3] = {
      {0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}};
  static const double kTritan[3][3] = {
      {1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}};
  const double (*m)[3] = type == ROLLTUI_CVD_PROTANOPIA ? kProtan : type == ROLLTUI_CVD_DEUTERANOPIA ? kDeutan : kTritan;
  out->r = clampd(m[0][0] * l.r + m[0][1] * l.g + m[0][2] * l.b, 0.0, 1.0);
  out->g = clampd(m[1][0] * l.r + m[1][1] * l.g + m[1][2] * l.b, 0.0, 1.0);
  out->b = clampd(m[2][0] * l.r + m[2][1] * l.g + m[2][2] * l.b, 0.0, 1.0);
}
