# Copyright 2026 Aniket Kulkarni
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED VERSION_FILE)
    message(FATAL_ERROR "VERSION_FILE is required")
endif()

# A 0.2.x installation must satisfy a request from the earlier 0.x feature
# line. This guards the documented promise that minor releases remain
# backward-compatible.
set(PACKAGE_FIND_VERSION "0.1")
set(PACKAGE_FIND_VERSION_MAJOR "0")
set(PACKAGE_FIND_VERSION_MINOR "1")
set(PACKAGE_FIND_VERSION_PATCH "0")
set(PACKAGE_FIND_VERSION_TWEAK "0")
set(PACKAGE_FIND_VERSION_COUNT "2")

include("${VERSION_FILE}")

if(NOT PACKAGE_VERSION_COMPATIBLE)
    message(
        FATAL_ERROR
        "NeuralPlus ${PACKAGE_VERSION} must satisfy a 0.1 version request"
    )
endif()
