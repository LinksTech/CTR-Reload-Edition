cmake_minimum_required(VERSION 3.20)
project(CTR-Native C)
include(CheckCCompilerFlag)

# The version players see: "CTR Reload <version> (<build id>)" on the start
# screen, in --version, in the Alpha-Maker and in the log. The upstream
# ctr-native version number is not shown to players.
# tools/package/package.sh reads this line to name the package.
set(CTR_NATIVE_VERSION "Beta 0")

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

# The packer for .rldtrack containers. A PROGRAM OF ITS OWN, not part of the game -
# a track author builds their container with it; the game uses nothing of it.
# The Alpha-Maker carries the same code (see below), and the release package
# ships only the Alpha-Maker. rldpack.exe is built for the self-test and for
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
add_executable(rldpack tools/rldpack.c platform/native_disc_image.c)
target_compile_definitions(rldpack PRIVATE CTR_NATIVE
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\")
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

if(MSVC)
    target_sources(rldpack PRIVATE tools/rldpack.rc platform/rldpack.manifest)
endif()

set_target_properties(rldpack PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# The Alpha-Maker: the front end for track authors, see the header of
# tools/alphamaker/alphamaker.h. ONE exe that carries rldpack inside
# (am_rldpack.c: the same tools/rldpack.c with a renamed main) and starts itself
# as a child process for every pack and check step - no second
# implementation of a check. Win32 front end, so only under MSVC; under
# MinGW it stays out like rldpack's version resource.
if(MSVC)
    add_executable(alphamaker WIN32
        tools/alphamaker/am_shell.c
        tools/alphamaker/am_track.c
        tools/alphamaker/am_cups.c
        tools/alphamaker/am_test.c
        tools/alphamaker/am_rldpack.c
        platform/native_disc_image.c
        tools/alphamaker/alphamaker.rc
        tools/alphamaker/alphamaker.manifest
    )
    # The same version and build ID as the game, for the title and the pairing
    # check - from generated/ctr_build_id.h, new on every build (target
    # ctr_build_id).
    target_compile_definitions(alphamaker PRIVATE CTR_NATIVE
        CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\")
    target_include_directories(alphamaker PRIVATE ${CMAKE_SOURCE_DIR}/include ${CTR_BUILD_ID_DIR})
    add_dependencies(alphamaker ctr_build_id)
    target_link_libraries(alphamaker PRIVATE comctl32 comdlg32 shell32 ole32 uuid uxtheme)
    set_target_properties(alphamaker PROPERTIES
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
    # The same self-test through the rldpack inside the Alpha-Maker: proves that the
    # built-in version is the same one and talks over the pipe.
    if(MSVC)
        add_test(NAME alphamaker_rldpack_selftest COMMAND $<TARGET_FILE:alphamaker> --rldpack selftest)
        set_tests_properties(alphamaker_rldpack_selftest PROPERTIES PASS_REGULAR_EXPRESSION "Self-test passed")
    endif()
    set_tests_properties(ctr_native_version PROPERTIES PASS_REGULAR_EXPRESSION "^CTR Reload Beta ")


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
    # The driver select grid for 0 to 32 custom entries: positions, navigation
    # and scrolling, with rows 0 and 1 held to the retail table. Like the GTE
    # test it only computes - on the D230 tables in the exe, without a window
    # or game data.
    add_test(NAME char_grid_selftest COMMAND $<TARGET_FILE:ctr_native> --dev --char-grid-selftest)
    set_tests_properties(char_grid_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "char grid selftest passed: [0-9]+ checks, 0 failures"
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

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
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
endif()
