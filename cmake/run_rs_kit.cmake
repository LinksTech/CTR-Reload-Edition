# The Creator Kit page of Reload Studio (tools/reloadstudio/rs_kit.c), ctest
# reloadstudio_kit:
#
#   cmake -DSTUDIO=<ReloadStudio.exe> -DSTANDIN=<rs_kit_standin.exe>
#         -DTEST=<rs_hires_selftest.exe> -DPACKER=<rldpack.exe> -DPYTHON=<python>
#         -DSOURCE=<repo root> -DDIR=<folder> -P run_rs_kit.cmake
#
# No Blender and no game data are used: rs_kit_standin.exe (made-up files only)
# stands in for blender.exe and for the extractor (the test-only setting
# kit.extract), and the "game" is a folder with a made-up assets\BIGFILE.BIG.
# Everything happens in a folder of this run, <folder>/run-<random>; older run
# folders are removed where they can be. Every Studio run has its own settings
# ini (the game program is a Windows program, so no run starts the game; the kit
# settings point at the stand-in) and its own TEMP/TMP below the run folder.
#
#   K9.1 the page end to end: generate with the demo on, the kit's files, no
#        kit.part, the guide, the bat file with the test command
#   K9.2 the demo character equals what the page Character builds from the
#        kit's demo model, byte for byte
#   K9.3 the bat and the page's test command agree
#   K9.4 no game data: the page says so, generate is a failed step, no files
#   K9.5 Blender missing, Blender of another version
#   K9.6 cancel while Blender works: nothing left, the old kit untouched
#   K9.7 Blender fails: the message with its fix, the old kit untouched
#   K9.8 the output folder rules: inside a git work tree (below the run
#        folder, which is set as GIT_CEILING_DIRECTORIES), inside the game
#   K9.9 Blender gets its folders below the settings ini of the run
#   L.9  the content guard refuses every name of the kit
#   HRG-41 the page Character warns at the build: a model with the kit's kart
#
# Output: "kit passed: ..." or "FAILED: ...".

foreach(required STUDIO STANDIN TEST PACKER PYTHON SOURCE DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "FAILED: -D${required}=... is missing")
    endif()
endforeach()

file(MAKE_DIRECTORY "${DIR}")
file(GLOB oldRuns LIST_DIRECTORIES true "${DIR}/run-*")
foreach(old IN LISTS oldRuns)
    execute_process(COMMAND ${CMAKE_COMMAND} -E rm -rf "${old}" RESULT_VARIABLE ignored)
endforeach()
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef run)
set(base "${DIR}/run-${run}")
file(MAKE_DIRECTORY "${base}")

# Temporary files of every program started here: below this run's folder.
file(MAKE_DIRECTORY "${base}/temp")
file(TO_NATIVE_PATH "${base}/temp" tempNative)
set(ENV{TEMP} "${tempNative}")
set(ENV{TMP} "${tempNative}")
# The run folder is git's ceiling: the build folder is inside a git work tree,
# whose .git the page would find above the kit folders and refuse them (the
# rule K9.8 tests with a .git of its own below the ceiling).
file(TO_NATIVE_PATH "${base}" baseNative)
set(ENV{GIT_CEILING_DIRECTORIES} "${baseNative}")

get_filename_component(standinName "${STANDIN}" NAME_WE)

# --- helpers ----------------------------------------------------------------

# The environment of the stand-in back to "everything good".
function(reset_env)
    foreach(name MODE VERSION DEMO ENVFILE LOG)
        set(ENV{RS_KIT_STANDIN_${name}})
    endforeach()
endfunction()

# The kit settings of a settings ini: game, blender, output, extractor (an empty
# value leaves the setting out).
function(kit_ini outvar game blender out extract)
    set(text "")
    foreach(pair "game|${game}" "blender|${blender}" "out|${out}" "extract|${extract}")
        string(REGEX MATCH "^([a-z]+)\\|(.*)$" ignored "${pair}")
        set(key "${CMAKE_MATCH_1}")
        set(value "${CMAKE_MATCH_2}")
        if(NOT value STREQUAL "")
            file(TO_NATIVE_PATH "${value}" native)
            string(APPEND text "kit.${key}=${native}\r\n")
        endif()
    endforeach()
    set(${outvar} "${text}" PARENT_SCOPE)
endfunction()

# One Studio run: name (its folder <run>/<name>), the exit code ("any" for none
# in particular), the kit lines of the ini, then the Studio's arguments. The
# stand-in's calls go to <run>/<name>/standin.log.
function(studio name expectExit iniKit)
    file(MAKE_DIRECTORY "${base}/${name}")
    file(REMOVE "${base}/${name}/auto.log" "${base}/${name}/standin.log")
    file(WRITE "${base}/${name}/rs.ini"
         "[reloadstudio]\r\ntest.exe=C:\\Windows\\System32\\hostname.exe\r\n${iniKit}")
    set(ENV{RS_KIT_STANDIN_LOG} "${base}/${name}/standin.log")
    execute_process(COMMAND "${STUDIO}" --settings "${base}/${name}/rs.ini" --log "${base}/${name}/auto.log" ${ARGN}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 120)
    set(ENV{RS_KIT_STANDIN_LOG})
    if(NOT expectExit STREQUAL "any" AND NOT rc STREQUAL "${expectExit}")
        set(text "")
        if(EXISTS "${base}/${name}/auto.log")
            file(READ "${base}/${name}/auto.log" text)
        endif()
        message(FATAL_ERROR "FAILED: run ${name}: Reload Studio ended with '${rc}', expected ${expectExit}\n${out}\n${text}")
    endif()
endfunction()

function(expect file regex)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "FAILED: ${file} does not exist (wanted a line matching '${regex}')")
    endif()
    file(READ "${file}" text)
    if(NOT text MATCHES "${regex}")
        message(FATAL_ERROR "FAILED: ${file} has no line matching '${regex}'\n${text}")
    endif()
endfunction()

function(expect_not file regex)
    file(READ "${file}" text)
    if(text MATCHES "${regex}")
        message(FATAL_ERROR "FAILED: ${file} has a line matching '${regex}'\n${text}")
    endif()
endfunction()

# The text is in the file, literally (no regex: dots and brackets are plain).
function(expect_lit file literal)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "FAILED: ${file} does not exist (wanted '${literal}')")
    endif()
    file(READ "${file}" text)
    string(FIND "${text}" "${literal}" at)
    if(at LESS 0)
        message(FATAL_ERROR "FAILED: ${file} lacks '${literal}'\n${text}")
    endif()
endfunction()

function(expect_not_lit file literal)
    file(READ "${file}" text)
    string(FIND "${text}" "${literal}" at)
    if(NOT at LESS 0)
        message(FATAL_ERROR "FAILED: ${file} has '${literal}'\n${text}")
    endif()
endfunction()

# The first line of a report that starts with the key, without its line end.
function(report_line file key outvar)
    file(READ "${file}" text)
    string(REGEX MATCH "(^|\n)${key}\t[^\r\n]*" line "${text}")
    set(${outvar} "${line}" PARENT_SCOPE)
endfunction()

# The files of a finished kit; the demo files with demo ON.
function(check_kit dir reportFile demo)
    set(names kit-kart.glb kit-wheel.glb kit-info.json creator-kit.blend creator-kit-demo.glb creator-kit-report.json
              scripts/make_creator_kit.py scripts/creator_kit_puppet.py scripts/creator_kit_clips.py
              scripts/creator_kit_standin.py CREATOR_KIT.html)
    if(demo)
        list(APPEND names demo-puppet.rldchar START-DEMO-PUPPE.bat)
    endif()
    foreach(n IN LISTS names)
        if(NOT EXISTS "${dir}/${n}")
            message(FATAL_ERROR "FAILED: the kit lacks ${dir}/${n}")
        endif()
    endforeach()
    # Every file of the top folder is in the report with its size.
    foreach(n IN LISTS names)
        if(NOT n MATCHES "/")
            expect("${reportFile}" "(^|\n)file\t${n}\t[0-9]+")
        endif()
    endforeach()
    if(EXISTS "${dir}/kit.part")
        message(FATAL_ERROR "FAILED: kit.part is left in ${dir}")
    endif()
endfunction()

# No file of a kit in the folder (it may not exist).
function(check_no_kit dir)
    if(EXISTS "${dir}")
        file(GLOB_RECURSE left "${dir}/*")
        if(left)
            message(FATAL_ERROR "FAILED: ${dir} holds files after a failed run: ${left}")
        endif()
    endif()
endfunction()

# --- the stand-in game and the test model -------------------------------------
file(MAKE_DIRECTORY "${base}/game/assets")
file(WRITE "${base}/game/assets/BIGFILE.BIG" "made-up stand-in for BIGFILE.BIG\n")
file(MAKE_DIRECTORY "${base}/nogame")
file(MAKE_DIRECTORY "${base}/models")
execute_process(COMMAND "${TEST}" --write "${base}/models"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 120)
set(demoModel "${base}/models/route-wheels.glb")
if(NOT rc EQUAL 0 OR NOT EXISTS "${demoModel}")
    message(FATAL_ERROR "FAILED: rs_hires_selftest --write made no route-wheels.glb (exit ${rc})\n${out}")
endif()

# --- K9.1 / K9.9: the page end to end -------------------------------------------
reset_env()
set(ENV{RS_KIT_STANDIN_DEMO} "${demoModel}")
set(ENV{RS_KIT_STANDIN_ENVFILE} "${base}/env.txt")
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/out-a" "${STANDIN}")
studio(k91 0 "${ini}"
       --do "page kit" --do "demo on" --do "generate" --do "report ${base}/k91/report.txt"
       --do "open guide" --do "test-demo" --do "quit")
reset_env()
set(auto "${base}/k91/auto.log")
set(rep "${base}/k91/report.txt")
expect("${auto}" "automation finished, 0 step\\(s\\) failed")
expect_lit("${rep}" "result\tok")
expect_lit("${rep}" "phase\textract\tok")
expect_lit("${rep}" "phase\tblender\tok")
expect_lit("${rep}" "phase\tdemo\tok")
check_kit("${base}/out-a" "${rep}" ON)
expect("${auto}" "open: guide [^\n]*\\(exists\\)")
# The bat starts the game with the demo character.
file(READ "${base}/out-a/START-DEMO-PUPPE.bat" bat)
string(REPLACE "\\" "/" batSlash "${bat}")
foreach(want "--char" "demo-puppet.rldchar" "--level 0")
    string(FIND "${bat}" "${want}" at)
    if(at LESS 0)
        message(FATAL_ERROR "FAILED: START-DEMO-PUPPE.bat lacks '${want}'\n${bat}")
    endif()
endforeach()
string(TOLOWER "${batSlash}" batLow)
string(TOLOWER "${base}/out-a/demo-puppet.rldchar" charLow)
string(FIND "${batLow}" "${charLow}" at)
string(FIND "${batLow}" "%~dp0" atRel)
if(at LESS 0 AND atRel LESS 0)
    message(FATAL_ERROR "FAILED: START-DEMO-PUPPE.bat names neither the full path of demo-puppet.rldchar nor %~dp0\n${bat}")
endif()
# What the stand-in was called with.
set(sa "${base}/k91/standin.log")
expect_lit("${sa}" "--python-exit-code | 1")
expect_lit("${sa}" "-- | --kit")
expect_lit("${sa}" "--game | ")
expect_lit("${sa}" "--version")

# K9.9: Blender's folders and TEMP/TMP are below the folder of the run's settings ini.
file(TO_NATIVE_PATH "${base}/k91" iniDirNative)
string(TOLOWER "${iniDirNative}" iniDirLow)
file(STRINGS "${base}/env.txt" envLines)
set(envSeen 0)
foreach(line IN LISTS envLines)
    string(REGEX MATCH "^([A-Z_]+)=(.*)$" ignored "${line}")
    set(key "${CMAKE_MATCH_1}")
    string(TOLOWER "${CMAKE_MATCH_2}" valueLow)
    string(REPLACE "/" "\\" valueLow "${valueLow}")
    string(FIND "${valueLow}" "${iniDirLow}" at)
    if(valueLow STREQUAL "" OR NOT at EQUAL 0)
        message(FATAL_ERROR "FAILED: Blender's ${key} is '${CMAKE_MATCH_2}', not below ${iniDirNative}")
    endif()
    math(EXPR envSeen "${envSeen} + 1")
endforeach()
if(NOT envSeen EQUAL 7)
    message(FATAL_ERROR "FAILED: env.txt has ${envSeen} variables, expected 7 (TEMP, TMP, BLENDER_USER_*)")
endif()

# --- K9.2: the demo is what the page Character builds -------------------------------
studio(k92 0 "" --do "page char" --do "route hires" --do "model ${base}/out-a/creator-kit-demo.glb"
       --do "name DEMO PUPPET" --do "out ${base}/page-demo.rldchar" --do "build" --do "quit")
if(NOT EXISTS "${base}/page-demo.rldchar")
    message(FATAL_ERROR "FAILED: the page Character built no page-demo.rldchar from the kit's demo model")
endif()
file(SHA256 "${base}/page-demo.rldchar" hashPage)
file(SHA256 "${base}/out-a/demo-puppet.rldchar" hashKit)
if(NOT hashPage STREQUAL hashKit)
    message(FATAL_ERROR "FAILED: demo-puppet.rldchar (${hashKit}) is not what the page Character builds (${hashPage})")
endif()

# --- K9.3: the bat and the page's test command -----------------------------------------
file(READ "${auto}" autoText)
string(REGEX MATCH "--char [^\r\n]*--level 0 --driver [0-9]+" testLine "${autoText}")
if(testLine STREQUAL "")
    message(FATAL_ERROR "FAILED: test-demo logged no '--char ... --level 0 --driver <n>' command line\n${autoText}")
endif()
string(REGEX MATCH "--level 0 --driver [0-9]+" testTail "${testLine}")
string(FIND "${bat}" "${testTail}" at)
if(at LESS 0)
    message(FATAL_ERROR "FAILED: the bat lacks '${testTail}' of the test command\nbat: ${bat}\ntest: ${testLine}")
endif()
string(FIND "${testLine}" "demo-puppet.rldchar" at)
if(at LESS 0)
    message(FATAL_ERROR "FAILED: the test command does not name demo-puppet.rldchar\n${testLine}")
endif()
message("kit passed: K9.1-K9.3 end to end, demo equals the page Character (${hashKit}), ${testTail}")

# --- K9.4: no game data ------------------------------------------------------------------
kit_ini(ini "${base}/nogame" "${STANDIN}" "${base}/out-d" "${STANDIN}")
studio(k94 1 "${ini}" --do "page kit" --do "report ${base}/k94/report.txt" --do "generate" --do "quit")
set(rep "${base}/k94/report.txt")
expect_lit("${rep}" "game\tmissing-data\t")
expect_lit("${rep}" "game-text\tCTR Reload has no game data yet.")
expect("${base}/k94/auto.log" "FAIL")
expect("${base}/k94/auto.log" "automation finished, [1-9][0-9]* step\\(s\\) failed")
check_no_kit("${base}/out-d")
message("kit passed: K9.4 no game data")

# --- K9.5: Blender missing, Blender of another version ------------------------------------------
kit_ini(ini "${base}/game" "${base}/no-blender.exe" "${base}/out-e" "${STANDIN}")
studio(k95a 0 "${ini}" --do "page kit" --do "wait 1500" --do "report ${base}/k95a/report.txt" --do "quit")
set(rep "${base}/k95a/report.txt")
expect_lit("${rep}" "blender\tmissing\t")
expect_lit("${rep}" "blender-text\tBlender was not found.\tInstall Blender 5.2 from blender.org, or choose blender.exe with Choose Blender.")
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/out-e" "${STANDIN}")
set(ENV{RS_KIT_STANDIN_VERSION} "Blender 3.6.0")
studio(k95b 0 "${ini}" --do "page kit" --do "wait 1500" --do "report ${base}/k95b/report.txt" --do "quit")
reset_env()
set(rep "${base}/k95b/report.txt")
expect_lit("${rep}" "blender\tversion\t")
expect_lit("${rep}" "blender-text\tThis is Blender 3.6.0; the Creator Kit needs Blender 5.2.\tInstall Blender 5.2 from blender.org.")
message("kit passed: K9.5 Blender missing and Blender 3.6.0")

# --- K9.6: cancel -----------------------------------------------------------------------------------
# First a good kit (demo off), its Blender file noted.
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/out-c" "${STANDIN}")
studio(k96good 0 "${ini}" --do "page kit" --do "demo off" --do "generate" --do "report ${base}/k96good/report.txt" --do "quit")
expect_lit("${base}/k96good/report.txt" "result\tok")
check_kit("${base}/out-c" "${base}/k96good/report.txt" OFF)
file(SHA256 "${base}/out-c/creator-kit.blend" blendBefore)
# Blender hangs; generate is started, then cancelled.
set(ENV{RS_KIT_STANDIN_MODE} "hang")
studio(k96 0 "${ini}" --do "page kit" --do "demo off" --do "now generate" --do "now wait 1500" --do "now cancel"
       --do "wait 1500" --do "report ${base}/k96/report.txt" --do "quit")
reset_env()
set(rep "${base}/k96/report.txt")
expect_lit("${rep}" "result\tcancelled")
expect_lit("${rep}" "phase\tblender\tcancelled")
if(EXISTS "${base}/out-c/kit.part")
    message(FATAL_ERROR "FAILED: kit.part is left after the cancel")
endif()
file(SHA256 "${base}/out-c/creator-kit.blend" blendAfter)
if(NOT blendBefore STREQUAL blendAfter)
    message(FATAL_ERROR "FAILED: the cancel changed the old creator-kit.blend")
endif()
execute_process(COMMAND powershell -NoProfile -Command
                "(Get-Process -Name ${standinName} -ErrorAction SilentlyContinue | Measure-Object).Count"
                OUTPUT_VARIABLE leftCount RESULT_VARIABLE rc TIMEOUT 60)
string(STRIP "${leftCount}" leftCount)
if(NOT leftCount STREQUAL "0")
    message(FATAL_ERROR "FAILED: ${leftCount} process(es) ${standinName}.exe left after the cancel")
endif()
message("kit passed: K9.6 cancel leaves nothing, old kit untouched, no stand-in process left")

# --- K9.7: Blender fails ---------------------------------------------------------------------------------
set(ENV{RS_KIT_STANDIN_MODE} "fail4")
studio(k97 any "${ini}" --do "page kit" --do "demo off" --do "generate" --do "report ${base}/k97/report.txt" --do "quit")
reset_env()
set(rep "${base}/k97/report.txt")
expect_lit("${rep}" "result\tfailed")
expect("${rep}" "(^|\n)msg\terror\t[^\r\n]*kit-info\\.json is missing in[^\r\n]*\t[^\r\n]*generate the kit files again")
file(SHA256 "${base}/out-c/creator-kit.blend" blendAfter)
if(NOT blendBefore STREQUAL blendAfter)
    message(FATAL_ERROR "FAILED: a failed Blender run changed the old creator-kit.blend")
endif()
if(EXISTS "${base}/out-c/kit.part")
    message(FATAL_ERROR "FAILED: kit.part is left after a failed run")
endif()
message("kit passed: K9.7 Blender fails with its message")

# --- K9.8: the output folder rules ----------------------------------------------------------------------------
file(MAKE_DIRECTORY "${base}/repo/.git")
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/repo/sub" "${STANDIN}")
studio(k98git 1 "${ini}" --do "page kit" --do "report ${base}/k98git/report.txt" --do "generate" --do "quit")
set(rep "${base}/k98git/report.txt")
expect_lit("${rep}" "out\tgit\t")
expect_lit("${rep}" "out-text\tThis folder is inside a git repository. The kit holds data from your game and must stay out of any repository.\tChoose a folder outside it.")
check_no_kit("${base}/repo/sub")
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/game/kit" "${STANDIN}")
studio(k98game 1 "${ini}" --do "page kit" --do "report ${base}/k98game/report.txt" --do "generate" --do "quit")
set(rep "${base}/k98game/report.txt")
expect_lit("${rep}" "out\tgame\t")
expect_lit("${rep}" "out-text\tThis folder is inside the game's folder.\tChoose a folder outside the game, for example in Documents.")
check_no_kit("${base}/game/kit")
message("kit passed: K9.8 output inside a git work tree and inside the game refused")

# --- L.9: the content guard knows every name of the kit --------------------------------------------------------------
set(guardNames "'kit-kart.glb','kit-wheel.glb','kit-info.json','creator-kit.blend','creator-kit-demo.glb','creator-kit-report.json','demo-puppet.rldchar','START-DEMO-PUPPE.bat','kit.part/x'")
execute_process(COMMAND "${PYTHON}" -I -c
                "import sys; sys.path.insert(0, '${SOURCE}/tools'); import content_guard as g; names=[${guardNames}]; bad=[n for n in names if not g.kit_name(n)]; print('not refused: ' + ' '.join(bad)); sys.exit(1 if bad else 0)"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 60)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "FAILED: content_guard.kit_name does not refuse every kit name (exit ${rc})\n${out}")
endif()
message("kit passed: L.9 content_guard refuses all kit names")

# --- HRG-41: the page Character warns at the build when a model carries the kit's kart ---
# rs_hires_selftest's route-kit.glb has the material ctrkit_paint (the mark of
# the Creator Kit's kart): the report of the page holds the fixed warning.
studio(hrg41 0 "" --do "page char" --do "route hires" --do "model ${base}/models/route-kit.glb"
       --do "report ${base}/hrg41/report.txt" --do "quit")
expect_lit("${base}/hrg41/report.txt"
           "This file contains the original kart from your game data. Don't share it, or export without the kart.")
expect_lit("${base}/hrg41/report.txt" "HRG-41")
message("kit passed: HRG-41 on the page Character")

message("kit passed: K9.1-K9.9, L.9 and HRG-41 (page end to end, demo equals the Character page, game data, Blender, cancel, failure, output rules, environment, names, the kit kart warning)")
