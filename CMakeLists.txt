cmake_minimum_required(VERSION 3.20)

project(
    NeuralPlus
    VERSION 0.1.0
    DESCRIPTION "A modular C++17 foundation for model clients, tools, agents, tracing, and RAG"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(NEURALPLUS_BUILD_EXAMPLES "Build NeuralPlus examples" ON)
option(NEURALPLUS_BUILD_TESTS "Build NeuralPlus tests" ON)
option(NEURALPLUS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(neuralplus src/agent.cpp)
add_library(NeuralPlus::neuralplus ALIAS neuralplus)

target_compile_features(neuralplus PUBLIC cxx_std_17)
target_include_directories(
    neuralplus
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    target_compile_options(neuralplus PRIVATE /W4 /permissive-)
    if(NEURALPLUS_WARNINGS_AS_ERRORS)
        target_compile_options(neuralplus PRIVATE /WX)
    endif()
else()
    target_compile_options(neuralplus PRIVATE -Wall -Wextra -Wpedantic)
    if(NEURALPLUS_WARNINGS_AS_ERRORS)
        target_compile_options(neuralplus PRIVATE -Werror)
    endif()
endif()

set_target_properties(
    neuralplus
    PROPERTIES
        OUTPUT_NAME neuralplus
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        CXX_EXTENSIONS OFF
)

if(NEURALPLUS_BUILD_EXAMPLES)
    add_executable(neuralplus_stateful_tools examples/stateful_parallel_tools.cpp)
    target_link_libraries(neuralplus_stateful_tools PRIVATE NeuralPlus::neuralplus)
endif()

if(NEURALPLUS_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(neuralplus_core_tests tests/core_tests.cpp)
    target_link_libraries(neuralplus_core_tests PRIVATE NeuralPlus::neuralplus)
    add_test(NAME neuralplus_core_tests COMMAND neuralplus_core_tests)
endif()

install(
    TARGETS neuralplus
    EXPORT NeuralPlusTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/NeuralPlusConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    cmake/NeuralPlusConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/NeuralPlusConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NeuralPlus
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/NeuralPlusConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/NeuralPlusConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NeuralPlus
)

install(
    EXPORT NeuralPlusTargets
    FILE NeuralPlusTargets.cmake
    NAMESPACE NeuralPlus::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NeuralPlus
)
