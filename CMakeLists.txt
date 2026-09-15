cmake_minimum_required(VERSION 3.20)

project(
    ubcm
    VERSION 0.1.0
    DESCRIPTION "Universal Bit-Code Machine"
    LANGUAGES CXX
)

include(CTest)

add_library(ubcm_vm STATIC
    src/bit_vector.cpp
    src/bit_cursor.cpp
    src/codec.cpp
    src/registers.cpp
    src/addressing.cpp
    src/runtime_reference.cpp
    src/node.cpp
    src/activation.cpp
    src/arithmetic.cpp
    src/builtin.cpp
    src/operand.cpp
    src/vm.cpp
    src/resolver.cpp
    src/program.cpp
    src/ubcm.cpp
)

add_library(ubcm::vm ALIAS ubcm_vm)

target_compile_features(ubcm_vm PUBLIC cxx_std_23)

target_include_directories(ubcm_vm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(ubcm_vm PROPERTIES
    CXX_EXTENSIONS OFF
)

add_executable(ubcm
    apps/ubcm.cpp
)

target_link_libraries(ubcm PRIVATE ubcm::vm)

add_executable(ubcm_demo examples/make_demo.cpp)
target_link_libraries(ubcm_demo PRIVATE ubcm::vm)

if(BUILD_TESTING)
    add_executable(ubcm_unit_tests
        tests/unit_tests.cpp
    )

    target_link_libraries(ubcm_unit_tests PRIVATE ubcm::vm)

    add_test(NAME ubcm.unit COMMAND ubcm_unit_tests)
    add_executable(ubcm_program_tests tests/program_tests.cpp)
    target_link_libraries(ubcm_program_tests PRIVATE ubcm::vm)
    add_test(NAME ubcm.program COMMAND ubcm_program_tests)
    add_executable(ubcm_resolver_tests tests/resolver_tests.cpp)
    target_link_libraries(ubcm_resolver_tests PRIVATE ubcm::vm)
    add_test(NAME ubcm.resolver COMMAND ubcm_resolver_tests)
    add_executable(ubcm_codec_fuzz_tests tests/codec_fuzz_tests.cpp)
    target_link_libraries(ubcm_codec_fuzz_tests PRIVATE ubcm::vm)
    add_test(NAME ubcm.codec_fuzz COMMAND ubcm_codec_fuzz_tests)
    add_test(NAME ubcm.cli COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:ubcm> -DDEMO=$<TARGET_FILE:ubcm_demo>
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_tests.cmake)
endif()
