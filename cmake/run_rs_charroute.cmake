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
#   [hires-png <glb> -o <converted>]                           ("Convert JPEG textures to PNG")
#   [hires-upscale <glb or converted> -o <upscaled>]           ("Upscale small textures")
#   hires-add <classic> <glb, converted or upscaled> -o <out> [--vertex-colors <mode>]
#                       (the mode only for a model with COLOR_0: the switch
#                       "Vertex color blending", include/rldhires_color.inc)
# This script runs that chain twice - once with rldpack, once with the Studio
# and the page's --machine - and wants the same SHA-256 at every step:
#   1. <folder> is emptied; rs_hires_selftest --write makes the karts (made in
#      code, no community content): route-bare.glb (an armature, no clips, no
#      wheel nodes, a 16 x 16 texture), route-tex64.glb (route-bare with a
#      64 x 64 texture), route-wheels.glb (an armature, 12 clips, its own 4
#      wheels), route-vcol.glb (route-bare with PS1-like COLOR_0, grey 128)
#      and route-jpeg.glb (route-bare with its 16 x 16 texture a JPEG), and
#      the fallback's switches per route as RsCharHires_FallbackArgs gives them
#      (fallback-<route>.txt, a CMake list).
#   2. Hi-Res: route-bare.glb without and with hires-upscale; Low-Poly:
#      route-bare.glb with hires-upscale, route-tex64.glb without and with
#      it; Hi-Res with wheels: route-wheels.glb with --wheels off.
#   3. Every result holds the classic model and the hi-res model (rldpack
#      info: chunks CHRI CMDL CHRG).
#   4. The vertex colours: route-vcol.glb on Hi-Res with --vertex-colors psx,
#      off and gltf; rldpack hires-check of each result names its mode, and
#      gltf is the bytes of the pack without the switch.
#   5. The JPEG textures: route-jpeg.glb on Hi-Res and Low-Poly with
#      hires-png (and hires-upscale of the converted model: its texture is 16
#      px); without hires-png hires-add refuses it; rs_hires_selftest
#      --png-check reads the converted model as the page does; make-char gives
#      the same classic fallback from the converted model as from the JPEG one.
# Prints "charroute passed: <route> sha256 <hash>" per route,
# "charroute passed: vertex colours psx off gltf", "charroute passed: jpeg
# converted to png" and "charroute passed: 3 routes" at the end.
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
# compare with). route-tex64.glb with "upscale on" (the author's choice) is
# the chain with hires-upscale. The line on the step Model (the report's
# "upscale step line:", rs_charroute.h RsCharHires_UpscaleStepLine) says
# "they will be upscaled" for the suggestion, "will stay as they are" for the
# author's off against it, "will be upscaled (your choice)" for the author's
# on without it and nothing for the 64 px kart as it starts; "upscale change"
# (a click on its "Change") opens Advanced at the box, in view and focused.
# The JPEG kart (rs_charroute.h, THE JPEG TEXTURES): without the click the
# page refuses it - the report says the button "Convert to PNG" is offered
# and the step Model has the HRG-16 line with that fix; "png convert" (the
# click) checks it as converted and builds rldpack's chain with hires-png
# (and hires-upscale, suggested for its 16 px texture) byte for byte on
# Hi-Res and Low-Poly, the line "Your JPEG textures will be converted to
# PNG." with "Change" on the step Model, "png change" opens Advanced at the
# box; "png off" after it refuses the kart again. No temp file of the page is
# left. What the page "Hi-Res (Preview)" did, now on the page Character
# (rs_char.c, THE FINISHED CHARACTER, THE CLIPS, THE CLASSIC MODEL): a finished
# character in the model field is checked and shown, nothing built (its report
# names its source, the check and every clip); the card Animations with
# hires-clip, hires-time, hires-play and view-filter (the report: the clip
# shown, a "hires summary:" with the joints, one "hires row:" per missing clip
# of route-bare.glb while the step keeps its one line); "classic <file>" packs
# route-tex64.glb into a finished character - rldpack hires-add's bytes, the
# character itself unchanged, the line on the step Model with "Change" - and
# "page hires" fails with the message that the page was removed. Prints
# "charroute page passed: upscale suggested and chosen", "charroute page
# passed: vertex colours psx off gltf", "charroute page passed: jpeg
# converted to png", "charroute page passed: the page Hi-Res on Character"
# and "charroute page passed: 3 routes".

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
# variable packExtra of the caller (a list, may be empty); hires-png first
# when the caller's variable routePng is ON.
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
    # "Convert JPEG textures to PNG".
    set(packP "${src}")
    set(packS "${src}")
    if(routePng)
        run("rldpack hires-png (${name})" "${PACKER}" hires-png "${src}" -o "${DIR}/p-${name}-png.glb")
        run("Reload Studio --rldpack hires-png (${name})" "${STUDIO}" --rldpack hires-png "${src}" -o "${DIR}/s-${name}-png.glb" --machine)
        same("the converted model (${name})" "${DIR}/p-${name}-png.glb" "${DIR}/s-${name}-png.glb")
        set(packP "${DIR}/p-${name}-png.glb")
        set(packS "${DIR}/s-${name}-png.glb")
    endif()
    # "Upscale small textures".
    if(upscale)
        run("rldpack hires-upscale (${name})" "${PACKER}" hires-upscale "${packP}" -o "${DIR}/p-${name}-upscaled.glb")
        run("Reload Studio --rldpack hires-upscale (${name})" "${STUDIO}" --rldpack hires-upscale "${packS}" -o "${DIR}/s-${name}-upscaled.glb" --machine)
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
route(lowpoly-tex64-upscaled route-tex64.glb "${fallbackLowpoly}" "" ON)
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

# The JPEG textures: the chain with hires-png (the converted texture is 16 px,
# so with hires-upscale as the page suggests it), on Hi-Res and Low-Poly.
set(routePng ON)
route(jpeg-hires route-jpeg.glb "${fallbackHires}" "" ON)
route(jpeg-lowpoly route-jpeg.glb "${fallbackLowpoly}" "" ON)
set(routePng OFF)
# Without the conversion the game's rules refuse the model: hires-add writes nothing.
execute_process(COMMAND "${PACKER}" hires-add "${DIR}/p-jpeg-hires.classic.part" "${DIR}/route-jpeg.glb" -o "${DIR}/p-jpeg-refused.rldchar"
                RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(result EQUAL 0 OR EXISTS "${DIR}/p-jpeg-refused.rldchar" OR NOT out MATCHES "HRG-16")
    message(FATAL_ERROR "charroute FAILED: hires-add took the JPEG kart without hires-png (exit ${result})\n${out}")
endif()
# The converted model as the page checks it.
run("rs_hires_selftest --png-check" "${TEST}" --png-check "${DIR}/route-jpeg.glb" "${DIR}/p-jpeg-hires-png.glb")
if(NOT out MATCHES "rs_hires png-check passed")
    message(FATAL_ERROR "charroute FAILED: the page's check of the converted kart\n${out}")
endif()
# make-char reads the JPEG itself: the classic fallback of the converted model
# is the same, so the chain may take the model as it is.
run("rldpack make-char (jpeg-hires, converted model)" "${PACKER}" make-char --model "${DIR}/p-jpeg-hires-png.glb" ${page} ${fallbackHires}
    --out "${DIR}/p-jpeg-hires-converted.classic.part")
same("the classic fallback of the converted model" "${DIR}/p-jpeg-hires.classic.part" "${DIR}/p-jpeg-hires-converted.classic.part")
message("charroute passed: jpeg converted to png")

# THE KART (rs_hires_model.h): a node "kart" with a mesh is packed as the kart
# part (--kart-part), a figure with the kit's mesh-less "kart" joint with the
# game's kart (--game-kart template), and so is a plain kart with the switch
# "Kart: from the player's game data" on (the page's runs below); the classic
# fallback of both is make-char --figure-only (the figure in a grey stand-in kart).
set(packExtra --kart-part)
route(kartpart route-kartpart.glb "${fallbackHires}" "--wheels;off" OFF)
set(packExtra --game-kart template)
route(kartjoint route-kartjoint.glb "${fallbackHires}" "--figure-only" OFF)
route(gamekart route-bare.glb "${fallbackHires}" "--figure-only" OFF)
set(packExtra "")
run("rldpack hires-check (kartpart)" "${PACKER}" hires-check "${DIR}/s-kartpart.rldchar")
if(NOT out MATCHES "parts: [^\n]*kart part")
    message(FATAL_ERROR "charroute FAILED: the kart part is not in the header of s-kartpart.rldchar\n${out}")
endif()
run("rldpack hires-check (kartjoint)" "${PACKER}" hires-check "${DIR}/s-kartjoint.rldchar")
if(NOT out MATCHES "parts: [^\n]*game")
    message(FATAL_ERROR "charroute FAILED: the game's kart is not in the header of s-kartjoint.rldchar\n${out}")
endif()
message("charroute passed: the kart (kart part, the kit's joint, the switch)")
message("charroute passed: 3 routes")

# The page only checks (no build): route word, model, verbs after the model
# (a list of --do arguments, may be empty); report into page-<name>-report.txt.
function(page_check name word model verbs)
    set(log "${DIR}/page-${name}.log")
    execute_process(COMMAND "${STUDIO}" --settings "${DIR}/rs.ini" --log "${log}"
                            --do "page char" --do "route ${word}" --do "model ${DIR}/${model}" ${verbs}
                            --do "report ${DIR}/page-${name}-report.txt" --do "quit"
                    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 240)
    if(NOT result EQUAL 0 OR NOT EXISTS "${DIR}/page-${name}-report.txt")
        set(text "")
        if(EXISTS "${log}")
            file(READ "${log}" text)
        endif()
        message(FATAL_ERROR "charroute FAILED: the page (${name}) ended with ${result}\n${out}\n${text}")
    endif()
endfunction()

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
    set(pageDo --do "report ${DIR}/page-lowpoly-report.txt" --do "upscale change")
    page_route(lowpoly lowpoly route-bare.glb p-lowpoly.rldchar)
    page_says(lowpoly "upscale small textures: on \\(the suggestion; suggested on, largest base colour texture 16 px\\)"
              "the page did not suggest the upscale for the 16 px kart on Low-Poly")
    page_says(lowpoly "upscale change: advanced open, card Options, \"Upscale small textures\" shown, in view, focus on the box"
              "\"Change\" did not open Advanced at the box")
    file(READ "${DIR}/page-lowpoly-report.txt" text)
    if(NOT text MATCHES "upscale small textures: on \\(suggested\\), suggested on \\(1 base colour texture\\(s\\), largest 16 px in the file")
        message(FATAL_ERROR "charroute FAILED: the report of the page (lowpoly) does not say the suggestion\n${text}")
    endif()
    if(NOT text MATCHES "upscale step line: \"Your textures are small \\(largest 16 px\\) - they will be upscaled\\.\" \\(shown, with \"Change\"\\)")
        message(FATAL_ERROR "charroute FAILED: the step Model (lowpoly) does not say that the textures will be upscaled\n${text}")
    endif()
    set(pageDo --do "report ${DIR}/page-lowpoly-tex64-report.txt")
    page_route(lowpoly-tex64 lowpoly route-tex64.glb p-lowpoly-tex64.rldchar)
    file(READ "${DIR}/page-lowpoly-tex64.log" text)
    if(text MATCHES "upscale small textures: on")
        message(FATAL_ERROR "charroute FAILED: the page suggested the upscale for the 64 px kart\n${text}")
    endif()
    file(READ "${DIR}/page-lowpoly-tex64-report.txt" text)
    if(NOT text MATCHES "upscale step line: \"\"[\r\n]")
        message(FATAL_ERROR "charroute FAILED: the step Model (lowpoly-tex64) has a line about the upscale\n${text}")
    endif()
    # The author's on for the 64 px kart: the chain with hires-upscale.
    set(pageDo --do "upscale on" --do "report ${DIR}/page-lowpoly-tex64-on-report.txt")
    page_route(lowpoly-tex64-on lowpoly route-tex64.glb p-lowpoly-tex64-upscaled.rldchar)
    file(READ "${DIR}/page-lowpoly-tex64-on-report.txt" text)
    if(NOT text MATCHES "upscale step line: \"Your textures will be upscaled \\(your choice\\)\\.\" \\(shown")
        message(FATAL_ERROR "charroute FAILED: the step Model (lowpoly-tex64-on) does not say the author's upscale\n${text}")
    endif()
    set(pageDo "")
    page_route(hires hires route-bare.glb p-hires-upscaled.rldchar)
    page_says(hires "upscale small textures: on \\(the suggestion" "the page did not suggest the upscale for the 16 px kart on Hi-Res")
    set(pageDo --do "upscale off" --do "report ${DIR}/page-hires-off-report.txt")
    page_route(hires-off hires route-bare.glb p-hires.rldchar)
    page_says(hires-off "upscale: off \\(the author's choice; suggested on" "the page did not take the author's choice")
    file(READ "${DIR}/page-hires-off-report.txt" text)
    if(NOT text MATCHES "upscale small textures: off \\(chosen by the author\\), suggested on")
        message(FATAL_ERROR "charroute FAILED: the report of the page (hires-off) does not say the author's choice\n${text}")
    endif()
    if(NOT text MATCHES "upscale step line: \"Your textures will stay as they are \\(your choice; suggested: upscale, largest 16 px\\)\\.\" \\(shown")
        message(FATAL_ERROR "charroute FAILED: the step Model (hires-off) does not say that the textures will stay as they are\n${text}")
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

    # THE KART: the page decides as the check says, byte for byte rldpack's
    # chain with the same switch; the plain kart gets the game's kart only
    # when the author switches it on.
    set(pageDo --do "upscale off" --do "report ${DIR}/page-kartpart-report.txt")
    page_route(kartpart hires route-kartpart.glb p-kartpart.rldchar)
    set(pageDo --do "upscale off" --do "report ${DIR}/page-kartjoint-report.txt")
    page_route(kartjoint hires route-kartjoint.glb p-kartjoint.rldchar)
    set(pageDo --do "upscale off" --do "gamekart on" --do "report ${DIR}/page-gamekart-report.txt")
    page_route(gamekart hires route-bare.glb p-gamekart.rldchar)
    set(pageDo "")
    file(READ "${DIR}/page-kartpart-report.txt" text)
    if(NOT text MATCHES "parts: kart part" OR NOT text MATCHES "Your kart is left out on the podium")
        message(FATAL_ERROR "charroute FAILED: the page (kartpart) does not pack the kart part or say so\n${text}")
    endif()
    file(READ "${DIR}/page-kartjoint-report.txt" text)
    if(NOT text MATCHES "parts: game kart \\(donor template\\)" OR NOT text MATCHES "game kart: on \\(suggested" OR
       NOT text MATCHES "game kart step line: \"The game adds the original kart from the player's game data; the classic model shows your figure in a simple grey kart\.\"" OR
       NOT text MATCHES "parts: game kart \\(donor template\\), classic model: the figure in a grey stand-in kart")
        message(FATAL_ERROR "charroute FAILED: the page (kartjoint) does not suggest the game's kart\n${text}")
    endif()
    file(READ "${DIR}/page-gamekart-report.txt" text)
    # Checked again with the game's kart: HRG-42 (its wheels come with it), no HRG-20.
    if(NOT text MATCHES "parts: game kart \\(donor template\\)" OR NOT text MATCHES "game kart: on \\(chosen" OR
       NOT text MATCHES "hires row: note HRG-42 " OR text MATCHES "hires row: [a-z]+ HRG-20 ")
        message(FATAL_ERROR "charroute FAILED: the page (gamekart) does not take the switch\n${text}")
    endif()
    file(READ "${DIR}/page-hires-off-report.txt" text)
    if(NOT text MATCHES "parts: none" OR NOT text MATCHES "game kart: off")
        message(FATAL_ERROR "charroute FAILED: the plain kart is not packed as before (hires-off)\n${text}")
    endif()
    message("charroute page passed: the kart (kart part, the kit's joint, the switch)")
    message("charroute page passed: vertex colours psx off gltf")

    # The JPEG kart without the click: refused, the button offered, the
    # HRG-16 line with its fix on the step Model.
    page_check(jpeg-refused hires route-jpeg.glb "")
    file(READ "${DIR}/page-jpeg-refused-report.txt" text)
    if(NOT text MATCHES "png convert: off \\(the start: never suggested\\), check of the model as it is, jpeg textures 1, button \"Convert to PNG\" offered")
        message(FATAL_ERROR "charroute FAILED: the page (jpeg-refused) does not offer \"Convert to PNG\"\n${text}")
    endif()
    if(NOT text MATCHES "page message: model\t4\tTexture 0 \\('paint'\\) is JPEG\\.\tHow to fix: Press \"Convert to PNG\"")
        message(FATAL_ERROR "charroute FAILED: the page (jpeg-refused) lacks the JPEG error with its fix\n${text}")
    endif()
    if(NOT text MATCHES "shown button: 230 Convert to PNG" OR NOT text MATCHES "png step line: \"\"[\r\n]")
        message(FATAL_ERROR "charroute FAILED: the page (jpeg-refused) does not show the button, or has a PNG line\n${text}")
    endif()
    page_says(jpeg-refused "check: cannot build yet" "the page took the JPEG kart without the click")
    # The click: the chain with hires-png (and the upscale it now suggests),
    # rldpack's bytes, on Hi-Res and Low-Poly; the line and "Change".
    set(pageDo --do "png convert" --do "report ${DIR}/page-jpeg-hires-report.txt" --do "png change")
    page_route(jpeg-hires hires route-jpeg.glb p-jpeg-hires.rldchar)
    page_says(jpeg-hires "png convert: 1 JPEG texture\\(s\\) converted for the check \\(rldpack hires-png -> route-jpeg-png\\.glb\\)"
              "the page did not check the converted kart")
    page_says(jpeg-hires "upscale small textures: on \\(the suggestion; suggested on, largest base colour texture 16 px\\)"
              "the page did not suggest the upscale for the converted 16 px texture")
    page_says(jpeg-hires "png change: advanced open, card Options, \"Convert JPEG textures to PNG\" shown, in view, focus on the box"
              "\"Change\" did not open Advanced at the box")
    file(READ "${DIR}/page-jpeg-hires-report.txt" text)
    if(NOT text MATCHES "png convert: on \\(chosen by the author\\), check of the model as converted, jpeg textures 1, button \"Convert to PNG\" not offered")
        message(FATAL_ERROR "charroute FAILED: the report of the page (jpeg-hires) does not say the conversion\n${text}")
    endif()
    if(NOT text MATCHES "png step line: \"Your JPEG textures will be converted to PNG\\.\" \\(shown, with \"Change\"\\)")
        message(FATAL_ERROR "charroute FAILED: the step Model (jpeg-hires) does not say that the JPEG textures will be converted\n${text}")
    endif()
    if(text MATCHES "is JPEG")
        message(FATAL_ERROR "charroute FAILED: the page (jpeg-hires) still has the JPEG error after the click\n${text}")
    endif()
    set(pageDo --do "png convert")
    page_route(jpeg-lowpoly lowpoly route-jpeg.glb p-jpeg-lowpoly.rldchar)
    set(pageDo "")
    # Unticked in Advanced: the model as it is again, refused.
    page_check(jpeg-off hires route-jpeg.glb "--do;png convert;--do;png off")
    file(READ "${DIR}/page-jpeg-off-report.txt" text)
    if(NOT text MATCHES "png convert: off \\(the start: never suggested\\), check of the model as it is, jpeg textures 1, button \"Convert to PNG\" offered" OR
       NOT text MATCHES "png step line: \"\"[\r\n]" OR NOT text MATCHES "is JPEG")
        message(FATAL_ERROR "charroute FAILED: \"png off\" did not bring the JPEG error back (jpeg-off)\n${text}")
    endif()
    # No temp file of the page's check or chain is left.
    foreach(left route-jpeg-png.glb route-jpeg-png-upscaled.glb)
        if(EXISTS "${DIR}/${left}")
            message(FATAL_ERROR "charroute FAILED: the page left its temp file ${left}")
        endif()
    endforeach()
    message("charroute page passed: jpeg converted to png")

    # THE FINISHED CHARACTER: a .rldchar with CHRG in the model field - checked
    # as its .glb, shown with the look of the file, nothing to build; the card
    # Options (Advanced open) offers none of the build's choices.
    page_check(character hires p-hires-wheels.rldchar "--do;advanced on")
    file(READ "${DIR}/page-character-report.txt" text)
    if(NOT text MATCHES "model source: a finished character" OR NOT text MATCHES "hires check: usable" OR
       NOT text MATCHES "hires clip: idle present" OR
       NOT text MATCHES "look source: the character p-hires-wheels\\.rldchar - shadow [a-z]+, exhaust [a-z]+" OR
       NOT text MATCHES "page message: model\t[0-9]\tA finished character: its Hi-Res model is checked and shown\\. Nothing is built from it\\.")
        message(FATAL_ERROR "charroute FAILED: the page (character) does not check and show a finished character\n${text}")
    endif()
    if(NOT text MATCHES "advanced: open, card options" OR text MATCHES "shown tick box: 210 " OR text MATCHES "shown tick box: 226 " OR
       text MATCHES "shown list: 240 " OR text MATCHES "upscale step line: \"[^\"]+\" \\(shown")
        message(FATAL_ERROR "charroute FAILED: the page (character) offers choices of the build that have no effect\n${text}")
    endif()
    page_says(character "check: a finished character - its CHRG checked \\(usable\\), nothing to build"
              "the page did not take the finished character as one")

    # THE CLIPS: the card Animations of route-wheels.glb (12 clips).
    page_check(clips hires route-wheels.glb
               "--do;extras animations;--do;hires-clip jump;--do;hires-time 0.25 90;--do;report ${DIR}/page-clips-mid-report.txt;--do;hires-play on;--do;hires-play off;--do;hires-clip bind;--do;view-filter nearest;--do;view-filter engine")
    page_says(clips "hires-clip: jump" "hires-clip did not choose the clip")
    page_says(clips "hires-time: jump 0\\.250 -> [1-9][0-9]* triangles" "hires-time did not show the frame")
    page_says(clips "hires-play: on" "hires-play did not play")
    page_says(clips "view-filter: nearest" "view-filter did not take nearest")
    file(READ "${DIR}/page-clips-mid-report.txt" text)
    if(NOT text MATCHES "clip shown: jump 0\\.250 90\\.0 paused" OR NOT text MATCHES "advanced: open, card animations" OR
       NOT text MATCHES "hires clip: jump present [0-9.]+" OR NOT text MATCHES "hires summary: clips 12/12 wheels 4/4 joints [1-9]")
        message(FATAL_ERROR "charroute FAILED: the report of the card Animations (clips)\n${text}")
    endif()
    file(READ "${DIR}/page-clips-report.txt" text)
    if(NOT text MATCHES "clip shown: bind ")
        message(FATAL_ERROR "charroute FAILED: hires-clip bind did not show the bind pose\n${text}")
    endif()
    # One report line per finding of the check, as rldpack hires-check lists
    # them (each missing clip its own HRG-19), on hires-warn.glb (rldpack
    # hires-test-files: 11 of 12 clips); the step Model keeps its one line.
    run("rldpack hires-test-files" "${PACKER}" hires-test-files "${DIR}")
    run("rldpack hires-check (rows)" "${PACKER}" hires-check "${DIR}/hires-warn.glb")
    string(REGEX MATCHALL "\n  (error|warning|note) HRG-[0-9]+ " packerRows "${out}")
    string(REGEX MATCHALL "\n  (error|warning|note) HRG-19 " packerClips "${out}")
    list(LENGTH packerRows packerCount)
    list(LENGTH packerClips packerClipCount)
    page_check(rows hires hires-warn.glb "")
    file(READ "${DIR}/page-rows-report.txt" text)
    string(REGEX MATCHALL "hires row: (error|warning|note) HRG-[0-9]+ " rows "${text}")
    string(REGEX MATCHALL "hires row: (error|warning|note) HRG-19 " rowClips "${text}")
    list(LENGTH rows rowCount)
    list(LENGTH rowClips rowClipCount)
    string(REGEX MATCHALL "page message: model\t[0-9]\t[0-9]+ of 12 animations present" folded "${text}")
    list(LENGTH folded foldedCount)
    if(packerClipCount LESS 1 OR NOT rowCount EQUAL packerCount OR NOT rowClipCount EQUAL packerClipCount OR NOT foldedCount EQUAL 1)
        message(FATAL_ERROR "charroute FAILED: report lines ${rowCount} (missing clips ${rowClipCount}) against rldpack's ${packerCount} "
                            "(${packerClipCount}), step lines about the clips ${foldedCount} (rows)\n${text}")
    endif()

    # THE CLASSIC MODEL from a finished character: rldpack hires-add into it,
    # byte for byte; the character itself stays as it was.
    run("rldpack hires-add (classic-from)" "${PACKER}" hires-add "${DIR}/p-hires.rldchar" "${DIR}/route-tex64.glb" -o "${DIR}/p-classic-from.rldchar")
    file(SHA256 "${DIR}/p-hires.rldchar" classicBefore)
    # (The page's exhaust set to off first: the preview must show the file's
    # look, never the fields' - "look source:" in the report.)
    set(pageDo --do "exhaust off" --do "classic ${DIR}/p-hires.rldchar" --do "report ${DIR}/page-classic-from-report.txt"
               --do "classic change")
    page_route(classic-from hires route-tex64.glb p-classic-from.rldchar)
    set(pageDo "")
    file(SHA256 "${DIR}/p-hires.rldchar" classicAfter)
    if(NOT classicBefore STREQUAL classicAfter)
        message(FATAL_ERROR "charroute FAILED: the page changed the character the classic model came from")
    endif()
    page_says(classic-from "check: ready to build - the classic model from p-hires\\.rldchar \\(no make-char\\)"
              "the page did not check the classic model from a character")
    page_says(classic-from "classic change: advanced open, card Options, \"Classic model\" shown, in view, focus on the box"
              "\"Change\" did not open Advanced at the list")
    file(READ "${DIR}/page-classic-from-report.txt" text)
    if(NOT text MATCHES "classic model: from the character [^\r\n]*p-hires\\.rldchar" OR
       NOT text MATCHES "look source: the character p-hires\\.rldchar - shadow [a-z]+, exhaust (retail|own)" OR
       NOT text MATCHES "classic step line: \"The classic model, name, look and voices come from p-hires\\.rldchar\\.\" \\(shown, with \"Change\"\\)")
        message(FATAL_ERROR "charroute FAILED: the step Model (classic-from) does not say where the classic model comes from\n${text}")
    endif()

    # The page Hi-Res is gone: a script that names it fails with the way to go.
    execute_process(COMMAND "${STUDIO}" --settings "${DIR}/rs.ini" --log "${DIR}/page-hires-gone.log" --do "page hires" --do "quit"
                    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 120)
    file(READ "${DIR}/page-hires-gone.log" text)
    if(result EQUAL 0 OR NOT text MATCHES "page hires was removed - use page char with route hires")
        message(FATAL_ERROR "charroute FAILED: \"page hires\" did not fail with its message (exit ${result})\n${text}")
    endif()
    message("charroute page passed: the page Hi-Res on Character")
    message("charroute page passed: 3 routes")
endif()

file(GLOB_RECURSE tempLeft LIST_DIRECTORIES true "${tempDir}/*")
if(tempLeft)
    message(FATAL_ERROR "charroute FAILED: files left in the run's TEMP ${tempDir}: ${tempLeft}")
endif()
