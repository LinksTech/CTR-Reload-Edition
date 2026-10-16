# collision_gen_container.cmake - one ctest step: the collision model
# generator (tools/collision_gen) builds a track from a procedural test model,
# rldpack packs it, and the game's own container checks judge it.
#
#   cmake -DPYTHON=<python> -DSRC=<source dir> -DRLDPACK=<rldpack> -DGAME=<ctr_native>
#         -DDIR=<folder> -P cmake/collision_gen_container.cmake
#
# 1. <folder> is removed and created empty.
# 2. make_test_track.py writes the test model ring_c (three sections joined by
#    portals); collision_gen.py builds LEV and VRM from it (and refuses to write
#    them when one of its own checks fails).
# 3. rldpack make packs good-collgen.rldtrack. A copy of the LEV with one
#    quadblock naming a restart point the level does not have is packed as
#    bad-collgen-restart.rldtrack.
# 4. <GAME> --dev --selftest-containers runs every check the game runs when it
#    loads a container (no window, no game data): the good file must load, the
#    bad one must be refused.

foreach(required PYTHON SRC RLDPACK GAME DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "collision_gen_container.cmake: -D${required}=... is missing")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}/model" "${DIR}/good" "${DIR}/bad" "${DIR}/containers")

function(run_step what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    message("${out}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "collision_gen_container.cmake: ${what} failed (exit ${result})")
    endif()
endfunction()

set(TOOLS "${SRC}/tools/collision_gen")
run_step("make_test_track" "${PYTHON}" "${TOOLS}/make_test_track.py" "${DIR}/model" ring_c)
run_step("collision_gen" "${PYTHON}" "${TOOLS}/collision_gen.py"
    --glb "${DIR}/model/ring_c.glb" --classes "${DIR}/model/ring_c_classes.tsv"
    --start "${DIR}/model/ring_c_start.txt" --route "${DIR}/model/ring_c_route.tsv"
    --out "${DIR}/good" --name collgen)

# The bad copy: quadblock 0 names restart point 250, which the level does not have.
file(COPY "${DIR}/good/collgen.vrm" DESTINATION "${DIR}/bad")
# (no semicolons in the script: CMake would split the argument there)
set(BAD_COPY "import struct, sys
b = bytearray(open(sys.argv[1], 'rb').read())
mesh = struct.unpack_from('<I', b, 4)[0]
quads = struct.unpack_from('<I', b, 4 + mesh + 12)[0]
b[4 + quads + 0x3E] = 250
open(sys.argv[2], 'wb').write(b)
")
run_step("bad copy" "${PYTHON}" -c "${BAD_COPY}" "${DIR}/good/collgen.lev" "${DIR}/bad/collgen.lev")
file(REMOVE "${DIR}/good/collgen_report.json" "${DIR}/good/collgen_centreline.tsv")

run_step("rldpack make (good)" "${RLDPACK}" make "${DIR}/good" --name "collision test" --modes race
    --out "${DIR}/containers/good-collgen.rldtrack")
run_step("rldpack make (bad)" "${RLDPACK}" make "${DIR}/bad" --name "collision test" --modes race
    --out "${DIR}/containers/bad-collgen-restart.rldtrack")
run_step("the game's container checks" "${GAME}" --dev --selftest-containers "${DIR}/containers")
