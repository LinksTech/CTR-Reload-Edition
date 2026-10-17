# run_rs_hires_selftest.cmake - the page "Hi-Res (Preview)" of Reload Studio
# against rldpack (ctest reloadstudio_hires_selftest).
#
#   cmake -DPACKER=<rldpack> -DSTUDIO=<ReloadStudio> -DTEST=<rs_hires_selftest>
#         -DDIR=<folder in the build tree> -P cmake/run_rs_hires_selftest.cmake
#
# 1. <folder> is removed and created empty; rldpack make-native-tests and
#    hires-test-files write a character (old_plain.rldchar) and the models.
# 2. rldpack hires-add packs hires-good.glb into the character (packer.rldchar);
#    Reload Studio packs the same with the arguments of its page
#    (RsHires_PackArgs: hires-add <character> <model> -o <out> --machine,
#    through --rldpack, as the page's button starts it) into studio.rldchar.
#    Both files must be byte for byte the same (SHA-256).
# 3. rs_hires_selftest <folder> checks the page's model side on these files.
# Prints "rs_hires files passed: sha256 <hash>" when all of it held.

foreach(required PACKER STUDIO TEST DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run_rs_hires_selftest.cmake: -D${required}=... is missing")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")

macro(run what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "rs_hires files FAILED: ${what} (exit ${result})\n${out}")
    endif()
endmacro()

run("make-native-tests" "${PACKER}" make-native-tests "${DIR}")
run("hires-test-files" "${PACKER}" hires-test-files "${DIR}")
run("rldpack hires-add" "${PACKER}" hires-add "${DIR}/old_plain.rldchar" "${DIR}/hires-good.glb" -o "${DIR}/packer.rldchar")
run("Reload Studio --rldpack hires-add" "${STUDIO}" --rldpack hires-add "${DIR}/old_plain.rldchar" "${DIR}/hires-good.glb" -o "${DIR}/studio.rldchar" --machine)
string(FIND "${out}" "@written" at)
if(at LESS 0)
    message(FATAL_ERROR "rs_hires files FAILED: the Studio's pack step wrote no @written line\n${out}")
endif()

file(SHA256 "${DIR}/packer.rldchar" packerHash)
file(SHA256 "${DIR}/studio.rldchar" studioHash)
if(NOT packerHash STREQUAL studioHash)
    message(FATAL_ERROR "rs_hires files FAILED: the Studio packed other bytes than rldpack (${studioHash} against ${packerHash})")
endif()

execute_process(COMMAND "${TEST}" "${DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
message("${out}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "rs_hires files FAILED: rs_hires_selftest (exit ${result})")
endif()
message("rs_hires files passed: sha256 ${packerHash}")
