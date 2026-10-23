# Engine tests.
#
#   cmake -B build && cmake --build build && ctest --test-dir build

find_package(Threads REQUIRED)

function(scotykach_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/tools")
    target_link_libraries(${name} PRIVATE scotykach_fw Threads::Threads)
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)
endfunction()

# Reel, Slice and Drift; host block sizes; every mode, route and control.
scotykach_test(engine_test engine_test.cpp)
add_test(NAME engine COMMAND engine_test)

# Several engines in one process.
scotykach_test(instances_test instances_test.cpp)
add_test(NAME instances COMMAND instances_test)

# patches/instances against the firmware as it runs on the module: the same
# scenarios through both builds must give the same bytes.
add_executable(scenario_dump scenario_dump.cpp)
add_executable(scenario_dump_single scenario_dump.cpp)
foreach(t scenario_dump scenario_dump_single)
    target_include_directories(${t} PRIVATE "${CMAKE_SOURCE_DIR}/tools")
endforeach()
target_link_libraries(scenario_dump        PRIVATE scotykach_fw)
target_link_libraries(scenario_dump_single PRIVATE scotykach_fw_single)

# The scenario names, as AllScenarios() in Scenarios.h lists them.
set(SCENARIOS)
foreach(mode reel slice drift)
    list(APPEND SCENARIOS ${mode})
    foreach(route stereo mono gen)
        list(APPEND SCENARIOS ${mode}-${route}-performance)
    endforeach()
endforeach()

set(index 0)
foreach(name IN LISTS SCENARIOS)
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/dumps")
    add_test(NAME dump_${name}        COMMAND scenario_dump        ${index} "${dir}/${name}.patched.raw")
    add_test(NAME dump_single_${name} COMMAND scenario_dump_single ${index} "${dir}/${name}.single.raw")
    set_tests_properties(dump_${name} dump_single_${name} PROPERTIES FIXTURES_SETUP dump_${name})
    add_test(NAME same_as_firmware_${name}
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${dir}/${name}.patched.raw" "${dir}/${name}.single.raw")
    set_tests_properties(same_as_firmware_${name} PROPERTIES FIXTURES_REQUIRED dump_${name})
    math(EXPR index "${index} + 1")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dumps")

# The list above must match the scenarios the program has.
add_test(NAME scenario_list COMMAND scenario_dump --list)
string(REPLACE ";" "\n" scenario_lines "${SCENARIOS}")
set_tests_properties(scenario_list PROPERTIES PASS_REGULAR_EXPRESSION "^${scenario_lines}\n$")

# The host-facing processor: rates, block sizes, Live's clock, parameters.
scotykach_test(processor_test processor_test.cpp)
add_test(NAME processor COMMAND processor_test)

# Sample-rate conversion around the 48 kHz module (copied from Capicola).
scotykach_test(resampler_test resampler_test.cpp)
add_test(NAME resampler COMMAND resampler_test)

# The whole firmware (engine and UI) against the engine alone; LEDs, gates.
scotykach_test(module_test module_test.cpp)
add_test(NAME module COMMAND module_test)

# The parameter table against the booted firmware.
scotykach_test(params_test params_test.cpp)
add_test(NAME params COMMAND params_test)
