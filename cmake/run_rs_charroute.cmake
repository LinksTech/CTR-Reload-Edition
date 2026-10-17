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
#   [hires-upscale <glb> -o <upscaled>]                        ("Upscale small textures")
#   hires-add <classic> <glb or upscaled> -o <out> [--vertex-colors <mode>]
#                       (the mode only for a model with COLOR_0: the switch
#                       "Vertex color blending", include/rldhires_color.inc)
# This script runs that chain twice - once with rldpack, once with the Studio
# and the page's --machine - and wants the same SHA-256 at every step:
#   1. <folder> is emptied; rs_hires_selftest --write makes the karts (made in
#      code, no community content): route-bare.glb (an armature, no clips, no
#      wheel nodes, a 16 x 16 texture), route-tex64.glb (route-bare with a
#      64 x 64 texture), route-wheels.glb (an armature, 12 clips, its own 4
#      wheels) and route-vcol.glb (route-bare with PS1-like COLOR_0, grey
#      128), and the fallback's switches per route as RsCharHires_FallbackArgs
#      gives them (fallback-<route>.txt, a CMake list).
#   2. Hi-Res: route-bare.glb without and with hires-upscale; Low-Poly:
#      route-bare.glb with hires-upscale, route-tex64.glb without; Hi-Res
#      with wheels: route-wheels.glb with --wheels off.
#   3. Every result holds the classic model and the hi-res model (rldpack
#      info: chunks CHRI CMDL CHRG).
#   4. The vertex colours: route-vcol.glb on Hi-Res with --vertex-colors psx,
#      off and gltf; rldpack hires-check of each result names its mode, and
#      gltf is the bytes of the pack without the switch.
# Prints "charroute passed: <route> sha256 <hash>" per route,
# "charroute passed: vertex colours psx off gltf" and "charroute passed: 3
# routes" at the end.
#
# With -DPAGE=ON (ctest reloadstudio_charroute_page) every route is built a
# third time by the page Character itself, in automation as an author clicks
# it - page char, route <word>, model, name, out, build - and the page's file
# must be rldpack's chain byte for byte; the classic fallback included (the
# page's own switches, RsCharHires_FallbackArgs and --wheels off for a model
# with its own wheels). "Upscale small textures" is off at the start of a
# route and suggested by the page only when every base colour texture is under
# 64 px (rs_charroute.h, THE UPSCALE): route-bare.glb (16 px) gets it on
# Low-Poly and Hi-Res - the bytes of the chain with hires-upscale -, with
# "upscale off" (the author's choice) the bytes without; route-tex64.glb
# (64 px) is built without it. The vertex colours too: route-vcol.glb as the
# page suggests it (PSX: PS1-like colours), and with "vcolor off" and "vcolor
# gltf" - each the bytes of rldpack's chain with that --vertex-colors (these
# runs, and the one with wheels, say "upscale off" to keep the chains they
# compare with). Prints "charroute page passed: upscale suggested and chosen",
# "charroute page passed: vertex colours psx off gltf" and "charroute page
# passed: 3 routes".

foreach(required PACKER STUDIO TEST DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run_rs_charroute.cmake: -D${required}=... is missing")
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
set(page --name "ROUTE TEST" --template 14 --class balanced --size 100 --mask uka)

# One route: name, model, fallback switches, extra switches (a list, may be
# empty), upscale (ON/OFF); the switches of hires-add after -o <out> in the
# variable packExtra of the caller (a list, may be empty).
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
    run("rldpack hires-add (${name})" "${PACKER}" hires-add "${DIR}/p-${name}.classic.part" "${packP}" -o "${DIR}/p-${name}.rldchar" ${packExtra})
    run("Reload Studio --rldpack hires-add (${name})" "${STUDIO}" --rldpack hires-add "${DIR}/s-${name}.classic.part" "${packS}" -o "${DIR}/s-${name}.rldchar"
        ${packExtra} --machine)
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

# The page itself (PAGE=ON): the route word, the model, the file of rldpack's
# chain to compare with; verbs after the model in the variable pageDo of the
# caller (a list of --do arguments, may be empty).
function(page_route name word model want)
    set(log "${DIR}/page-${name}.log")
    execute_process(COMMAND "${STUDIO}" --settings "${DIR}/rs.ini" --log "${log}"
                            --do "page char" --do "route ${word}" --do "model ${DIR}/${model}" ${pageDo} --do "name ROUTE TEST"
                            --do "out ${DIR}/page-${name}.rldchar" --do "build" --do "quit"
                    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 240)
    set(text "")
    if(EXISTS "${log}")
        file(READ "${log}" text)
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "charroute FAILED: the page (${name}) ended with ${result}\n${out}\n${text}")
    endif()
    same("the page's character (${name})" "${DIR}/${want}" "${DIR}/page-${name}.rldchar")
    message("charroute page passed: ${name}")
endfunction()

set(packExtra "")
route(hires route-bare.glb "${fallbackHires}" "" OFF)
route(hires-upscaled route-bare.glb "${fallbackHires}" "" ON)
route(lowpoly route-bare.glb "${fallbackLowpoly}" "" ON)
route(lowpoly-tex64 route-tex64.glb "${fallbackLowpoly}" "" OFF)
route(hires-wheels route-wheels.glb "${fallbackHires}" "--wheels;off" OFF)

# The vertex colours: the same kart with COLOR_0 in every mode, and without
# the switch (= gltf, the bytes of before).
route(vcol-none route-vcol.glb "${fallbackHires}" "" OFF)
foreach(mode psx off gltf)
    set(packExtra --vertex-colors ${mode})
    route(vcol-${mode} route-vcol.glb "${fallbackHires}" "" OFF)
    run("rldpack hires-check (vcol-${mode})" "${PACKER}" hires-check "${DIR}/s-vcol-${mode}.rldchar")
    if(NOT out MATCHES "vertex colours: ${mode}")
        message(FATAL_ERROR "charroute FAILED: hires-check of vcol-${mode} does not name the mode ${mode}\n${out}")
    endif()
endforeach()
set(packExtra "")
same("--vertex-colors gltf is the pack without the switch" "${DIR}/p-vcol-none.rldchar" "${DIR}/p-vcol-gltf.rldchar")
file(SHA256 "${DIR}/p-vcol-psx.rldchar" hashPsx)
file(SHA256 "${DIR}/p-vcol-off.rldchar" hashOff)
file(SHA256 "${DIR}/p-vcol-gltf.rldchar" hashGltf)
if(hashPsx STREQUAL hashOff OR hashPsx STREQUAL hashGltf OR hashOff STREQUAL hashGltf)
    message(FATAL_ERROR "charroute FAILED: two vertex colour modes gave the same bytes")
endif()
message("charroute passed: vertex colours psx off gltf")
message("charroute passed: 3 routes")

# The page's log must say what it should.
function(page_says name pattern what)
    file(READ "${DIR}/page-${name}.log" text)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "charroute FAILED: ${what} (page ${name})\n${text}")
    endif()
endfunction()

if(PAGE)
    # "Upscale small textures": off at the start, suggested for the 16 px kart
    # on Low-Poly and on Hi-Res (the chain with hires-upscale), kept off for
    # the 64 px kart (the chain without), and off as the author's choice (the
    # chain without). The report says who set it.
    set(pageDo --do "report ${DIR}/page-lowpoly-report.txt")
    page_route(lowpoly lowpoly route-bare.glb p-lowpoly.rldchar)
    page_says(lowpoly "upscale small textures: on \\(the suggestion; suggested on, largest base colour texture 16 px\\)"
              "the page did not suggest the upscale for the 16 px kart on Low-Poly")
    file(READ "${DIR}/page-lowpoly-report.txt" text)
    if(NOT text MATCHES "upscale small textures: on \\(suggested\\), suggested on \\(1 base colour texture\\(s\\), largest 16 px in the file")
        message(FATAL_ERROR "charroute FAILED: the report of the page (lowpoly) does not say the suggestion\n${text}")
    endif()
    set(pageDo "")
    page_route(lowpoly-tex64 lowpoly route-tex64.glb p-lowpoly-tex64.rldchar)
    file(READ "${DIR}/page-lowpoly-tex64.log" text)
    if(text MATCHES "upscale small textures: on")
        message(FATAL_ERROR "charroute FAILED: the page suggested the upscale for the 64 px kart\n${text}")
    endif()
    page_route(hires hires route-bare.glb p-hires-upscaled.rldchar)
    page_says(hires "upscale small textures: on \\(the suggestion" "the page did not suggest the upscale for the 16 px kart on Hi-Res")
    set(pageDo --do "upscale off" --do "report ${DIR}/page-hires-off-report.txt")
    page_route(hires-off hires route-bare.glb p-hires.rldchar)
    page_says(hires-off "upscale: off \\(the author's choice; suggested on" "the page did not take the author's choice")
    file(READ "${DIR}/page-hires-off-report.txt" text)
    if(NOT text MATCHES "upscale small textures: off \\(chosen by the author\\), suggested on")
        message(FATAL_ERROR "charroute FAILED: the report of the page (hires-off) does not say the author's choice\n${text}")
    endif()
    message("charroute page passed: upscale suggested and chosen")
    # The kart with wheels and the vertex colours: their chains have no
    # upscale (the author says off).
    set(pageDo --do "upscale off")
    page_route(hires-wheels hires route-wheels.glb p-hires-wheels.rldchar)
    # The vertex colours: the suggestion (PSX: PS1-like), then the switch off
    # and glTF of Advanced - each rldpack's bytes with that mode.
    page_route(vcol-psx hires route-vcol.glb p-vcol-psx.rldchar)
    page_says(vcol-psx "vertex colors: psx \\(the suggestion" "the page did not suggest PSX for the PS1-like kart")
    set(pageDo --do "upscale off" --do "vcolor off")
    page_route(vcol-off hires route-vcol.glb p-vcol-off.rldchar)
    set(pageDo --do "upscale off" --do "vcolor gltf")
    page_route(vcol-gltf hires route-vcol.glb p-vcol-gltf.rldchar)
    set(pageDo "")
    message("charroute page passed: vertex colours psx off gltf")
    message("charroute page passed: 3 routes")
endif()

file(GLOB_RECURSE tempLeft LIST_DIRECTORIES true "${tempDir}/*")
if(tempLeft)
    message(FATAL_ERROR "charroute FAILED: files left in the run's TEMP ${tempDir}: ${tempLeft}")
endif()
