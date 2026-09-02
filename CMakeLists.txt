# rolltui — a small terminal-UI library of our own (plan/phase-9.md, requirement 9).
#
# Header-first: every pure component is one header under rolltui/; the compiled units
# (Terminal.cpp, vendored md4c) arrive in later milestones. `rolltui` is therefore an
# INTERFACE target today. Its include directory is the REPO ROOT so consumers and the
# headers themselves write `#include "rolltui/Unicode.hpp"` — and nothing else: roll's
# own `include/` is never on this target's path, and the tests below build with only
# this directory and rolltui/tests. That is the whole of Done-when (g).
#
# Tests use their own 30-line harness (rolltui/tests/rolltui_test.hpp), not roll's
# tests/test_util.hpp, for the same reason.

add_library(rolltui INTERFACE)
target_include_directories(rolltui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(rolltui INTERFACE cxx_std_23)

set(ROLLTUI_UCD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ucd)

function(add_rolltui_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE rolltui)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  target_compile_definitions(${name} PRIVATE ROLLTUI_UCD_DIR="${ROLLTUI_UCD_DIR}")
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# Milestone 2: the Unicode conformance suites, in full, and the width table.
add_rolltui_test(rolltui-grapheme-break-test tests/grapheme_break_test.cpp)
add_rolltui_test(rolltui-line-break-test tests/line_break_test.cpp)
add_rolltui_test(rolltui-width-test tests/width_test.cpp)
