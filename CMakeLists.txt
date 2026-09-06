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
# directory is the REPO ROOT so consumers write `#include "rolltui/rolltui.h"` — and
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
# THE LIBRARY IS THE C IMPLEMENTATION (2026-09-04, the user's call after Phase 15 m6).
# Phase 14 built `-DROLLTUI_C` as a rollback flag: both implementations in the tree, satisfying
# the same tests, so a module that went badly was reverted by deleting one filename from a list
# rather than by unwinding a merge. It did its job — Phase 15 ported the whole library behind it
# and every milestone landed green on both sides — and the flag is now spent. The sixteen
# `*Cpp.cpp` files are deleted; these are the implementation.
#
# WHAT THE DELETION BUYS, beyond 8,157 lines: a control point planted in one of these files used
# to be ABSENT in the other configuration, so a two-sided negative control had to declare which
# build it reached (`plan/phase-16.md`'s reach parameter, now struck). With one implementation a
# control is simply present, and `main`'s `check_controlled` crosses over unchanged.
#
# The C++ files that REMAIN are not implementations of these — they are the C++ API over them
# (`Screen.cpp`, `Wrap.cpp`, `Menu.cpp`, …), plus the C++ halves of data that is C in the
# library and C++ at its edge (`LayoutTree.cpp`, `MenuTree.cpp`, `DocumentCpp.cpp`).
set(ROLLTUI_SLICE c/rolltui_geom.c c/rolltui_screen.c c/rolltui_wrap.c c/rolltui_unicode.c
                  c/rolltui_diff.c c/rolltui_effects.c c/rolltui_keys.c
                  c/rolltui_theme.c c/rolltui_bindings.c
                  c/rolltui_presets.c c/rolltui_markdown.c c/rolltui_layout.c
                  c/rolltui_input.c c/rolltui_menu.c
                  c/rolltui_widgets.c c/rolltui_widget_kinds.c c/rolltui_transcript.c
                  c/rolltui_json.c c/rolltui_app_profile.c
                                         # Phase 17 m1: the parser, and the app profile on
                                         #   top of it. The profile's C boundary takes and
                                         #   returns TEXT — a JSON tree never crosses it,
                                         #   which is the leak `rolltui-paint` demonstrated
                                         #   by including Json.hpp for a single dump call.
                  c/rolltui_theme_analysis.c c/rolltui_theme_gen.c
                                         # Phase 17 m1: the colour maths and the generator's
                                         #   PRNG/ruleset lookup — the parts of
                                         #   ThemeAnalysis/ThemeGen with no Theme, Role or
                                         #   json::Value in them. Their .cpp files REMAIN,
                                         #   unlike the *Cpp.cpp Phase 15 deleted: the
                                         #   report, the auto-fix and generate() are real
                                         #   C++ logic over a Theme, not a binding, and they
                                         #   port when Theme and Json do.
                  c/rolltui_terminal.c)  # Phase 17 m1: the one unit that touches a file
                                         #   descriptor — 43 POSIX calls that were wrapped in
                                         #   C++, which is the strongest argument FOR C here.

file(GLOB ROLLTUI_THEME_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/themes/*.json)
set(ROLLTUI_EMBEDDED_PRESETS ${CMAKE_CURRENT_BINARY_DIR}/embedded_presets.c)
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
set(ROLLTUI_EMBEDDED_LAYOUTS ${CMAKE_CURRENT_BINARY_DIR}/embedded_layouts.c)
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
set(ROLLTUI_EMBEDDED_MENUS ${CMAKE_CURRENT_BINARY_DIR}/embedded_menus.c)
add_custom_command(OUTPUT ${ROLLTUI_EMBEDDED_MENUS}
  COMMAND ${CMAKE_COMMAND} -DOUT=${ROLLTUI_EMBEDDED_MENUS} -DTABLE=kMenus "-DFILES=${ROLLTUI_MENUS}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  DEPENDS ${ROLLTUI_MENUS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  COMMENT "Embedding the shipped rolltui menus"
  VERBATIM)

# Milestone 17: the Bindings domain's shipped presets (presets/bindings/*.json) the same way.
file(GLOB ROLLTUI_BINDINGS_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/bindings/*.json)
set(ROLLTUI_EMBEDDED_BINDINGS ${CMAKE_CURRENT_BINARY_DIR}/embedded_bindings.c)
add_custom_command(OUTPUT ${ROLLTUI_EMBEDDED_BINDINGS}
  COMMAND ${CMAKE_COMMAND} -DOUT=${ROLLTUI_EMBEDDED_BINDINGS} -DTABLE=kBindingsPresets "-DFILES=${ROLLTUI_BINDINGS_PRESETS}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  DEPENDS ${ROLLTUI_BINDINGS_PRESETS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_presets.cmake
  COMMENT "Embedding the shipped rolltui bindings presets"
  VERBATIM)

add_library(rolltui STATIC
  unicode_tables.c            # generated by tools/gen_unicode_tables.cpp — ONE table
                              #   file compiled by both languages (Phase 14 m5), so the
                              #   two algorithm implementations share the data rather
                              #   than each carrying 9,800 lines that could disagree.
  c/rolltui_alloc.c           # Phase 14 m4: the C side's CLOSED SET of allocation
                              #   strategies. Compiled into BOTH configurations for the same
                              #   reason as UnicodeSeam.cpp below — a file only one build
                              #   links is one only that build can find a defect in.
  c/rolltui_marker.c          # Phase 15 m4: the "▼ N more" marker, whose one definition
                              #   now has a C caller. Both configurations, same reason.
  c/rolltui_str.c             # Phase 15 m5: THE OWNED STRING AND THE OWNED ARRAY. Both
                              #   configurations: it is the storage two recursive trees and
                              #   the widget map are built out of, and one definition of "who
                              #   owns these bytes" is the whole point of the milestone.
  c/rolltui_frame_ops.c       # Phase 15 m5: put_text / fill / tint. They were methods on the
                              #   C++ `Frame` in Screen.cpp, which is fine while only C++
                              #   draws and impossible once the widgets are C.
  c/rolltui_layout_tree.c     # Phase 15 m5: THE SPLIT TREE, as data. Both configurations,
                              #   for the span store's reason — the flag chooses the
                              #   placement ALGORITHM, never the shape of a node.
  c/rolltui_menu_tree.c       # Phase 15 m5: THE MENU TREE, the second recursive owning tree,
                              #   also in both configurations and for the same reason.
  c/rolltui_document.c        # Phase 15 m5e: THE DOCUMENT MODEL, as data. Both
                              #   configurations, and for a second reason: a HOST fills it,
                              #   so two shapes of an entry would be two things to disagree.
  c/rolltui_undo.c            # Phase 17: opaque-snapshot undo, the C form of the last
                              #   pure-C++ template. The editors keep a C++ adapter of
                              #   their own under tools/, which is a HOST's business.
  c/rolltui_style.c           # Phase 17: the 49 role names, from the one X-macro list
  c/rolltui_library_actions.c # Phase 17: the 59 library actions as DATA, once — four
                              #   consumers had each copied them verbatim.
  c/rolltui_embedded.c        # the shipped presets as C DATA — see its header for the
                              #   fifth-copy incident that moved them out of C++.
  c/rolltui_render.c          # the frame -> bytes step `rolltui_screen.h` deliberately
                              #   deferred ("porting them is its own step"): render_diff,
                              #   render_full and frame_to_text, filling a caller's buffer.
  c/rolltui_swap.c            # THE DOUBLE BUFFER, because three hosts had written it. The
                              #   per-frame lifetime stops existing and a host's repaint
                              #   lands inside the budget instead of beside it.
  c/rolltui_map.c             # Phase 15 m5: THE STRING-KEYED TABLE, once — seven
                              #   `unordered_map<string, T>`s across two modules had it
                              #   invisibly. Both configurations, like the rest of the data.
  c/rolltui_md_lines.c        # Phase 15 m4: THE SPAN STORE. Also in both configurations,
                              #   and for a second reason stated at its header: it is the
                              #   DATA both implementations of the renderer fill, and two
                              #   copies of a data structure is two things that can disagree
                              #   about what a span is. The flag chooses the ALGORITHM.
  ${ROLLTUI_SLICE}            # the library proper, in C (Phase 14 + Phase 15)
  c/rolltui_mem.c             # Phase 13, moved to C at Phase 17 m1: the library's ONE entry
                              #   point for memory. Memory.hpp is a thin forwarding shim now.
  c/rolltui_lifetime.c        # Phase 14 m6a, moved to C at Phase 17 m1: shutdown(), and no
                              #   init — the release point that makes "did we leak?"
                              #   answerable. Lifetime.hpp is a thin forwarding shim now.
                              #   release`, whose only declaration (rolltui/Scratch.hpp,
                              #   out of scope here) is deliberately not `inline`, so it
                              #   needs an ordinary out-of-line definition somewhere.
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

# Phase 17 m2: THE PUBLIC HEADER's own guarantees, as assertions. That including
# `rolltui/rolltui.h` alone is SUFFICIENT is proved by this translation unit compiling and
# calling the API; that the header declares NOTHING of its own is a grep control which proves
# itself against a planted declaration first.
add_rolltui_test(rolltui-public-header-test tests/public_header_test.cpp)
# Phase 19 m2: THE OPT-IN LIST. A test may include a `rolltui/c/` header — an internal one, since
# the public declarations live in `rolltui/rolltui.h` — by naming it in its source and being listed
# here. `public_header_test` asserts that every other consumer (hosts, tools, every unlisted test)
# includes none: the count is an exact zero, not a ratchet.
# PHASE 20 m6: RENAMED IN MEANING — this is no longer only tests. The STUDIO and its three
# editors are on it: they are rolltui's OWN authoring tool for rolltui's OWN files (themes,
# layouts, bindings), nobody outside this repo builds one, and the user's call is that the
# studio "doesn't count as having public functions ... it can include private headers." So its
# reach stops justifying PUBLIC, exactly as a test's does. `rolltui-paint` is NOT here: a
# generic painting app is the closest thing in this tree to what an outsider would write.
set(ROLLTUI_INTERNAL_TESTS
  # the studio and its editors — rolltui's own tool, not a consumer
  studio.cpp theme_editor.cpp theme_editor.hpp keys_editor.cpp keys_editor.hpp
  layout_editor.cpp layout_editor.hpp tool_actions.hpp undo_stack.hpp
  # suites that test an implementation
  bindings_test.cpp budget_test.cpp deliverability_test.cpp effects_test.cpp grapheme_break_test.cpp
  input_test.cpp keys_test.cpp layout_editor_test.cpp layout_test.cpp lifetime_test.cpp line_break_test.cpp
  markdown_test.cpp md_test_helpers.hpp menu_test.cpp presets_test.cpp terminal_test.cpp theme_gen_test.cpp
  width_test.cpp word_break_test.cpp wrap_test.cpp
  screen_test.cpp transcript_test.cpp theme_test.cpp theme_analysis_test.cpp
  # PHASE 20 m6: the two EDITOR unit tests joined. They were in the public-only set, and an
  # editor's unit test is not a consumer-shaped program — it drives one editor, which is part
  # of the studio. Decision 2: only a consumer-shaped program's reach is evidence.
  keys_editor_test.cpp theme_editor_test.cpp)
list(JOIN ROLLTUI_INTERNAL_TESTS "," ROLLTUI_INTERNAL_TESTS_JOINED)
target_compile_definitions(rolltui-public-header-test PRIVATE ROLLTUI_INTERNAL_TESTS="${ROLLTUI_INTERNAL_TESTS_JOINED}")

# Phase 20 m1/m4: THE OTHER HALF OF THE LIST, AND THE ONE THAT IS THE INSTRUMENT. Moving a
# function INTERNAL costs a suite an opt-in, so the opt-in list only grows — and if nearly every
# suite is on it, the exact-zero above stops saying anything. What keeps it meaningful is the set
# that stays OUT: programs shaped like a CONSUMER, which include `rolltui/rolltui.h` and nothing
# else. What one of these reaches is PUBLIC for that reason, not because a test reaches it.
#   - `c_consumer_test.c` is MANDATORY and can never be moved: it is the pure-C proof that the
#     public API alone builds a screen, draws a frame and drives a preset store.
#   - the whole-host suites run the real binaries; the meta-tests scan sources; roll's own tests
#     are a HOST's, and roll builds from the one header.
# `public_header_test` asserts this set is non-empty, contains the C consumer and at least one
# whole-host suite, and that no function it reaches is INTERNAL.
# THE INSTRUMENT, and it is this list rather than the one above: the programs that build
# against `rolltui/rolltui.h` ALONE. Moving a function INTERNAL costs a suite an opt-in, so the
# list above only grows; if everything ended up on it, the exact-zero bypass check would stop
# saying anything. What keeps it meaningful is that these stay OUT — and they are CONSUMER-SHAPED
# (Phase 20 m6, decision 2): the pure-C consumer, which is the proof that the public API alone
# builds a screen, draws a frame and drives a preset store; the three suites that drive whole
# host binaries; and the two meta-tests, which scan sources rather than call the library.
set(ROLLTUI_PUBLIC_ONLY_TESTS
  c_consumer_test.c explorer_test.cpp paint_art_test.cpp
  authored_screen_test.cpp files_only_test.cpp studio_golden_test.cpp
  public_header_test.cpp ownership_test.cpp)
list(JOIN ROLLTUI_PUBLIC_ONLY_TESTS "," ROLLTUI_PUBLIC_ONLY_TESTS_JOINED)
target_compile_definitions(rolltui-public-header-test PRIVATE ROLLTUI_PUBLIC_ONLY_TESTS="${ROLLTUI_PUBLIC_ONLY_TESTS_JOINED}")

# Phase 14 m6a: THE RELEASE POINT, as an assertion — after shutdown() the library holds
# NOTHING, asserted as a number in both configurations, with a control that proves the gauge
# can see a retention before it is trusted to report a zero. Until this existed, "did the
# library leak?" had no answer: by-design retention and a real leak look identical.
add_rolltui_test(rolltui-lifetime-test tests/lifetime_test.cpp)

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
add_executable(rolltui-paint examples/paint.cpp)
target_link_libraries(rolltui-paint PRIVATE rolltui)
target_compile_options(rolltui-paint PRIVATE -Wall -Wextra)
target_include_directories(rolltui-paint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)

# Phase 21: THE FOURTH CONSUMER — a read-only file-system browser whose custom widget is a
# macOS-style column view. It is the first widget in this repo with INTERNAL STRUCTURE the
# library does not already model (children of its own, a selection that propagates sideways,
# two scroll axes, a content-dependent width), which is what makes it an instrument rather
# than a demo: what it could NOT do through `rolltui/rolltui.h` alone is the phase's finding.
# It is a CONSUMER like `rolltui-paint` — it never opts in to an internal header.
add_executable(rolltui-explorer examples/explorer.cpp)
target_link_libraries(rolltui-explorer PRIVATE rolltui)
target_compile_options(rolltui-explorer PRIVATE -Wall -Wextra)

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

# Phase 21 m4: the explorer's golden frames and its files-only control — the screen's own
# words (the window ids, the browser scope's actions, the titles) appear in no source the
# binary is built from, exactly as `files_only_test` asserts for the studio's fixture screen.
# Phase 21: paint as the ADVERSARIAL probe — the ramps make a real picture, a hand-picked RGB
# down-converts at every depth (paint is the first consumer whose colour is user DATA rather
# than the theme's expression of a state), and the block ramp meets East Asian AMBIGUOUS width
# head-on instead of avoiding it.
add_rolltui_test(rolltui-paint-art-test tests/paint_art_test.cpp)
target_compile_definitions(rolltui-paint-art-test PRIVATE ROLLTUI_PAINT_BIN="$<TARGET_FILE:rolltui-paint>")
add_dependencies(rolltui-paint-art-test rolltui-paint)

add_rolltui_test(rolltui-explorer-test tests/explorer_test.cpp)
target_compile_definitions(rolltui-explorer-test PRIVATE
  ROLLTUI_EXPLORER_BIN="$<TARGET_FILE:rolltui-explorer>"
  ROLLTUI_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
add_dependencies(rolltui-explorer-test rolltui-explorer)

add_rolltui_test(rolltui-files-only-test tests/files_only_test.cpp)
target_compile_definitions(rolltui-files-only-test PRIVATE
  ROLLTUI_STUDIO_BIN="$<TARGET_FILE:rolltui-studio>")
add_dependencies(rolltui-files-only-test rolltui-studio)

# Phase 16 m6: THE PURE-C CONSUMER — the claim this library had never executed once. Not
# `add_rolltui_test`, because that function's `target_include_directories(… tests)` would put
# the C++ harness on the path and this file must not be able to reach it: it includes
# `rolltui/rolltui.h` and nothing else of the library's, which is the property under test.
#
# **THE TARGET'S LANGUAGE IS THE CHECK.** A C++ compiler accepting a `.c` file proves nothing —
# under `__cplusplus` every struct in these headers gains constructors and `RolltuiStr` gains
# conversions, so a missing C-callable entry point is silently supplied by a method. CMake
# already infers C from the extension; `LANGUAGE C` says it rather than relying on the
# inference, and the `#error` at the top of the file is the second, independent guard.
add_executable(rolltui-c-consumer-test tests/c_consumer_test.c)
set_source_files_properties(tests/c_consumer_test.c PROPERTIES LANGUAGE C)
set_target_properties(rolltui-c-consumer-test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(rolltui-c-consumer-test PRIVATE rolltui)
target_compile_options(rolltui-c-consumer-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME rolltui-c-consumer-test COMMAND rolltui-c-consumer-test)
