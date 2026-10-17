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
# 2b. The option "Upscale small textures": rldpack hires-upscale and the Studio
#    with the page's arguments (RsHires_UpscaleArgs) enlarge hires-tex.glb into
#    packer-upscaled.glb and hires-tex-upscaled.glb - the same bytes, and the
#    source unchanged.
# 3. rs_hires_selftest <folder> checks the page's model side on these files.
# Prints "rs_hires files passed: sha256 <hash>" when all of it held.

foreach(required PACKER STUDIO TEST DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run_rs_hires_selftest.cmake: -D${required}=... is missing")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")

# rldpack's temporary files (the copies of a glTF's own images,
# tools/rldpack_gltf.inc; the reduction's cache, tools/rldpack_reduce.inc) go
# to TEMP, else TMP. The Studio's rldpack face (--rldpack) never reads
# --settings, so every program this script starts gets TEMP and TMP in a
# folder of this run below <folder>: two test runs at the same time never
# share one, and nothing of the test lands in the user's %TEMP%. At the end
# the folder must be empty - every program removed its own copies.
set(tempDir "${DIR}/temp")
file(MAKE_DIRECTORY "${tempDir}")
file(TO_NATIVE_PATH "${tempDir}" tempDirNative)
set(ENV{TEMP} "${tempDirNative}")
set(ENV{TMP} "${tempDirNative}")

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

file(SHA256 "${DIR}/hires-tex.glb" sourceBefore)
run("rldpack hires-upscale" "${PACKER}" hires-upscale "${DIR}/hires-tex.glb" -o "${DIR}/packer-upscaled.glb")
run("Reload Studio --rldpack hires-upscale" "${STUDIO}" --rldpack hires-upscale "${DIR}/hires-tex.glb" -o "${DIR}/hires-tex-upscaled.glb" --machine)
string(FIND "${out}" "@written" at)
if(at LESS 0)
    message(FATAL_ERROR "rs_hires files FAILED: the Studio's upscale step wrote no @written line\n${out}")
endif()
file(SHA256 "${DIR}/hires-tex.glb" sourceAfter)
file(SHA256 "${DIR}/packer-upscaled.glb" upPacker)
file(SHA256 "${DIR}/hires-tex-upscaled.glb" upStudio)
if(NOT sourceBefore STREQUAL sourceAfter)
    message(FATAL_ERROR "rs_hires files FAILED: hires-upscale changed its source model")
endif()
if(NOT upPacker STREQUAL upStudio)
    message(FATAL_ERROR "rs_hires files FAILED: the Studio upscaled to other bytes than rldpack (${upStudio} against ${upPacker})")
endif()

execute_process(COMMAND "${TEST}" "${DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
message("${out}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "rs_hires files FAILED: rs_hires_selftest (exit ${result})")
endif()
file(GLOB_RECURSE tempLeft LIST_DIRECTORIES true "${tempDir}/*")
if(tempLeft)
    message(FATAL_ERROR "rs_hires files FAILED: files left in the run's TEMP ${tempDir}: ${tempLeft}")
endif()
message("rs_hires files passed: sha256 ${packerHash}")
