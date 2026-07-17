# Architecture

## Objectives

NeuralPlus is intended to become a native C++ foundation for model-backed applications without coupling application code to one provider, transport, tracing backend, or retrieval system.

The architecture prioritizes:

1. Stable interfaces at subsystem boundaries
2. Explicit ownership through standard smart pointers
3. Session isolation and thread-safe tool state
4. Provider-neutral model and message types
5. Extensibility through inheritance where runtime substitution matters
6. Templates where compile-time typing materially improves tool authoring
7. Small, independently testable modules

## Core runtime

### `Model`

`Model` is the provider-neutral generation interface. OpenAI, Gemini, Anthropic, local runtimes, routers, caches, and retry wrappers can all present the same contract.

`ModelDecorator` enables object composition:

```text
TracingModel -> RetryModel -> RateLimitedModel -> OpenAIModel
```

Each decorator owns or references the next model and can inspect or transform requests and responses.

### `Tool`

A tool is a class with identity, description, input schema, and invocation behavior. Class-based tools can encapsulate dependencies while session state is provided explicitly through `ToolContext`.

`TypedTool<Input, Output>` separates:

- Wire-format parsing
- Typed execution
- Wire-format serialization

This gives tool authors compile-time types while keeping the core independent of a specific JSON implementation.

### `Session` and `StateStore`

A `Session` owns conversation messages and a shared `StateStore`. The state store uses `std::any` behind a thread-safe API so unrelated tools can keep strongly typed values under namespaced keys.

The `update<T>` operation is atomic and is the preferred mechanism for state modified by parallel tool calls.

### `Agent`

The agent implements the provider-neutral orchestration loop:

1. Append the user message.
2. Send messages and tool specifications to the model.
3. Append the assistant response.
4. Execute requested tools sequentially or in parallel.
5. Append tool results.
6. Repeat until the model returns a final response or the configured round limit is reached.

Tool-call results are appended in the model-requested order even when execution occurs concurrently.

### `RunObserver`

Observers receive model and tool lifecycle events. This intentionally small interface is the seam for later features:

- File and SQLite traces
- Replay capture
- OpenTelemetry spans and metrics
- Debug logging
- Cost and latency accounting

Observer implementations must be thread-safe because tool callbacks may be emitted concurrently. Observer failures are isolated from model and tool execution.

## Planned modules

```text
include/neuralplus/
  core/              Stable orchestration interfaces
  providers/         OpenAI, Gemini, Anthropic, and future adapters
  transport/         HTTP and streaming abstractions
  tracing/           Trace events, stores, and replay
  telemetry/         OpenTelemetry adapters
  mcp/               MCP transport, discovery, and tool bridge
  rag/               Documents, retrievers, rerankers, and query transforms
```

Implementation code will mirror the public module boundaries under `src/`.

## Dependency policy

The core should remain lightweight and avoid forcing provider-specific dependencies on all consumers. Dependencies should be:

- Hidden behind public interfaces where practical
- Optional by CMake feature flags
- Version-pinned in CI
- Replaceable when a subsystem is explicitly pluggable

Provider modules may use an external JSON and HTTP implementation, but those choices should not leak into core public APIs unless a later design review establishes a stable value abstraction.

## Error model

The bootstrap version uses exceptions for programming errors and tool invocation failures are converted to structured `ToolResult` values by the agent.

Before provider clients stabilize, the project will introduce a consistent error taxonomy covering:

- Invalid configuration
- Authentication
- Rate limiting
- Transport failures
- Provider response errors
- Schema/serialization failures
- Tool execution failures
- Cancellation and timeout

## Concurrency model

- One `Session` should be driven by one active `Agent::run` call at a time.
- Distinct sessions can run concurrently.
- Tool calls within one model round can execute concurrently.
- `StateStore::update` provides atomic read-modify-write behavior.
- Tools and observers participating in parallel execution must be thread-safe.

Cancellation, executors, and user-supplied scheduling are planned after the initial provider milestone.

## API stability

Until `1.0.0`, public interfaces may evolve. Changes should still follow these rules:

- Prefer additive changes.
- Document migration steps for breaking changes.
- Keep provider-specific types outside the core API.
- Add tests for execution semantics before refactoring the agent loop.
