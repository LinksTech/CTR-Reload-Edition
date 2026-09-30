# Runs the shader probe, then compiles everything it wrote to SPIR-V.
#
# Invoked by ctest, not by hand. Expects PROBE, OUTDIR and VALIDATOR on the
# command line.
#
# With no validator it prints SPIRV_CHECK_SKIPPED and stops. The test's
# SKIP_REGULAR_EXPRESSION picks that up. In a normal build this cannot happen:
# the build itself needs glslangValidator (tools/build_shaders.cmake). A script
# run with cmake -P cannot choose its own exit code, so the signal has to be in
# the output.

if(NOT VALIDATOR OR VALIDATOR MATCHES "NOTFOUND")
    message(STATUS "SPIRV_CHECK_SKIPPED - glslangValidator not found")
    message(STATUS "Install the Vulkan SDK, or set VULKAN_SDK, to enable this check.")
    return()
endif()

file(REMOVE_RECURSE "${OUTDIR}")
file(MAKE_DIRECTORY "${OUTDIR}")

execute_process(
    COMMAND "${PROBE}" "${OUTDIR}"
    RESULT_VARIABLE probeResult
    OUTPUT_VARIABLE probeOutput
    ERROR_VARIABLE probeError
)

if(NOT probeResult EQUAL 0)
    message(FATAL_ERROR "shader probe failed (${probeResult}):\n${probeOutput}${probeError}")
endif()

file(GLOB shaders "${OUTDIR}/*.vert" "${OUTDIR}/*.frag")
list(LENGTH shaders total)

if(total EQUAL 0)
    message(FATAL_ERROR "shader probe wrote nothing to ${OUTDIR}")
endif()

set(failures "")

foreach(shader IN LISTS shaders)
    get_filename_component(name "${shader}" NAME)

    execute_process(
        COMMAND "${VALIDATOR}" -V "${shader}" -o "${shader}.spv"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )

    if(result EQUAL 0)
        message(STATUS "SPIR-V ok: ${name}")
    else()
        message(STATUS "SPIR-V FAILED: ${name}\n${output}${error}")
        list(APPEND failures "${name}")
    endif()
endforeach()

list(LENGTH failures failureCount)

if(failureCount GREATER 0)
    message(FATAL_ERROR
        "${failures} do not compile as Vulkan GLSL.\n"
        "The assembled sources are in ${OUTDIR} - the failing line numbers refer to those files.\n"
        "The shader body used something the Vulkan preamble in tools/shader_spirv_probe.c "
        "does not accept, or that preamble has drifted from the one native_renderer.c builds "
        "at runtime. Those two are the same shader seen twice and must agree.")
endif()

message(STATUS "all ${total} shader stages compile to SPIR-V")
