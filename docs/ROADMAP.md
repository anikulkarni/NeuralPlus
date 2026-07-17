# Roadmap

This roadmap describes intended direction, not a delivery commitment. Milestones may be split or reordered when design or portability work requires it.

## Milestone 0 — Repository bootstrap

**Status: initial draft complete**

- [x] C++17 and CMake project structure
- [x] Provider-neutral model request/response types
- [x] Class-based tool abstraction
- [x] Typed tool extension template
- [x] Session-scoped, thread-safe state
- [x] Parallel tool execution
- [x] Model decorator seam for composition
- [x] Observer seam for tracing and telemetry
- [x] Tests, example, install targets, and cross-platform CI definition
- [x] Public-facing documentation and contribution policies

## Milestone 1 — OpenAI, Gemini, and Anthropic clients

**Goal:** a common model-client experience with provider-native tool calling.

Planned work:

- Shared HTTP transport interface
- Secure API-key configuration
- Timeouts, retry policy, and provider error mapping
- OpenAI model adapter
- Gemini model adapter
- Anthropic model adapter
- Provider request/response translation
- Tool schema and tool result translation
- Parallel tool-call preservation where providers support it
- Mock transport tests with recorded, sanitized fixtures
- Provider examples and compatibility matrix

Acceptance criteria:

- The same `Agent` and `Tool` code can run against all three providers.
- Tests do not require real credentials.
- Secrets never appear in exception strings, logs, or fixtures.

## Milestone 2 — Pluggable tracing and replay

**Goal:** record complete model/tool execution and replay it deterministically.

Planned work:

- Structured trace event model
- Trace sink/source interfaces
- JSON Lines file backend
- SQLite backend
- Configurable content redaction
- Correlation IDs across model rounds and tool calls
- Deterministic replay model
- Schema versioning and migration policy

## Milestone 3 — OpenTelemetry instrumentation

**Goal:** standard observability without coupling the runtime to one backend.

Planned work:

- Optional OpenTelemetry build feature
- Model and tool spans
- Latency, token, error, retry, and tool-call metrics
- Provider/request correlation attributes
- Sensitive-attribute controls
- Example export to an OpenTelemetry collector

## Milestone 4 — MCP client support

**Goal:** expose discovered MCP tools through the NeuralPlus tool registry.

Planned work:

- MCP client interface
- Standard input/output transport
- Streamable HTTP transport
- Tool discovery and schema conversion
- MCP tool proxy implementation
- Connection lifecycle and reconnection
- Authentication extension points
- Resource and prompt support after the tool path stabilizes

## Milestone 5 — Model expansion and composition

**Goal:** support broader providers and richer model execution strategies.

Planned work:

- Streaming generation
- Cancellation and deadlines
- Model fallback and retry decorators
- Routing and policy-based model selection
- Rate limiting and concurrency controls
- Local inference adapters
- Additional hosted providers
- Structured output helpers
- Embedding and reranking model interfaces

## Milestone 6 — RAG and query optimization

**Goal:** composable retrieval pipelines suitable for native applications.

Planned work:

- Document and chunk types
- Loader and splitter interfaces
- Embedding providers
- Vector-store interface and initial adapters
- BM25/sparse retrieval interface
- Hybrid retrieval and reciprocal-rank fusion
- Metadata filters
- Reranking
- Query rewriting, decomposition, and multi-query retrieval
- Retrieval tracing and evaluation hooks

## Cross-cutting work

The following continue across milestones:

- Linux, macOS, and Windows portability
- ABI/API design reviews
- Static analysis and sanitizers
- Documentation and examples
- Secure defaults and secret redaction
- Benchmarking
- Package-manager support such as vcpkg and Conan
