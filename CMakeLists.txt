# rolltui — a small terminal-UI library of our own (plan/phase-9.md, requirement 9).
#
# WHAT "SMALL" AND "LIGHTWEIGHT" MEAN HERE IS MEASURED, NOT ASSERTED (Phase 13). A real
# 120x40 render of a 40-entry document costs 108 allocations, 7 KB and ~250 us — down from
# 887 / 455 KB — and `rolltui-budget-test` fails if any of those move in either direction.
# The target is a steady frame that allocates NOTHING, with every allocation that does
# happen named; `rolltui-ownership-test` holds the other half (three ownership shapes, no
# shared_ptr, no hand-rolled new/delete).
#
# One static library. Headers are the API; implementations live in .cpp files, and a
# vendored dependency (md4c) is included only by the .cpp that uses it, so a consumer
# pays for a component once at link time rather than at every include (measured
# 2026-09-01: a translation unit that merely included the header-only Markdown.hpp
# cost 0.8 s and a 197 KB object; that is what this layout removes). The include
# directory is the REPO ROOT so consumers write `#include "rolltui/Wrap.hpp"` — and
# nothing else: roll's own `include/` is never on this target's path, and the tests
# below build with only this directory and rolltui/tests. That is the whole of
# phase 9's Done-when (g).
#
# Tests use their own 30-line harness (rolltui/tests/rolltui_test.hpp), not roll's
# tests/test_util.hpp, for the same reason.

# Milestone 11: the presets that SHIP are real files (rolltui/presets/themes/*.json)
# so the editor can write them; they are embedded at build time and parsed at start
# like the built-in layouts. A new file needs a re-run of cmake (CONFIGURE_DEPENDS
# re-globs on every build in the generators that support it).
# Phase 14 — THE C EXPERIMENT'S SWITCH (plan/phase-14.md m1). Both implementations live in
# the tree and satisfy the SAME tests; this flag picks which object files link. Rollback is
# turning it back off, which is what makes "immediate" true — a branch would diverge and
# stop being immediate within a week.
option(ROLLTUI_C "Build the library's ported slice in C rather than C++ (Phase 14)" OFF)
if(ROLLTUI_C)
  # m2 IN PROGRESS: rolltui_screen.c is compiled (so it is syntax- and warning-checked)
  # but nothing calls it yet — `rolltui::Frame` still holds its own storage. Wiring it is
  # the rest of m2; see plan/phase-14.md for the two hazards found while designing it.
  set(ROLLTUI_SLICE c/rolltui_geom.c c/rolltui_screen.c)
  message(STATUS "rolltui: the ported slice is C (ROLLTUI_C=ON)")
else()
  set(ROLLTUI_SLICE GeomCpp.cpp)
endif()

file(GLOB ROLLTUI_THEME_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/themes/*.json)
set(ROLLTUI_EMBEDDED_PRESETS ${CMAKE_CURRENT_BINARY_DIR}/embedded_presets.cpp)
add_custom_command(OUTPUT ${ROLLTUI_EMBEDDED_PRESETS}
  COMMAND ${CMAKE_COMMAND} -DOUT=${ROLLTUI_EMBEDDED_PRESETS} -DTABLE=kThemePresets "-DFILES=${ROLLTUI_THEME_PRESETS}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  DEPENDS ${ROLLTUI_THEME_PRESETS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  COMMENT "Embedding the shipped rolltui theme presets"
  VERBATIM)
# Phase 10 m1: the Layout domain's shipped presets (presets/layouts/*.json). These are
# also the built-in layouts — builtin_layout() parses this same embedded table, so the
# four layouts have ONE definition site (a file), not a file plus a string in Layout.cpp.
file(GLOB ROLLTUI_LAYOUT_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/layouts/*.json)
set(ROLLTUI_EMBEDDED_LAYOUTS ${CMAKE_CURRENT_BINARY_DIR}/embedded_layouts.cpp)
add_custom_command(OUTPUT ${ROLLTUI_EMBEDDED_LAYOUTS}
  COMMAND ${CMAKE_COMMAND} -DOUT=${ROLLTUI_EMBEDDED_LAYOUTS} -DTABLE=kLayoutPresets "-DFILES=${ROLLTUI_LAYOUT_PRESETS}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  DEPENDS ${ROLLTUI_LAYOUT_PRESETS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  COMMENT "Embedding the shipped rolltui layout presets"
  VERBATIM)

# Phase 10 m3: the shipped MENU files (presets/menus/*.json). Not a preset domain — a
# menu has no working copy; it is a file a layout names (`menu:<name>`) and the library
# resolves, the shipped rung under the user's directory and the host's own embedded
# menus (rolltui/Widgets.hpp).
file(GLOB ROLLTUI_MENUS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/menus/*.json)
set(ROLLTUI_EMBEDDED_MENUS ${CMAKE_CURRENT_BINARY_DIR}/embedded_menus.cpp)
add_custom_command(OUTPUT ${ROLLTUI_EMBEDDED_MENUS}
  COMMAND ${CMAKE_COMMAND} -DOUT=${ROLLTUI_EMBEDDED_MENUS} -DTABLE=kMenus "-DFILES=${ROLLTUI_MENUS}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  DEPENDS ${ROLLTUI_MENUS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  COMMENT "Embedding the shipped rolltui menus"
  VERBATIM)

# Milestone 17: the Bindings domain's shipped presets (presets/bindings/*.json) the same way.
file(GLOB ROLLTUI_BINDINGS_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/bindings/*.json)
set(ROLLTUI_EMBEDDED_BINDINGS ${CMAKE_CURRENT_BINARY_DIR}/embedded_bindings.cpp)
add_custom_command(OUTPUT ${ROLLTUI_EMBEDDED_BINDINGS}
  COMMAND ${CMAKE_COMMAND} -DOUT=${ROLLTUI_EMBEDDED_BINDINGS} -DTABLE=kBindingsPresets "-DFILES=${ROLLTUI_BINDINGS_PRESETS}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  DEPENDS ${ROLLTUI_BINDINGS_PRESETS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  COMMENT "Embedding the shipped rolltui bindings presets"
  VERBATIM)

add_library(rolltui STATIC
  unicode_tables.cpp          # generated by tools/gen_unicode_tables.cpp
  Unicode.cpp
  Wrap.cpp
  Markdown.cpp
  Diff.cpp
  Json.cpp
  Theme.cpp
  ${ROLLTUI_SLICE}            # Phase 14: the ported slice — C or C++, chosen by ROLLTUI_C
  Memory.cpp                  # Phase 13: the library's ONE entry point for memory
  Effects.cpp                 # Phase 12 m6: motion is the theme's, a state is the widget's
  Screen.cpp
  Keys.cpp
  Terminal.cpp                # the one unit that touches a file descriptor
  Transcript.cpp
  Layout.cpp
  Widgets.cpp                 # Phase 10 m2: widgets by kind, sources by name
  Input.cpp
  Menu.cpp
  Presets.cpp
  ThemeAnalysis.cpp
  ThemeGen.cpp
  Bindings.cpp
  AppProfile.cpp              # Phase 11 m4: what a layout may name inside one app
  ${ROLLTUI_EMBEDDED_PRESETS}
  ${ROLLTUI_EMBEDDED_LAYOUTS}
  ${ROLLTUI_EMBEDDED_MENUS}
  ${ROLLTUI_EMBEDDED_BINDINGS}  # generated by cmake/embed_presets.cmake
  third_party/md4c/md4c.c     # vendored, MIT; see third_party/md4c/README.md
)
target_include_directories(rolltui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(rolltui PUBLIC cxx_std_23)
target_compile_options(rolltui PRIVATE -Wall -Wextra)
set_source_files_properties(third_party/md4c/md4c.c PROPERTIES COMPILE_OPTIONS "-w")  # not ours to warn about

set(ROLLTUI_UCD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ucd)
set(ROLLTUI_FIXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

function(add_rolltui_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE rolltui)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  if(ROLLTUI_C)
    target_compile_definitions(${name} PRIVATE ROLLTUI_C_BUILD=1)
  endif()
  target_compile_definitions(${name} PRIVATE
    ROLLTUI_UCD_DIR="${ROLLTUI_UCD_DIR}"
    ROLLTUI_FIXTURE_DIR="${ROLLTUI_FIXTURE_DIR}"
    ROLLTUI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    ROLLTUI_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets/themes"
    ROLLTUI_LAYOUTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets/layouts"
    ROLLTUI_MENUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets/menus"
    ROLLTUI_BINDINGS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets/bindings")
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# Milestone 2: the Unicode conformance suites, in full, and the width table.
add_rolltui_test(rolltui-grapheme-break-test tests/grapheme_break_test.cpp)
add_rolltui_test(rolltui-line-break-test tests/line_break_test.cpp)
add_rolltui_test(rolltui-width-test tests/width_test.cpp)
# Milestone 9: UAX #29 word boundaries (the double-click), plus the escape stripper.
add_rolltui_test(rolltui-word-break-test tests/word_break_test.cpp)

# Milestone 3: the wrap engine — edge-case table and the three properties.
add_rolltui_test(rolltui-wrap-test tests/wrap_test.cpp)

# Milestone 4: the markdown renderer over vendored md4c — never drops a character.
add_rolltui_test(rolltui-markdown-test tests/markdown_test.cpp)

# Milestone 5: theme (roles → styles), our JSON, downgrade, SGR, the grep control.
add_rolltui_test(rolltui-theme-test tests/theme_test.cpp)

# Phase 13 m2: OWNERSHIP — no shared ownership in the library (a grep control with a
# planted-pattern liveness check), no hand-rolled new/delete, and a recorded census of
# every raw pointer in a public header, so adding one forces its lifetime to be stated.
add_rolltui_test(rolltui-ownership-test tests/ownership_test.cpp)

# Phase 13 m1: THE BUDGET — allocations, bytes and wall-clock per frame, asserted as a
# RANGE against numbers recorded from a real render, with the counter proved armed on
# every run (an exact accounting check plus a live wasteful-widget control).
add_rolltui_test(rolltui-budget-test tests/budget_test.cpp)

# Phase 12 m6: effects — the two properties over EVERY registered kind (an effect never
# changes a span's width and never writes outside its span), the built-in kind table, and
# "no wakeups with no marks".
add_rolltui_test(rolltui-effects-test tests/effects_test.cpp)

# Milestone 6: the cell grid + frame diff (golden bytes), the key/mouse decoder
# (table), and Terminal on a real pty with a forked child killed by SIGTERM.
add_rolltui_test(rolltui-screen-test tests/screen_test.cpp)
add_rolltui_test(rolltui-keys-test tests/keys_test.cpp)
add_rolltui_test(rolltui-terminal-test tests/terminal_test.cpp)
set_tests_properties(rolltui-terminal-test PROPERTIES TIMEOUT 60)

# Phase 12 m3: DELIVERABILITY — a chord a terminal cannot deliver is refused, not bound
# to silence. The table is measured against the ENCODINGS (encode_key fed to the real
# KeyDecoder over ~1000 chords × 3 protocols), the loader's refusal is named, the same
# file loads clean under kitty, and the protocol is negotiated on a real pty pair by a
# forked terminal that answers — or does not.
add_rolltui_test(rolltui-deliverability-test tests/deliverability_test.cpp)
set_tests_properties(rolltui-deliverability-test PROPERTIES TIMEOUT 60)

# Milestone 8: placement (the resolve table + tiling properties), the split tree, the
# layout loader and the four built-ins, border joining, the stack's focus routing.
add_rolltui_test(rolltui-layout-test tests/layout_test.cpp)

# Milestone 9: the transcript widget — layout cache, entry-anchored scroll, follow +
# marker, folding, logical-coordinate selection with auto-scroll and copy, OSC 8.
add_rolltui_test(rolltui-transcript-test tests/transcript_test.cpp)

# Milestone 10: the input widget — grapheme-boundary caret, cell wrap, selection by
# key and mouse, history with a draft, paste sanitising; the key table in Input.hpp.
add_rolltui_test(rolltui-input-test tests/input_test.cpp)

# Milestone 11: the menu widget — five item kinds, level navigation, typed filter,
# breadcrumb, palette mode, the menu file format; the key table is in Menu.hpp.
add_rolltui_test(rolltui-menu-test tests/menu_test.cpp)

# Milestone 15: the colour maths against published reference values, the report and
# badges on the built-ins, the unknown-colour rule, claimed badges, auto-fix.
add_rolltui_test(rolltui-theme-analysis-test tests/theme_analysis_test.cpp)
# Milestone 15: the seeded generator — deterministic, every ruleset readable at chaos
# 0, chaos 1 breaks a rule for some seed, meta round-trips.
add_rolltui_test(rolltui-theme-gen-test tests/theme_gen_test.cpp)

# Milestone 17: bindings as data — chords parse and print, the shipped default loads
# clean and is the whole table, lookup by scope, conflicts, the Enter rule, bind/unbind,
# the file round trip, help lines from the live table.
add_rolltui_test(rolltui-bindings-test tests/bindings_test.cpp)

# Milestone 11: the preset system — the five rules on the Theme domain, the shipped
# files equal to the built-ins, the four-rung precedence table, OSC 11 parsing.
add_rolltui_test(rolltui-presets-test tests/presets_test.cpp)

# Milestone 7: the studio (Phase 11 m2 renamed it; plan/phase-11.md) — renders a fixture transcript in a theme; `--frame WxH`
# prints one frame and exits, which is also the golden-frame harness and the
# screenshot tool. Interactive mode hot-reloads a theme file on mtime change.
# Milestones 14-17 grow it into the library's own editor: the theme editor (tools/
# theme_editor.*), the layout editor, the keys editor. ROLLTUI_SHIPPED_DIR is where the
# editor's "write a shipped preset" privilege writes — the source tree, for the next
# build — unless --shipped says otherwise.
add_executable(rolltui-studio tools/studio.cpp tools/theme_editor.cpp tools/layout_editor.cpp tools/keys_editor.cpp)
target_link_libraries(rolltui-studio PRIVATE rolltui)
target_compile_options(rolltui-studio PRIVATE -Wall -Wextra)
target_compile_definitions(rolltui-studio PRIVATE ROLLTUI_SHIPPED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets")

# Phase 11 m6: THE THIRD HOST — a text-mode painting app with no transcript and no
# input, whose whole interaction is one registered widget kind. It is the phase's proof
# target: the studio authors its screen from its PROFILE alone, and this binary runs the
# file. `--frame WxH` prints one frame (the studio's convention); `--stroke X,Y-X,Y`
# synthesises a press, the drags between and a release, so a canvas can be shown to
# receive them with no terminal.
add_executable(rolltui-paint tools/paint.cpp)
target_link_libraries(rolltui-paint PRIVATE rolltui)
target_compile_options(rolltui-paint PRIVATE -Wall -Wextra)

# Milestone 14: the theme editor's model — every role reachable, live preview, commit,
# cancel, undo/redo, the palette, the written-back pair object — driven without a
# terminal.
add_rolltui_test(rolltui-theme-editor-test tests/theme_editor_test.cpp)
target_sources(rolltui-theme-editor-test PRIVATE tools/theme_editor.cpp)
target_include_directories(rolltui-theme-editor-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)

# Milestone 16: the layout editor's model — selection, split/swap/hide/delete, border
# and content as live choices, title and size inputs, nudges, popups, the seam drag,
# undo/redo, and the edited layout round-tripping through the loader.
add_rolltui_test(rolltui-layout-editor-test tests/layout_editor_test.cpp)
target_sources(rolltui-layout-editor-test PRIVATE tools/layout_editor.cpp)
target_include_directories(rolltui-layout-editor-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)

# Milestone 17: the keys editor's model — capture a chord, a conflict moves and says
# so, the Enter rule refused by name, remove and clear, undo/redo, the outcomes.
add_rolltui_test(rolltui-keys-editor-test tests/keys_editor_test.cpp)
target_sources(rolltui-keys-editor-test PRIVATE tools/keys_editor.cpp)
target_include_directories(rolltui-keys-editor-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)

add_rolltui_test(rolltui-studio-golden-test tests/studio_golden_test.cpp)
target_compile_definitions(rolltui-studio-golden-test PRIVATE
  ROLLTUI_STUDIO_BIN="$<TARGET_FILE:rolltui-studio>")
add_dependencies(rolltui-studio-golden-test rolltui-studio)

# Phase 10 milestone 6 — THE PHASE'S PROOF: a screen that exists only as files (a
# layout, a menu, a bindings file and a document under tests/fixtures/screen) copied
# into a scratch preset directory and run by the real studio, which has never heard
# of it. Its controls are what carry the weight: no source names the screen, the chord
# comes only from the bindings file, an undeclared action shows no shortcut, and a file
# written after the build opens in it.
# Phase 11 milestone 6 — THE PHASE'S PROOF, one level up from the files-only test: the
# studio, driven through its own design editor by keystrokes and knowing the target only
# from `rolltui-paint --profile`, authors a three-window screen for an app it is not,
# saves it, and a second process renders it. Its controls are the pair: drop --app and the
# identical keystrokes build nothing, and no source names the authored screen.
add_rolltui_test(rolltui-authored-screen-test tests/authored_screen_test.cpp)
target_compile_definitions(rolltui-authored-screen-test PRIVATE
  ROLLTUI_STUDIO_BIN="$<TARGET_FILE:rolltui-studio>"
  ROLLTUI_PAINT_BIN="$<TARGET_FILE:rolltui-paint>")
add_dependencies(rolltui-authored-screen-test rolltui-studio rolltui-paint)

add_rolltui_test(rolltui-files-only-test tests/files_only_test.cpp)
target_compile_definitions(rolltui-files-only-test PRIVATE
  ROLLTUI_STUDIO_BIN="$<TARGET_FILE:rolltui-studio>")
add_dependencies(rolltui-files-only-test rolltui-studio)
