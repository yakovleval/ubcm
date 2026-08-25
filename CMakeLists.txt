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

if(BUILD_TESTING)
    add_executable(ubcm_unit_tests
        tests/unit_tests.cpp
    )

    target_link_libraries(ubcm_unit_tests PRIVATE ubcm::vm)

    add_test(NAME ubcm.unit COMMAND ubcm_unit_tests)
endif()
