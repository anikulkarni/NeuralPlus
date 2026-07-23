<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# Extending NeuralPlus

## Add a tool

Use `FunctionTool` unless the tool needs a reusable class:

```cpp
neuralplus::ToolSpec spec;
spec.name = "lookup";
spec.description = "Look up one record by ID.";
spec.input_schema = {
    {"type", "object"},
    {"properties", {{"id", {{"type", "string"}}}}},
    {"required", {"id"}},
};

auto lookup = std::make_shared<neuralplus::FunctionTool>(
    std::move(spec),
    [&repository](neuralplus::ToolContext& context,
                  const neuralplus::JsonValue& arguments) {
        context.state().set("lookup.last_id", arguments.at("id").get<std::string>());
        return neuralplus::ToolOutput::json(
            repository.lookup(arguments.at("id").get<std::string>()));
    });
```

A tool should validate all model-generated input, use stable names, keep its
output concise, and avoid global session data. The SDK serializes calls to the
same shared `Tool` object. Different tool objects can run in parallel and must
synchronize any captured services they share.

`ToolSpec::provider_options` can add declaration fields understood only by the
selected provider. The adapter applies the stable `name`, `description`, and
`input_schema` after these options, so portable fields always win. To combine
provider-native server tools with local tools, put the native declarations in
the `tools` array of `ModelDescriptor::provider_options` or
`GenerateOptions::provider_options`; the adapter appends local declarations.
Provider options cannot enable OpenAI or Anthropic streaming, or OpenAI
background responses, because `AIClient::generate` exposes a synchronous
complete-response contract.

The `AIClient` constructor snapshots every validated `ToolSpec`. Do not rely on
mutating a declaration after client construction. Mutable execution state
belongs in the tool implementation or `SessionState`, with the synchronization
required by the application's concurrency policy.

Subclass `Tool` when lifecycle, dependency ownership, or test seams make a
class clearer. Implement `spec()` and `invoke()`; no registry or task wrapper
is needed.

## Add a provider

Subclass `AIClient` and implement exactly one method:

```cpp
class MyProviderClient final : public neuralplus::AIClient {
public:
    MyProviderClient(neuralplus::ModelDescriptor model,
                     const neuralplus::ClientOptions& options)
        : AIClient(std::move(model), options) {}

private:
    MyProviderClient(const MyProviderClient&) = delete;
    MyProviderClient& operator=(const MyProviderClient&) = delete;

    neuralplus::AIResponse
    generate_once(const neuralplus::AIRequest& request) override {
        // Map the normalized request, send it, and map one response.
    }
};
```

Keep these concerns inside the adapter:

1. resolve configuration without exposing secrets;
2. map normalized messages, content, options, and tool schemas;
3. call the injected `HttpTransport`;
4. validate and map the response;
5. turn HTTP/provider failures into `ProviderError`.

Set `AIResponse::requires_continuation` when the provider protocol requires
another round even though there are no local tool calls. Preserve the native
assistant material in `Message::provider_metadata`; `AIClient` appends that
message and calls `generate_once` again. Anthropic `pause_turn` is the built-in
example. The base class applies the same round limit to continuation rounds.

Populate only the `TokenUsage` fields the response actually reports or that the
adapter can calculate safely. Do not convert a missing counter to zero. The
base class preserves this distinction while accumulating multi-round usage.

Do not reimplement session mutation, multi-round tool execution, or tracer
fan-out. Tests should inject `MockHttpTransport` with sanitized JSON. The
default suite must not need credentials or network access.

One client may process different sessions concurrently. Built-in adapters
support this; a custom provider's `generate_once` implementation must be
thread-safe.

## Add a custom model

Use `FunctionAIClient` for an in-process runtime, test double, or thin
application adapter. Populate `ModelDescriptor::capabilities` accurately so
unsupported content and tools fail before a request is sent. The convenience
class serializes calls to its callback.

## Add a tracer or logger

Subclass `Tracer` for a reusable backend, or supply a `FunctionTracer`:

```cpp
auto tracer = std::make_shared<neuralplus::FunctionTracer>(
    [](const neuralplus::TraceEvent& event) {
        application_logger(neuralplus::to_string(event.level), event.name);
    });
```

A custom `Tracer::record` must be thread-safe, bounded, and non-blocking enough
for the application's latency goals. `FunctionTracer` serializes calls to its
callback. The client catches tracer exceptions, but a backend should still
report its own delivery failures out of band.

Treat `payload` as sensitive. Preserve the default metadata-only policy, use
allowlisted attributes, and avoid authorization headers and request bodies.

For OpenTelemetry, translate the event in a `FunctionTracer` to the official
[OpenTelemetry C++ API](https://opentelemetry.io/docs/languages/cpp/instrumentation/).
Use `run_id` for the logical generation, `operation_id` for a model round or
tool call, and provider request IDs as attributes. An application owns SDK
initialization, exporters, sampling, and shutdown. NeuralPlus does not
force-link the OpenTelemetry SDK.

A minimal bridge keeps the OpenTelemetry objects in application code:

```cpp
auto bridge = std::make_shared<neuralplus::FunctionTracer>(
    [&spans](const neuralplus::TraceEvent& event) {
        // `spans` is an application-owned run/operation span registry.
        auto span = spans.for_event(event.run_id, event.operation_id);
        span->SetAttribute("neuralplus.event_type",
                           neuralplus::to_string(event.type));
        span->SetAttribute("neuralplus.operation_name", event.name);
        // Start/end the corresponding span for *_start/*_end events.
    });
```

Map `generation_start`/`generation_end` to a run span,
`provider_start`/`provider_end` and `tool_start`/`tool_end` to child spans, and
`error` to an error status. Do not attach `payload` unless the application's
telemetry policy explicitly permits it.

`SyslogTracer` follows
[POSIX syslog](https://pubs.opengroup.org/onlinepubs/9699919799/functions/syslog.html)
and is unavailable on Windows. It supplies the facility with each record and
uses an identity prefix without changing process-global `openlog` state.

## Add a transport

Implement `HttpTransport::send` to integrate an application HTTP stack.
Preserve these security properties of the built-in
[libcurl transport](https://curl.se/libcurl/c/libcurl-easy.html):

- TLS peer and host verification enabled by default;
- only HTTP and HTTPS protocols;
- no automatic redirects;
- validated header names and values;
- bounded request timeouts;
- bounded response bodies and aggregate response headers;
- errors that omit URLs, headers, and bodies.

`HttpTransportOptions` defaults to a 16 MiB response-body limit and a 256 KiB
aggregate response-header limit. Custom transports should apply equivalent
application-appropriate bounds and should never return partial data as a
successful response after crossing one.

Transport instances may be shared by multiple clients, so implementations must
be safe for the application's concurrency pattern.
