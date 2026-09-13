//
// product_flags_test.cpp — WHAT A FLAG ON A PRODUCT COMMAND LINE IS ALLOWED TO BE.
//
// A command line is not a configuration system. A setting reachable only by a flag is a second
// configuration system with neither discoverability nor persistence, competing with the one that
// has both — and it wins by accident, because a flag is what a person finds first.
//
// EVERY FLAG ON A SHIPPED BINARY IS ONE OF FOUR THINGS, and the four are not close:
//
//   1. A SELF-TEST HOOK — `--frame`, `--keys`, `--stroke`, `--tick`, `--dump-role` and their
//      kin exist to make a deterministic non-interactive frame for a golden test. They are
//      ADDITIVE: `<product>-selftest` is the same source compiled again WITH them, so the
//      shipped binary does not contain them and the binary that gets verified is not the one
//      that ships. Asserted per app, on the artifact (`paint_art_test.cpp` section 0).
//   2. A REAL FEATURE RUN HEADLESSLY — `--check NAME|FILE` runs the accessibility checker and
//      `--generate RULESET` the seeded generator. The theme editor offers both from inside the
//      app, so these are a non-interactive entry to a shipped capability, not a test hook.
//      They stay, and they stay in the product.
//   3. A TERMINAL FACT — `--ambiguous-wide` states how a terminal draws East Asian AMBIGUOUS
//      glyphs. It is not a preference and not a hook; it is something true of the terminal that
//      the process cannot yet ask. It belongs with `mode` and `depth` as a setting, and until
//      the cursor-position probe lands it is the last flag of its kind on a product.
//   4. CONFIGURATION — `--theme`, `--layout`, `--bindings`, `--presets`, `--depth`, `--mode`.
//      **These are what this suite exists to keep out.** Every one names a preset or a value the
//      preset system already holds, autosaves and offers a UI for.
//
// THE CONTROL IS THE POINT AND IT RUNS FIRST. Asking a binary "do you reject --theme?" and
// getting a rejection proves nothing on its own: a binary that rejects EVERYTHING answers the
// same way, and so does one that failed to start. So each product is first shown to ACCEPT a
// flag it legitimately has, and only then shown to refuse the configuration ones.
//
#include <cstdio>
#include <cstddef>
#include <string>
#include <vector>

#include "rolltui_test.hpp"

using namespace rolltui_test;
using namespace testkit;

#if !defined(ROLLTUI_STUDIO_PRODUCT_BIN) || !defined(ROLLTUI_PAINT_PRODUCT_BIN) || \
    !defined(DIRKTUI_PRODUCT_BIN)
#error "the three product binaries must be named"
#endif

namespace {

std::string run(const std::string& cmd, int& rc) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) { rc = -1; return out; }
  char buf[4096];
  while (std::size_t n = std::fread(buf, 1, sizeof buf, p)) out.append(buf, n);
  rc = pclose(p);
  return out;
}

// A flag is REFUSED when the binary does not act on it. Every product answers an unknown flag
// with its usage line, so "usage appeared AND the flag is not in it" is the shape to check —
// a binary that silently ignored the flag would print no usage and fail here.
bool refuses(const std::string& bin, const std::string& flag, std::string& why) {
  int rc = 0;
  const std::string out = run(std::string("'") + bin + "' " + flag + " x 2>&1", rc);
  const bool said_usage = out.find("usage:") != std::string::npos;
  const bool advertises = out.find(flag) != std::string::npos;
  if (!said_usage) why = "no usage line for " + flag;
  else if (advertises) why = "usage still advertises " + flag;
  return said_usage && !advertises;
}

}  // namespace

int main() {
  struct Product {
    const char* name;
    const char* bin;
    const char* keeps;  // a flag it legitimately has, and so advertises — the control
  };
  // `--ambiguous-wide` is category 3 and every product takes it; the studio additionally keeps
  // `--check`, which is category 2. Both are the control, not the subject.
  const std::vector<Product> products = {
      {"rolltui-studio", ROLLTUI_STUDIO_PRODUCT_BIN, "--check"},
      {"rolltui-paint", ROLLTUI_PAINT_PRODUCT_BIN, "--ambiguous-wide"},
      {"dirktui", DIRKTUI_PRODUCT_BIN, "--ambiguous-wide"},
  };

  // ---- the control: the usage line IS an inventory, and the search finds what is in it -----
  // Both halves of this suite read one instrument — the usage a product prints for an unknown
  // flag — so the control is that a flag the product legitimately KEEPS is found there. Without
  // it, "the flag is not in the usage" is satisfied just as well by a usage that never printed,
  // a binary that failed to start, or a search that matches nothing.
  for (const Product& p : products) {
    int rc = 0;
    const std::string usage = run(std::string("'") + p.bin + "' --no-such-flag 2>&1", rc);
    check(usage.find("usage:") != std::string::npos,
          std::string(p.name) + " answers an unknown flag with its usage");
    check(usage.find(p.keeps) != std::string::npos,
          std::string(p.name) + " advertises '" + p.keeps + "', which it keeps — so the inventory is real [" +
              usage.substr(0, usage.find('\n')) + "]");
  }

  // ---- and a kept flag is ACCEPTED, not merely listed ---------------------------------------
  // Advertised and accepted are different properties, and the gap between them is a real defect
  // shape here: these argv loops close their `else` chain INSIDE the self-test `#ifdef`, so a
  // branch added above it dangles in the product build — the flag is taken and the usage prints
  // anyway. Every product past this point reaches the terminal check, which is the first thing
  // after argv, so "the usage did NOT print" is what says the flag was consumed.
  for (const Product& p : products) {
    if (std::string(p.keeps) != "--ambiguous-wide") continue;  // the studio's is asserted below
    int rc = 0;
    // stdin from /dev/null, stated rather than inherited: a product that draws on /dev/tty (dirktui)
    // must refuse before it touches the terminal, and "stdin is not a terminal" is the
    // precondition it refuses on. Inheriting ctest's stdin would make this probe's safety depend
    // on how ctest was launched.
    const std::string out = run(std::string("'") + p.bin + "' " + p.keeps + " < /dev/null 2>&1", rc);
    check(out.find("usage:") == std::string::npos && out.find("terminal") != std::string::npos,
          std::string(p.name) + " ACCEPTS '" + p.keeps + "' and gets past argv to the terminal check");
  }

  // ---- and the two REAL FEATURES actually run in the shipped binary ------------------------
  // Advertised is not the same as present. `--check` is a capability a theme author reaches from
  // a script, so the product has to DO it, not merely list it.
  {
    int rc = 0;
    const std::string checked =
        run(std::string("'") + ROLLTUI_STUDIO_PRODUCT_BIN + "' --check default 2>&1", rc);
    check(checked.find("badges:") != std::string::npos,
          "the shipped rolltui-studio runs --check and prints a theme's badges");
    const std::string generated =
        run(std::string("'") + ROLLTUI_STUDIO_PRODUCT_BIN + "' --generate analogous --seed 3 2>&1", rc);
    check(generated.find("\"roles\"") != std::string::npos,
          "…and --generate writes a theme file the preset system could read");
    // A ruleset that does not exist is refused BY NAME with the set that does, rather than
    // quietly generating something from a default nobody asked for.
    const std::string bad =
        run(std::string("'") + ROLLTUI_STUDIO_PRODUCT_BIN + "' --generate nonsuch 2>&1", rc);
    check(bad.find("no ruleset named nonsuch") != std::string::npos && bad.find("analogous") != std::string::npos,
          "…and an unknown ruleset is refused by name, listing the ones that exist");
  }

  // ---- the subject: no product takes a configuration flag ------------------------------------
  for (const Product& p : products) {
    for (const char* flag : {"--theme", "--layout", "--bindings", "--presets", "--depth", "--mode"}) {
      std::string why;
      check(refuses(p.bin, flag, why),
            std::string(p.name) + " refuses the configuration flag " + flag +
                (why.empty() ? "" : " — " + why));
    }
  }

  return report("rolltui_product_flags_test");
}
