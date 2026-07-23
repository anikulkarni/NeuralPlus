// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

/// @file
/// Cross-platform public symbol visibility macro.

#pragma once

#if defined(_WIN32) && defined(NEURALPLUS_SHARED)
#if defined(NEURALPLUS_BUILDING_LIBRARY)
#define NEURALPLUS_API __declspec(dllexport)
#else
#define NEURALPLUS_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && defined(NEURALPLUS_SHARED)
#define NEURALPLUS_API __attribute__((visibility("default")))
#else
#define NEURALPLUS_API
#endif
