# --- testkit: the test module both sides depend on ----------------------------
#
# IT BELONGS TO NEITHER SIDE, and that is a build property rather than a naming convention: it
# links nothing, includes nothing of roll's and nothing of rolltui's, and sits at the repository
# root so neither tree owns it. A shared LEAF that both depend on is not an upward dependency,
# which is what lets `rolltui` keep the property that it builds with roll's include/ and src/
# deleted — asserted by `rolltui-boundary-test`, which was written before any of this moved.
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
add_executable(testkit-test testkit_test.cpp)
target_link_libraries(testkit-test PRIVATE testkit)
target_compile_options(testkit-test PRIVATE -Wall -Wextra)
add_test(NAME testkit-test COMMAND testkit-test)
