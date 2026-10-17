# The module loader of Reload Studio (tools/reloadstudio/rs_module.c), ctest
# reloadstudio_modules:
#
#   cmake -DSTUDIO=<ReloadStudio.exe> -DDIR=<folder> -DMODULES=<dll|dll|...>
#         -P run_rs_modules.cmake
#
# The test DLLs (tools/reloadstudio/rs_module_test.c, one per variant) are
# COPIED into a folder of this run, <folder>/run-<random>/modules, with a text
# file named not-a-dll.dll beside them: Reload Studio loads the copies, never
# the linker's files, so a virus scanner that holds a loaded DLL for a while
# cannot stop the next build. Older run folders are removed where they can be.
# Every Studio run has its own settings file (with the game program set to a
# Windows program, so that no run starts the game) and so its own modules.log.
#
#   a  --modules-dir: one line per file in modules.log, the two good modules
#      answer their verbs, both crashes are caught (create at the start,
#      automate on a verb) and turn their module off, Track still works, the
#      sidebar lists the good modules and not the crashed ones
#   b  --no-modules: no module, no modules.log, one line in the log
#   c  a modules folder that does not exist: no modules.log, one line
#
# Output: "modules passed: ..." or "FAILED: ...".

foreach(required STUDIO DIR MODULES)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "FAILED: -D${required}=... is missing")
    endif()
endforeach()
# The DLLs come separated by "|" (a ";" would split the argument of add_test).
string(REPLACE "|" ";" MODULES "${MODULES}")

file(MAKE_DIRECTORY "${DIR}")
file(GLOB oldRuns LIST_DIRECTORIES true "${DIR}/run-*")
foreach(old IN LISTS oldRuns)
    execute_process(COMMAND ${CMAKE_COMMAND} -E rm -rf "${old}" RESULT_VARIABLE ignored)
endforeach()
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef run)
set(base "${DIR}/run-${run}")
file(MAKE_DIRECTORY "${base}/modules")
foreach(m IN LISTS MODULES)
    file(COPY "${m}" DESTINATION "${base}/modules")
endforeach()
file(WRITE "${base}/modules/not-a-dll.dll" "This is a text file, not a DLL.\n")
list(LENGTH MODULES dllCount)
math(EXPR fileCount "${dllCount} + 1")

# Temporary files of every program started here: below this run's folder.
file(MAKE_DIRECTORY "${base}/temp")
file(TO_NATIVE_PATH "${base}/temp" tempNative)
set(ENV{TEMP} "${tempNative}")
set(ENV{TMP} "${tempNative}")

function(studio name expectExit)
    file(MAKE_DIRECTORY "${base}/${name}")
    file(WRITE "${base}/${name}/rs.ini" "[reloadstudio]\r\ntest.exe=C:\\Windows\\System32\\hostname.exe\r\n")
    execute_process(COMMAND "${STUDIO}" --settings "${base}/${name}/rs.ini" --log "${base}/${name}/auto.log" ${ARGN}
                    RESULT_VARIABLE rc TIMEOUT 120)
    if(NOT rc STREQUAL "${expectExit}")
        message(FATAL_ERROR "FAILED: run ${name}: Reload Studio ended with '${rc}', expected ${expectExit}")
    endif()
endfunction()

function(expect file regex)
    file(READ "${file}" text)
    if(NOT text MATCHES "${regex}")
        message(FATAL_ERROR "FAILED: ${file} has no line matching '${regex}'")
    endif()
endfunction()

function(expect_not file regex)
    file(READ "${file}" text)
    if(text MATCHES "${regex}")
        message(FATAL_ERROR "FAILED: ${file} has a line matching '${regex}'")
    endif()
endfunction()

# --- a: the modules folder ---------------------------------------------------
studio(a 1 --modules-dir "${base}/modules"
       --do "page ok-test" --do "ok-ping" --do "page ok-two" --do "ok2-ping"
       --do "page crash-auto" --do "crash-now" --do "page crash-auto" --do "page crash-create"
       --do "page track" --do "controls ${base}/a/controls.tsv" --do "quit")
set(log "${base}/a/modules.log")
if(NOT EXISTS "${log}")
    message(FATAL_ERROR "FAILED: run a wrote no modules.log")
endif()
expect("${log}" "modules: folder [^\n]* - ${fileCount} file\\(s\\)\n")
expect("${log}" "\nmodule rs_modtest_ok\\.dll: loaded \"Test module\" \\(page ok-test\\)\n")
expect("${log}" "\nmodule rs_modtest_ok2\\.dll: loaded \"Test module\" \\(page ok-two\\)\n")
expect("${log}" "\nmodule rs_modtest_crash_auto\\.dll: loaded \"Test module\" \\(page crash-auto\\)\n")
expect("${log}" "\nmodule rs_modtest_crash_create\\.dll: loaded \"Test module\" \\(page crash-create\\)\n")
expect("${log}" "\nmodule rs_modtest_abi\\.dll: not loaded - made for module interface 99, this Reload Studio has 1\n")
expect("${log}" "\nmodule rs_modtest_core\\.dll: not loaded - needs Reload Studio 99\\.0\\.0 or newer, this is [0-9]+\\.[0-9]+\\.[0-9]+\n")
expect("${log}" "\nmodule rs_modtest_noentry\\.dll: not loaded - it has no function ReloadStudioModule\n")
expect("${log}" "\nmodule rs_modtest_null\\.dll: not loaded - ReloadStudioModule gave no module\n")
expect("${log}" "\nmodule rs_modtest_dupword\\.dll: not loaded - its page word 'track' is taken by Track\n")
expect("${log}" "\nmodule rs_modtest_needs\\.dll: not loaded - needs the module 'no-such-module', which is not there\n")
expect("${log}" "\nmodule rs_modtest_crash_entry\\.dll: not loaded - crashed while loading \\(exception 0xC0000005\\)\n")
expect("${log}" "\nmodule rs_modtest_badword\\.dll: not loaded - its page word 'Bad Word' is not valid")
expect("${log}" "\nmodule not-a-dll\\.dll: not loaded - it is not a 32-bit Windows DLL \\(error 193")
expect("${log}" "\nmodule crash-create: crashed in create \\(exception 0xC0000005\\) - turned off until Reload Studio starts again\n")
expect("${log}" "\nmodule crash-auto: crashed in automate \\(exception 0xC0000005\\) - turned off until Reload Studio starts again\n")
expect("${log}" "\nmodules: 4 loaded, 9 not loaded \\(details in ")
# Exactly one line per file.
file(STRINGS "${log}" perFile REGEX "^module (rs_modtest_[a-z0-9_]+|not-a-dll)\\.dll: ")
list(LENGTH perFile perFileCount)
if(NOT perFileCount EQUAL fileCount)
    message(FATAL_ERROR "FAILED: modules.log has ${perFileCount} lines for files, expected ${fileCount}")
endif()
set(auto "${base}/a/auto.log")
expect("${auto}" "ok-test: pong")
expect("${auto}" "ok-two: pong")
expect("${auto}" "FAIL: the module 'crash-auto' was turned off")
expect("${auto}" "FAIL: the module 'crash-create' was turned off")
expect("${auto}" "automation finished, 3 step\\(s\\) failed")
set(side "${base}/a/controls.tsv")
expect("${side}" "\nside\tok-test\t")
expect("${side}" "\nside\tok-two\t")
expect("${side}" "\nside\ttrack\t")
expect("${side}" "\npage\ttrack\t")
expect_not("${side}" "\nside\tcrash-")

# --- b: --no-modules ---------------------------------------------------------
studio(b 1 --modules-dir "${base}/modules" --no-modules --do "page ok-test" --do "quit")
if(EXISTS "${base}/b/modules.log")
    message(FATAL_ERROR "FAILED: run b (--no-modules) wrote modules.log")
endif()
expect("${base}/b/auto.log" "modules: external modules not loaded \\(--no-modules\\)")
expect("${base}/b/auto.log" "FAIL: unknown page 'ok-test'")
expect_not("${base}/b/auto.log" "\nmodule ")

# --- c: no modules folder -----------------------------------------------------
studio(c 0 --modules-dir "${base}/no-such-folder" --do "page track" --do "quit")
if(EXISTS "${base}/c/modules.log")
    message(FATAL_ERROR "FAILED: run c (no folder) wrote modules.log")
endif()
expect("${base}/c/auto.log" "modules: no folder [^\n]*no-such-folder - built-in modules only")

message("modules passed: ${fileCount} files, 4 loaded, 9 not loaded, 2 crashes caught, --no-modules and no folder")
