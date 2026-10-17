# run_rldhires_files.cmake - rldpack hires-check, hires-add and hires-strip on
# real files (ctest rldhires_files).
#
#   cmake -DPACKER=<rldpack> -DDIR=<folder in the build tree> -P cmake/run_rldhires_files.cmake
#
# 1. <folder> is removed and created empty.
# 2. rldpack make-native-tests <folder> writes the test characters (old_plain:
#    CHRI + CMDL only; good_probe-poses: with CNET and CTXT, chunks the
#    envelope does not know), rldpack hires-test-files <folder> the models
#    hires-good.glb (no finding), hires-warn.glb (warnings) and hires-bad.glb (an
#    error).
# 3. For both characters: hires-add twice gives the same bytes; hires-strip of
#    it is the character again, byte for byte (so every other chunk and its
#    directory entry stayed as it was); hires-add on a file with CHRG replaces
#    it (stripped again: the character); verify passes; hires-check reads the
#    chunk back; hires-add of the bad model fails and writes nothing.
# Prints "rldhires files passed: <n> checks" at the end, FATAL_ERROR on the
# first failure.

foreach(required PACKER DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run_rldhires_files.cmake: -D${required}=... is missing")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
set(checks 0)

# Runs rldpack with the arguments; result in <prefix>_RESULT, output in <prefix>_OUT.
function(pack prefix)
    execute_process(COMMAND "${PACKER}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    set(${prefix}_RESULT "${result}" PARENT_SCOPE)
    set(${prefix}_OUT "${out}" PARENT_SCOPE)
endfunction()

# A failed check ends the run with the output of the last rldpack call.
macro(fail what)
    message(FATAL_ERROR "rldhires files FAILED: ${what}\n${LAST_OUT}")
endmacro()

pack(make make-native-tests "${DIR}")
set(LAST_OUT "${make_OUT}")
if(NOT (make_RESULT EQUAL 0))
    fail("make-native-tests")
endif()
math(EXPR checks "${checks} + 1")
pack(glb hires-test-files "${DIR}")
set(LAST_OUT "${glb_OUT}")
if(NOT (glb_RESULT EQUAL 0))
    fail("hires-test-files")
endif()
math(EXPR checks "${checks} + 1")

# The models alone.
pack(c hires-check "${DIR}/hires-good.glb")
set(LAST_OUT "${c_OUT}")
if(NOT (c_RESULT EQUAL 0))
    fail("hires-check of the good model exits 0")
endif()
math(EXPR checks "${checks} + 1")
string(FIND "${c_OUT}" "hires check: 0 errors, 0 warnings, 0 notes, clips 12/12, wheels 4/4, skin 2 joints, 5 triangles, textures 1 (1024 bytes RGBA8)" at)
if(NOT (at GREATER -1))
    fail("the summary line of the good model")
endif()
math(EXPR checks "${checks} + 1")
pack(c hires-check "${DIR}/hires-warn.glb")
set(LAST_OUT "${c_OUT}")
if(NOT (c_RESULT EQUAL 0))
    fail("hires-check of a model with warnings only exits 0")
endif()
math(EXPR checks "${checks} + 1")
string(FIND "${c_OUT}" "hires check: 0 errors, 2 warnings, 0 notes, clips 11/12, wheels 0/4" at)
if(NOT (at GREATER -1))
    fail("the summary line of the model with warnings")
endif()
math(EXPR checks "${checks} + 1")
pack(c hires-check "${DIR}/hires-bad.glb")
set(LAST_OUT "${c_OUT}")
if(NOT (c_RESULT EQUAL 1))
    fail("hires-check of a model with an error exits 1")
endif()
math(EXPR checks "${checks} + 1")
string(FIND "${c_OUT}" "HRG-16" at)
if(NOT (at GREATER -1))
    fail("the bad model names its rule")
endif()
math(EXPR checks "${checks} + 1")

foreach(base old_plain good_probe-poses)
    set(in "${DIR}/${base}.rldchar")
    file(SHA256 "${in}" baseHash)

    pack(c hires-check "${in}")
    set(LAST_OUT "${c_OUT}")
    if(NOT (c_RESULT EQUAL 1))
        fail("${base}: hires-check without CHRG exits 1")
    endif()
    math(EXPR checks "${checks} + 1")

    pack(a hires-add "${in}" "${DIR}/hires-good.glb" -o "${DIR}/${base}-a.rldchar")
    set(LAST_OUT "${a_OUT}")
    if(NOT (a_RESULT EQUAL 0))
        fail("${base}: hires-add")
    endif()
    math(EXPR checks "${checks} + 1")
    pack(a hires-add "${in}" "${DIR}/hires-good.glb" -o "${DIR}/${base}-b.rldchar")
    if(NOT (a_RESULT EQUAL 0))
        fail("${base}: hires-add a second time")
    endif()
    math(EXPR checks "${checks} + 1")
    file(SHA256 "${DIR}/${base}-a.rldchar" hashA)
    file(SHA256 "${DIR}/${base}-b.rldchar" hashB)
    if(NOT (hashA STREQUAL hashB))
        fail("${base}: the same input gives the same bytes")
    endif()
    math(EXPR checks "${checks} + 1")
    if(NOT (NOT hashA STREQUAL baseHash))
        fail("${base}: hires-add changed the file")
    endif()
    math(EXPR checks "${checks} + 1")

    pack(v verify "${DIR}/${base}-a.rldchar")
    set(LAST_OUT "${v_OUT}")
    if(NOT (v_RESULT EQUAL 0))
        fail("${base}: verify passes with CHRG")
    endif()
    math(EXPR checks "${checks} + 1")

    pack(c hires-check "${DIR}/${base}-a.rldchar")
    set(LAST_OUT "${c_OUT}")
    if(NOT (c_RESULT EQUAL 0))
        fail("${base}: hires-check of the packed CHRG exits 0")
    endif()
    math(EXPR checks "${checks} + 1")
    string(FIND "${c_OUT}" "clips 12/12, wheels 4/4, skin 2 joints, 5 triangles" at)
    if(NOT (at GREATER -1))
        fail("${base}: hires-check reads the chunk back")
    endif()
    math(EXPR checks "${checks} + 1")

    pack(s hires-strip "${DIR}/${base}-a.rldchar" -o "${DIR}/${base}-s.rldchar")
    set(LAST_OUT "${s_OUT}")
    if(NOT (s_RESULT EQUAL 0))
        fail("${base}: hires-strip")
    endif()
    math(EXPR checks "${checks} + 1")
    file(SHA256 "${DIR}/${base}-s.rldchar" hashS)
    if(NOT (hashS STREQUAL baseHash))
        fail("${base}: hires-strip of hires-add is the character byte for byte")
    endif()
    math(EXPR checks "${checks} + 1")

    pack(r hires-add "${DIR}/${base}-a.rldchar" "${DIR}/hires-warn.glb" -o "${DIR}/${base}-r.rldchar")
    set(LAST_OUT "${r_OUT}")
    if(NOT (r_RESULT EQUAL 0))
        fail("${base}: hires-add over an existing CHRG")
    endif()
    math(EXPR checks "${checks} + 1")
    pack(c hires-check "${DIR}/${base}-r.rldchar")
    set(LAST_OUT "${c_OUT}")
    string(FIND "${c_OUT}" "clips 11/12, wheels 0/4" at)
    if(NOT (at GREATER -1))
        fail("${base}: the CHRG was replaced")
    endif()
    math(EXPR checks "${checks} + 1")
    pack(s hires-strip "${DIR}/${base}-r.rldchar" -o "${DIR}/${base}-rs.rldchar")
    file(SHA256 "${DIR}/${base}-rs.rldchar" hashRS)
    if(NOT (hashRS STREQUAL baseHash))
        fail("${base}: after a replace every other chunk is still the character's")
    endif()
    math(EXPR checks "${checks} + 1")

    pack(x hires-add "${in}" "${DIR}/hires-bad.glb" -o "${DIR}/${base}-x.rldchar")
    set(LAST_OUT "${x_OUT}")
    if(NOT (x_RESULT EQUAL 1))
        fail("${base}: hires-add of a model with an error exits 1")
    endif()
    math(EXPR checks "${checks} + 1")
    if(NOT (NOT EXISTS "${DIR}/${base}-x.rldchar"))
        fail("${base}: hires-add of a model with an error writes nothing")
    endif()
    math(EXPR checks "${checks} + 1")
endforeach()

message("rldhires files passed: ${checks} checks")
