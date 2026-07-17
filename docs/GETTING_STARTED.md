# Getting started

## Requirements

- A C++17-capable compiler
- CMake 3.20 or newer
- Git for source-based use

The bootstrap core has no mandatory third-party runtime dependencies.

## Configure and build

```bash
git clone https://github.com/anikulkarni/NeuralPlus.git
cd NeuralPlus
cmake -S . -B build -DNEURALPLUS_BUILD_TESTS=ON -DNEURALPLUS_BUILD_EXAMPLES=ON
cmake --build build --config Release
```

## Test

Single-config generators:

```bash
ctest --test-dir build --output-on-failure
```

Multi-config generators:

```bash
ctest --test-dir build --output-on-failure -C Release
```

## Install

```bash
cmake --install build --prefix ./install
```

A downstream CMake project can then use:

```cmake
find_package(NeuralPlus CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE NeuralPlus::neuralplus)
```

Set `CMAKE_PREFIX_PATH` to the chosen installation prefix when necessary.

## Create a tool

Derive directly from `Tool` when your parser and output are naturally string-based. Derive from `TypedTool<Input, Output>` when typed execution is valuable.

State that should survive multiple tool calls belongs in `context.state`. Use `update<T>` for values modified by parallel calls.

## Create a model

A provider adapter derives from `Model` and converts `ModelRequest` into the provider wire format. The adapter converts the provider response back into `ModelResponse`, including all requested tool calls.

Applications should depend on the `Model` interface rather than a concrete provider wherever runtime substitution is useful.

## Run an agent

1. Create a model object.
2. Add tools to a shared `ToolRegistry`.
3. Construct an `Agent`.
4. Create one `Session` per independent conversation.
5. Call `Agent::run`.

Do not invoke `run` concurrently on the same session. Separate sessions can be processed concurrently.
