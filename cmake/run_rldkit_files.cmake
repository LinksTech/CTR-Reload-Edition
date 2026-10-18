# run_rldkit_files.cmake - rldpack kit-standin and kit-extract on files (ctest
# rldkit_files). No game data: the stand-in is made in code.
#
#   cmake -DPACKER=<rldpack> -DDIR=<folder in the build tree> -P cmake/run_rldkit_files.cmake
#
# 1. <folder> is removed and created empty.
# 2. kit-standin writes <folder>/BIGFILE.BIG; kit-extract --game <folder> reads
#    it like the game's own and writes the six kit files.
# 3. kit-kart.glb is checked with the game's rules in PSX mode: no error, the
#    kit mark said (HRG-41); kit-kart-gltf.glb
#    in glTF mode without HRG-36; the wheel opens without an error.
# 4. A second kit-extract gives the same bytes; driver 15 and a missing folder
#    of game data are refused.
# Prints "rldkit files passed: <n> checks" at the end, FATAL_ERROR on the first
# failure.

foreach(required PACKER DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run_rldkit_files.cmake: -D${required}=... is missing")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}/game")
file(MAKE_DIRECTORY "${DIR}/kit")
file(MAKE_DIRECTORY "${DIR}/again")
set(checks 0)

function(pack prefix)
    execute_process(COMMAND "${PACKER}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    set(${prefix}_RESULT "${result}" PARENT_SCOPE)
    set(${prefix}_OUT "${out}" PARENT_SCOPE)
endfunction()

macro(fail what)
    message(FATAL_ERROR "rldkit files FAILED: ${what}\n${LAST_OUT}")
endmacro()

pack(s kit-standin -o "${DIR}/game")
set(LAST_OUT "${s_OUT}")
if(NOT (s_RESULT EQUAL 0 AND EXISTS "${DIR}/game/BIGFILE.BIG"))
    fail("kit-standin writes BIGFILE.BIG")
endif()
math(EXPR checks "${checks} + 1")

pack(k kit-extract --game "${DIR}/game" -o "${DIR}/kit")
set(LAST_OUT "${k_OUT}")
if(NOT (k_RESULT EQUAL 0))
    fail("kit-extract of the stand-in")
endif()
math(EXPR checks "${checks} + 1")
foreach(name kit-kart.glb kit-kart-gltf.glb kit-wheel.glb kit-wheel-gltf.glb kit-info.json kit-extract.txt)
    if(NOT EXISTS "${DIR}/kit/${name}")
        fail("kit-extract writes ${name}")
    endif()
    math(EXPR checks "${checks} + 1")
endforeach()
string(FIND "${k_OUT}" "12 retail triangles" at)
if(NOT (at GREATER -1))
    fail("the report names the stand-in's 12 kart triangles")
endif()
math(EXPR checks "${checks} + 1")

pack(c hires-check "${DIR}/kit/kit-kart.glb" --vertex-colors psx)
set(LAST_OUT "${c_OUT}")
string(FIND "${c_OUT}" "HRG-41" at41)
string(FIND "${c_OUT}" "hires check: 0 errors" atSum)
if(NOT (c_RESULT EQUAL 0 AND at41 GREATER -1 AND atSum GREATER -1))
    fail("kit-kart.glb: no error, the kit mark (HRG-41)")
endif()
math(EXPR checks "${checks} + 1")
pack(c hires-check "${DIR}/kit/kit-kart-gltf.glb")
set(LAST_OUT "${c_OUT}")
string(FIND "${c_OUT}" "HRG-36" at36)
if(NOT (c_RESULT EQUAL 0 AND at36 EQUAL -1))
    fail("kit-kart-gltf.glb in glTF mode: no error, no HRG-36")
endif()
math(EXPR checks "${checks} + 1")
pack(c hires-check "${DIR}/kit/kit-wheel.glb" --vertex-colors psx)
set(LAST_OUT "${c_OUT}")
string(FIND "${c_OUT}" "hires check: 0 errors" atSum)
if(NOT (c_RESULT EQUAL 0 AND atSum GREATER -1))
    fail("kit-wheel.glb opens without an error")
endif()
math(EXPR checks "${checks} + 1")

pack(k kit-extract --game "${DIR}/game" -o "${DIR}/again")
set(LAST_OUT "${k_OUT}")
foreach(name kit-kart.glb kit-wheel.glb kit-info.json)
    file(SHA256 "${DIR}/kit/${name}" h1)
    file(SHA256 "${DIR}/again/${name}" h2)
    if(NOT (h1 STREQUAL h2))
        fail("${name}: the same game data gives the same bytes")
    endif()
    math(EXPR checks "${checks} + 1")
endforeach()

pack(x kit-extract --game "${DIR}/game" --driver 15 -o "${DIR}/kit")
set(LAST_OUT "${x_OUT}")
if(NOT (x_RESULT EQUAL 2))
    fail("driver 15 (no kart) is a usage error")
endif()
math(EXPR checks "${checks} + 1")
pack(x kit-extract --game "${DIR}/no-such-folder" -o "${DIR}/kit")
set(LAST_OUT "${x_OUT}")
if(NOT (x_RESULT EQUAL 1))
    fail("a folder without game data is refused")
endif()
math(EXPR checks "${checks} + 1")

message("rldkit files passed: ${checks} checks")
