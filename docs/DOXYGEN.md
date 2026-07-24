<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# Generate API documentation

NeuralPlus uses Doxygen through a CMake target. The docs build reads public
headers and repository Markdown, treats documentation warnings as errors, and
writes a browsable HTML site.

## Install the tools

Doxygen 1.9 or newer, CMake 3.20 or newer, Ninja, and the normal NeuralPlus
build dependencies are required.

```bash
# Ubuntu or Debian
sudo apt-get install cmake doxygen ninja-build libcurl4-openssl-dev

# Rocky Linux, RHEL, Oracle Linux, or CentOS Stream
sudo dnf install cmake doxygen ninja-build libcurl-devel

# macOS with Homebrew
brew install cmake doxygen ninja nlohmann-json
```

On Windows, install current Doxygen and Ninja releases and make them available
on `PATH`. Use the same Visual Studio and vcpkg dependencies documented in
[Getting started](GETTING_STARTED.md).

## Generate or refresh HTML

From the repository root:

```bash
cmake --preset docs
cmake --build --preset docs
```

The main page is:

```text
build/docs/api/index.html
```

After changing a public header or Markdown file, rerun the build command. For
a complete refresh:

```bash
cmake --build --preset docs --clean-first
```

Without presets:

```bash
cmake -S . -B build/docs \
  -DNEURALPLUS_BUILD_DOCS=ON \
  -DNEURALPLUS_BUILD_EXAMPLES=OFF \
  -DNEURALPLUS_BUILD_TESTS=OFF
cmake --build build/docs --target neuralplus_docs
```

The `neuralplus_docs` target fails on undocumented public API or malformed
Doxygen markup. Fix the source warning rather than suppressing the check.
GitHub CI runs the same target for every pull request.
