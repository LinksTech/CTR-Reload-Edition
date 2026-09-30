# run_selftest.cmake - one ctest step: the game writes made-up input, then
# judges it.
#
#   cmake -DGAME=<ctr_native> -DDIR=<folder> -DMAKE_SWITCH=<switch>
#         -DTEST_SWITCH=<switch> -P cmake/run_selftest.cmake
#
# 1. <folder> is removed and created empty - a file left over from an older
#    run must never be judged.
# 2. <GAME> --dev <MAKE_SWITCH> <folder> writes good-* and bad-* files into it.
# 3. <GAME> --dev <TEST_SWITCH> <folder> judges every file and exits 0 only if
#    every good-* file was accepted and every bad-* file refused.
#
# Both switches run before any window, audio or asset initialisation (no GPU,
# no disc image, no game data). Used by the tests selftest_bad_containers and
# selftest_bad_disc in CMakeLists.txt. The output of both runs is passed
# through, so a failed test shows which file was UNEXPECTED.

foreach(required GAME DIR MAKE_SWITCH TEST_SWITCH)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run_selftest.cmake: -D${required}=... is missing")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")

execute_process(
    COMMAND "${GAME}" --dev ${MAKE_SWITCH} "${DIR}"
    RESULT_VARIABLE makeResult
    OUTPUT_VARIABLE makeOut
    ERROR_VARIABLE makeOut
)
message("${makeOut}")
if(NOT makeResult EQUAL 0)
    message(FATAL_ERROR "run_selftest.cmake: ${MAKE_SWITCH} failed (exit ${makeResult})")
endif()

execute_process(
    COMMAND "${GAME}" --dev ${TEST_SWITCH} "${DIR}"
    RESULT_VARIABLE testResult
    OUTPUT_VARIABLE testOut
    ERROR_VARIABLE testOut
)
message("${testOut}")
if(NOT testResult EQUAL 0)
    message(FATAL_ERROR "run_selftest.cmake: ${TEST_SWITCH} reported a failure (exit ${testResult})")
endif()
