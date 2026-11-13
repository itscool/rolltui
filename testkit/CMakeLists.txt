# --- testkit: the negative-control harness rolltui's suite is built on --------------------------
#
# A LEAF: it links nothing and includes nothing but the C library, so anything may depend on it
# and it depends on nothing back. The library's own sources include its one header as
# `rolltui/testkit/testctl.h`, the spelling that resolves in any host that compiles them
# (`rolltui-boundary-test` holds every library include to that), and a host that keeps its own
# testkit target is not given a second one.
#
# TWO BUILDS OF THE SAME SOURCES, the shape roll already uses for `roll_core`:
#   * `testkit`     — what every ordinary suite links. `testkit_ctl_on` is a MACRO yielding 0,
#                     so a control name is never even an argument and cannot reach the artifact.
#                     `testctl.c` is not compiled into it at all: there is no table to reach.
#   * `testkit_ctl` — adds TESTKIT_CONTROL and the table. Only a suite that FLIPS controls links
#                     it, and it propagates the define PUBLIC so `check_controlled` exists there
#                     and nowhere else.
add_library(testkit STATIC testkit.c)
add_library(testkit_ctl STATIC testkit.c testctl.c)
foreach(lib testkit testkit_ctl)
  target_include_directories(${lib} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_options(${lib} PRIVATE -Wall -Wextra)
endforeach()
target_compile_definitions(testkit_ctl PUBLIC TESTKIT_CONTROL=1)

# THE MODULE'S OWN SUITE, and it has no explicit calls in `main()` on purpose: every assertion is
# inside a TESTKIT_TEST body, so a broken self-registration makes this suite run ZERO assertions
# and the zero-assertion guard fails it. The two mechanisms catch each other's absence, which is
# what lets both be trusted without a hand-run control.
# A project that carries this module for its own suites and does not test the module itself sets
# TESTKIT_BUILD_TESTS OFF first; unset, the suite is built and registered.
if(NOT DEFINED TESTKIT_BUILD_TESTS OR TESTKIT_BUILD_TESTS)
  add_executable(testkit-test testkit_test.cpp)
  target_link_libraries(testkit-test PRIVATE testkit)
  target_compile_options(testkit-test PRIVATE -Wall -Wextra)
  add_test(NAME testkit-test COMMAND testkit-test)
endif()
