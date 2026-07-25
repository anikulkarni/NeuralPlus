<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# NeuralPlus

[![C++17](https://img.shields.io/badge/C%2B%2B-17%2B-blue.svg)](https://isocpp.org/)
[![GitHub repository](https://img.shields.io/badge/GitHub-View%20repository-181717?logo=github)](https://github.com/anikulkarni/NeuralPlus)
[![CI](https://github.com/anikulkarni/NeuralPlus/actions/workflows/ci.yml/badge.svg)](https://github.com/anikulkarni/NeuralPlus/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://neuralplus.dev/)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://github.com/anikulkarni/NeuralPlus/blob/main/LICENSE)

<p align="center">
  <img src="docs/assets/neuralplus-social-preview.png"
       alt="NeuralPlus — an open-source C++17 AI SDK and LLM client library."
       width="1200">
</p>

NeuralPlus is an open-source, provider-independent C++17 AI SDK and LLM client
library for OpenAI, Anthropic, Gemini, tool calling, session state, and tracing.

> **Status:** General Availability. Version 0.2.0 is the first supported
> release of the simplified NeuralPlus API.

## The design in five pieces

- `AIClient` owns the complete conversation/tool loop.
- `OpenAIClient`, `AnthropicClient`, `GeminiClient`, and
  `OpenAICompatibleClient` translate one provider round.
- `Session` owns conversation messages plus a thread-safe, process-local cache.
- `Tool` is the formal executable extension point; `FunctionTool` covers most
  applications without a new subclass.
- Any number of `Tracer` objects can be attached to a client. Built-ins cover
  console, JSON Lines files, memory, callbacks, and POSIX syslog.

A model name is data (`ModelDescriptor::id`), not a new C++ type. This keeps
the class tree stable when providers add models. Typed configurations for
common current models provide readable defaults without restricting custom
model IDs.

## Build

Prerequisites are CMake 3.20+, a C++17 compiler, libcurl development files, and
Threads. CMake first looks for nlohmann/json 3.12.0 and otherwise downloads its
checksum-pinned release archive.

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The `dev` and `release` build presets build the library, tests, and all
examples. To build only the examples, use the matching examples preset:

```bash
cmake --build --preset dev-examples
# Or: cmake --build --preset release-examples
```

Example executables are placed in `build/dev/examples/` or
`build/release/examples/`.

The equivalent generator-independent commands are documented in
[Getting started](docs/GETTING_STARTED.md).

## OpenAI client example

The factory returns the common `AIClient` interface. The OpenAI configuration
reads `OPENAI_API_KEY` when `config.api_key` is not set:

```cpp
#include <neuralplus/neuralplus.hpp>

#include <iostream>
#include <utility>

int main() {
    auto config = neuralplus::models::openai::gpt_5_6_terra();
    auto client = neuralplus::make_client(std::move(config));
    neuralplus::Session session;

    const auto response =
        client->generate(session, "Explain RAII in one sentence.");
    std::cout << response.message.text() << '\n';
}
```

Change only the typed configuration passed to `make_client` to select another
provider. The same `Session`, tools, and tracers work with every built-in
provider.
Credentials can also be assigned directly to the provider configuration; see
[Credentials](https://github.com/anikulkarni/NeuralPlus/blob/main/docs/GETTING_STARTED.md#credentials).

The catalog includes current OpenAI, Anthropic, and Gemini configurations:
[Model configurations](docs/MODELS.md). An arbitrary provider model remains
one typed configuration constructor away.

## More LLM client examples

The built-in clients make real provider requests. They use
`ANTHROPIC_API_KEY`, `GEMINI_API_KEY`/`GOOGLE_API_KEY`, and `OPENAI_API_KEY`
when an explicit key is not assigned.

### Anthropic conversation

```cpp
auto config = neuralplus::models::anthropic::claude_sonnet_5();
auto client = neuralplus::make_client(std::move(config));

neuralplus::Session session;
session.set_system("Answer as a concise C++ mentor.");

const auto response =
    client->generate(session, "When should I use std::string_view?");
std::cout << response.message.text() << '\n';
```

### Gemini conversation

```cpp
auto config = neuralplus::models::gemini::gemini_3_6_flash();
auto client = neuralplus::make_client(std::move(config));
neuralplus::Session session;

const auto response =
    client->generate(session, "Give me three practical RAII examples.");
std::cout << response.message.text() << '\n';
```

### OpenAI-compatible server

Use the same interface with a local or hosted Chat Completions-compatible
server:

```cpp
neuralplus::OpenAICompatibleConfig config(
    "local-model", "http://localhost:8000/v1");
config.api_key_environment = "LOCAL_LLM_API_KEY";  // Optional.

auto client = neuralplus::make_client(std::move(config));
const auto response = client->generate("Explain move semantics simply.");
std::cout << response.message.text() << '\n';
```

### Multimodal request

```cpp
auto config = neuralplus::models::openai::gpt_5_6_terra();
auto client = neuralplus::make_client(std::move(config));

neuralplus::Message::Contents contents;
contents.push_back(
    neuralplus::Content::text("Describe the architecture in this image."));
contents.push_back(neuralplus::Content::image_url(
    "https://example.com/architecture.png", "image/png"));

const auto response =
    client->generate(neuralplus::Message::user(std::move(contents)));
std::cout << response.message.text() << '\n';
```

Complete, runnable programs:

| Scenario | OpenAI | Anthropic | Gemini | Compatible server |
| --- | --- | --- | --- | --- |
| Chatbot | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/openai_chatbot.cpp) | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/anthropic_chatbot.cpp) | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/gemini_chatbot.cpp) | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/openai_compatible_chatbot.cpp) |
| Custom model/config | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/openai_custom_config.cpp) | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/anthropic_custom_config.cpp) | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/gemini_custom_config.cpp) | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/openai_compatible_custom_config.cpp) |
| Multimodal | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/openai_multimodal.cpp) | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/anthropic_multimodal.cpp) | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/gemini_multimodal.cpp) | [source](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/openai_compatible_multimodal.cpp) |

## Add a stateful tool and tracers

```cpp
neuralplus::ToolSpec spec;
spec.name = "increment";
spec.description = "Increment this session's counter.";
spec.input_schema = {
    {"type", "object"},
    {"properties", {{"delta", {{"type", "integer"}}}}},
    {"required", {"delta"}},
};

auto tool = std::make_shared<neuralplus::FunctionTool>(
    std::move(spec),
    [](neuralplus::ToolContext& context,
       const neuralplus::JsonValue& arguments) {
        const int delta = arguments.at("delta").get<int>();
        const int value = context.state().update<int>(
            "counter", 0, [delta](int current) { return current + delta; });
        return neuralplus::ToolOutput::json({{"counter", value}});
    });

neuralplus::ClientOptions options;
options.tools = {tool};
options.tracers = {
    std::make_shared<neuralplus::ConsoleTracer>(),
    std::make_shared<neuralplus::FileTracer>("trace.jsonl"),
};
```

Trace output is metadata-only by default. `capture_trace_payloads` makes
payloads available to in-memory, callback, and custom tracers. `FileTracer`
also requires `FileTracerOptions::include_payloads`; console and syslog output
remain metadata-only. New trace files are created with mode `0600` on POSIX.
Provider rounds, total tool calls, concurrent tool callbacks, and HTTP response
sizes all have configurable bounds. Tool declarations are validated and
snapshotted when the client is constructed.

The complete credential-free example uses `FunctionAIClient`:
[examples/simple_session.cpp](https://github.com/anikulkarni/NeuralPlus/blob/main/examples/simple_session.cpp).
Ready-to-run chatbots, custom-model programs, and multimodal examples use the
real built-in provider clients:
[Provider examples](docs/EXAMPLES.md).

## Documentation

- [Published documentation and example source](https://neuralplus.dev/)
- [Getting started](docs/GETTING_STARTED.md)
- [Architecture and class diagram](docs/ARCHITECTURE.md)
- [Model configurations](docs/MODELS.md)
- [Provider examples](docs/EXAMPLES.md)
- [Generating Doxygen documentation](docs/DOXYGEN.md)
- [Extending clients, tools, and tracing](docs/EXTENDING.md)
- [Roadmap](docs/ROADMAP.md)
- [Contributing](CONTRIBUTING.md)
- [Contributors](CONTRIBUTORS.md)
- [Code of Conduct](CODE_OF_CONDUCT.md)
- [Security policy](SECURITY.md)

Generate API documentation with:

```bash
cmake --preset docs
cmake --build --preset docs
```

The generated entry page is `build/docs/api/index.html`, and CI publishes the
same output from `main` to GitHub Pages. See the
[Doxygen guide](docs/DOXYGEN.md) for installation, refresh, publishing, and
non-preset commands.

## Supported environments

The library targets C++17 on Linux, macOS, and Windows. Every CI run covers
Ubuntu 22.04/24.04, macOS 14, Windows Server 2022, Rocky Linux 9, Debian 12,
Red Hat UBI 9, Oracle Linux 9, and CentOS Stream 9. UBI is a compatibility
signal for RHEL 9, not Red Hat certification. See
[Getting started](https://github.com/anikulkarni/NeuralPlus/blob/main/docs/GETTING_STARTED.md#platform-support)
for compiler and platform details.

## License

NeuralPlus is licensed under the
[Apache License 2.0](https://github.com/anikulkarni/NeuralPlus/blob/main/LICENSE).
Dependency
attributions and licenses are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
