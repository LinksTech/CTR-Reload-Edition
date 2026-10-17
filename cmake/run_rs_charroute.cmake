# run_rs_charroute.cmake - the build chain of the guided flow "Build a
# character" (Reload Studio, page Character, routes Low-Poly and Hi-Res)
# against rldpack, byte for byte (ctest reloadstudio_charroute).
#
#   cmake -DPACKER=<rldpack> -DSTUDIO=<ReloadStudio> -DTEST=<rs_hires_selftest>
#         -DDIR=<folder in the build tree> -P cmake/run_rs_charroute.cmake
#
# The page builds a CHRG route as a chain of rldpack commands, started through
# Reload Studio's --rldpack (rs_charroute.h, decision E6 of the round's plan):
#   make-char --model <glb> <the page's switches> <RsCharHires_FallbackArgs>
#             [--wheels off when the model has its own four wheels] --out <classic>
#   [hires-upscale <glb> -o <upscaled>]                        Low-Poly ("Upscale small textures")
#   hires-add <classic> <glb or upscaled> -o <out>
# This script runs that chain twice - once with rldpack, once with the Studio
# and the page's --machine - and wants the same SHA-256 at every step:
#   1. <folder> is emptied; rs_hires_selftest --write makes the karts (made in
#      code, no community content): route-bare.glb (an armature, no clips, no
#      wheel nodes, a 16 x 16 texture) and route-wheels.glb (an armature, 12
#      clips, its own 4 wheels), and the fallback's switches per route as
#      RsCharHires_FallbackArgs gives them (fallback-<route>.txt, a CMake list).
#   2. Hi-Res: route-bare.glb; Low-Poly: route-bare.glb with hires-upscale;
#      Hi-Res with wheels: route-wheels.glb with --wheels off.
#   3. Every result holds the classic model and the hi-res model (rldpack
#      info: chunks CHRI CMDL CHRG).
# Prints "charroute passed: <route> sha256 <hash>" per route and
# "charroute passed: 3 routes" at the end.

foreach(required PACKER STUDIO TEST DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run_rs_charroute.cmake: -D${required}=... is missing")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")

macro(run what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "charroute FAILED: ${what} (exit ${result})\n${out}")
    endif()
endmacro()

# Both files there and the same bytes.
macro(same what a b)
    if(NOT EXISTS "${a}" OR NOT EXISTS "${b}")
        message(FATAL_ERROR "charroute FAILED: ${what}: a file is missing (${a}, ${b})")
    endif()
    file(SHA256 "${a}" hashA)
    file(SHA256 "${b}" hashB)
    if(NOT hashA STREQUAL hashB)
        message(FATAL_ERROR "charroute FAILED: ${what}: the Studio wrote other bytes than rldpack (${hashB} against ${hashA})")
    endif()
endmacro()

run("rs_hires_selftest --write" "${TEST}" --write "${DIR}")
file(READ "${DIR}/fallback-lowpoly.txt" fallbackLowpoly)
file(READ "${DIR}/fallback-hires.txt" fallbackHires)
if(NOT fallbackLowpoly MATCHES "--reduce;auto" OR NOT fallbackHires MATCHES "--reduce;auto")
    message(FATAL_ERROR "charroute FAILED: the fallback's switches lack --reduce auto (${fallbackLowpoly} / ${fallbackHires})")
endif()

# The page's own make-char switches (rs_char.c, Char_MakeArgs, its defaults).
set(page --name "ROUTE TEST" --template 14 --class balanced --size 100)

# One route: name, model, fallback switches, extra switches (a list, may be
# empty), upscale (ON/OFF).
function(route name model fallback extra upscale)
    set(src "${DIR}/${model}")
    # The classic fallback.
    run("rldpack make-char (${name})" "${PACKER}" make-char --model "${src}" ${page} ${fallback} ${extra} --out "${DIR}/p-${name}.classic.part")
    run("Reload Studio --rldpack make-char (${name})" "${STUDIO}" --rldpack make-char --machine --model "${src}" ${page} ${fallback} ${extra}
        --out "${DIR}/s-${name}.classic.part")
    string(FIND "${out}" "@msg\terror" at)
    if(NOT at LESS 0)
        message(FATAL_ERROR "charroute FAILED: the Studio's make-char (${name}) said an error\n${out}")
    endif()
    same("the classic fallback (${name})" "${DIR}/p-${name}.classic.part" "${DIR}/s-${name}.classic.part")
    # "Upscale small textures".
    set(packP "${src}")
    set(packS "${src}")
    if(upscale)
        run("rldpack hires-upscale (${name})" "${PACKER}" hires-upscale "${src}" -o "${DIR}/p-${name}-upscaled.glb")
        run("Reload Studio --rldpack hires-upscale (${name})" "${STUDIO}" --rldpack hires-upscale "${src}" -o "${DIR}/s-${name}-upscaled.glb" --machine)
        same("the upscaled model (${name})" "${DIR}/p-${name}-upscaled.glb" "${DIR}/s-${name}-upscaled.glb")
        set(packP "${DIR}/p-${name}-upscaled.glb")
        set(packS "${DIR}/s-${name}-upscaled.glb")
    endif()
    # The hi-res model into the classic one.
    run("rldpack hires-add (${name})" "${PACKER}" hires-add "${DIR}/p-${name}.classic.part" "${packP}" -o "${DIR}/p-${name}.rldchar")
    run("Reload Studio --rldpack hires-add (${name})" "${STUDIO}" --rldpack hires-add "${DIR}/s-${name}.classic.part" "${packS}" -o "${DIR}/s-${name}.rldchar" --machine)
    string(FIND "${out}" "@written" at)
    if(at LESS 0)
        message(FATAL_ERROR "charroute FAILED: the Studio's hires-add (${name}) wrote no @written line\n${out}")
    endif()
    same("the character (${name})" "${DIR}/p-${name}.rldchar" "${DIR}/s-${name}.rldchar")
    # The classic model and the hi-res model in it.
    run("rldpack info (${name})" "${PACKER}" info "${DIR}/s-${name}.rldchar")
    if(NOT out MATCHES "chunks +CHRI CMDL CHRG")
        message(FATAL_ERROR "charroute FAILED: ${name}: the character lacks CMDL or CHRG\n${out}")
    endif()
    file(SHA256 "${DIR}/s-${name}.rldchar" hash)
    message("charroute passed: ${name} sha256 ${hash}")
endfunction()

route(hires route-bare.glb "${fallbackHires}" "" OFF)
route(lowpoly route-bare.glb "${fallbackLowpoly}" "" ON)
route(hires-wheels route-wheels.glb "${fallbackHires}" "--wheels;off" OFF)
message("charroute passed: 3 routes")
