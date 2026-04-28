cmake_minimum_required(VERSION 3.20)

project(shade VERSION 0.2.0 LANGUAGES CXX)

add_library(shade INTERFACE)

target_include_directories(shade INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(shade INTERFACE cxx_std_23)

option(SHADE_BUILD_TESTS "Build shade tests" ON)
option(SHADE_BUILD_EXAMPLES "Build shade examples" ON)
option(SHADE_BUILD_BENCHMARKS "Build shade benchmarks" ON)

if(SHADE_BUILD_TESTS)
    enable_testing()
    add_executable(shade_tests tests/shade_tests.cpp)
    target_link_libraries(shade_tests PRIVATE shade)
    add_test(NAME shade_tests COMMAND shade_tests)
endif()

if(SHADE_BUILD_EXAMPLES)
    add_executable(shade_demo examples/demo.cpp)
    target_link_libraries(shade_demo PRIVATE shade)
endif()

if(SHADE_BUILD_BENCHMARKS)
    add_executable(shade_bench benchmarks/dispatch_bench.cpp)
    target_link_libraries(shade_bench PRIVATE shade)
endif()