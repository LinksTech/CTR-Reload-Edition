cmake_minimum_required(VERSION 3.20)
project(CTR-Native C)
include(CheckCCompilerFlag)

# The version players see: "CTR Reload <version> (<build id>)" on the start
# screen, in --version, in Reload Studio and in the log. The upstream
# ctr-native version number is not shown to players.
# tools/package/package.sh reads this line to name the package.
set(CTR_NATIVE_VERSION "0.7.5 Beta")

# The version of the tools, Reload Studio and rldpack together - one number for
# both, defined only on the next line: "rldpack <version> (<build id>)" in
# rldpack --version, "<version> (<build id>)" in the sidebar of Reload Studio,
# "Reload Studio <version> (<build id>)" in its help, and the version resources
# of both exes. The game keeps CTR_NATIVE_VERSION, the container format its own
# numbers (RLD_VERSION_MAJOR/MINOR in include/rldtrack.inc). The parts
# major.minor.patch are taken from this line for the version resources.
set(CTR_TOOLS_VERSION "0.9.0")
if(NOT CTR_TOOLS_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "CTR_TOOLS_VERSION must be <major>.<minor>.<patch>, not '${CTR_TOOLS_VERSION}'")
endif()
# Given to every target that compiles rldpack.c, Reload Studio or their version
# resources (tools/rldpack.rc, tools/reloadstudio/reloadstudio.rc).
set(CTR_TOOLS_VERSION_DEFINES
    CTR_TOOLS_VERSION=\"${CTR_TOOLS_VERSION}\"
    CTR_TOOLS_VERSION_MAJOR=${CMAKE_MATCH_1}
    CTR_TOOLS_VERSION_MINOR=${CMAKE_MATCH_2}
    CTR_TOOLS_VERSION_PATCH=${CMAKE_MATCH_3})

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "CTR Native currently requires a 32-bit target. "
        "Select Win32 for MSVC, an i686 MinGW compiler on Windows, or pass -m32 "
        "in the initial compiler and linker flags on Linux."
    )
endif()

set(CTR_NATIVE_MSVC_FRONTEND OFF)
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    set(CTR_NATIVE_MSVC_FRONTEND ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)

# Build ID CTR_NATIVE_BUILD_ID: created on EVERY build, not only at configure
# time - otherwise a build after new commits would carry the old ID, and the
# pairing check in tools/package/package.sh would take two states as one pair.
# The target ctr_build_id always runs (no OUTPUT, so never up to date) and
# writes generated/ctr_build_id.h only when the content is new; every consumer
# depends on it and pulls in the header via #include. Format and cases are in the
# header of cmake/CtrBuildId.cmake. git is looked up here because the build (for
# example from Visual Studio) can have a different PATH than the configure step.
find_package(Git QUIET)
if(GIT_EXECUTABLE)
    set(CTR_BUILD_ID_GIT ${GIT_EXECUTABLE})
else()
    set(CTR_BUILD_ID_GIT git)
endif()
set(CTR_BUILD_ID_DIR ${CMAKE_BINARY_DIR}/generated)
set(CTR_BUILD_ID_HEADER ${CTR_BUILD_ID_DIR}/ctr_build_id.h)
add_custom_target(ctr_build_id ALL
    COMMAND ${CMAKE_COMMAND}
        -DOUT=${CTR_BUILD_ID_HEADER}
        -DSRC=${CMAKE_SOURCE_DIR}
        -DGIT=${CTR_BUILD_ID_GIT}
        -P ${CMAKE_SOURCE_DIR}/cmake/CtrBuildId.cmake
    BYPRODUCTS ${CTR_BUILD_ID_HEADER}
    COMMENT "Build ID from the state of the tree"
    VERBATIM
)

function(ctr_native_add_label_warning_if_supported target warning flag_check)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror=unknown-warning-option")
    endif()

    check_c_compiler_flag("${warning}" "${flag_check}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")

    if(${flag_check})
        target_compile_options(${target} PRIVATE "${warning}")
    endif()
endfunction()

# --- Waiting for a free exe before linking ----------------------------------
#
# An on-access virus scanner may keep an .exe open for a second or two after
# that exe was started (it scans the image); a link that falls into that window
# fails with LNK1104 although the build is fine. So every executable target
# runs cmake/wait_exe_free.ps1 right before its link step (PRE_LINK): when the
# linker could write the exe, it returns at once and prints nothing; else it
# names the process holding the file, waits visibly until the file is free
# (log line "exe-wait: <file> free after <ms> ms", counted per build - see the
# head of the script and BUILDING.md) and fails the build after 30 s. The link
# itself is never retried.
#
# The loop at the end of this file calls ctr_wait_exe_free for every
# executable target made in this file, so a new add_executable needs nothing
# extra. Only on a Windows host (the script is PowerShell) building Windows
# programs; anywhere else the function adds nothing.
if(CMAKE_HOST_WIN32)
    find_program(CTR_POWERSHELL NAMES powershell pwsh)
    if(NOT CTR_POWERSHELL)
        message(STATUS "PowerShell not found: executables link without waiting for a free exe")
    endif()
endif()

function(ctr_wait_exe_free target)
    if(NOT CMAKE_HOST_WIN32 OR NOT WIN32 OR NOT CTR_POWERSHELL)
        return()
    endif()
    # The Visual Studio generators run a PRE_LINK step on every build, also when
    # MSBuild then finds the exe up to date and does not link. The script gets
    # the link inputs (object files, linked library files) so that a running
    # exe does not stop a build that has nothing to link; see -Inputs in the
    # script. Ninja and Makefiles run the step only when they link.
    set(inputs_args)
    if(CMAKE_GENERATOR MATCHES "^Visual Studio")
        set(inputs "$<JOIN:$<TARGET_OBJECTS:${target}>,\n>")
        get_target_property(libs ${target} LINK_LIBRARIES)
        foreach(lib IN LISTS libs)
            if(TARGET "${lib}")
                get_target_property(aliased "${lib}" ALIASED_TARGET)
                if(aliased)
                    set(lib "${aliased}")
                endif()
                get_target_property(lib_type "${lib}" TYPE)
                if(lib_type STREQUAL "STATIC_LIBRARY" OR lib_type STREQUAL "SHARED_LIBRARY")
                    string(APPEND inputs "\n$<TARGET_LINKER_FILE:${lib}>")
                endif()
            endif()
        endforeach()
        set(inputs_file "${CMAKE_BINARY_DIR}/exe-wait/${target}-$<CONFIG>.txt")
        file(GENERATE OUTPUT "${inputs_file}" CONTENT "${inputs}\n")
        set(inputs_args -Inputs "${inputs_file}")
    endif()
    add_custom_command(TARGET ${target} PRE_LINK
        COMMAND ${CTR_POWERSHELL} -NoProfile -NonInteractive -ExecutionPolicy Bypass
            -File ${CMAKE_SOURCE_DIR}/cmake/wait_exe_free.ps1 $<TARGET_FILE:${target}>
            ${inputs_args}
        VERBATIM
    )
endfunction()

# --- Vulkan ------------------------------------------------------------------
#
# Headers only, never the import library. Every Vulkan entry point is fetched at
# runtime through SDL_Vulkan_GetVkGetInstanceProcAddr, so nothing links against
# vulkan-1.
#
# That is not a preference. The SDK ships a 64-bit import library and no 32-bit
# one, and this is a 32-bit build - there is nothing to link even if we wanted
# to. Loading dynamically is what portable Vulkan code does anyway, and it drops
# the SDK as a runtime dependency: the loader that matters is the vulkan-1.dll
# the graphics driver installs.
find_path(CTR_VULKAN_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    HINTS "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include"
)

# Vulkan is the only backend. It used to be optional because OpenGL was there to
# fall back on; with OpenGL gone there is nothing to fall back to, so a missing
# SDK is a configure error rather than a quieter build that draws nothing.
if(NOT CTR_VULKAN_INCLUDE_DIR)
    message(FATAL_ERROR "vulkan/vulkan.h was not found, and it is the only backend. Install the Vulkan SDK or set VULKAN_SDK.")
endif()

# SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)

# SDL_RENDER and SDL_DIALOG are ON, and both only for the first-start screen.
#
# They were OFF because the game itself uses neither the one nor the other
# - it draws with Vulkan and opens no file dialogs. The
# first-start screen, however, runs BEFORE the Vulkan setup, because the game's
# renderer loads exactly the data this screen is supposed to fetch first. It
# therefore needs a small drawing path of its own, and that is SDL's
# 2D renderer including its built-in debug font.
#
# A disabled SDL subsystem only shows up at RUN TIME: it compiles
# and links, and then SDL_CreateWindowAndRenderer says "SDL not built with
# rendering support". That happened on a test machine: no window, only a
# console line. SDL_ShowOpenFileDialog would have done the same, just one key
# press later.
set(SDL_RENDER ON CACHE BOOL "" FORCE)
set(SDL_DIALOG ON CACHE BOOL "" FORCE)

set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
# Pinned on: Vulkan is the only backend. A disabled SDL subsystem fails at
# run time and quietly: SDL_Vulkan_GetVkGetInstanceProcAddr would simply
# return nothing, and the search would start in the wrong place.
set(SDL_VULKAN ON CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/SDL)

# CTR Native
add_executable(ctr_native main.c)

# UTF-8 as the ANSI code page, so that paths with non-ASCII characters get
# through argv and fopen. See platform/ctr_native.manifest.
if(MSVC)
    target_sources(ctr_native PRIVATE platform/ctr_native.manifest)
endif()
set_target_properties(ctr_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(ctr_native PRIVATE ${CMAKE_SOURCE_DIR}/include)
# externals/cgltf/cgltf.h - the glTF parser of the static model (platform/engine_static_load.c).
target_include_directories(ctr_native PRIVATE ${CMAKE_SOURCE_DIR}/externals/cgltf)

# The packer for .rldtrack containers. A PROGRAM OF ITS OWN, not part of the game -
# a track author builds their container with it; the game uses nothing of it.
# Reload Studio carries the same code (see below), and the release package
# ships only Reload Studio. rldpack.exe is built for the self-test and for
# use on the command line.
#
# The version resource is only attached under MSVC. It gives the binary a
# name, an origin and a version - see the header of tools/rldpack.rc: an
# antivirus scanner once judged the file by "who is this file", not by its
# code. Under MinGW it stays out, because windres needs a tool chain
# of its own and the packer is only built along there.
#
# platform/native_disc_image.c is the game's image reader. rldpack make
# needs it for exactly one file: SOUNDS/KART.HWL from ctr-u.bin, when an
# author has no loose KART.HWL.
# The same reader instead of a second one - it only depends on Platform_Log, and
# rldpack.c provides that as a silent stub. CTR_NATIVE, because without it macros.h
# assumes the PSX side and defines size_t itself.
#
# tools/rldpack_image.c carries the JPEG and TGA decoders of stb_image (the copy
# in SDL's tree, MIT) for the textures of OBJ models - a translation unit of
# its own, so that nothing else of SDL comes along.
add_executable(rldpack tools/rldpack.c tools/rldpack_image.c platform/native_disc_image.c)
target_compile_definitions(rldpack PRIVATE CTR_NATIVE
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\" ${CTR_TOOLS_VERSION_DEFINES})
# Build ID from generated/ctr_build_id.h (target ctr_build_id, above).
target_include_directories(rldpack PRIVATE ${CTR_BUILD_ID_DIR})
add_dependencies(rldpack ctr_build_id)

# make lists the track folder and resolves paths (opendir, realpath,
# readlink). With C_EXTENSIONS OFF glibc hides those as long as nobody
# asks for POSIX.
if(NOT WIN32)
    target_compile_definitions(rldpack PRIVATE _DEFAULT_SOURCE)
endif()

# include/rldtrack.inc - the format, written once, used from two sides.
# The game pulls in the same file; see its header.
target_include_directories(rldpack PRIVATE ${CMAKE_SOURCE_DIR}/include)
# externals/cgltf/cgltf.h - the glTF parser of make-char (tools/rldpack_gltf.inc).
target_include_directories(rldpack PRIVATE ${CMAKE_SOURCE_DIR}/externals/cgltf)

if(MSVC)
    target_sources(rldpack PRIVATE tools/rldpack.rc platform/rldpack.manifest)
endif()

set_target_properties(rldpack PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# FLOATING POINT, PINNED for rldpack and Reload Studio (which carries the same
# code). make-char computes in double, and a container must come out byte for
# byte the same on every machine - the goldens in tools/rldpack_char.inc check
# exactly that. It holds as long as every + - * / is rounded to double on its
# own: SSE2, no contraction into FMA, no reordering. /fp:precise and
# /arch:SSE2 are what MSVC does for x86 anyway; they are written down here so
# that a global /fp:fast or a changed default cannot change containers without
# a word. GCC and Clang for i686 would compute double in the 80-bit x87 unit
# instead; -msse2 -mfpmath=sse gives them the same arithmetic as MSVC,
# -ffp-contract=off and -fno-fast-math keep FMA and reordering out.
# tools/rldpack.c stops the build with #error if they are overridden anyway.
if(CTR_NATIVE_MSVC_FRONTEND)
    set(CTR_RLDPACK_FP_OPTIONS /fp:precise /arch:SSE2)
else()
    set(CTR_RLDPACK_FP_OPTIONS -msse2 -mfpmath=sse -ffp-contract=off -fno-fast-math)
endif()
target_compile_options(rldpack PRIVATE ${CTR_RLDPACK_FP_OPTIONS})

# The files of the Creator Kit that Reload Studio carries inside (page Creator
# Kit, tools/reloadstudio/rs_kit.c): its Blender scripts and its guide, as
# RCDATA (ids RS_RES_KIT_* in reloadstudio.h). A resource script of their full
# paths in the build folder; a change of one of the files builds it again.
set(CTR_KIT_RESOURCES
    templates/make_creator_kit.py
    templates/creator_kit_puppet.py
    templates/creator_kit_clips.py
    templates/creator_kit_standin.py
    docs/CREATOR_KIT.html
    templates/creator_kit_classic.py)
set(ctr_kit_rc "// Made by CMakeLists.txt: the files of the Creator Kit (rs_kit.c).
")
set(ctr_kit_id 201)
foreach(f IN LISTS CTR_KIT_RESOURCES)
    string(APPEND ctr_kit_rc "${ctr_kit_id} RCDATA \"${CMAKE_SOURCE_DIR}/${f}\"
")
    math(EXPR ctr_kit_id "${ctr_kit_id} + 1")
endforeach()
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/reloadstudio_kit.rc CONTENT "${ctr_kit_rc}")
list(TRANSFORM CTR_KIT_RESOURCES PREPEND ${CMAKE_SOURCE_DIR}/ OUTPUT_VARIABLE ctr_kit_files)
set_source_files_properties(${CMAKE_BINARY_DIR}/generated/reloadstudio_kit.rc PROPERTIES OBJECT_DEPENDS "${ctr_kit_files}")

# Reload Studio: the front end for track authors, see the header of
# tools/reloadstudio/reloadstudio.h. ONE exe that carries rldpack inside
# (rs_rldpack.c: the same tools/rldpack.c with a renamed main) and starts itself
# as a child process for every pack and check step - no second
# implementation of a check. Win32 front end, so only under MSVC; under
# MinGW it stays out like rldpack's version resource.
if(MSVC)
    add_executable(ReloadStudio WIN32
        tools/reloadstudio/rs_shell.c
        tools/reloadstudio/rs_module.c
        tools/reloadstudio/rs_track.c
        tools/reloadstudio/rs_cups.c
        tools/reloadstudio/rs_test.c
        tools/reloadstudio/rs_char.c
        tools/reloadstudio/rs_view.c
        tools/reloadstudio/rs_anim.c
        tools/reloadstudio/rs_wheels.c
        tools/reloadstudio/rs_tex.c
        tools/reloadstudio/rs_hires_model.c
        tools/reloadstudio/rs_charhires.c
        tools/reloadstudio/rs_cards.c
        tools/reloadstudio/rs_kit.c
        tools/reloadstudio/rs_rldpack.c
        tools/rldpack_image.c
        platform/native_disc_image.c
        tools/reloadstudio/reloadstudio.rc
        ${CMAKE_BINARY_DIR}/generated/reloadstudio_kit.rc
        tools/reloadstudio/reloadstudio.manifest
    )
    # The tools version (CTR_TOOLS_VERSION, above) for the sidebar, the help and
    # the version resource; the game's version for the help ("for CTR Reload
    # ..."); the same build ID as the game for the title and the pairing check -
    # from generated/ctr_build_id.h, new on every build (target ctr_build_id).
    target_compile_definitions(ReloadStudio PRIVATE CTR_NATIVE
        CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\" ${CTR_TOOLS_VERSION_DEFINES})
    target_include_directories(ReloadStudio PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/externals/cgltf ${CTR_BUILD_ID_DIR})
    add_dependencies(ReloadStudio ctr_build_id)
    # The same floating point flags as rldpack (see there).
    target_compile_options(ReloadStudio PRIVATE ${CTR_RLDPACK_FP_OPTIONS})
    target_link_libraries(ReloadStudio PRIVATE comctl32 comdlg32 shell32 ole32 uuid uxtheme)
    set_target_properties(ReloadStudio PROPERTIES
        OUTPUT_NAME ReloadStudio
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
endif()

# Assembles the shader sources exactly as the renderer does. Used twice: to
# generate the SPIR-V the renderer links in, and by the ctest
# shader_spirv_dialect, which names the shader that broke.
add_executable(shader_spirv_probe tools/shader_spirv_probe.c)
set_target_properties(shader_spirv_probe PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

find_program(CTR_GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
)

set(CTR_SPV_HEADER ${CMAKE_BINARY_DIR}/generated/native_shaders_spv.h)

add_custom_command(
    OUTPUT ${CTR_SPV_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DPROBE=$<TARGET_FILE:shader_spirv_probe>
        -DOUTDIR=${CMAKE_BINARY_DIR}/generated/shader_sources
        -DVALIDATOR=${CTR_GLSLANG_VALIDATOR}
        -DHEADER=${CTR_SPV_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_shaders.cmake
    DEPENDS
        shader_spirv_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/native_shaders.inc
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/engine_water_shader.inc
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/engine_static_shader.inc
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_shaders.cmake
    COMMENT "Compiling shaders to SPIR-V"
    VERBATIM
)

add_custom_target(ctr_native_shaders DEPENDS ${CTR_SPV_HEADER})
add_dependencies(ctr_native ctr_native_shaders)

# Build ID: generated/ctr_build_id.h lies in the same folder as the headers
# above it (CTR_BUILD_ID_DIR), the include path below already carries it.
add_dependencies(ctr_native ctr_build_id)

target_include_directories(ctr_native PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CTR_VULKAN_INCLUDE_DIR}
)
target_compile_definitions(ctr_native PRIVATE
    CTR_NATIVE
    CTR_INTERNAL
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\"
)
if(CTR_NATIVE_MSVC_FRONTEND)
    target_compile_options(ctr_native PRIVATE
        /W4 #TODO(aalhendi): fix warnings
        /bigobj
        /arch:SSE2
        /wd4152
        /wd4244
        /wd4245
        /wd4310
        /wd5287

        # Symbols in the release build too.
        #
        # /Zi switches no optimisation off, it only writes a .pdb next to it.
        # Without it the crash report in native_platform.c reports bare
        # addresses, and an address without a name is half an answer.
        /Zi
    )
else()
    target_compile_options(ctr_native PRIVATE
        -msse
        -Wenum-conversion
        -Wenum-compare
        -Wswitch-enum
        -Wformat
        -Wparentheses
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Release>:-g>
    )

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(ctr_native PRIVATE -Wall -Wextra -Wstrict-aliasing=2)
        ctr_native_add_label_warning_if_supported(ctr_native -Wfree-labels CTR_HAS_WFREE_LABELS)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        ctr_native_add_label_warning_if_supported(ctr_native -Wc23-extensions CTR_HAS_WC23_EXTENSIONS)
    endif()
endif()
target_link_libraries(ctr_native SDL3::SDL3)
if(MSVC)
    # /DEBUG so that the .pdb is actually written, /OPT:REF and /OPT:ICF because
    # /DEBUG otherwise switches them off and the binary grows.
    target_link_options(ctr_native PRIVATE /DEBUG /OPT:REF /OPT:ICF)

    # dbghelp for the crash report: symbol names and line numbers from the
    # .pdb. Part of the Windows SDK, no new dependency.
    target_link_libraries(ctr_native dbghelp)
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_options(ctr_native PRIVATE -static-libgcc)
endif()
if(MINGW)
    target_link_options(ctr_native PRIVATE -static)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME ctr_native_version COMMAND $<TARGET_FILE:ctr_native> --version)

    # SHA-256 against RFC 6234, the determinism the container format requires
    # (built twice from the same inputs, compared byte for byte) and the make
    # step against fixed cases. The hashing is self-written in this tree, so it
    # has to prove itself.
    add_test(NAME rldpack_selftest COMMAND $<TARGET_FILE:rldpack> selftest)
    # The self-test of the reduction's cache (tools/rldpack_reduce.inc,
    # RldRed_TestCache) writes, damages and removes rldpack-reduce-<nn>.bin in
    # TEMP. Each test that runs it gets a TEMP and TMP of its own in the build
    # tree (made here), so that the rldpack and the Reload Studio copy of it,
    # and two test runs at the same time, never touch one file. The folder is
    # empty after a run: the test removes its file.
    set(ctr_selftest_temp_rldpack "${CMAKE_BINARY_DIR}/selftest/temp-rldpack")
    file(MAKE_DIRECTORY "${ctr_selftest_temp_rldpack}")
    file(TO_NATIVE_PATH "${ctr_selftest_temp_rldpack}" ctr_selftest_temp_rldpack)
    set_tests_properties(rldpack_selftest PROPERTIES
        ENVIRONMENT "TEMP=${ctr_selftest_temp_rldpack};TMP=${ctr_selftest_temp_rldpack}")
    # rldpack --version: "rldpack <CTR_TOOLS_VERSION> (<build id>)" and the
    # line "container format <major>.<minor>", the numbers read here from
    # include/rldtrack.inc - the tools version and the format each from its one
    # definition. The game's version test (ctr_native_version) is below.
    # Read at configure time: a changed RLD_VERSION_MINOR configures again,
    # so the expected line never stays at the old number.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/include/rldtrack.inc)
    file(STRINGS ${CMAKE_SOURCE_DIR}/include/rldtrack.inc ctr_rld_format_lines
         REGEX "^#define RLD_VERSION_(MAJOR|MINOR) [0-9]+$")
    string(REGEX REPLACE ".*RLD_VERSION_MAJOR ([0-9]+).*" "\\1" ctr_rld_major "${ctr_rld_format_lines}")
    string(REGEX REPLACE ".*RLD_VERSION_MINOR ([0-9]+).*" "\\1" ctr_rld_minor "${ctr_rld_format_lines}")
    string(REPLACE "." "\\." ctr_tools_version_re "${CTR_TOOLS_VERSION}")
    set(ctr_rldpack_version_re
        "^rldpack ${ctr_tools_version_re} \\([^)]+\\)\r?\ncontainer format ${ctr_rld_major}\\.${ctr_rld_minor}\r?\n$")
    add_test(NAME rldpack_version COMMAND $<TARGET_FILE:rldpack> --version)
    set_tests_properties(rldpack_version PROPERTIES PASS_REGULAR_EXPRESSION "${ctr_rldpack_version_re}")
    # "version" is the same command.
    add_test(NAME rldpack_version_word COMMAND $<TARGET_FILE:rldpack> version)
    set_tests_properties(rldpack_version_word PROPERTIES PASS_REGULAR_EXPRESSION "${ctr_rldpack_version_re}")
    # The hi-res driver (chunk CHRG, include/rldhires.inc): every rule HRG-n
    # fired and not fired on GLBs made in code, the clip chains, the budget, and
    # a container without CHRG read as before (tools/rldpack_hires_test.inc).
    add_test(NAME rldhires_selftest COMMAND $<TARGET_FILE:rldpack> selftest-hires)
    set_tests_properties(rldhires_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "hires selftest passed: [0-9]+ checks, 0 failures"
        FAIL_REGULAR_EXPRESSION "FAIL")
    # hires-check, hires-add and hires-strip on files: the same bytes twice,
    # strip of add is the character again byte for byte, replace, a model with
    # an error writes nothing (cmake/run_rldhires_files.cmake).
    add_test(NAME rldhires_files
        COMMAND ${CMAKE_COMMAND}
            -DPACKER=$<TARGET_FILE:rldpack>
            -DDIR=${CMAKE_BINARY_DIR}/selftest/rldhires
            -P ${CMAKE_SOURCE_DIR}/cmake/run_rldhires_files.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(rldhires_files PROPERTIES
        PASS_REGULAR_EXPRESSION "rldhires files passed: [0-9]+ checks"
        TIMEOUT 300)
    # The Creator Kit (include/rldkit.inc) on a code-made stand-in of the game
    # data: decoding, the kart told from the figure, smoothing, atlas, wheels,
    # the GLBs through the hi-res rules (tools/rldpack_kit.inc).
    add_test(NAME rldkit_selftest COMMAND $<TARGET_FILE:rldpack> selftest-kit)
    set_tests_properties(rldkit_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "kit selftest passed: [0-9]+ checks, 0 failures"
        FAIL_REGULAR_EXPRESSION "FAIL")
    # kit-standin and kit-extract on files (cmake/run_rldkit_files.cmake).
    add_test(NAME rldkit_files
        COMMAND ${CMAKE_COMMAND}
            -DPACKER=$<TARGET_FILE:rldpack>
            -DDIR=${CMAKE_BINARY_DIR}/selftest/rldkit
            -P ${CMAKE_SOURCE_DIR}/cmake/run_rldkit_files.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(rldkit_files PROPERTIES
        PASS_REGULAR_EXPRESSION "rldkit files passed: [0-9]+ checks"
        TIMEOUT 300)
    # The same self-test through the rldpack inside Reload Studio: proves that the
    # built-in version is the same one and talks over the pipe.
    if(MSVC)
        add_test(NAME reloadstudio_rldpack_selftest COMMAND $<TARGET_FILE:ReloadStudio> --rldpack selftest)
        # A TEMP of its own, as rldpack_selftest above (the cache's self-test).
        set(ctr_selftest_temp_rs_rldpack "${CMAKE_BINARY_DIR}/selftest/temp-rs-rldpack")
        file(MAKE_DIRECTORY "${ctr_selftest_temp_rs_rldpack}")
        file(TO_NATIVE_PATH "${ctr_selftest_temp_rs_rldpack}" ctr_selftest_temp_rs_rldpack)
        set_tests_properties(reloadstudio_rldpack_selftest PROPERTIES PASS_REGULAR_EXPRESSION "Self-test passed"
            ENVIRONMENT "TEMP=${ctr_selftest_temp_rs_rldpack};TMP=${ctr_selftest_temp_rs_rldpack}")
        # The rldpack inside Reload Studio has the same version.
        add_test(NAME reloadstudio_rldpack_version COMMAND $<TARGET_FILE:ReloadStudio> --rldpack --version)
        set_tests_properties(reloadstudio_rldpack_version PROPERTIES PASS_REGULAR_EXPRESSION "${ctr_rldpack_version_re}")
        # The help of Reload Studio (--help into a pipe): its version, the game's
        # and the container format.
        string(REPLACE "." "\\." ctr_native_version_re "${CTR_NATIVE_VERSION}")
        add_test(NAME reloadstudio_version COMMAND $<TARGET_FILE:ReloadStudio> --help)
        set_tests_properties(reloadstudio_version PROPERTIES PASS_REGULAR_EXPRESSION
            "^Reload Studio ${ctr_tools_version_re} \\([^)]+\\) -[^\n]*\r?\nfor CTR Reload ${ctr_native_version_re}, container format ${ctr_rld_major}\\.${ctr_rld_minor}\\.")
        # And the hi-res rules (include/rldhires.inc) inside Reload Studio: the same code.
        add_test(NAME reloadstudio_kit_selftest COMMAND $<TARGET_FILE:ReloadStudio> --rldpack selftest-kit)
        set_tests_properties(reloadstudio_kit_selftest PROPERTIES
            PASS_REGULAR_EXPRESSION "kit selftest passed: [0-9]+ checks, 0 failures"
            FAIL_REGULAR_EXPRESSION "FAIL")
        add_test(NAME reloadstudio_rldhires_selftest COMMAND $<TARGET_FILE:ReloadStudio> --rldpack selftest-hires)
        set_tests_properties(reloadstudio_rldhires_selftest PROPERTIES
            PASS_REGULAR_EXPRESSION "hires selftest passed: [0-9]+ checks, 0 failures"
            FAIL_REGULAR_EXPRESSION "FAIL")

        # The textures of the Studio's 3D preview (tools/reloadstudio/rs_tex.c):
        # rldpack's make-char and char-wheel write the preview blocks, rs_tex.c
        # reads them back; the levels against the game's goldens, the colour
        # formula against the float32 shader. A console program of its own
        # (tools/reloadstudio/rs_tex_test.c), only built with the tests.
        add_executable(rs_tex_selftest
            tools/reloadstudio/rs_tex_test.c
            tools/reloadstudio/rs_tex.c
            tools/rldpack_image.c
            platform/native_disc_image.c
            tools/rldpack.rc
            platform/rldpack.manifest
        )
        target_compile_definitions(rs_tex_selftest PRIVATE CTR_NATIVE
            CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\" ${CTR_TOOLS_VERSION_DEFINES})
        target_include_directories(rs_tex_selftest PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/externals/cgltf ${CTR_BUILD_ID_DIR})
        add_dependencies(rs_tex_selftest ctr_build_id)
        target_compile_options(rs_tex_selftest PRIVATE ${CTR_RLDPACK_FP_OPTIONS})
        set_target_properties(rs_tex_selftest PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        add_test(NAME reloadstudio_tex_selftest COMMAND $<TARGET_FILE:rs_tex_selftest>)
        set_tests_properties(reloadstudio_tex_selftest PROPERTIES PASS_REGULAR_EXPRESSION "rs_tex selftest passed: ")

        # The hi-res model side of the page Character (tools/reloadstudio/rs_hires_model.c): its
        # list, summary and bind-pose picture against rldpack's findings, a
        # character's CHRG, and the pack step - the Studio's hires-add with the
        # page's arguments gives rldpack's bytes (cmake/run_rs_hires_selftest.cmake).
        add_executable(rs_hires_selftest
            tools/reloadstudio/rs_hires_test.c
            tools/reloadstudio/rs_hires_model.c
            tools/reloadstudio/rs_charhires.c
            tools/reloadstudio/rs_tex.c
            tools/rldpack.rc
            platform/rldpack.manifest
        )
        target_compile_definitions(rs_hires_selftest PRIVATE CTR_NATIVE
            CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\" ${CTR_TOOLS_VERSION_DEFINES})
        target_include_directories(rs_hires_selftest PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/externals/cgltf ${CTR_BUILD_ID_DIR})
        add_dependencies(rs_hires_selftest ctr_build_id)
        target_compile_options(rs_hires_selftest PRIVATE ${CTR_RLDPACK_FP_OPTIONS})
        set_target_properties(rs_hires_selftest PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        add_test(NAME reloadstudio_hires_selftest
            COMMAND ${CMAKE_COMMAND}
                -DPACKER=$<TARGET_FILE:rldpack>
                -DSTUDIO=$<TARGET_FILE:ReloadStudio>
                -DTEST=$<TARGET_FILE:rs_hires_selftest>
                -DDIR=${CMAKE_BINARY_DIR}/selftest/rs-hires
                -P ${CMAKE_SOURCE_DIR}/cmake/run_rs_hires_selftest.cmake
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
        set_tests_properties(reloadstudio_hires_selftest PROPERTIES
            PASS_REGULAR_EXPRESSION "rs_hires files passed: sha256 [0-9a-f]+"
            FAIL_REGULAR_EXPRESSION "FAIL"
            TIMEOUT 300)

        # The build chain of the guided flow (page Character, routes Low-Poly and
        # Hi-Res, rs_charroute.h): make-char with the page's switches and
        # RsCharHires_FallbackArgs, hires-upscale, hires-add - through Reload
        # Studio's --rldpack the same bytes as rldpack's own (cmake/run_rs_charroute.cmake).
        add_test(NAME reloadstudio_charroute
            COMMAND ${CMAKE_COMMAND}
                -DPACKER=$<TARGET_FILE:rldpack>
                -DSTUDIO=$<TARGET_FILE:ReloadStudio>
                -DTEST=$<TARGET_FILE:rs_hires_selftest>
                -DDIR=${CMAKE_BINARY_DIR}/selftest/rs-charroute
                -P ${CMAKE_SOURCE_DIR}/cmake/run_rs_charroute.cmake
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
        set_tests_properties(reloadstudio_charroute PROPERTIES
            PASS_REGULAR_EXPRESSION "charroute passed: 3 routes"
            FAIL_REGULAR_EXPRESSION "FAILED"
            TIMEOUT 300)
        # The same chain through the page Character in automation (route, model,
        # name, out, build): the page's file has the SHA-256 of rldpack's chain.
        add_test(NAME reloadstudio_charroute_page
            COMMAND ${CMAKE_COMMAND}
                -DPACKER=$<TARGET_FILE:rldpack>
                -DSTUDIO=$<TARGET_FILE:ReloadStudio>
                -DTEST=$<TARGET_FILE:rs_hires_selftest>
                -DDIR=${CMAKE_BINARY_DIR}/selftest/rs-charroute-page
                -DPAGE=ON
                -P ${CMAKE_SOURCE_DIR}/cmake/run_rs_charroute.cmake
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
        set_tests_properties(reloadstudio_charroute_page PROPERTIES
            PASS_REGULAR_EXPRESSION "charroute page passed: 3 routes"
            FAIL_REGULAR_EXPRESSION "FAILED"
            TIMEOUT 600)

        # The module loader of Reload Studio (tools/reloadstudio/rs_module.c):
        # one small DLL per variant of tools/reloadstudio/rs_module_test.c (a
        # good module, one that needs it, crashes in create and in automate,
        # another module interface, a newer core, no entry, no module, a word
        # taken, a module missing, a crash in the entry, a bad word), only with
        # the tests. cmake/run_rs_modules.cmake copies them into a folder of its
        # run and checks modules.log, the automation log and the sidebar.
        set(ctr_modtest_variants ok ok2 crash_create crash_auto abi core noentry null
            dupword needs crash_entry badword)
        set(ctr_modtest_dlls "")
        set(ctr_modtest_n 0)
        foreach(variant IN LISTS ctr_modtest_variants)
            math(EXPR ctr_modtest_n "${ctr_modtest_n} + 1")
            add_library(rs_modtest_${variant} SHARED tools/reloadstudio/rs_module_test.c)
            target_compile_definitions(rs_modtest_${variant} PRIVATE RS_TEST_VARIANT=${ctr_modtest_n})
            set_target_properties(rs_modtest_${variant} PROPERTIES
                OUTPUT_NAME rs_modtest_${variant}
                C_STANDARD 17
                C_STANDARD_REQUIRED ON
                C_EXTENSIONS OFF
            )
            # "|" between the files: a ";" would split the argument of add_test.
            if(ctr_modtest_dlls)
                string(APPEND ctr_modtest_dlls "|")
            endif()
            string(APPEND ctr_modtest_dlls "$<TARGET_FILE:rs_modtest_${variant}>")
        endforeach()
        add_test(NAME reloadstudio_modules
            COMMAND ${CMAKE_COMMAND}
                -DSTUDIO=$<TARGET_FILE:ReloadStudio>
                -DMODULES=${ctr_modtest_dlls}
                -DDIR=${CMAKE_BINARY_DIR}/selftest/rs-modules
                -P ${CMAKE_SOURCE_DIR}/cmake/run_rs_modules.cmake
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
        set_tests_properties(reloadstudio_modules PROPERTIES
            PASS_REGULAR_EXPRESSION "modules passed: "
            FAIL_REGULAR_EXPRESSION "FAILED"
            TIMEOUT 300)
    endif()
    set_tests_properties(ctr_native_version PROPERTIES PASS_REGULAR_EXPRESSION "^CTR Reload ${CTR_NATIVE_VERSION} ")


    # The build already proves this by generating the SPIR-V it links against,
    # so as a test it repeats work. It is kept because it says which shader broke
    # and why, where a failed build step says only that a step failed - and
    # because it is the one thing that still checks the shader bodies when the
    # rest of the build is cached.
    add_test(NAME shader_spirv_dialect
        COMMAND ${CMAKE_COMMAND}
            -DPROBE=$<TARGET_FILE:shader_spirv_probe>
            -DOUTDIR=${CMAKE_BINARY_DIR}/shader_spirv
            -DVALIDATOR=${CTR_GLSLANG_VALIDATOR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_shader_spirv.cmake
    )
    set_tests_properties(shader_spirv_dialect PROPERTIES
        SKIP_REGULAR_EXPRESSION "SPIRV_CHECK_SKIPPED"
    )
    # Both GTE paths, the same inputs, all 64 registers compared.
    #
    # As a test and not as a comment in a file, because "that is just
    # the same computation with other names" is exactly the kind of claim that
    # has already been wrong twice here. It runs without a window, without data and
    # without a graphics card - it only computes.
    add_test(NAME gte_paths_identical COMMAND $<TARGET_FILE:ctr_native> --dev --gte-selftest)
    set_tests_properties(gte_paths_identical PROPERTIES
        PASS_REGULAR_EXPRESSION "selftest passed: [0-9]+ operations, 0 differences"
    )
    # The line formats of --dev-drive-log (game/DebugMenu.c): it only formats.
    add_test(NAME drive_log_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --dev-drive-log-selftest)
    set_tests_properties(drive_log_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "drive log selftest passed: [0-9]+ checks, 0 failures"
    )
    # The driver select grid for 0 to 32 custom entries: positions, navigation
    # and scrolling, with rows 0 and 1 held to the retail table. Like the GTE
    # test it only computes - on the D230 tables in the exe, without a window
    # or game data.
    add_test(NAME char_grid_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --char-grid-selftest)
    set_tests_properties(char_grid_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "char grid selftest passed: [0-9]+ checks, 0 failures"
    )
    # The play mods of the MODS page without a race: the settings lines, the
    # status line of the MODS box, --dev-mods-play, gravity and hang time,
    # grip, night colour, difficulty, boss sections, the item box and the CPU
    # items, the rules of RETRO FUELED. It only computes.
    add_test(NAME mods_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --mods-selftest)
    set_tests_properties(mods_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "mods selftest passed: [0-9]+ checks, 0 failures"
    )
    # The menu mouse without a window: window to menu mapping (letterbox,
    # pillarbox, widescreen shifts), round trip, active rules. It only computes.
    # The native engine without a window (platform/engine_*.c): maths, camera,
    # materials, scene, effect geometry, VRAM decoder. It only computes.
    add_test(NAME engine_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --engine-selftest)
    set_tests_properties(engine_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "engine selftest passed: [0-9]+ checks, 0 failures"
    )
    # The external track (--ext-track, platform/engine_exttrack.c): the JSON
    # reader and each part's selftest, without a window, game data or track
    # data. The switch's refusals before any window.
    foreach(ext_part json coll race nav crates music phys obstacle)
        add_test(NAME ext_selftest_${ext_part} COMMAND $<TARGET_FILE:ctr_native> --dev --ext-selftest ${ext_part})
        set_tests_properties(ext_selftest_${ext_part} PROPERTIES
            PASS_REGULAR_EXPRESSION "ext selftest ${ext_part} passed: [0-9]+ checks, 0 failures"
        )
    endforeach()
    add_test(NAME ext_selftest_unknown COMMAND $<TARGET_FILE:ctr_native> --dev --ext-selftest other)
    set_tests_properties(ext_selftest_unknown PROPERTIES PASS_REGULAR_EXPRESSION "switch --ext-selftest expects json, coll, race, nav, crates, music, phys or obstacle, not 'other'")
    add_test(NAME ext_track_needs_dev COMMAND $<TARGET_FILE:ctr_native> --ext-track d --engine --engine-track --level 14)
    set_tests_properties(ext_track_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --ext-track requires --dev")
    add_test(NAME ext_track_needs_engine COMMAND $<TARGET_FILE:ctr_native> --dev --level 14 --ext-track d --dev-race-mode stop)
    set_tests_properties(ext_track_needs_engine PROPERTIES PASS_REGULAR_EXPRESSION "switch --ext-track requires --engine")
    add_test(NAME ext_track_needs_level COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --ext-track d --dev-race-mode stop)
    set_tests_properties(ext_track_needs_level PROPERTIES PASS_REGULAR_EXPRESSION "switch --ext-track requires --level")
    add_test(NAME ext_track_needs_track COMMAND $<TARGET_FILE:ctr_native> --dev --engine --level 14 --ext-track d --dev-race-mode stop)
    set_tests_properties(ext_track_needs_track PROPERTIES PASS_REGULAR_EXPRESSION "switch --ext-track requires --engine-track")
    add_test(NAME ext_respawn_needs_track COMMAND $<TARGET_FILE:ctr_native> --dev --dev-ext-respawn 300 --dev-race-mode stop)
    set_tests_properties(ext_respawn_needs_track PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-ext-respawn requires --ext-track")
    add_test(NAME ext_respawn_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --level 14 --ext-track d --dev-ext-respawn x --dev-race-mode stop)
    set_tests_properties(ext_respawn_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-ext-respawn expects a race frame 0..1000000")
    add_test(NAME ext_obstacle_place_needs_track COMMAND $<TARGET_FILE:ctr_native> --dev --dev-ext-obstacle-place 600 --dev-race-mode stop)
    set_tests_properties(ext_obstacle_place_needs_track PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-ext-obstacle-place requires --ext-track")
    add_test(NAME ext_obstacle_place_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --level 14 --ext-track d --dev-ext-obstacle-place x --dev-race-mode stop)
    set_tests_properties(ext_obstacle_place_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-ext-obstacle-place expects a game timer 0..1000000")
    add_test(NAME ext_kart_log_needs_track COMMAND $<TARGET_FILE:ctr_native> --dev --dev-ext-kart-log --dev-race-mode stop)
    set_tests_properties(ext_kart_log_needs_track PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-ext-kart-log requires --ext-track")
    add_test(NAME ext_crate_log_needs_track COMMAND $<TARGET_FILE:ctr_native> --dev --dev-ext-crate-log --dev-race-mode stop)
    set_tests_properties(ext_crate_log_needs_track PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-ext-crate-log requires --ext-track")
    add_test(NAME ext_telemetry_needs_track COMMAND $<TARGET_FILE:ctr_native> --dev --dev-ext-telemetry --dev-race-mode stop)
    set_tests_properties(ext_telemetry_needs_track PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-ext-telemetry requires --ext-track")
    add_test(NAME ext_cam_log_needs_track COMMAND $<TARGET_FILE:ctr_native> --dev --dev-ext-cam-log 52 --dev-race-mode stop)
    set_tests_properties(ext_cam_log_needs_track PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-ext-cam-log requires --ext-track")
    add_test(NAME ext_cam_log_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --level 14 --ext-track d --dev-ext-cam-log 0 --dev-race-mode stop)
    set_tests_properties(ext_cam_log_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-ext-cam-log expects the kart's height H in world units, 1..1000")
    # The engine program on the graphics card (platform/engine_testscene.c,
    # Engine_SampleSelfTest): a hidden window and the Vulkan device, made-up VRAM
    # with poisoned rings around small UV boxes, drawn magnified with 4 and 1
    # samples through the instance and the mosaic way; no texel outside the box,
    # and the built-in controls (whole page as box, wider mosaic cells) must see
    # ring pixels. No game data. Skipped on a machine without a Vulkan device.
    add_test(NAME engine_sample_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --engine-sample-selftest)
    set_tests_properties(engine_sample_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "engine sample selftest passed: 4 samples, boxes 0 ring pixels on both ways and both sample counts, control fired: [1-9][0-9]* ring pixels \\(whole page as box\\), mosaic control [1-9][0-9]*"
        FAIL_REGULAR_EXPRESSION "engine sample selftest FAILED"
        SKIP_REGULAR_EXPRESSION "engine sample selftest SKIPPED"
    )
    # The engine's switches (main.c, NativeArgs_GateDevSwitches): a line that
    # cannot mean anything ends the start with exit code 64 and its reason,
    # before any window or data; the lines are what ctest checks. Each line
    # carries --dev-race-mode stop behind: should the check under test let it
    # through, the next check ends it there - never a window.
    add_test(NAME engine_track_needs_engine COMMAND $<TARGET_FILE:ctr_native> --dev --engine-track --dev-race-mode stop)
    set_tests_properties(engine_track_needs_engine PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-track requires --engine")
    add_test(NAME engine_fail_needs_engine COMMAND $<TARGET_FILE:ctr_native> --dev --engine-fail start --dev-race-mode stop)
    set_tests_properties(engine_fail_needs_engine PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-fail requires --engine")
    add_test(NAME engine_fail_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-fail later --dev-race-mode stop)
    set_tests_properties(engine_fail_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-fail expects start or track, not 'later'")
    add_test(NAME engine_fail_track_needs_track COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-fail track --dev-race-mode stop)
    set_tests_properties(engine_fail_track_needs_track PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-fail track requires --engine-track")
    add_test(NAME engine_fail_once COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-fail start --engine-fail track --dev-race-mode stop)
    set_tests_properties(engine_fail_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-fail is given 2 times - once only")
    add_test(NAME engine_fail_needs_dev COMMAND $<TARGET_FILE:ctr_native> --engine-fail start --dev-race-mode stop)
    set_tests_properties(engine_fail_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-fail requires --dev")
    add_test(NAME no_engine_not_with_engine COMMAND $<TARGET_FILE:ctr_native> --dev --engine --no-engine --dev-race-mode stop)
    set_tests_properties(no_engine_not_with_engine PROPERTIES PASS_REGULAR_EXPRESSION "switch --no-engine does not go with --engine")
    # The engine is the default for one player (include/platform/engine.h,
    # DEFAULT): --no-engine, --engine and --engine-track are player switches.
    # Without --dev they pass the gate, and the developer switch behind them
    # ends the start there; their pairing rules hold without --dev as well.
    add_test(NAME no_engine_without_dev COMMAND $<TARGET_FILE:ctr_native> --no-engine --dev-race-mode stop)
    set_tests_properties(no_engine_without_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-race-mode requires --dev")
    add_test(NAME engine_without_dev COMMAND $<TARGET_FILE:ctr_native> --engine --engine-track --dev-race-mode stop)
    set_tests_properties(engine_without_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-race-mode requires --dev")
    add_test(NAME engine_track_needs_engine_without_dev COMMAND $<TARGET_FILE:ctr_native> --engine-track)
    set_tests_properties(engine_track_needs_engine_without_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-track requires --engine")
    add_test(NAME no_engine_not_with_engine_without_dev COMMAND $<TARGET_FILE:ctr_native> --engine --no-engine)
    set_tests_properties(no_engine_not_with_engine_without_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --no-engine does not go with --engine")
    add_test(NAME engine_menuscene_still_dev COMMAND $<TARGET_FILE:ctr_native> --engine --engine-menuscene)
    set_tests_properties(engine_menuscene_still_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-menuscene requires --dev")
    add_test(NAME dev_tt_ghost_needs_tt COMMAND $<TARGET_FILE:ctr_native> --dev --dev-tt-ghost --dev-race-mode stop)
    set_tests_properties(dev_tt_ghost_needs_tt PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-tt-ghost requires --dev-race-mode tt")
    add_test(NAME dev_tt_ghost_needs_dev COMMAND $<TARGET_FILE:ctr_native> --dev-tt-ghost --dev-race-mode stop)
    set_tests_properties(dev_tt_ghost_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-tt-ghost requires --dev")
    add_test(NAME dev_kart_steer_lead_value COMMAND $<TARGET_FILE:ctr_native> --dev --dev-kart-steer --dev-kart-steer-lead 3 --dev-race-mode stop)
    set_tests_properties(dev_kart_steer_lead_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-kart-steer-lead expects 1 or 2, not '3'")
    add_test(NAME dev_kart_steer_lead_needs_steer COMMAND $<TARGET_FILE:ctr_native> --dev --dev-kart-steer-lead 1 --dev-race-mode stop)
    set_tests_properties(dev_kart_steer_lead_needs_steer PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-kart-steer-lead requires --dev-kart-steer")
    add_test(NAME dev_kart_steer_lead_needs_dev COMMAND $<TARGET_FILE:ctr_native> --dev-kart-steer --dev-kart-steer-lead 1 --dev-race-mode stop)
    set_tests_properties(dev_kart_steer_lead_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-kart-steer(-lead)? requires --dev")
    add_test(NAME dev_kart_steer_unstick_needs_steer COMMAND $<TARGET_FILE:ctr_native> --dev --dev-kart-steer-unstick --dev-race-mode stop)
    set_tests_properties(dev_kart_steer_unstick_needs_steer PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-kart-steer-unstick requires --dev-kart-steer")
    add_test(NAME dev_kart_steer_unstick_needs_dev COMMAND $<TARGET_FILE:ctr_native> --dev-kart-steer --dev-kart-steer-unstick --dev-race-mode stop)
    set_tests_properties(dev_kart_steer_unstick_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-kart-steer(-unstick)? requires --dev")
    add_test(NAME dev_cup_value COMMAND $<TARGET_FILE:ctr_native> --dev --dev-cup 4 --dev-race-mode stop)
    set_tests_properties(dev_cup_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-cup expects a cup index 0..3, not '4'")
    add_test(NAME dev_cup_not_with_level COMMAND $<TARGET_FILE:ctr_native> --dev --dev-cup 0 --level 3 --dev-race-mode stop)
    set_tests_properties(dev_cup_not_with_level PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-cup is a direct start of its own, not with --level")
    add_test(NAME dev_cup_not_with_race_mode COMMAND $<TARGET_FILE:ctr_native> --dev --dev-cup 1 --dev-race-mode stop)
    set_tests_properties(dev_cup_not_with_race_mode PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-cup is a direct start of its own, not with --dev-race-mode")
    add_test(NAME dev_cup_needs_dev COMMAND $<TARGET_FILE:ctr_native> --dev-cup 0 --dev-race-mode stop)
    set_tests_properties(dev_cup_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-cup requires --dev")
    add_test(NAME engine_oracle_needs_engine COMMAND $<TARGET_FILE:ctr_native> --dev --engine-oracle o.tsv --dev-race-mode stop)
    set_tests_properties(engine_oracle_needs_engine PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-oracle requires --engine")
    add_test(NAME engine_oracle_needs_track COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-oracle o.tsv --dev-race-mode stop)
    set_tests_properties(engine_oracle_needs_track PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-oracle requires --engine-track")
    add_test(NAME engine_oracle_needs_dev COMMAND $<TARGET_FILE:ctr_native> --engine-oracle o.tsv --dev-race-mode stop)
    set_tests_properties(engine_oracle_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-oracle requires --dev")
    add_test(NAME engine_oracle_fault_needs_oracle COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-oracle-fault cell:0 --dev-race-mode stop)
    set_tests_properties(engine_oracle_fault_needs_oracle PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-oracle-fault requires --engine-oracle")
    add_test(NAME engine_oracle_fault_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-oracle o.tsv --engine-oracle-fault 1 --dev-race-mode stop)
    set_tests_properties(engine_oracle_fault_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-oracle-fault expects clut:<n>, cell:<n> or spread:<n> \\(n 0 to 999999\\), not '1'")
    add_test(NAME engine_oracle_fault_spread_once COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-oracle o.tsv --engine-oracle-fault spread:1 --engine-oracle-fault spread:2 --dev-race-mode stop)
    set_tests_properties(engine_oracle_fault_spread_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-oracle-fault is given twice for spread")
    add_test(NAME engine_oracle_fault_once COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-oracle o.tsv --engine-oracle-fault clut:1 --engine-oracle-fault clut:2 --dev-race-mode stop)
    set_tests_properties(engine_oracle_fault_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-oracle-fault is given twice for clut")
    # The static model and the fly camera (round of 2026-10-16): their pairing rules, before any window.
    add_test(NAME engine_static_needs_engine COMMAND $<TARGET_FILE:ctr_native> --dev --engine-static-model m.glb --dev-race-mode stop)
    set_tests_properties(engine_static_needs_engine PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-static-model requires --engine")
    add_test(NAME engine_static_needs_track COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-static-model m.glb --dev-race-mode stop)
    set_tests_properties(engine_static_needs_track PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-static-model requires --engine-track")
    add_test(NAME engine_static_light_needs_model COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-static-light --dev-race-mode stop)
    set_tests_properties(engine_static_light_needs_model PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-static-light requires --engine-static-model")
    add_test(NAME engine_flycam_auto_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-flycam-auto 10 --dev-race-mode stop)
    set_tests_properties(engine_flycam_auto_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-flycam-auto expects a frame count 60..1000000")
    add_test(NAME engine_flycam_path_needs_auto COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-flycam-path p.txt --dev-race-mode stop)
    set_tests_properties(engine_flycam_path_needs_auto PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-flycam-path requires --engine-flycam-auto")
    add_test(NAME engine_flycam_not_with_auto COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-flycam --engine-flycam-auto 600 --dev-race-mode stop)
    set_tests_properties(engine_flycam_not_with_auto PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-flycam does not go with --engine-flycam-auto")
    add_test(NAME engine_flycam_needs_dev COMMAND $<TARGET_FILE:ctr_native> --engine --engine-flycam)
    set_tests_properties(engine_flycam_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-flycam requires --dev")
    add_test(NAME engine_hide_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-hide trak --dev-race-mode stop)
    set_tests_properties(engine_hide_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-hide expects sky, water and/or track, comma-separated, not 'trak'")
    add_test(NAME shot_series_value COMMAND $<TARGET_FILE:ctr_native> --dev --shot-series 100,0,2 --dev-race-mode stop)
    set_tests_properties(shot_series_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --shot-series expects <first>,<count>,<step>")
    add_test(NAME shot_series_needs_dev COMMAND $<TARGET_FILE:ctr_native> --shot-series 100,10,2 --dev-race-mode stop)
    set_tests_properties(shot_series_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --shot-series requires --dev")
    add_test(NAME engine_static_lit_needs_model COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-static-lit --dev-race-mode stop)
    set_tests_properties(engine_static_lit_needs_model PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-static-lit requires --engine-static-model")
    add_test(NAME engine_static_lit_sun_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-static-model m.glb --engine-static-lit --engine-static-lit-sun 0,0,0,0.5,0.5 --dev-race-mode stop)
    set_tests_properties(engine_static_lit_sun_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-static-lit-sun expects x,y,z,ambient,diffuse")
    add_test(NAME engine_static_style_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-static-model m.glb --engine-static-style shiny --dev-race-mode stop)
    set_tests_properties(engine_static_style_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-static-style expects hires or lowpoly")
    # The hi-res drivers on by default (format 4.2): --no-hires-chars and the old name
    # --engine-hires-chars are player switches (each passes the gate without --dev, so the
    # measuring --dev-race-mode after it is the one refused), they do not go together, the
    # measuring switches need no switch of their own but do not go with the way out.
    add_test(NAME hires_filter_without_alias COMMAND $<TARGET_FILE:ctr_native> --dev --engine-hires-chars-filter nearest --dev-race-mode stop)
    set_tests_properties(hires_filter_without_alias PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-race-mode expects tt or adventure")
    add_test(NAME hires_log_without_alias COMMAND $<TARGET_FILE:ctr_native> --dev --engine-hires-chars-log --dev-race-mode stop)
    set_tests_properties(hires_log_without_alias PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-race-mode expects tt or adventure")
    add_test(NAME hires_off_without_dev COMMAND $<TARGET_FILE:ctr_native> --no-hires-chars --dev-race-mode stop)
    set_tests_properties(hires_off_without_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-race-mode requires --dev")
    add_test(NAME hires_alias_without_dev COMMAND $<TARGET_FILE:ctr_native> --engine-hires-chars --dev-race-mode stop)
    set_tests_properties(hires_alias_without_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-race-mode requires --dev")
    add_test(NAME hires_off_not_with_alias COMMAND $<TARGET_FILE:ctr_native> --dev --no-hires-chars --engine-hires-chars --dev-race-mode stop)
    set_tests_properties(hires_off_not_with_alias PROPERTIES PASS_REGULAR_EXPRESSION "switch --no-hires-chars does not go with --engine-hires-chars")
    add_test(NAME hires_log_not_with_off COMMAND $<TARGET_FILE:ctr_native> --dev --no-hires-chars --engine-hires-chars-log --dev-race-mode stop)
    set_tests_properties(hires_log_not_with_off PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-hires-chars-log does not go with --no-hires-chars")
    add_test(NAME hires_filter_not_with_off COMMAND $<TARGET_FILE:ctr_native> --dev --no-hires-chars --engine-hires-chars-filter linear --dev-race-mode stop)
    set_tests_properties(hires_filter_not_with_off PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-hires-chars-filter does not go with --no-hires-chars")
    add_test(NAME hires_log_needs_dev COMMAND $<TARGET_FILE:ctr_native> --engine-hires-chars-log --dev-race-mode stop)
    set_tests_properties(hires_log_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-hires-chars-log requires --dev")
    # The test start of the Reload Studio's Test in game (main.c, THE TEST START): --char,
    # --level 0..17 and --driver 0..14 together, each once, pass without --dev; alone, with
    # another number or beside another developer switch they still need --dev.
    add_test(NAME test_start_without_dev COMMAND $<TARGET_FILE:ctr_native> --char ${CMAKE_CURRENT_BINARY_DIR}/test-start/a.rldchar --level 0 --driver 14 --dev-race-mode stop)
    set_tests_properties(test_start_without_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-race-mode requires --dev")
    add_test(NAME test_start_level_needs_dev COMMAND $<TARGET_FILE:ctr_native> --level 0 --driver 14 --dev-race-mode stop)
    set_tests_properties(test_start_level_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --level requires --dev")
    add_test(NAME test_start_char_needs_dev COMMAND $<TARGET_FILE:ctr_native> --char ${CMAKE_CURRENT_BINARY_DIR}/test-start/a.rldchar --dev-race-mode stop)
    set_tests_properties(test_start_char_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --char requires --dev")
    add_test(NAME test_start_twice_needs_dev COMMAND $<TARGET_FILE:ctr_native> --char ${CMAKE_CURRENT_BINARY_DIR}/test-start/a.rldchar --level 0 --level 1 --driver 14 --dev-race-mode stop)
    set_tests_properties(test_start_twice_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --char requires --dev")
    add_test(NAME test_start_level_range COMMAND $<TARGET_FILE:ctr_native> --char ${CMAKE_CURRENT_BINARY_DIR}/test-start/a.rldchar --level 18 --driver 14 --dev-race-mode stop)
    set_tests_properties(test_start_level_range PROPERTIES PASS_REGULAR_EXPRESSION "switch --level of the test start expects a race track 0..17, not '18'")
    add_test(NAME test_start_driver_range COMMAND $<TARGET_FILE:ctr_native> --char ${CMAKE_CURRENT_BINARY_DIR}/test-start/a.rldchar --level 0 --driver 15 --dev-race-mode stop)
    set_tests_properties(test_start_driver_range PROPERTIES PASS_REGULAR_EXPRESSION "switch --driver of the test start expects a template 0..14, not '15'")
    add_test(NAME test_start_other_dev_switch COMMAND $<TARGET_FILE:ctr_native> --char a.rldchar --chars-dir ${CMAKE_CURRENT_BINARY_DIR}/test-start --level 0 --driver 14 --dev-race-mode stop)
    set_tests_properties(test_start_other_dev_switch PROPERTIES PASS_REGULAR_EXPRESSION "switch --chars-dir requires --dev")
    add_test(NAME test_start_with_dev_any_level COMMAND $<TARGET_FILE:ctr_native> --dev --char ${CMAKE_CURRENT_BINARY_DIR}/test-start/a.rldchar --level 30 --driver 15 --dev-race-mode stop)
    set_tests_properties(test_start_with_dev_any_level PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-race-mode expects tt or adventure")
    # THE READ OF --char ON A MISSING FILE (platform/native_chars.c,
    # NativeChar_LoadGivenFile): a --char that is not there says "file not found:
    # <path>" - absolute as the test start of Reload Studio gives it, and a name in
    # --chars-dir -, never DAMAGED; a broken file and a folder still say DAMAGED.
    # The selftest makes its files in the build folder and compares whole lines.
    add_test(NAME char_given_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --char-given-selftest ${CMAKE_CURRENT_BINARY_DIR}/selftest/char-given)
    set_tests_properties(char_given_selftest PROPERTIES PASS_REGULAR_EXPRESSION "char given selftest passed: 7 checks, 0 failures")
    add_test(NAME hires_filter_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine-hires-chars --engine-hires-chars-filter blurry --dev-race-mode stop)
    set_tests_properties(hires_filter_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-hires-chars-filter expects nearest or linear")
    add_test(NAME engine_static_grade_value COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-static-model m.glb --engine-static-style lowpoly --engine-static-grade 4,1 --dev-race-mode stop)
    set_tests_properties(engine_static_grade_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-static-grade expects saturation,contrast, each 0..3")
    add_test(NAME engine_flycam_smooth_needs_auto COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-flycam-smooth --dev-race-mode stop)
    set_tests_properties(engine_flycam_smooth_needs_auto PROPERTIES PASS_REGULAR_EXPRESSION "switch --engine-flycam-smooth requires --engine-flycam-auto")
    add_test(NAME level_tour_oracle_needs_oracle COMMAND $<TARGET_FILE:ctr_native> --dev --level-tour races --level-tour-oracle 300 --dev-race-mode stop)
    set_tests_properties(level_tour_oracle_needs_oracle PROPERTIES PASS_REGULAR_EXPRESSION "switch --level-tour-oracle requires --engine-oracle")
    add_test(NAME level_tour_oracle_needs_tour COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-oracle o.tsv --level-tour-oracle 300 --dev-race-mode stop)
    set_tests_properties(level_tour_oracle_needs_tour PROPERTIES PASS_REGULAR_EXPRESSION "switch --level-tour-oracle requires --level-tour")
    add_test(NAME level_tour_oracle_list COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-oracle o.tsv --level-tour races --level-tour-oracle 300,,750 --dev-race-mode stop)
    set_tests_properties(level_tour_oracle_list PROPERTIES PASS_REGULAR_EXPRESSION "'300,,750' refused at entry 2")
    add_test(NAME level_tour_oracle_see_needs_list COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-oracle o.tsv --level-tour races --level-tour-oracle-see L4:tikka --dev-race-mode stop)
    set_tests_properties(level_tour_oracle_see_needs_list PROPERTIES PASS_REGULAR_EXPRESSION "switch --level-tour-oracle-see requires --level-tour-oracle")
    add_test(NAME level_tour_oracle_see_form COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-oracle o.tsv --level-tour races --level-tour-oracle 300 --level-tour-oracle-see L4:a+b+c+d --dev-race-mode stop)
    set_tests_properties(level_tour_oracle_see_form PROPERTIES PASS_REGULAR_EXPRESSION "'L4:a\\+b\\+c\\+d' refused at entry 1")
    add_test(NAME level_tour_frames_list COMMAND $<TARGET_FILE:ctr_native> --dev --level-tour races --level-tour-frames 900,1200 --dev-race-mode stop)
    set_tests_properties(level_tour_frames_list PROPERTIES PASS_REGULAR_EXPRESSION "switch --level-tour-frames expects one count and L<level>@<count> entries")
    add_test(NAME engine_oracle_switches_pass COMMAND $<TARGET_FILE:ctr_native> --dev --engine --engine-track --engine-oracle o.tsv --engine-oracle-fault clut:0 --engine-oracle-fault cell:0 --engine-oracle-fault spread:0 --level-tour races,containers,arenas --level-tour-frames 900,L78@2100 --level-tour-oracle 300,750,L0@630 --level-tour-oracle-see L4:flamejet+tikka,L5:plant --dev-race-mode stop)
    set_tests_properties(engine_oracle_switches_pass PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-race-mode expects tt or adventure, not 'stop'")
    add_test(NAME mouse_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --mouse-selftest)
    set_tests_properties(mouse_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "mouse selftest passed: [0-9]+ checks, 0 failures"
    )
    # The keyboard bindings file ctr-controls.cfg: parsing and fallbacks, saving,
    # swaps, race and menu context, key capture. Works in a folder of the build.
    add_test(NAME controls_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --controls-selftest ${CMAKE_BINARY_DIR}/selftest/controls)
    set_tests_properties(controls_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "controls selftest passed: [0-9]+ checks, 0 failures"
    )
    # --dev-char-seat-files (main.c, the first loop of main): given twice,
    # together with --dev-char-seats or --char, or with a list not of the form,
    # it ends the start with exit code 64 and its reason, before any window or
    # data; so does the switch without --dev. A name that is not a file of the
    # roster ends it only after the roster is read (game data, not in ctest).
    add_test(NAME dev_char_seat_files_once COMMAND $<TARGET_FILE:ctr_native> --dev --dev-char-seat-files a.rldchar --dev-char-seat-files b.rldchar)
    set_tests_properties(dev_char_seat_files_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-char-seat-files is given 2 times - once only")
    add_test(NAME dev_char_seat_files_not_with_seats COMMAND $<TARGET_FILE:ctr_native> --dev --dev-char-seats all --dev-char-seat-files a.rldchar)
    set_tests_properties(dev_char_seat_files_not_with_seats PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-char-seat-files does not go with --dev-char-seats")
    add_test(NAME dev_char_seat_files_not_with_char COMMAND $<TARGET_FILE:ctr_native> --dev --char a.rldchar --dev-char-seat-files a.rldchar)
    set_tests_properties(dev_char_seat_files_not_with_char PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-char-seat-files does not go with --char")
    add_test(NAME dev_char_seat_files_form COMMAND $<TARGET_FILE:ctr_native> --dev --dev-char-seat-files a.rldchar,,b.rldchar)
    set_tests_properties(dev_char_seat_files_form PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-char-seat-files expects 1 to 8 file names of the folder, commas between, got a.rldchar,,b.rldchar")
    add_test(NAME dev_char_seat_files_nine COMMAND $<TARGET_FILE:ctr_native> --dev --dev-char-seat-files a,b,c,d,e,f,g,h,i)
    set_tests_properties(dev_char_seat_files_nine PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-char-seat-files expects 1 to 8 file names")
    add_test(NAME dev_char_seat_files_needs_dev COMMAND $<TARGET_FILE:ctr_native> --dev-char-seat-files a.rldchar)
    set_tests_properties(dev_char_seat_files_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --dev-char-seat-files requires --dev")
    # The bytes of the generated native probe meshes, texture, pose meshes, wheel
    # mesh, wheel texture and mips texture (platform/native_probe.c) against
    # their golden SHA-256. Only hashes tables.
    add_test(NAME native_probe_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --native-probe-selftest)
    set_tests_properties(native_probe_selftest PROPERTIES PASS_REGULAR_EXPRESSION "native probe selftest passed: 36 vertices, 54 indices, sha256 87ded398127ef569456a9760ad0d8d7938c96f2db1e0d66a818bcd7865971373, textured mesh sha256 1948ecd790f9518408f043329ce36461d52cc6bc11dccd939a5f5d6dd4e3241a, texture 256x256 sha256 91a84b2163c3ec26e128e4d17ebc15b063b496055f830afc28341c20252cea4f, 8 pose meshes sha256 1033d715c90b852b4bd487a81b4f8b1c8002f635ffc70fc65e3814788ff55e6b, 2 wheel halves sha256 136fb282e11273f06882a4217c755c2a033d8c4871bfddbfef3746a2a80e2190, wheel texture sha256 21d43021f3bad68dcbb515b38b82f00b12ed7e2fdbcda0ebd3ecc9f8871740a6, mips texture 9 levels sha256 f581d2705016ddd01ec899b3eef7c82886f9730c2b5a92ef4ddd69c66382ad1a")
    # --native-wheel-report without the probe on measures nothing: the start
    # has to end with exit code 64 and its reason, before any window or data
    # (main.c, the first loop of main). The exit code itself is not what ctest
    # checks here, the line is.
    add_test(NAME native_wheel_report_needs_probe COMMAND $<TARGET_FILE:ctr_native> --dev --native-wheel-report)
    set_tests_properties(native_wheel_report_needs_probe PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-wheel-report only acts with the probe on")
    # Given twice it ends the start the same way (as --native-probe-seat); no
    # probe on the line, so even a reader that let it through could not start a race.
    add_test(NAME native_wheel_report_once COMMAND $<TARGET_FILE:ctr_native> --dev --native-wheel-report --native-wheel-report)
    set_tests_properties(native_wheel_report_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-wheel-report is given 2 times - once only")
    # --native-hide-exhaust (main.c, the first loop of main): given twice it ends
    # the start with exit code 64 and its reason, before any window or data; so
    # does the switch without --dev (the gate of the developer switches).
    add_test(NAME native_hide_exhaust_once COMMAND $<TARGET_FILE:ctr_native> --dev --native-hide-exhaust --native-hide-exhaust)
    set_tests_properties(native_hide_exhaust_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-hide-exhaust is given 2 times - once only")
    add_test(NAME native_hide_exhaust_needs_dev COMMAND $<TARGET_FILE:ctr_native> --native-hide-exhaust)
    set_tests_properties(native_hide_exhaust_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-hide-exhaust requires --dev")
    # --native-seam-report and --native-depth-tint (main.c, the first loop of
    # main, step 4c): without --native-preview, or given twice, they end the
    # start with exit code 64 and their reason, before any window or data.
    add_test(NAME native_seam_report_needs_preview COMMAND $<TARGET_FILE:ctr_native> --dev --native-seam-report)
    set_tests_properties(native_seam_report_needs_preview PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-seam-report only acts together with --native-preview")
    add_test(NAME native_seam_report_once COMMAND $<TARGET_FILE:ctr_native> --dev --native-preview --native-seam-report --native-seam-report)
    set_tests_properties(native_seam_report_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-seam-report is given 2 times - once only")
    add_test(NAME native_depth_tint_needs_preview COMMAND $<TARGET_FILE:ctr_native> --dev --native-depth-tint)
    set_tests_properties(native_depth_tint_needs_preview PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-depth-tint only acts together with --native-preview")
    add_test(NAME native_depth_tint_once COMMAND $<TARGET_FILE:ctr_native> --dev --native-preview --native-depth-tint --native-depth-tint)
    set_tests_properties(native_depth_tint_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-depth-tint is given 2 times - once only")
    # --native-split-report (main.c, step 4e-1b): given twice it ends the start
    # with exit code 64 and its reason; without --dev the gate does.
    add_test(NAME native_split_report_once COMMAND $<TARGET_FILE:ctr_native> --dev --native-split-report --native-split-report)
    set_tests_properties(native_split_report_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-split-report is given 2 times - once only")
    add_test(NAME native_split_report_needs_dev COMMAND $<TARGET_FILE:ctr_native> --native-split-report)
    set_tests_properties(native_split_report_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-split-report requires --dev")
    # --native-twin (main.c, step 4d): without --native-preview, or given twice,
    # it ends the start with exit code 64 and its reason.
    add_test(NAME native_twin_needs_preview COMMAND $<TARGET_FILE:ctr_native> --dev --native-twin)
    set_tests_properties(native_twin_needs_preview PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-twin only acts together with --native-preview")
    add_test(NAME native_twin_once COMMAND $<TARGET_FILE:ctr_native> --dev --native-preview --native-twin --native-twin)
    set_tests_properties(native_twin_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-twin is given 2 times - once only")
    # --native-filter (main.c, the first loop of main): a value other than
    # nearest or linear, the switch given twice, or no --native-preview end the
    # start with exit code 64 and their reason, before any window or data; so
    # does the switch without --dev (the gate of the developer switches). The
    # lines are what ctest checks.
    add_test(NAME native_filter_value COMMAND $<TARGET_FILE:ctr_native> --dev --native-preview --native-filter bilinear)
    set_tests_properties(native_filter_value PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-filter expects nearest or linear, got bilinear")
    add_test(NAME native_filter_once COMMAND $<TARGET_FILE:ctr_native> --dev --native-preview --native-filter nearest --native-filter linear)
    set_tests_properties(native_filter_once PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-filter is given 2 times - once only")
    add_test(NAME native_filter_needs_preview COMMAND $<TARGET_FILE:ctr_native> --dev --native-filter linear)
    set_tests_properties(native_filter_needs_preview PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-filter only acts together with --native-preview")
    add_test(NAME native_filter_needs_dev COMMAND $<TARGET_FILE:ctr_native> --native-filter linear)
    set_tests_properties(native_filter_needs_dev PROPERTIES PASS_REGULAR_EXPRESSION "switch --native-filter requires --dev")
    # The native texture manager (platform/native_tex.c): its tables, the
    # shader's way back to gamma, the edge rule and the levels of a fixed image
    # against their golden SHA-256. Computes only; no window, no device.
    add_test(NAME native_tex_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --native-tex-selftest)
    set_tests_properties(native_tex_selftest PROPERTIES PASS_REGULAR_EXPRESSION "native tex selftest passed: table round trip 256 of 256, shader round trip 256 of 256 margin 0.50 of 0.50, 10 edge checks, 12 uniform chains, tables sha256 f0af3130ba0f9051eb58eb97eca390622a3ab0c7633d2d3deba5c54a9572598b, 32x16 chain 6 levels 2732 bytes sha256 srgb a5a29bd35b804143de9d5d2d906be0dce6d804724b5f1a1206d1477590dee862 linear 35f614f4e3eb7d1eab2b6fd0fe77469e628aaf1d3c2e6d45f289b36a89e18680")
    # The depth of a native draw across view z 0x1000, through the queue's own
    # matrix steps (platform/native_depth_check.c). One made-up instance; the
    # values are deterministic, so the whole line is pinned. Appended (step 4e):
    # the water line against the queue's own split steps, the side selectors
    # against the queue's own selector, and the factor rule of the native chars;
    # on a second line the mirror (REFLECTION, SPECIAL, the mirrored wheels); on a
    # third the checks of the review of stage a (a nodding kart, the items of a
    # split view, the wheels against DrawTires, the classes of the colour factor);
    # on a fourth the bin markers of the twin (cells, the link, the limits, row w
    # of the paint order without the draw offset, the fallbacks of a twin view).
    add_test(NAME native_depth_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --native-depth-selftest)
    set_tests_properties(native_depth_selftest PROPERTIES PASS_REGULAR_EXPRESSION "native depth selftest passed: normal shift 2 to 0, w 4164.0625 to 4165.0391, step 0.0235 percent, rest 0.0006 percent, matrix part 0.0339 percent, wrong shift 299.9 and 75.0 percent, matrix 300.1 and 75.0 percent, huge shift 0 to -2, w 4371.2500 to 4372.1562, step 0.0207 percent, rest 0.0021 percent, matrix part 0.0339 percent, wrong shift 299.9 and 75.0 percent, matrix 300.1 and 75.0 percent, shift checks held, char scale 0x1000 shift 2 to 0, w 4095.0000 to 4096.0000, step 0.0244 percent, rest 0.0000 percent, matrix part 0.1119 percent, wrong shift 299.9 and 75.0 percent, matrix 300.4 and 75.0 percent, char scale 0x0ccd shift 2 to 0, w 4095.0000 to 4096.0000, step 0.0244 percent, rest 0.0000 percent, matrix part 0.3077 percent, wrong shift 299.9 and 75.0 percent, matrix 301.2 and 75.1 percent, char passed, split R near: handler split, selector both-mask, composed same, splitLine 2112, 729 points .360 below, 369 above., 0 differ, level plane 72 differ, split R far: handler split, selector both-mask, composed same, splitLine 528, 720 points .360 below, 360 above., 0 differ, level plane 36 differ, split P near: handler split, selector both-mask, composed same, splitLine 1200, 648 points .324 below, 324 above., 0 differ, level plane 0 differ, selector sides 20 cases 0 differ, shift/mask error max 0.50, dim error max 1.25, labels held, factor rule 8 cases 0 differ, split passed[
]+native depth selftest mirror, reflection R near: handler reflection, selector negative, splitLine 2112, 125 corners, error max 0.839, winding turned, reflection R far: handler reflection, selector negative, splitLine 528, 125 corners, error max 0.961, winding turned, special: handler special, selector both-mask, splitLine -8000, 125 corners, error max 0.000, winding turned, wheels error max 0.000000000, reflection selectors 5 cases 0 differ, mirror passed[
]+native depth selftest split items, split R near nodding: handler split, splitLine 2112, 729 points .360 below, 369 above., 0 differ, level plane 72 differ, items of a split view held, wheels against DrawTires 640 cases 0 differ, shift/mask classes 8 cases 0 differ, marker bin of depth range 10..31 middle 20 nearest 10, items passed[
]+native depth selftest twin bin markers: bins 40 33 33 25 12 9 2 in the cells 10..31 give the cells 31 25 12 10, 5 keys held, linked into those cells only, arena keeps 128, paint runs report 65 cells as over 64, row w same with the draw offset .near, far, mirror., view: items without a body one middle marker each in their ranges, no paint order and a full arena link nothing .held., bins passed[
]+native depth selftest own wheels: mirror error max 0.003906, steer error max 0.001953, rim 12.796875 world units .16 model units x 0x0ccc / 4096., roll error 0, rotations 8 of 8, wheelSize 0 held, own wheels passed")

    # VALUES ARE REFUSED, NEVER CUT (main.c, NativeArgs_ValueFits). A path one
    # character too long for --shot-name and a --shot list with one entry too
    # many end the start in the switch gate, before the GTE self-test behind
    # them can run; a list of 128 entries and 767 characters - past the 511 at
    # which it was cut before - passes the gate. No window, no data.
    string(REPEAT "a" 1024 CTR_TEST_LONG_NAME)
    set(CTR_TEST_SHOTS_128 "10000")
    foreach(CTR_TEST_SHOT RANGE 10001 10127)
        string(APPEND CTR_TEST_SHOTS_128 ",${CTR_TEST_SHOT}")
    endforeach()
    add_test(NAME args_refuse_long_shot_name
        COMMAND $<TARGET_FILE:ctr_native> --dev --shot-name ${CTR_TEST_LONG_NAME} --gte-selftest)
    set_tests_properties(args_refuse_long_shot_name PROPERTIES
        PASS_REGULAR_EXPRESSION "switch --shot-name takes at most 1023 characters, got 1024 - refused, never cut"
        FAIL_REGULAR_EXPRESSION "selftest passed"
    )
    add_test(NAME args_refuse_shot_list_129
        COMMAND $<TARGET_FILE:ctr_native> --dev --shot ${CTR_TEST_SHOTS_128},10128 --gte-selftest)
    set_tests_properties(args_refuse_shot_list_129 PROPERTIES
        PASS_REGULAR_EXPRESSION "switch --shot takes at most 128 entries, got 129 - refused, never cut"
        FAIL_REGULAR_EXPRESSION "selftest passed"
    )
    add_test(NAME args_accept_shot_list_128
        COMMAND $<TARGET_FILE:ctr_native> --dev --shot ${CTR_TEST_SHOTS_128} --gte-selftest)
    set_tests_properties(args_accept_shot_list_128 PROPERTIES
        PASS_REGULAR_EXPRESSION "selftest passed: [0-9]+ operations, 0 differences"
        FAIL_REGULAR_EXPRESSION "refused"
    )

    # FOREIGN INPUT, REFUSED CLEANLY. A container (.rldtrack) and a disc image
    # come from strangers: every length, offset and index in them is checked
    # before it is used. These two tests feed the game made-up files - good-*
    # must be accepted, bad-* refused, and nothing may crash, hang or write
    # outside its folder. cmake/run_selftest.cmake empties the folder, lets the
    # game write the test files into it (--make-test-containers /
    # --make-test-disc, platform/native_testfiles.c) and then runs the game's
    # self-test switch on it. Both switches run before any window, audio or
    # asset initialisation: no GPU, no disc image, no game data.
    add_test(NAME selftest_bad_containers
        COMMAND ${CMAKE_COMMAND}
            -DGAME=$<TARGET_FILE:ctr_native>
            -DDIR=${CMAKE_BINARY_DIR}/selftest/containers
            -DMAKE_SWITCH=--make-test-containers
            -DTEST_SWITCH=--selftest-containers
            -P ${CMAKE_SOURCE_DIR}/cmake/run_selftest.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(NAME selftest_bad_disc
        COMMAND ${CMAKE_COMMAND}
            -DGAME=$<TARGET_FILE:ctr_native>
            -DDIR=${CMAKE_BINARY_DIR}/selftest/disc
            -DMAKE_SWITCH=--make-test-disc
            -DTEST_SWITCH=--selftest-disc
            -P ${CMAKE_SOURCE_DIR}/cmake/run_selftest.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    # A directory that points back at the root must not hang the reader; if it
    # does, the timeout ends the test instead of the build.
    set_tests_properties(selftest_bad_containers selftest_bad_disc PROPERTIES TIMEOUT 300)

    # THE VULKAN RETIRE LISTS (platform/native_gfx_vk.c), without a device: one
    # level change of 100 textures and 25 framebuffers with the lists cut to 8
    # entries - full lists are drained at once, nothing is left behind, every
    # handle is destroyed exactly once.
    add_test(NAME native_retire_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --native-retire-selftest)
    set_tests_properties(native_retire_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "native retire selftest passed: 100 textures, 25 framebuffers, cap 8, [1-9][0-9]* drain\\(s\\), 0 leak\\(s\\), 15 checks, 0 failures"
    )

    # THE NATIVE MODEL OF A CUSTOM CHARACTER (CNET, CTXT; preview). rldpack
    # writes its test characters into the build folder (never into the
    # source tree), the game reads each through the roster read and the
    # native read without and with --native-preview: an old file loads as
    # before, a good one is ready only with the preview, a broken one costs
    # only the native part (the driver keeps its CMDL), and without the
    # preview not one of them is looked at.
    add_test(NAME char_native_selftest
        COMMAND ${CMAKE_COMMAND}
            -DPACKER=$<TARGET_FILE:rldpack>
            -DGAME=$<TARGET_FILE:ctr_native>
            -DDIR=${CMAKE_BINARY_DIR}/selftest/native-chars
            -DESCAPE=${CMAKE_BINARY_DIR}/../tools
            -P ${CMAKE_SOURCE_DIR}/cmake/run_char_native_selftest.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(char_native_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "char native selftest passed: 66 files, [0-9]+ checks, 0 failures"
        TIMEOUT 300
        FIXTURES_SETUP native_char_files
    )
    # THE GPU SET OF A CUSTOM CHARACTER (step 4c, platform/native_char_gpu.c),
    # without a device: the good files the test above had rldpack write go
    # through the native read and the set build - ranges, index sort, the
    # vertex bytes of every pose (one SHA-256 over all four files), the pose
    # index of all 47 frames, the wheel middles and the refusal of a blend
    # material.
    add_test(NAME native_char_gpu_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --native-char-gpu-selftest ${CMAKE_BINARY_DIR}/selftest/native-chars)
    set_tests_properties(native_char_gpu_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "native char gpu selftest passed: 4 files, 40 checks, 0 failures, sha256 efc584b94d73f6708bed71ff81c89a533479566755edebea6b47735a9cb16497"
        FIXTURES_REQUIRED native_char_files
    )
    # The second line of the same self-test: wheels version 3 and the win and lose
    # morph targets, on the files of the character native self-test.
    add_test(NAME native_char_gpu_selftest_v3 COMMAND $<TARGET_FILE:ctr_native> --dev --native-char-gpu-selftest ${CMAKE_BINARY_DIR}/selftest/native-chars)
    set_tests_properties(native_char_gpu_selftest_v3 PROPERTIES
        PASS_REGULAR_EXPRESSION "native char gpu selftest wheels v3 and win lose passed: 6 files, 6 checks, 0 failures"
        FIXTURES_REQUIRED native_char_files
    )
    # The same for the native model of an OBJ (make-char --native-model on):
    # rldpack make-native-tests --obj writes the self-test's mini OBJ without
    # and with its native model (one CMDL), the game reads them as above.
    add_test(NAME char_native_obj_selftest
        COMMAND ${CMAKE_COMMAND}
            -DPACKER=$<TARGET_FILE:rldpack>
            -DGAME=$<TARGET_FILE:ctr_native>
            -DDIR=${CMAKE_BINARY_DIR}/selftest/native-obj
            -DSET=obj
            -P ${CMAKE_SOURCE_DIR}/cmake/run_char_native_selftest.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(char_native_obj_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "char native selftest passed: 3 files, [0-9]+ checks, 0 failures"
        TIMEOUT 300
    )
    # THE RETAIL TWIN (step 4d, platform/native_twin.c), without a device: a
    # retail model and a VRAM made in memory through the converter (golden
    # SHA-256 of the vertex bytes of every pose and of the RGBA8, computed
    # apart), the winding against the retail sign test, two refusals; then
    # old_plain of the files above: every pose point equal to its record.
    add_test(NAME native_twin_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --native-twin-selftest ${CMAKE_BINARY_DIR}/selftest/native-chars)
    set_tests_properties(native_twin_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "native twin selftest passed: 1 of 1 file read, 36 checks, 0 failures, sha256 vertices 44191f9ea764dd3122e950b578316f47535bd834ef700152033fa080e8989b36 rgba 986d4c3a10b4bd36478d8d875dbe3f48519c00ba15fd26b8fc00b2f5050ce533 full height e4b47ceaf474380c77c2f2a4dfb7f8851a2a4b8a28b78c86a27d3dde59bf026b"
        FIXTURES_REQUIRED native_char_files
    )
    # make-char --native-model on needs --wheel-model or --wheels off (the game
    # draws a native model only with wheels of its own or with the kart wheels
    # hidden): without either rldpack refuses the switch - exit not 0, the
    # message, no file; with --wheels off the same box builds. --wheel-model
    # beside --wheels off, and --wheel-model without --native-model on, are
    # refused the same way (the wheel itself: the self-test, rldpack_selftest).
    # The script and the box OBJ are written here into the build folder.
    file(WRITE ${CMAKE_BINARY_DIR}/rldpack_native_wheels_test.cmake [=[
file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
file(WRITE "${DIR}/box.obj" "v -0.5 0 -1\nv 0.5 0 -1\nv 0.5 0 1\nv -0.5 0 1\nv -0.5 0.5 -1\nv 0.5 0.5 -1\nv 0.5 0.5 1\nv -0.5 0.5 1\nf 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 4 8 5 1\n")
execute_process(COMMAND "${PACKER}" make-char --model "${DIR}/box.obj" --name T --template 14 --native-model on --out "${DIR}/refused.rldchar"
    RESULT_VARIABLE refusedResult OUTPUT_VARIABLE refusedOut ERROR_VARIABLE refusedOut)
message("${refusedOut}")
if(refusedResult EQUAL 0)
    message(FATAL_ERROR "rldpack native wheels test: --native-model on without --wheels off was not refused (exit 0)")
endif()
if(NOT refusedOut MATCHES "--native-model on needs --wheel-model")
    message(FATAL_ERROR "rldpack native wheels test: the refusal does not name --wheel-model and --wheels off")
endif()
if(EXISTS "${DIR}/refused.rldchar")
    message(FATAL_ERROR "rldpack native wheels test: a file was written although the switch was refused")
endif()
execute_process(COMMAND "${PACKER}" make-char --model "${DIR}/box.obj" --name T --template 14 --wheels off --native-model on --wheel-model "${DIR}/box.obj" --out "${DIR}/contradict.rldchar"
    RESULT_VARIABLE contradictResult OUTPUT_VARIABLE contradictOut ERROR_VARIABLE contradictOut)
if(contradictResult EQUAL 0 OR NOT contradictOut MATCHES "--wheel-model and --wheels off contradict each other" OR EXISTS "${DIR}/contradict.rldchar")
    message("${contradictOut}")
    message(FATAL_ERROR "rldpack native wheels test: --wheel-model beside --wheels off was not refused, or not said, or a file was written")
endif()
execute_process(COMMAND "${PACKER}" make-char --model "${DIR}/box.obj" --name T --template 14 --wheel-model "${DIR}/box.obj" --out "${DIR}/alone.rldchar"
    RESULT_VARIABLE aloneResult OUTPUT_VARIABLE aloneOut ERROR_VARIABLE aloneOut)
if(aloneResult EQUAL 0 OR NOT aloneOut MATCHES "--wheel-model needs --native-model on" OR EXISTS "${DIR}/alone.rldchar")
    message("${aloneOut}")
    message(FATAL_ERROR "rldpack native wheels test: --wheel-model without --native-model on was not refused, or not said, or a file was written")
endif()
execute_process(COMMAND "${PACKER}" make-char --model "${DIR}/box.obj" --name T --template 14 --wheels off --native-model on --out "${DIR}/built.rldchar"
    RESULT_VARIABLE builtResult OUTPUT_VARIABLE builtOut ERROR_VARIABLE builtOut)
if(NOT builtResult EQUAL 0 OR NOT EXISTS "${DIR}/built.rldchar")
    message("${builtOut}")
    message(FATAL_ERROR "rldpack native wheels test: the same box with --wheels off did not build (exit ${builtResult})")
endif()
message("rldpack native wheels test passed: refused without --wheels off (exit ${refusedResult}, no file), built with it; --wheel-model refused beside --wheels off and without --native-model on")
]=])
    add_test(NAME rldpack_native_wheels_refused
        COMMAND ${CMAKE_COMMAND}
            -DPACKER=$<TARGET_FILE:rldpack>
            -DDIR=${CMAKE_BINARY_DIR}/selftest/native-wheels
            -P ${CMAKE_BINARY_DIR}/rldpack_native_wheels_test.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(rldpack_native_wheels_refused PROPERTIES
        PASS_REGULAR_EXPRESSION "rldpack native wheels test passed: refused without --wheels off \\(exit [1-9][0-9]*, no file\\), built with it; --wheel-model refused beside --wheels off and without --native-model on"
        TIMEOUT 120
    )

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # The content guard (tools/content_guard.py, also run by GitHub and the
    # pre-push hook): its rules against paths and contents made up in the
    # script, and the package check (--package, tools/package/package.sh step
    # 6) against fake packages it makes in code in the build folder.
    add_test(NAME content_guard_selftest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/content_guard.py --self-test)
    set_tests_properties(content_guard_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "content_guard: self-test passed - [0-9]+ case"
        FAIL_REGULAR_EXPRESSION "FAIL")
    add_test(NAME content_guard_package
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/content_guard.py
            --package-self-test ${CMAKE_BINARY_DIR}/selftest/content-guard-package)
    set_tests_properties(content_guard_package PROPERTIES
        PASS_REGULAR_EXPRESSION "content_guard: package self-test passed - [0-9]+ case"
        FAIL_REGULAR_EXPRESSION "FAIL")
    # The page Creator Kit of Reload Studio (tools/reloadstudio/rs_kit.c) end to
    # end without Blender and without game data: rs_kit_standin.exe stands in
    # for blender.exe and for the extractor, the game is a made-up folder
    # (cmake/run_rs_kit.cmake). Only with the tests.
    if(MSVC)
        add_executable(rs_kit_standin tools/reloadstudio/rs_kit_standin.c)
        set_target_properties(rs_kit_standin PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        add_test(NAME reloadstudio_kit
            COMMAND ${CMAKE_COMMAND}
                -DSTUDIO=$<TARGET_FILE:ReloadStudio>
                -DSTANDIN=$<TARGET_FILE:rs_kit_standin>
                -DTEST=$<TARGET_FILE:rs_hires_selftest>
                -DPACKER=$<TARGET_FILE:rldpack>
                -DPYTHON=${Python3_EXECUTABLE}
                -DSOURCE=${CMAKE_SOURCE_DIR}
                -DDIR=${CMAKE_BINARY_DIR}/selftest/rs-kit
                -P ${CMAKE_SOURCE_DIR}/cmake/run_rs_kit.cmake
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
        set_tests_properties(reloadstudio_kit PROPERTIES
            PASS_REGULAR_EXPRESSION "kit passed: "
            FAIL_REGULAR_EXPRESSION "FAILED"
            TIMEOUT 900)
    endif()
    add_test(
        NAME ctr_match_unit
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_SOURCE_DIR}/tools/matching
            -p test_*.py
    )

    # Everything in this repository is English: comments, messages, names.
    # Exceptions for older German names: tools/lang_check_allow.txt.
    add_test(
        NAME lang_check
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/lang_check.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    # Material comparison engine against retail (oracle log): synthetic logs for
    # every rule, the exception list in tools/ and the fault control.
    add_test(
        NAME material_compare_selftest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/material_compare.py --selftest
    )
    set_tests_properties(material_compare_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "material_compare selftest passed: [0-9]+ checks, 0 failures"
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1"
    )

    # Hi-res driver sharpness on texture pixels only (key-coloured mask,
    # eroded): synthetic images for every rule of the measure.
    add_test(
        NAME hires_mask_sharp_selftest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/hires_mask_sharp.py selftest
    )
    set_tests_properties(hires_mask_sharp_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "selftest: [0-9]+ checks, 0 failures"
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1"
    )

    # Hi-res driver (chunk CHRG) tools: the GLB checker (in-memory GLBs, one
    # good file and one negative case per rule), the per-tick log evaluation
    # (clip per state, blend ramps, wheels), the budget evaluation (p99 per
    # variant of one load run) and the review sheets, each on synthetic data.
    foreach(hires_tool hires_glb_check hires_tick_eval hires_budget_eval hires_sheet hires_chars_dir hires_brightness)
        add_test(
            NAME ${hires_tool}_selftest
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/${hires_tool}.py selftest
        )
        set_tests_properties(${hires_tool}_selftest PROPERTIES
            PASS_REGULAR_EXPRESSION "${hires_tool} selftest: [0-9]+ checks, 0 failures"
            ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1"
        )
    endforeach()

    # The creator kit (templates/make_creator_kit.py and its tools): the GLB
    # measurements and the guide builder on code-made data, the guide HTML
    # up to date with its source, and the generator end to end with Blender
    # 5.2 against rldpack (skipped, code 77, where Blender 5.2 is missing).
    add_test(NAME creator_kit_measure_selftest COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/creator_kit_measure.py selftest)
    set_tests_properties(creator_kit_measure_selftest PROPERTIES PASS_REGULAR_EXPRESSION "creator-kit-measure selftest: [0-9]+ ok, 0 FAIL" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_test(NAME creator_kit_guide_selftest COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/creator_kit_guide.py selftest)
    set_tests_properties(creator_kit_guide_selftest PROPERTIES PASS_REGULAR_EXPRESSION "creator_kit_guide selftest: [0-9]+ cases, 0 failed" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_test(NAME creator_kit_guide_check COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/creator_kit_guide.py check)
    set_tests_properties(creator_kit_guide_check PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_test(NAME creator_kit_test_selftest COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/creator_kit_test.py selftest)
    set_tests_properties(creator_kit_test_selftest PROPERTIES PASS_REGULAR_EXPRESSION "creator-kit-test selftest: [0-9]+ checks, 0 failures" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_test(NAME creator_kit COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/creator_kit_test.py --rldpack $<TARGET_FILE:rldpack> --work ${CMAKE_BINARY_DIR}/selftest/creator_kit)
    set_tests_properties(creator_kit PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    # Collision model generator (tools/collision_gen): procedural test models,
    # the generator's checks in the game's arithmetic, and the container it
    # makes judged by the game's own container checks.
    add_test(
        NAME collision_gen_selftest
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_SOURCE_DIR}/tools/collision_gen
            -p test_*.py
    )
    add_test(
        NAME collision_gen_container
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON=${Python3_EXECUTABLE}
            -DSRC=${CMAKE_SOURCE_DIR}
            -DRLDPACK=$<TARGET_FILE:rldpack>
            -DGAME=$<TARGET_FILE:ctr_native>
            -DDIR=${CMAKE_BINARY_DIR}/selftest/collision_gen
            -P ${CMAKE_SOURCE_DIR}/cmake/collision_gen_container.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(collision_gen_selftest collision_gen_container PROPERTIES
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1"
        TIMEOUT 600
    )
    set_tests_properties(collision_gen_container PROPERTIES
        PASS_REGULAR_EXPRESSION "2 container\\(s\\): 1 good-, 1 bad-, 0 unexpected"
    )
endif()

# Every executable target of this file waits before its link until the exe is
# free (ctr_wait_exe_free, above).
get_property(ctr_wait_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(ctr_wait_target IN LISTS ctr_wait_targets)
    get_target_property(ctr_wait_type ${ctr_wait_target} TYPE)
    if(ctr_wait_type STREQUAL "EXECUTABLE")
        ctr_wait_exe_free(${ctr_wait_target})
    endif()
endforeach()
