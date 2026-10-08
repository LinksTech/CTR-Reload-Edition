# run_char_native_selftest.cmake - one ctest step: rldpack writes the test
# characters of the native model (CNET, CTXT; preview), then the game judges
# them.
#
#   cmake -DPACKER=<rldpack> -DGAME=<ctr_native> -DDIR=<folder> [-DESCAPE=<folder>]
#         -P cmake/run_char_native_selftest.cmake
#
# 1. <folder> (inside the build folder) is removed and created empty - a file
#    left over from an older run must never be judged.
# 2. <PACKER> make-native-tests <folder> writes old_*, none_*, good_*, bad_*
#    and damaged_* characters into it (tools/rldpack_native_test.inc).
# 3. <GAME> --dev --char-native-selftest <folder> reads every file through the
#    roster read and the native read without and with --native-preview and
#    exits 0 only if each did what its name says (platform/native_chars.c).
#
# No window, no game data, no GPU. Used by the test char_native_selftest in
# CMakeLists.txt; the output of both runs is passed through.

foreach(required PACKER GAME DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run_char_native_selftest.cmake: -D${required}=... is missing")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")

execute_process(
    COMMAND "${PACKER}" make-native-tests "${DIR}"
    RESULT_VARIABLE makeResult
    OUTPUT_VARIABLE makeOut
    ERROR_VARIABLE makeOut
)
message("${makeOut}")
if(NOT makeResult EQUAL 0)
    message(FATAL_ERROR "run_char_native_selftest.cmake: rldpack make-native-tests failed (exit ${makeResult})")
endif()

# 2b. The build-folder lock judges the resolved path, not its text: ESCAPE
#     ("<build>/../tools") names a folder outside the build folder and must
#     be refused (exit 2), and not one file may land there.
if(DEFINED ESCAPE AND NOT "${ESCAPE}" STREQUAL "")
    set(escapeBefore "")
    if(EXISTS "${ESCAPE}")
        file(GLOB escapeBefore "${ESCAPE}/*.rldchar")
    endif()
    execute_process(
        COMMAND "${PACKER}" make-native-tests "${ESCAPE}"
        RESULT_VARIABLE escapeResult
        OUTPUT_VARIABLE escapeOut
        ERROR_VARIABLE escapeOut
    )
    message("${escapeOut}")
    if(NOT escapeResult EQUAL 2)
        message(FATAL_ERROR "run_char_native_selftest.cmake: make-native-tests ${ESCAPE} was not refused (exit ${escapeResult})")
    endif()
    if(EXISTS "${ESCAPE}")
        file(GLOB escapeAfter "${ESCAPE}/*.rldchar")
        if(NOT "${escapeAfter}" STREQUAL "${escapeBefore}")
            message(FATAL_ERROR "run_char_native_selftest.cmake: make-native-tests wrote into ${ESCAPE}")
        endif()
    endif()
    message("char native selftest: make-native-tests refused ${ESCAPE} (outside the build folder)")
endif()

execute_process(
    COMMAND "${GAME}" --dev --char-native-selftest "${DIR}"
    RESULT_VARIABLE testResult
    OUTPUT_VARIABLE testOut
    ERROR_VARIABLE testOut
)
message("${testOut}")
if(NOT testResult EQUAL 0)
    message(FATAL_ERROR "run_char_native_selftest.cmake: --char-native-selftest reported a failure (exit ${testResult})")
endif()
