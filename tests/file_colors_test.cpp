// file_colors_test.cpp — files coloured as `ls` colours them: LS_COLORS and LSCOLORS parsed, a host's glob and regular-expression
// rules ahead of them, the type rules in `ls`'s order, and the column browser drawing its names in them.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to read cells back */
#include "rolltui_test.hpp"

using testkit::check;

namespace {
namespace fs = std::filesystem;

RolltuiStyle sgr(const char* s) {
  RolltuiStyle st{};
  rolltui_style_from_sgr(s, std::strlen(s), &st);
  return st;
}

// What `fc` gives a name of this mode, from a plain style; "" when nothing matched.
std::string match(const RolltuiFileColors* fc, const char* name, unsigned mode, int link = 0) {
  RolltuiStyle st{};
  if (!rolltui_file_colors_match(fc, name, std::strlen(name), mode, link, &st)) return "";
  std::string out;
  if (st.fg.kind == RolltuiStyleColorRaw::Kind::Indexed) out += "fg" + std::to_string(st.fg.index);
  if (st.fg.kind == RolltuiStyleColorRaw::Kind::Rgb) out += "rgb" + std::to_string(st.fg.r) + "," + std::to_string(st.fg.g) + "," + std::to_string(st.fg.b);
  if (st.bg.kind == RolltuiStyleColorRaw::Kind::Indexed) out += " bg" + std::to_string(st.bg.index);
  if (st.bold) out += " bold";
  if (st.underline) out += " underline";
  if (st.italic) out += " italic";
  if (st.dim) out += " dim";
  return out.empty() ? "plain" : out;
}

constexpr unsigned kFile = 0100644, kExec = 0100755, kDir = 0040755, kFifo = 0010644, kSock = 0140755, kSticky = 0041777,
                   kSetuid = 0104755;

void test_gnu() {
  RolltuiFileColors* fc = rolltui_file_colors_new();
  const char* ls = "rs=0:di=01;34:ln=01;36:pi=40;33:so=01;35:or=40;31;01:ex=01;32:su=37;41:tw=30;42:"
                   "*.tar=01;31:*.TGZ=01;31:*.md=38;5;208:*README=4:*.c=38;2;10;20;30:fi=0:bogus";
  const std::size_t taken = rolltui_file_colors_add_gnu(fc, ls, std::strlen(ls));
  check(taken == 14, "LS_COLORS: each type and suffix entry is taken, an unknown key and a stray word are not [" + std::to_string(taken) + "]");
  check(match(fc, "src", kDir) == "fg4 bold", "a folder is di [" + match(fc, "src", kDir) + "]");
  check(match(fc, "tmp", kSticky) == "fg0 bg2", "…a sticky folder others can write to is tw");
  check(match(fc, "pipe", kFifo) == "fg3 bg0", "a named pipe is pi");
  check(match(fc, "sock", kSock) == "fg5 bold", "a socket is so");
  check(match(fc, "run.sh", kExec) == "fg2 bold", "an executable is ex");
  check(match(fc, "sudo", kSetuid) == "fg7 bg1", "…and setuid outranks it, as in ls");
  check(match(fc, "a.tar", kFile) == "fg1 bold", "a plain file by its suffix");
  check(match(fc, "b.tgz", kFile) == "fg1 bold", "…case aside");
  check(match(fc, "notes.md", kFile) == "fg208", "…in the 256 palette");
  check(match(fc, "main.c", kFile) == "rgb10,20,30", "…in true colour");
  check(match(fc, "README", kFile) == " underline", "…a suffix that is a whole name [" + match(fc, "README", kFile) + "]");
  check(match(fc, "tool.tar", kExec) == "fg2 bold", "an executable is ex whatever its suffix, as in ls");
  check(match(fc, "gone", 0, 2) == "fg1 bg0 bold", "a broken link is or");
  check(match(fc, "plain.txt", kFile) == "plain", "a plain file with no suffix of its own is fi");
  const char* later = "*.tar=01;33";
  rolltui_file_colors_add_gnu(fc, later, std::strlen(later));
  check(match(fc, "a.tar", kFile) == "fg3 bold", "a later suffix entry wins, as in ls");
  rolltui_file_colors_free(fc);
  RolltuiFileColors* bare = rolltui_file_colors_new();
  const char* only = "ln=01;36";
  rolltui_file_colors_add_gnu(bare, only, std::strlen(only));
  check(match(bare, "gone", 0, 2) == "fg6 bold" && match(bare, "x.txt", kFile).empty(),
        "with no or entry a broken link is ln, and a file nothing names is left alone");
  rolltui_file_colors_free(bare);
}

void test_bsd_and_env() {
  RolltuiFileColors* fc = rolltui_file_colors_new();
  check(rolltui_file_colors_add_bsd(fc, "exfxcxdxbxegedabagacad", 22) == 11, "LSCOLORS: eleven pairs");
  check(match(fc, "src", kDir) == "fg4", "BSD's di is the first pair, e for blue [" + match(fc, "src", kDir) + "]");
  check(match(fc, "run", kExec) == "fg1", "…ex the fifth, b for red");
  check(match(fc, "tmp", kSticky) == "fg0 bg2", "…tw the tenth, a on c");
  rolltui_file_colors_free(fc);
  RolltuiFileColors* up = rolltui_file_colors_new();
  rolltui_file_colors_add_bsd(up, "Exfxcxdxbxegedabagacad", 22);
  check(match(up, "src", kDir) == "fg4 bold", "…an uppercase letter is bold");
  rolltui_file_colors_free(up);
  // THE ENVIRONMENT: LS_COLORS first, LSCOLORS when it is not set
  ::setenv("LS_COLORS", "di=01;35", 1);
  ::setenv("LSCOLORS", "exfxcxdxbxegedabagacad", 1);
  RolltuiFileColors* env = rolltui_file_colors_new();
  check(rolltui_file_colors_add_env(env) == 1 && match(env, "d", kDir) == "fg5 bold", "the environment's LS_COLORS is read first");
  rolltui_file_colors_free(env);
  ::unsetenv("LS_COLORS");
  RolltuiFileColors* bsd = rolltui_file_colors_new();
  check(rolltui_file_colors_add_env(bsd) == 11 && match(bsd, "d", kDir) == "fg4", "…and LSCOLORS without it");
  rolltui_file_colors_free(bsd);
  ::unsetenv("LSCOLORS");
  RolltuiFileColors* none = rolltui_file_colors_new();
  check(rolltui_file_colors_add_env(none) == 0 && match(none, "d", kDir).empty(), "with neither, nothing is coloured");
  rolltui_file_colors_free(none);
}

void test_rules() {
  RolltuiFileColors* fc = rolltui_file_colors_new();
  const char* ls = "di=01;34:*.md=33:ex=32";
  rolltui_file_colors_add_gnu(fc, ls, std::strlen(ls));
  check(rolltui_file_colors_add_rule(fc, "*.MD", std::strlen("*.MD"), ROLLTUI_FILE_REGULAR, sgr("35")) == 1, "a glob rule is taken");
  auto rule = [&](const char* pattern, unsigned char type, const char* style) {
    return rolltui_file_colors_add_rule(fc, pattern, std::strlen(pattern), type, sgr(style));
  };
  check(rule("re:^(Makefile|CMakeLists\\.txt)$", ROLLTUI_FILE_ANY, "1;36") == 1, "…and a regular expression");
  check(rule("re:([", ROLLTUI_FILE_ANY, "1") == 0, "…but not one that does not compile");
  rule("node_modules", ROLLTUI_FILE_DIR, "2");
  rule("[a-c]?.log", ROLLTUI_FILE_ANY, "31");
  rule("*.log", ROLLTUI_FILE_ANY, "34");
  check(match(fc, "notes.md", kFile) == "fg5", "a host's rule comes before LS_COLORS's suffix, case aside");
  check(match(fc, "Makefile", kFile) == "fg6 bold" && match(fc, "CMakeLists.txt", kFile) == "fg6 bold" && match(fc, "aMakefile", kFile).empty(),
        "a regular expression, pinned with ^ and $ [" + match(fc, "aMakefile", kFile) + "]");
  check(match(fc, "node_modules", kDir) == " dim", "a rule for folders matches a folder, ahead of di [" + match(fc, "node_modules", kDir) + "]");
  check(match(fc, "node_modules", kFile).empty(), "…and not a file of that name");
  check(match(fc, "b1.log", kFile) == "fg1", "a set and a one-character wildcard: the first rule that matches wins");
  check(match(fc, "zz.log", kFile) == "fg4", "…and the next when it does not");
  check(match(fc, "run.md", kExec) == "fg2", "a REGULAR rule leaves an executable to ex");
  rule("[!a]*.cfg", ROLLTUI_FILE_ANY, "35");
  check(match(fc, "b.cfg", kFile) == "fg5" && match(fc, "a.cfg", kFile).empty(), "[!a] is any one character but a");
  rolltui_file_colors_free(fc);
}

// ---- the column browser draws its names in them ------------------------------------------------------------------------------

std::vector<std::string> g_rows;
RolltuiFrame* draw_picker(RolltuiWindows* w, RolltuiWindowStack* st, const RolltuiStyle* styles) {
  const RolltuiRect area{0, 0, 40, 8};
  rolltui_windows_sync(w, st);
  rolltui_windows_layout(w, st, area);
  RolltuiFrame* f = rolltui_frame_new(40, 8, styles[ROLLTUI_ROLE_BACKGROUND]);
  RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
  struct Slot {
    RolltuiWindows* w;
    const RolltuiStyle* s;
  } slot{w, styles};
  rolltui_window_stack_compose(
      st, f, area, styles, rolltui_layout_default_roles(),
      [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
        Slot* sl = static_cast<Slot*>(c);
        rolltui_windows_draw(sl->w, rn, fr, sl->s, rolltui_windows_default_roles());
      },
      &slot, 0, cs);
  rolltui_compose_scratch_free(cs);
  return f;
}
int row_of(RolltuiFrame* f, const std::string& name, int* x) {
  for (int y = 0; y < rolltui_frame_height(f); ++y)
    for (int cx = 0; cx + static_cast<int>(name.size()) <= rolltui_frame_width(f); ++cx) {
      bool same = true;
      for (std::size_t k = 0; k < name.size() && same; ++k) {
        std::size_t n = 0;
        const char* g = rolltui_frame_glyph(f, cx + static_cast<int>(k), y, &n);
        same = n == 1 && g[0] == name[k];
      }
      if (same) {
        *x = cx;
        return y;
      }
    }
  return -1;
}

void test_picker() {
  char tmpl[] = "/tmp/rolltui_file_colors_XXXXXX";
  const char* made = ::mkdtemp(tmpl);
  check(made != nullptr, "a scratch directory");
  if (!made) return;
  const fs::path root = made;
  struct RemoveAtEnd {
    fs::path p;
    ~RemoveAtEnd() {
      std::error_code ec;
      fs::remove_all(p, ec);
    }
  } remove_at_end{root};
  fs::create_directories(root / "alpha");
  for (const char* n : {"b.tar", "c.txt", "d.sh"}) {
    const int fd = ::open((root / n).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) ::close(fd);
  }
  ::chmod((root / "d.sh").c_str(), 0755);
  RolltuiContext* ctx = rolltui_context_new();
  rolltui_context_set_library_defaults(ctx);
  RolltuiWindows* w = rolltui_windows_new(ctx);
  RolltuiWindowStack* st = rolltui_window_stack_new();
  constexpr std::string_view screen = R"({ "resizable": false, "name": "f", "root": { "id": "pick", "content": "filepicker", "focusable": true } })";
  RolltuiLayoutReport rep{};
  RolltuiLayout* l = rolltui_load_layout_text(screen.data(), screen.size(), nullptr, 0, nullptr, &rep);
  rolltui_layout_report_release(&rep);
  rolltui_window_stack_set_base(st, rolltui_layout_base(l));
  const std::string dir = root.string();
  RolltuiPickerOptions opts{};
  rolltui_picker_options_init(&opts);
  opts.motion = 0; /* steady frames: the comparisons are of colours, not of a column sliding */
  rolltui_windows_set_picker_options(w, "filepicker", 10, &opts);
  rolltui_windows_set_picker_dir(w, "filepicker", 10, dir.data(), dir.size());
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  rolltui_frame_free(draw_picker(w, st, styles)); /* the first frame opens the columns; the ones after it are the steady state */
  RolltuiFrame* plain = draw_picker(w, st, styles);
  RolltuiFileColors* fc = rolltui_file_colors_new();
  const char* ls = "di=01;34:ex=01;32:*.tar=01;31";
  rolltui_file_colors_add_gnu(fc, ls, std::strlen(ls));
  rolltui_windows_set_picker_file_colors(w, "filepicker", 10, fc);
  RolltuiFrame* lit = draw_picker(w, st, styles);
  int x = 0;
  RolltuiCellRaw c{};
  const int tar = row_of(lit, "b.tar", &x);
  rolltui_frame_cell(lit, x, tar, &c);
  check(tar >= 0 && c.style.fg.kind == RolltuiStyleColorRaw::Kind::Indexed && c.style.fg.index == 1 && c.style.bold,
        "the browser draws a name in its file colour: b.tar red and bold");
  check(c.style.bg == styles[ROLLTUI_ROLE_BACKGROUND].bg, "…on the browser's own ground");
  const int sh = row_of(lit, "d.sh", &x);
  rolltui_frame_cell(lit, x, sh, &c);
  check(sh >= 0 && c.style.fg.index == 2 && c.style.bold, "…an executable green");
  const int txt = row_of(lit, "c.txt", &x);
  RolltuiCellRaw before{};
  rolltui_frame_cell(lit, x, txt, &c);
  {
    int px = 0;
    const int py = row_of(plain, "c.txt", &px);
    rolltui_frame_cell(plain, px, py, &before);
  }
  check(txt >= 0 && c.style == before.style, "…and a file nothing names as the theme draws it");
  int tx = 0;
  const int al = row_of(lit, "b.tar", &tx) - 1; /* the folder the cursor starts on, the row above */
  RolltuiCellRaw alpha_lit{}, alpha_plain{};
  rolltui_frame_cell(lit, tx, al, &alpha_lit);
  rolltui_frame_cell(plain, tx, al, &alpha_plain);
  std::size_t gn = 0;
  const char* g = rolltui_frame_glyph(lit, tx, al, &gn);
  check(al >= 1 && std::string(g, gn) == "a" && alpha_lit.style == alpha_plain.style,
        "the cursor's row keeps the cursor's style: the folder under it is not recoloured");
  rolltui_windows_set_picker_file_colors(w, "filepicker", 10, nullptr);
  RolltuiFrame* again = draw_picker(w, st, styles);
  {
    int bx = 0, cx = 0;
    const int by = row_of(again, "b.tar", &bx), cy = row_of(again, "c.txt", &cx);
    rolltui_frame_cell(again, bx, by, &c);
    rolltui_frame_cell(again, cx, cy, &before);
  }
  auto desc = [](const RolltuiStyle& st) {
    return std::to_string(st.fg.r) + "," + std::to_string(st.fg.g) + "," + std::to_string(st.fg.b) + "/" + std::to_string(st.bg.r) + "," +
           std::to_string(st.bg.g) + "," + std::to_string(st.bg.b) + " b" + std::to_string(st.bold) + " d" + std::to_string(st.dim) +
           " r" + std::to_string(st.reverse) + " u" + std::to_string(st.underline);
  };
  check(c.style == before.style, "NULL puts the theme's colours back: b.tar is drawn as c.txt is [" + desc(c.style) + " vs " + desc(before.style) + "]");
  rolltui_frame_free(plain);
  rolltui_frame_free(lit);
  rolltui_frame_free(again);
  rolltui_file_colors_free(fc);
  rolltui_effect_map_free(fx);
  rolltui_windows_free(w);
  rolltui_window_stack_free(st);
  rolltui_layout_free(l);
  rolltui_context_free(ctx);
}

}  // namespace

int main() {
  test_gnu();
  test_bsd_and_env();
  test_rules();
  test_picker();
  return testkit::report("file_colors_test");
}
