# Copyright 2026 Aniket Kulkarni
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(
    NeuralPlus
    VERSION 0.2.0
    DESCRIPTION "A small C++17 SDK for AI clients, sessions, tools, and tracing"
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)

include(CMakePackageConfigHelpers)
include(FetchContent)
include(GNUInstallDirs)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(NEURALPLUS_TOP_LEVEL_DEFAULT OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(NEURALPLUS_TOP_LEVEL_DEFAULT ON)
endif()

option(NEURALPLUS_BUILD_DOCS "Build NeuralPlus API documentation" OFF)
option(
    NEURALPLUS_BUILD_EXAMPLES
    "Build NeuralPlus examples"
    ${NEURALPLUS_TOP_LEVEL_DEFAULT}
)
option(
    NEURALPLUS_BUILD_TESTS
    "Build NeuralPlus tests"
    ${NEURALPLUS_TOP_LEVEL_DEFAULT}
)
option(NEURALPLUS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
unset(NEURALPLUS_TOP_LEVEL_DEFAULT)

function(neuralplus_enable_strict_warnings target)
    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
                /W4
                /permissive-
                /wd4251
                /wd4275
        )
        if(NEURALPLUS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE -Wall -Wextra -Wpedantic
        )
        if(NEURALPLUS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
if(WIN32 AND DEFINED CURL_VERSION_STRING AND
   CURL_VERSION_STRING VERSION_LESS "7.84.0")
    message(
        FATAL_ERROR
        "NeuralPlus requires libcurl 7.84.0 or newer on Windows for "
        "thread-safe global initialization"
    )
endif()

find_package(nlohmann_json 3.12.0 EXACT QUIET)
if(NOT nlohmann_json_FOUND)
    set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann/json tests" FORCE)
    set(JSON_Install ON CACHE BOOL "Install the fetched nlohmann/json package" FORCE)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH
            SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

set(
    NEURALPLUS_SOURCES
    src/client.cpp
    src/models.cpp
    src/provider_common.cpp
    src/providers/anthropic.cpp
    src/providers/gemini.cpp
    src/providers/openai.cpp
    src/providers/openai_compatible.cpp
    src/session.cpp
    src/tool.cpp
    src/tracing.cpp
    src/transport.cpp
    src/types.cpp
)

add_library(neuralplus ${NEURALPLUS_SOURCES})
add_library(NeuralPlus::neuralplus ALIAS neuralplus)

target_compile_features(neuralplus PUBLIC cxx_std_17)
target_compile_definitions(neuralplus PRIVATE NEURALPLUS_BUILDING_LIBRARY)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(neuralplus PUBLIC NEURALPLUS_SHARED)
endif()

target_include_directories(
    neuralplus
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    neuralplus
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        CURL::libcurl
        Threads::Threads
)

neuralplus_enable_strict_warnings(neuralplus)

set_target_properties(
    neuralplus
    PROPERTIES
        CXX_EXTENSIONS OFF
        OUTPUT_NAME neuralplus
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
)

set(NEURALPLUS_PROVIDER_EXAMPLE_TARGETS)

if(NEURALPLUS_BUILD_EXAMPLES)
    function(neuralplus_add_example example_name)
        set(example_target "neuralplus_${example_name}")
        add_executable(${example_target} "examples/${example_name}.cpp")
        target_link_libraries(${example_target} PRIVATE NeuralPlus::neuralplus)
        neuralplus_enable_strict_warnings(${example_target})
        set(
            NEURALPLUS_PROVIDER_EXAMPLE_TARGETS
            ${NEURALPLUS_PROVIDER_EXAMPLE_TARGETS} ${example_target}
            PARENT_SCOPE
        )
    endfunction()

    add_executable(neuralplus_simple_session examples/simple_session.cpp)
    target_link_libraries(neuralplus_simple_session PRIVATE NeuralPlus::neuralplus)
    neuralplus_enable_strict_warnings(neuralplus_simple_session)

    neuralplus_add_example(openai_chatbot)
    neuralplus_add_example(openai_custom_config)
    neuralplus_add_example(openai_multimodal)
    neuralplus_add_example(anthropic_chatbot)
    neuralplus_add_example(anthropic_custom_config)
    neuralplus_add_example(anthropic_multimodal)
    neuralplus_add_example(gemini_chatbot)
    neuralplus_add_example(gemini_custom_config)
    neuralplus_add_example(gemini_multimodal)
    neuralplus_add_example(openai_compatible_chatbot)
    neuralplus_add_example(openai_compatible_custom_config)
    neuralplus_add_example(openai_compatible_multimodal)
endif()

if(NEURALPLUS_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(neuralplus_core_tests tests/core_tests.cpp)
    target_link_libraries(neuralplus_core_tests PRIVATE NeuralPlus::neuralplus)
    neuralplus_enable_strict_warnings(neuralplus_core_tests)
    add_test(NAME neuralplus_core_tests COMMAND neuralplus_core_tests)

    add_executable(neuralplus_provider_tests tests/provider_tests.cpp)
    target_link_libraries(neuralplus_provider_tests PRIVATE NeuralPlus::neuralplus)
    neuralplus_enable_strict_warnings(neuralplus_provider_tests)
    add_test(NAME neuralplus_provider_tests COMMAND neuralplus_provider_tests)

    add_executable(neuralplus_model_tests tests/model_tests.cpp)
    target_link_libraries(neuralplus_model_tests PRIVATE NeuralPlus::neuralplus)
    neuralplus_enable_strict_warnings(neuralplus_model_tests)
    add_test(NAME neuralplus_model_tests COMMAND neuralplus_model_tests)

    if(NEURALPLUS_BUILD_EXAMPLES)
        add_executable(
            neuralplus_example_support_tests
            tests/example_support_tests.cpp
        )
        target_link_libraries(
            neuralplus_example_support_tests
            PRIVATE NeuralPlus::neuralplus
        )
        neuralplus_enable_strict_warnings(neuralplus_example_support_tests)
        add_test(
            NAME neuralplus_example_support_tests
            COMMAND neuralplus_example_support_tests
        )

        add_test(
            NAME neuralplus_simple_session_example
            COMMAND neuralplus_simple_session
        )
        foreach(example_target IN LISTS NEURALPLUS_PROVIDER_EXAMPLE_TARGETS)
            add_test(
                NAME "${example_target}_help"
                COMMAND ${example_target} --help
            )
        endforeach()
    endif()
endif()

if(NEURALPLUS_BUILD_DOCS)
    find_package(Doxygen 1.9 REQUIRED)
    set(DOXYGEN_EXTRACT_ALL NO)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_HTML_OUTPUT api)
    set(DOXYGEN_PROJECT_BRIEF "${PROJECT_DESCRIPTION}")
    set(DOXYGEN_QUIET YES)
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_STRIP_FROM_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(DOXYGEN_WARN_IF_DOC_ERROR YES)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)
    doxygen_add_docs(
        neuralplus_docs
        include
        README.md
        docs
        CHANGELOG.md
        CODE_OF_CONDUCT.md
        CONTRIBUTING.md
        CONTRIBUTORS.md
        SECURITY.md
        THIRD_PARTY_NOTICES.md
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating NeuralPlus API documentation"
    )
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
install(
    FILES
        LICENSE
        NOTICE
        CONTRIBUTORS.md
        THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/NeuralPlus
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/NeuralPlusConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
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
