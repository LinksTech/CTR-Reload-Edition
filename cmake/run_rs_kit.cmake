# The Creator Kit page of Reload Studio (tools/reloadstudio/rs_kit.c), ctest
# reloadstudio_kit:
#
#   cmake -DSTUDIO=<ReloadStudio.exe> -DSTANDIN=<rs_kit_standin.exe>
#         -DTEST=<rs_hires_selftest.exe> -DPACKER=<rldpack.exe> -DPYTHON=<python>
#         -DSOURCE=<repo root> -DDIR=<folder> -P run_rs_kit.cmake
#
# No Blender and no game data are used: rs_kit_standin.exe (made-up files only)
# stands in for blender.exe; the game data is rldpack's kit-standin (a BIGFILE.BIG
# made in code, no game data), read by the real kit-extract of the page.
# Everything happens in a folder of this run, <folder>/run-<random>; older run
# folders are removed where they can be. Every Studio run has its own settings
# ini (the game program is a Windows program, so no run starts the game; the kit
# settings point at the stand-in) and its own TEMP/TMP below the run folder.
#
#   K9.1 the page end to end: generate with the demo on, all three ways (the
#        default), the kit's files in the layout K4 (the kit folder, vanilla\,
#        lowpoly\, hires\), no kit.part, the guide, the bat file with the test
#        command; the generator called with --routes vanilla,lowpoly,hires
#   K1   the notice: every NOTICE.txt and scripts\CREATOR_KIT_NOTICE.txt byte
#        for byte templates/CREATOR_KIT_NOTICE.txt; the step Generate shows it
#        (its text equals the file after line ends are made alike) as plain
#        text above "Generate demo files", nothing beside it (no icon)
#   K9.10 one way chosen ("ways vanilla|lowpoly|hires"): only that way's folder
#        is written, exactly its files; the demo is offered with Hi-Res only
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

# The kit settings of a settings ini: game, blender, output (an empty value
# leaves the setting out).
function(kit_ini outvar game blender out)
    set(text "")
    foreach(pair "game|${game}" "blender|${blender}" "out|${out}")
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

# The notice as the repository has it (K1), and the same with its line ends
# made alike (LF, none at the end) for the text the Studio shows.
file(READ "${SOURCE}/templates/CREATOR_KIT_NOTICE.txt" noticeSource)
string(REPLACE "\r\n" "\n" noticeNorm "${noticeSource}")
string(REGEX REPLACE "[\n ]+$" "" noticeNorm "${noticeNorm}")
set(noticeCount 0)

# A file of the kit is the notice, byte for byte.
function(check_notice file)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "FAILED: ${file} does not exist (the notice)")
    endif()
    file(READ "${file}" text)
    if(NOT text STREQUAL noticeSource)
        message(FATAL_ERROR "FAILED: ${file} is not templates/CREATOR_KIT_NOTICE.txt byte for byte\n${text}")
    endif()
    math(EXPR n "${noticeCount} + 1")
    set(noticeCount ${n} PARENT_SCOPE)
endfunction()

# The files of a finished kit in the layout K4: those of the kit folder, and
# for each way of ways (vanilla, lowpoly, hires) its folder with exactly its
# files - the demo files in hires\ with demo ON; a way not in ways has no
# folder. Every file is in the report with its size; every NOTICE.txt and
# scripts\CREATOR_KIT_NOTICE.txt is the notice.
function(check_kit dir reportFile demo ways)
    set(names kit-kart.glb kit-kart-gltf.glb kit-wheel.glb kit-wheel-gltf.glb kit-info.json kit-extract.txt NOTICE.txt
              scripts/make_creator_kit.py scripts/creator_kit_puppet.py scripts/creator_kit_clips.py
              scripts/creator_kit_standin.py scripts/creator_kit_classic.py scripts/CREATOR_KIT_NOTICE.txt CREATOR_KIT.html)
    foreach(w vanilla lowpoly hires)
        list(FIND ways "${w}" at)
        if(at LESS 0)
            if(EXISTS "${dir}/${w}")
                message(FATAL_ERROR "FAILED: ${dir}/${w} exists, but the way ${w} was not chosen (${ways})")
            endif()
            continue()
        endif()
        set(want creator-kit-${w}.blend creator-kit-${w}-figure.glb NOTICE.txt creator-kit-report.json)
        if(w STREQUAL "hires")
            list(APPEND want creator-kit-demo-local.glb)
            if(demo)
                list(APPEND want demo-puppet.rldchar START-DEMO-PUPPE.bat)
            endif()
        endif()
        file(GLOB have RELATIVE "${dir}/${w}" "${dir}/${w}/*")
        list(SORT have)
        list(SORT want)
        if(NOT have STREQUAL want)
            message(FATAL_ERROR "FAILED: ${dir}/${w} holds '${have}', expected exactly '${want}'")
        endif()
        foreach(n IN LISTS want)
            list(APPEND names "${w}/${n}")
        endforeach()
    endforeach()
    foreach(n IN LISTS names)
        if(NOT EXISTS "${dir}/${n}")
            message(FATAL_ERROR "FAILED: the kit lacks ${dir}/${n}")
        endif()
        string(REPLACE "/" "\\" reported "${n}")
        expect_lit("${reportFile}" "\nfile\t${reported}\t")
        if(n MATCHES "NOTICE\\.txt$")
            check_notice("${dir}/${n}")
        endif()
    endforeach()
    set(noticeCount ${noticeCount} PARENT_SCOPE)
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
execute_process(COMMAND "${STUDIO}" --rldpack kit-standin -o "${base}/game/assets"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 120)
if(NOT rc EQUAL 0 OR NOT EXISTS "${base}/game/assets/BIGFILE.BIG")
    message(FATAL_ERROR "FAILED: --rldpack kit-standin wrote no BIGFILE.BIG (exit ${rc})\n${out}")
endif()
file(MAKE_DIRECTORY "${base}/nogame")
file(MAKE_DIRECTORY "${base}/models")
execute_process(COMMAND "${TEST}" --write "${base}/models"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 120)
set(demoModel "${base}/models/route-kartpart.glb")
if(NOT rc EQUAL 0 OR NOT EXISTS "${demoModel}")
    message(FATAL_ERROR "FAILED: rs_hires_selftest --write made no route-kartpart.glb (exit ${rc})\n${out}")
endif()

# --- K9.1 / K9.9: the page end to end -------------------------------------------
reset_env()
set(ENV{RS_KIT_STANDIN_DEMO} "${demoModel}")
set(ENV{RS_KIT_STANDIN_ENVFILE} "${base}/env.txt")
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/out-a")
studio(k91 0 "${ini}"
       --do "page kit" --do "report ${base}/k91/start.txt" --do "step 3" --do "report ${base}/k91/notice.txt"
       --do "controls ${base}/k91/step3.tsv"
       --do "demo on" --do "generate" --do "report ${base}/k91/report.txt"
       --do "open guide" --do "open template hires" --do "open template vanilla" --do "test-demo" --do "quit")
reset_env()
set(auto "${base}/k91/auto.log")
set(rep "${base}/k91/report.txt")
expect("${auto}" "automation finished, 0 step\\(s\\) failed")
expect("${auto}" "open: template [^\n]*hires.creator-kit-hires\\.blend \\(exists\\)")
expect("${auto}" "open: template [^\n]*vanilla.creator-kit-vanilla\\.blend \\(exists\\)")
expect_lit("${base}/k91/start.txt" "ways\tall\tvanilla,lowpoly,hires")
expect_lit("${base}/k91/start.txt" "demo\ton\toffered")
expect_lit("${rep}" "result\tok")
expect_lit("${rep}" "phase\textract\tok")
expect_lit("${rep}" "phase\tblender\tok")
expect_lit("${rep}" "phase\tdemo\tok")
check_kit("${base}/out-a" "${rep}" ON "vanilla;lowpoly;hires")
expect("${auto}" "open: guide [^\n]*\\(exists\\)")
expect_lit("${base}/k91/standin.log" "--kit | ")
expect_lit("${base}/k91/standin.log" " | --routes | vanilla,lowpoly,hires")
expect_not_lit("${base}/k91/standin.log" "--vcol")

# --- K1: the notice on the step Generate --------------------------------------------
# The report holds the text of the control, every line end as the two characters \n.
report_line("${base}/k91/notice.txt" "notice" noticeLine)
string(REGEX REPLACE "^\n?notice\tshown\t" "" noticeShown "${noticeLine}")
if(noticeShown STREQUAL noticeLine)
    message(FATAL_ERROR "FAILED: the step Generate does not show the notice\n${noticeLine}")
endif()
string(REPLACE "\\n" "\n" noticeShown "${noticeShown}")
if(NOT noticeShown STREQUAL noticeNorm)
    message(FATAL_ERROR "FAILED: the notice the Studio shows is not templates/CREATOR_KIT_NOTICE.txt\nshown:\n${noticeShown}\nfile:\n${noticeNorm}")
endif()
math(EXPR noticeCount "${noticeCount} + 1")
expect("${base}/k91/notice.txt" "(^|\n)step\t3\n")
# Plain text (a label), right above the bar with "Generate demo files", nothing
# beside it: no other control shown in its rows (no icon).
file(STRINGS "${base}/k91/step3.tsv" controls REGEX "^control\t")
set(noticeRect "")
set(mainRect "")
foreach(line IN LISTS controls)
    string(REPLACE "\t" ";" f "${line}")
    list(GET f 1 cid)
    list(GET f 2 cclass)
    if(cid EQUAL 175)
        set(noticeRect "${line}")
        set(noticeClass "${cclass}")
        list(SUBLIST f 3 5 noticeBox)
    elseif(cid EQUAL 161)
        list(SUBLIST f 3 5 mainBox)
        list(GET f 12 mainText)
    endif()
endforeach()
if(noticeRect STREQUAL "" OR NOT DEFINED mainBox)
    message(FATAL_ERROR "FAILED: step3.tsv has no notice (175) or main button (161)")
endif()
list(GET noticeBox 1 nTop)
list(GET noticeBox 3 nBottom)
list(GET noticeBox 4 nShown)
list(GET mainBox 1 mTop)
if(NOT nShown EQUAL 1 OR NOT noticeClass STREQUAL "Static" OR nBottom GREATER mTop OR NOT mainText STREQUAL "Generate demo files")
    message(FATAL_ERROR "FAILED: the notice is not a shown label above \"Generate demo files\" (${noticeRect}; main ${mainText} at ${mTop})")
endif()
set(beside 0)
foreach(line IN LISTS controls)
    string(REPLACE "\t" ";" f "${line}")
    list(GET f 1 cid)
    list(GET f 4 cTop)
    list(GET f 6 cBottom)
    list(GET f 7 cShown)
    if(NOT cid EQUAL 175 AND cShown EQUAL 1 AND cBottom GREATER nTop AND cTop LESS nBottom)
        math(EXPR beside "${beside} + 1")
        message("control beside the notice: ${line}")
    endif()
endforeach()
if(NOT beside EQUAL 0)
    message(FATAL_ERROR "FAILED: ${beside} control(s) shown beside the notice (it stands alone, no icon)")
endif()
message("kit passed: K1 the notice - shown above \"Generate demo files\" as plain text, alone, the text of the file")

# The bat starts the game with the demo character.
file(READ "${base}/out-a/hires/START-DEMO-PUPPE.bat" bat)
string(REPLACE "\\" "/" batSlash "${bat}")
foreach(want "--char" "demo-puppet.rldchar" "--level 0")
    string(FIND "${bat}" "${want}" at)
    if(at LESS 0)
        message(FATAL_ERROR "FAILED: START-DEMO-PUPPE.bat lacks '${want}'\n${bat}")
    endif()
endforeach()
string(TOLOWER "${batSlash}" batLow)
string(TOLOWER "${base}/out-a/hires/demo-puppet.rldchar" charLow)
string(FIND "${batLow}" "${charLow}" at)
string(FIND "${batLow}" "%~dp0" atRel)
if(at LESS 0 AND atRel LESS 0)
    message(FATAL_ERROR "FAILED: START-DEMO-PUPPE.bat names neither the full path of demo-puppet.rldchar nor %~dp0\n${bat}")
endif()
# What the stand-in was called with.
set(sa "${base}/k91/standin.log")
expect_lit("${sa}" "--python-exit-code | 1")
expect_lit("${sa}" "-- | --kit")
expect_lit("${sa}" "--threads | 1 | --background | --factory-startup")
# The game data: the real kit-extract through the Studio's own rldpack.
expect_lit("${auto}" "rldpack kit-extract --game ")
expect_lit("${base}/k91/report.txt" "file\tkit-extract.txt\t")
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
studio(k92 0 "" --do "page char" --do "route hires" --do "model ${base}/out-a/hires/creator-kit-demo-local.glb"
       --do "name DEMO PUPPET" --do "out ${base}/page-demo.rldchar" --do "build" --do "quit")
if(NOT EXISTS "${base}/page-demo.rldchar")
    message(FATAL_ERROR "FAILED: the page Character built no page-demo.rldchar from the kit's demo model")
endif()
file(SHA256 "${base}/page-demo.rldchar" hashPage)
file(SHA256 "${base}/out-a/hires/demo-puppet.rldchar" hashKit)
if(NOT hashPage STREQUAL hashKit)
    message(FATAL_ERROR "FAILED: demo-puppet.rldchar (${hashKit}) is not what the page Character builds (${hashPage})")
endif()
# The demo's kart is the kart part (--kart-part): the podium and the menu leave it out.
execute_process(COMMAND "${STUDIO}" --rldpack hires-check "${base}/out-a/hires/demo-puppet.rldchar"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 120)
if(NOT out MATCHES "parts: [^\n]*kart part")
    message(FATAL_ERROR "FAILED: demo-puppet.rldchar is not packed with the kart part\n${out}")
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
kit_ini(ini "${base}/nogame" "${STANDIN}" "${base}/out-d")
studio(k94 1 "${ini}" --do "page kit" --do "report ${base}/k94/report.txt" --do "generate" --do "quit")
set(rep "${base}/k94/report.txt")
expect_lit("${rep}" "game\tmissing-data\t")
expect_lit("${rep}" "game-text\tCTR Reload has no game data yet.")
expect("${base}/k94/auto.log" "FAIL")
expect("${base}/k94/auto.log" "automation finished, [1-9][0-9]* step\\(s\\) failed")
check_no_kit("${base}/out-d")
message("kit passed: K9.4 no game data")

# --- K9.5: Blender missing, Blender of another version ------------------------------------------
kit_ini(ini "${base}/game" "${base}/no-blender.exe" "${base}/out-e")
studio(k95a 0 "${ini}" --do "page kit" --do "wait 1500" --do "report ${base}/k95a/report.txt" --do "quit")
set(rep "${base}/k95a/report.txt")
expect_lit("${rep}" "blender\tmissing\t")
expect_lit("${rep}" "blender-text\tBlender was not found.\tInstall Blender 5.2 from blender.org, or choose blender.exe with Choose Blender.")
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/out-e")
set(ENV{RS_KIT_STANDIN_VERSION} "Blender 3.6.0")
studio(k95b 0 "${ini}" --do "page kit" --do "wait 1500" --do "report ${base}/k95b/report.txt" --do "quit")
reset_env()
set(rep "${base}/k95b/report.txt")
expect_lit("${rep}" "blender\tversion\t")
expect_lit("${rep}" "blender-text\tThis is Blender 3.6.0; the Creator Kit needs Blender 5.2.\tInstall Blender 5.2 from blender.org.")
message("kit passed: K9.5 Blender missing and Blender 3.6.0")

# --- K9.6: cancel -----------------------------------------------------------------------------------
# First a good kit (demo off), its Blender file noted.
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/out-c")
studio(k96good 0 "${ini}" --do "page kit" --do "demo off" --do "generate" --do "report ${base}/k96good/report.txt" --do "quit")
expect_lit("${base}/k96good/report.txt" "result\tok")
check_kit("${base}/out-c" "${base}/k96good/report.txt" OFF "vanilla;lowpoly;hires")
file(SHA256 "${base}/out-c/hires/creator-kit-hires.blend" blendBefore)
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
file(SHA256 "${base}/out-c/hires/creator-kit-hires.blend" blendAfter)
if(NOT blendBefore STREQUAL blendAfter)
    message(FATAL_ERROR "FAILED: the cancel changed the old hires/creator-kit-hires.blend")
endif()
# Only this build's stand-in counts (its path): a ctest of another build folder
# may run the same test at the same time, with a stand-in of its own.
file(TO_NATIVE_PATH "${STANDIN}" standinNative)
execute_process(COMMAND powershell -NoProfile -Command
                "(@(Get-CimInstance Win32_Process -Filter \"Name='${standinName}.exe'\" -ErrorAction SilentlyContinue | Where-Object { $_.ExecutablePath -eq '${standinNative}' })).Count"
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
file(SHA256 "${base}/out-c/hires/creator-kit-hires.blend" blendAfter)
if(NOT blendBefore STREQUAL blendAfter)
    message(FATAL_ERROR "FAILED: a failed Blender run changed the old hires/creator-kit-hires.blend")
endif()
if(EXISTS "${base}/out-c/kit.part")
    message(FATAL_ERROR "FAILED: kit.part is left after a failed run")
endif()
message("kit passed: K9.7 Blender fails with its message")

# --- K9.10: one way chosen writes only that way -----------------------------------------------------------
# Each way alone into a folder of its own: only its folder, exactly its files;
# the demo is offered with Hi-Res only (asked for, it is skipped otherwise).
foreach(way vanilla lowpoly hires)
    kit_ini(ini "${base}/game" "${STANDIN}" "${base}/out-${way}")
    if(way STREQUAL "hires")
        set(demoArg off)
    else()
        set(demoArg on)
    endif()
    studio(k910${way} 0 "${ini}" --do "page kit" --do "ways ${way}" --do "demo ${demoArg}" --do "generate"
           --do "report ${base}/k910${way}/report.txt" --do "quit")
    set(rep "${base}/k910${way}/report.txt")
    expect("${base}/k910${way}/auto.log" "automation finished, 0 step\\(s\\) failed")
    expect_lit("${rep}" "result\tok")
    expect_lit("${rep}" "ways\t${way}\t${way}\n")
    if(way STREQUAL "hires")
        expect_lit("${rep}" "demo\toff\toffered")
    else()
        expect_lit("${rep}" "demo\ton\tnot offered")
    endif()
    expect_lit("${rep}" "phase\tdemo\tskipped")
    check_kit("${base}/out-${way}" "${rep}" OFF "${way}")
    expect_lit("${base}/k910${way}/standin.log" " | --routes | ${way}\n")
endforeach()
# One way into a kit of all three: that way is made again, the others stay as they were.
file(SHA256 "${base}/out-c/lowpoly/creator-kit-lowpoly.blend" lowBefore)
file(WRITE "${base}/out-c/vanilla/creator-kit-vanilla.blend" "an older template")
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/out-c")
studio(k910keep 0 "${ini}" --do "page kit" --do "ways vanilla" --do "generate" --do "report ${base}/k910keep/report.txt"
       --do "quit")
expect_lit("${base}/k910keep/report.txt" "result\tok")
file(SHA256 "${base}/out-c/lowpoly/creator-kit-lowpoly.blend" lowAfter)
file(SHA256 "${base}/out-c/hires/creator-kit-hires.blend" hiAfter)
file(READ "${base}/out-c/vanilla/creator-kit-vanilla.blend" vanNow)
if(NOT lowBefore STREQUAL lowAfter OR NOT blendBefore STREQUAL hiAfter OR vanNow STREQUAL "an older template")
    message(FATAL_ERROR "FAILED: Vanilla into a kit of all three: the other ways changed or Vanilla was not made again")
endif()
message("kit passed: K9.10 one way chosen writes only that way (3/3), a kit's other ways stay as they were; "
        "notices checked: ${noticeCount}")

# --- K9.8: the output folder rules ----------------------------------------------------------------------------
file(MAKE_DIRECTORY "${base}/repo/.git")
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/repo/sub")
studio(k98git 1 "${ini}" --do "page kit" --do "report ${base}/k98git/report.txt" --do "generate" --do "quit")
set(rep "${base}/k98git/report.txt")
expect_lit("${rep}" "out\tgit\t")
expect_lit("${rep}" "out-text\tThis folder is inside a git repository. The kit holds files from your game, so it is made outside of repositories.\tChoose a folder outside it.")
check_no_kit("${base}/repo/sub")
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/game/kit")
studio(k98game 1 "${ini}" --do "page kit" --do "report ${base}/k98game/report.txt" --do "generate" --do "quit")
set(rep "${base}/k98game/report.txt")
expect_lit("${rep}" "out\tgame\t")
expect_lit("${rep}" "out-text\tThis folder is inside the game's folder.\tChoose a folder outside the game, for example in Documents.")
check_no_kit("${base}/game/kit")

# --- K7: Start from an old character ---------------------------------------------------
# An old character made here (the test model through make-char, template 14):
# the page exports its model with rldpack char-export (classic.glb in the kit,
# byte for byte rldpack's) and the generator gets --classic with it.
execute_process(COMMAND "${PACKER}" make-char --model "${base}/models/route-bare.glb" --name "OLD DRIVER" --template 14
                        --rig bind-pose --out "${base}/old.rldchar"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 120)
if(NOT rc EQUAL 0 OR NOT EXISTS "${base}/old.rldchar")
    message(FATAL_ERROR "FAILED: make-char made no old character (exit ${rc})\n${out}")
endif()
execute_process(COMMAND "${PACKER}" char-export "${base}/old.rldchar" -o "${base}/old-ref.glb"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out TIMEOUT 120)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "FAILED: rldpack char-export of the old character (exit ${rc})\n${out}")
endif()
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/out-k7")
studio(k7 0 "${ini}" --do "page kit" --do "demo off" --do "classic ${base}/old.rldchar" --do "generate"
       --do "report ${base}/k7/report.txt" --do "quit")
expect_lit("${base}/k7/report.txt" "result\tok")
expect_lit("${base}/k7/report.txt" "file\tclassic.glb\t")
expect("${base}/k7/report.txt" "(^|\n)classic\t[^\n]*old\\.rldchar")
expect("${base}/k7/auto.log" "rldpack char-export [^\n]*old\\.rldchar")
expect("${base}/k7/standin.log" "--classic \\| [^\n]*classic\\.glb")
file(SHA256 "${base}/out-k7/classic.glb" hashPage)
file(SHA256 "${base}/old-ref.glb" hashRef)
if(NOT hashPage STREQUAL hashRef)
    message(FATAL_ERROR "FAILED: classic.glb of the kit (${hashPage}) is not rldpack char-export's (${hashRef})")
endif()
# Not a character file: refused before anything runs, the folder stays empty.
kit_ini(ini "${base}/game" "${STANDIN}" "${base}/out-k7bad")
studio(k7bad 1 "${ini}" --do "page kit" --do "demo off" --do "classic ${base}/models/route-bare.glb" --do "generate"
       --do "report ${base}/k7bad/report.txt" --do "quit")
expect_lit("${base}/k7bad/report.txt" "msg\terror\tThe old character cannot be read: it is no .rldchar file.\t")
check_no_kit("${base}/out-k7bad")
message("kit passed: K7 start from an old character (classic.glb = rldpack char-export, --classic to the generator, a wrong file refused)")
message("kit passed: K9.8 output inside a git work tree and inside the game refused")

# --- L.9: the content guard knows every name of the kit --------------------------------------------------------------
# The names: those of rs_kit.c between its markers KIT-OUTPUT-NAMES, and the work folder.
file(STRINGS "${SOURCE}/tools/reloadstudio/rs_kit.c" kitSource)
set(inNames OFF)
set(guardNames "'kit.part/x'")
foreach(line IN LISTS kitSource)
    if(line MATCHES "KIT-OUTPUT-NAMES BEGIN")
        set(inNames ON)
    elseif(line MATCHES "KIT-OUTPUT-NAMES END")
        set(inNames OFF)
    elseif(inNames)
        string(REGEX MATCHALL "L\"[^\"]+\"" found "${line}")
        foreach(f IN LISTS found)
            string(REGEX REPLACE "^L\"(.*)\"$" "\\1" f "${f}")
            string(APPEND guardNames ",'${f}'")
        endforeach()
    endif()
endforeach()
if(NOT guardNames MATCHES "creator-kit-hires-figure.glb")
    message(FATAL_ERROR "FAILED: the names between KIT-OUTPUT-NAMES in rs_kit.c were not read (${guardNames})")
endif()
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
           "This file contains the kart from your game files.")
expect_lit("${base}/hrg41/report.txt" "HRG-41")
message("kit passed: HRG-41 on the page Character")

message("kit passed: K9.1-K9.10, K1, L.9 and HRG-41 (page end to end in the layout K4, the notice, one way alone, demo equals the Character page, game data, Blender, cancel, failure, output rules, environment, names, the kit kart warning)")
