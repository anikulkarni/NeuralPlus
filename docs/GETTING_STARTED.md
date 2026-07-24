<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# Getting started

## Requirements

- CMake 3.20 or newer
- A C++17 compiler
- libcurl development files
- Platform threads
- nlohmann/json 3.12.0, or network access for the checksum-pinned fallback
- Ninja when using the supplied presets

Typical Linux packages:

```bash
# Ubuntu or Debian
sudo apt-get install cmake ninja-build g++ libcurl4-openssl-dev

# Rocky, RHEL, Oracle Linux, or CentOS Stream
sudo dnf install cmake ninja-build gcc-c++ libcurl-devel
```

On macOS, install CMake and Ninja with your package manager; libcurl is
available in the system SDK. On Windows, Visual Studio 2022 plus vcpkg packages
`curl` and `nlohmann-json` is the tested setup.

## Configure, build, and test

With Ninja:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

With another generator:

```bash
cmake -S . -B build \
  -DNEURALPLUS_BUILD_TESTS=ON \
  -DNEURALPLUS_BUILD_EXAMPLES=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

Provider protocol unit tests use `MockHttpTransport`; they need no API keys and
make no live provider requests. Runnable examples compile against the real
clients and production transport. CI invokes their offline `--help` paths; see
[Provider examples](EXAMPLES.md).

## Install and consume

```bash
# After the dev preset:
cmake --install build/dev --prefix ./install

# After the generator-independent commands above:
cmake --install build --config Release --prefix ./install
```

Downstream CMake:

```cmake
find_package(NeuralPlus 0.2 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE NeuralPlus::neuralplus)
```

The package config resolves Threads, libcurl, and nlohmann/json. Add the
installation prefix to `CMAKE_PREFIX_PATH` when it is not in a standard
location.

## Credentials

Credentials resolve at client construction. Built-in clients exact-redact the
resolved key and sensitive authentication-header values from HTTP responses
before parsing and from `ProviderError` fields. Set
`HttpHeader::sensitive = true` for a custom credential header whose name does
not contain a standard key, token, secret, credential, cookie, or authorization
marker:

| Client | Resolution order |
|---|---|
| `OpenAIClient` | `OpenAIConfig::api_key`, then `OPENAI_API_KEY`, otherwise error |
| `AnthropicClient` | `AnthropicConfig::api_key`, then `ANTHROPIC_API_KEY`, otherwise error |
| `GeminiClient` | `GeminiConfig::api_key`, then `GEMINI_API_KEY`, then `GOOGLE_API_KEY`, otherwise error |
| `OpenAICompatibleClient` | Explicit key, then the configured environment-variable name; no key is allowed for local endpoints |

Do not commit `.env` files or keys. Prefer the host's secret store, inject the
value into the process environment, and restrict that environment to the
minimum required process. Exact redaction cannot recognize a transformed
credential. An injected `HttpTransport` must also avoid placing request
credentials in its own exception messages or logs.

For example:

```cpp
config.extra_headers.push_back(
    neuralplus::HttpHeader{
        "X-Custom-Auth", application_secret, true});
```

## Create a client and session

```cpp
auto config = neuralplus::models::openai::gpt_5_6_terra();

neuralplus::ClientOptions options;
options.tracers.push_back(std::make_shared<neuralplus::ConsoleTracer>());
options.max_model_rounds = 8;
options.max_parallel_tool_calls = 4;
options.max_tool_calls_per_generation = 64;

auto client = neuralplus::make_client(std::move(config), options);

neuralplus::SessionOptions session_options;
session_options.system_message = "Be concise.";
neuralplus::Session session{std::move(session_options)};

auto first = client->generate(session, "What is dependency injection?");
auto second = client->generate(session, "Give me a C++ example.");
```

The overloaded factory is type-safe: pass `AnthropicConfig`, `GeminiConfig`, or
`OpenAICompatibleConfig` to select another provider without changing the
calling code. The second call automatically includes the first conversation.
Use one session per independent conversation.

See [Model configurations](MODELS.md) for ready-to-use choices and the custom
model pattern.

## Tool execution limits

The values shown above are the defaults. `max_model_rounds` bounds all provider
rounds, including provider-native continuation. When a provider response would
exceed `max_tool_calls_per_generation`, the SDK rejects the whole requested
batch before invoking any tool from that response. `max_parallel_tool_calls`
limits concurrent callbacks; `parallel_tool_calls = false` makes execution
sequential. Tool results are recorded in provider-request order in either mode.
Calls to the same shared `Tool` object are serialized; different tool objects
may run concurrently. A tool requested on the final allowed model round is not
invoked because no remaining round could consume its result.

Tool declarations are immutable for the lifetime of a client. The constructor
calls `Tool::spec()` once and snapshots the validated schema, description, and
provider options. Construct a new client when a declaration needs to change.

## Multimodal content

Build a multimodal input from normalized `Content` values:

```cpp
neuralplus::Message input = neuralplus::Message::user({
    neuralplus::Content::text("Describe this image."),
    neuralplus::Content::image_bytes(png_bytes, "image/png"),
});
auto response = client->generate(session, input);
```

The current protocol adapters map the following content. This is an
implementation support table, not a claim that every model offered by a
provider accepts or generates every listed form.

| Client | User input mapped by the adapter | Output normalized by the adapter | Local tool-result content |
|---|---|---|---|
| OpenAI Responses | Text; image URL/bytes; file URL/bytes | Text; image-generation bytes; other native items as OpenAI extensions | Text, image, file, or OpenAI extension |
| Anthropic Messages | Text; image URL/bytes; document URL; inline PDF | Text; other native blocks as Anthropic extensions | Text, image, supported document, or Anthropic extension |
| Gemini generateContent | Text; image/audio/file bytes; Gemini File API or `gs://` URI | Text; inline/referenced image, audio, and file data; other parts as Gemini extensions | Text, inline image/audio/file bytes, or Gemini extension |
| OpenAI-compatible Chat Completions | Text; image URL/bytes; MP3/WAV bytes; file bytes | Text; image URL; audio bytes/transcript; other parts as compatible extensions | Text only |

Use `Content::extension` only when the application intentionally depends on one
provider's native JSON shape. An extension is rejected by a different provider
rather than translated silently.

`ModelDescriptor::capabilities` is the model-level gate and should describe the
configured model accurately. Adapter support is the upper bound: enabling a
capability cannot add a wire mapping that the table marks unsupported.
Conversely, an output mapping says how the SDK normalizes returned data, not
that the model is guaranteed to produce it.

## Token usage

`AIResponse::usage` returned by `generate` accumulates all provider rounds.
Each counter is optional and stays unknown if any round omitted it; absence is
not converted to zero. Explicit zero values remain available. Input tokens
include cache-read and cache-creation tokens, while their component counters
remain separately available when a provider reports them.

## Session rules

- `messages()` returns a safe snapshot.
- `state()` is a generic tool cache; values stay in process and are not traced.
- `clear_messages()` retains the system instruction and cache.
- Only one `generate` call may use a session at once.
- Separate sessions can be used concurrently.
- The callback passed to `SessionState::update` must not call another method on
  the same state object.

## Generate API documentation

Install Doxygen 1.9 or newer and follow
[Generate API documentation](DOXYGEN.md). The `neuralplus_docs` target checks
public documentation and writes `build/docs/api/index.html`.

## Platform support

Support describes source builds and tests, not vendor certification or binary
ABI compatibility across arbitrary toolchains.

| Tier | Environments | Validation |
|---|---|---|
| Tier 1 | Ubuntu 22.04 and 24.04; Debian 12; Red Hat UBI 9; Rocky Linux 9; Oracle Linux 9; CentOS Stream 9; macOS 14; Windows Server 2022 | Built and tested on every CI run |
| Best effort | Other current Linux distributions and newer OS/toolchain versions | Community reports and compatible C++17/libcurl installations |

UBI 9 exercises an Enterprise Linux userspace and is a useful RHEL 9
compatibility signal. It is not a RHEL test subscription and NeuralPlus is not
Red Hat certified. Rocky, Oracle, and CentOS testing similarly does not imply
vendor endorsement.

Tier 1 compiler families are GCC, Clang/Apple Clang, and Visual Studio 2022
MSVC. The project requires C++17 but does not promise ABI compatibility between
different standard libraries or major compiler versions.
