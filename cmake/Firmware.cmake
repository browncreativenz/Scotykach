# The Spotykach firmware and DaisySP, built for the desktop.
#
# The firmware submodule is never modified. At configure time its src/ is
# copied into the build tree and the patches are applied to the copy, in
# order. Files are only rewritten when their content changes, so a
# reconfigure doesn't rebuild everything. Each patch says why it exists:
#
#   patches/portability/  make the firmware behave on x86 as it does on the
#                         module's Cortex-M7, where the two differ
#   patches/instances/    let several engines share one process, as plugin
#                         instances in Live do
#
# Defines:
#   daisysp                 DaisySP (MIT), as pinned by the firmware
#   scotykach_fw            firmware engine with all patches, the shim and
#                           ScotykachEngine: what the plugin uses
#   scotykach_fw_single     the same with only the portability patches: the
#                           firmware as it runs on the module, for the test
#                           that the instance patches don't change the sound.
#                           One engine per process only.

# ── DaisySP ─────────────────────────────────────────────────────────────────
file(GLOB_RECURSE DAISYSP_SOURCES CONFIGURE_DEPENDS "${DAISYSP_DIR}/Source/*.cpp")
add_library(daisysp STATIC ${DAISYSP_SOURCES})
target_include_directories(daisysp SYSTEM PUBLIC
    "${DAISYSP_DIR}/Source"
    "${DAISYSP_DIR}/Source/Utility")
# -fwrapv: DaisySP's WhiteNoise (used by the firmware's random LFO) relies
# on signed overflow wrapping, as it does on the module's Cortex-M7. This
# makes that defined behaviour here. Firmware sources get it too, since
# they compile the header.
set(SCOTYKACH_FIRMWARE_FLAGS ${SCOTYKACH_FP_FLAGS} -fwrapv)
target_compile_options(daisysp PRIVATE ${SCOTYKACH_FIRMWARE_FLAGS} -w)

# ── Patched copies of the firmware sources ──────────────────────────────────
file(GLOB PORTABILITY_PATCHES "${CMAKE_SOURCE_DIR}/patches/portability/*.patch")
file(GLOB INSTANCE_PATCHES    "${CMAKE_SOURCE_DIR}/patches/instances/*.patch")
list(SORT PORTABILITY_PATCHES)
list(SORT INSTANCE_PATCHES)
file(GLOB_RECURSE FW_ALL_FILES RELATIVE "${SPOTYKACH_DIR}" "${SPOTYKACH_DIR}/src/*")
list(TRANSFORM FW_ALL_FILES PREPEND "${SPOTYKACH_DIR}/" OUTPUT_VARIABLE FW_ALL_PATHS)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${PORTABILITY_PATCHES} ${INSTANCE_PATCHES} ${FW_ALL_PATHS})

find_program(PATCH_EXECUTABLE patch)
if(NOT PATCH_EXECUTABLE)
    message(FATAL_ERROR "The 'patch' tool is needed to apply patches/ to the firmware.")
endif()

# Copies the firmware's src/ to <dest>/src with the given patches applied.
function(scotykach_patched_copy dest)
    set(stage "${dest}-stage")
    file(REMOVE_RECURSE "${stage}")
    file(MAKE_DIRECTORY "${stage}")
    file(COPY "${SPOTYKACH_DIR}/src" DESTINATION "${stage}")
    foreach(p IN LISTS ARGN)
        execute_process(
            COMMAND "${PATCH_EXECUTABLE}" -p1 -s -N -i "${p}"
            WORKING_DIRECTORY "${stage}"
            RESULT_VARIABLE patch_result
            OUTPUT_VARIABLE patch_output
            ERROR_VARIABLE  patch_output)
        if(NOT patch_result EQUAL 0)
            file(RELATIVE_PATH pname "${CMAKE_SOURCE_DIR}" "${p}")
            message(FATAL_ERROR
                "${pname} no longer applies to external/spotykach:\n${patch_output}\n"
                "Was the firmware submodule moved to another version?")
        endif()
    endforeach()
    foreach(f IN LISTS FW_ALL_FILES)
        get_filename_component(fdir "${dest}/${f}" DIRECTORY)
        file(MAKE_DIRECTORY "${fdir}")
        file(COPY_FILE "${stage}/${f}" "${dest}/${f}" ONLY_IF_DIFFERENT)
    endforeach()
    file(REMOVE_RECURSE "${stage}")
endfunction()

scotykach_patched_copy("${CMAKE_BINARY_DIR}/firmware"
    ${PORTABILITY_PATCHES} ${INSTANCE_PATCHES})
scotykach_patched_copy("${CMAKE_BINARY_DIR}/firmware-single"
    ${PORTABILITY_PATCHES})

# ── Firmware engine libraries ───────────────────────────────────────────────
set(SCOTYKACH_SHIM_DIR "${CMAKE_SOURCE_DIR}/core/shim")

function(scotykach_firmware_library name src_root)
    file(GLOB fw_sources "${src_root}/core/*.cpp" "${src_root}/core/fx/*.cpp")
    set(own_sources
        "${SCOTYKACH_SHIM_DIR}/Context.cpp"
        "${SCOTYKACH_SHIM_DIR}/hw/buffer.sdram.cpp"
        "${CMAKE_SOURCE_DIR}/core/ScotykachEngine.cpp")

    add_library(${name} STATIC ${fw_sources} ${own_sources})
    # The shim comes first, so the firmware's "hw/buffer.sdram.h" and
    # <daisy_seed.h> resolve to the desktop versions.
    target_include_directories(${name} PUBLIC
        "${SCOTYKACH_SHIM_DIR}"
        "${CMAKE_SOURCE_DIR}/core")
    target_include_directories(${name} SYSTEM PUBLIC "${src_root}")
    target_compile_options(${name} PUBLIC
        "SHELL:-include \"${SCOTYKACH_SHIM_DIR}/prelude.h\"")
    target_compile_options(${name} PRIVATE ${SCOTYKACH_FP_FLAGS})
    target_link_libraries(${name} PUBLIC daisysp)

    # Firmware code builds as it does for the Daisy, without our warnings.
    set_source_files_properties(${fw_sources} TARGET_DIRECTORY ${name}
        PROPERTIES COMPILE_OPTIONS "-fwrapv;-w")
    set_source_files_properties(${own_sources} TARGET_DIRECTORY ${name}
        PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra>")
endfunction()

scotykach_firmware_library(scotykach_fw        "${CMAKE_BINARY_DIR}/firmware/src")
scotykach_firmware_library(scotykach_fw_single "${CMAKE_BINARY_DIR}/firmware-single/src")
