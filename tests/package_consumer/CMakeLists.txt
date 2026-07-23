# Copyright 2026 Aniket Kulkarni
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(NeuralPlusPackageConsumer LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(NeuralPlus 0.2 CONFIG REQUIRED)

add_executable(neuralplus_package_consumer main.cpp)
target_link_libraries(
    neuralplus_package_consumer
    PRIVATE NeuralPlus::neuralplus
)
