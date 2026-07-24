<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# Generate API documentation

NeuralPlus uses Doxygen through a CMake target. The docs build reads public
headers, repository Markdown, and the checked-in example programs. It treats
documentation warnings as errors and writes one browsable HTML site containing
the guides, API reference, and syntax-highlighted example source.

The current documentation from `main` is published at
[neuralplus.dev](https://neuralplus.dev/).

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

Open `build/docs/api/examples.html` to browse the generated source examples.

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

## GitHub Pages publishing

The `Doxygen` CI job builds and validates the site for pull requests without
publishing it. Following a successful push to `main`, the job uploads
`build/docs/api` and the `Deploy documentation` job publishes that exact
artifact to GitHub Pages. No generated HTML is committed to the repository,
and no provider credentials are used.

The repository administrator must configure **Settings → Pages → Build and
deployment → Source** as **GitHub Actions**. The workflow and repository Pages
setting are the only publishing configuration required.
