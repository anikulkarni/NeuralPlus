<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# Roadmap

This is direction, not a delivery commitment.

## Release policy

Version 0.2.0 is the first General Availability release. Compatible fixes ship
as 0.2.x patch releases. The next planned feature release is 0.3.0.

## 0.2.0 — simplified core

Released on July 24, 2026.

- [x] One provider-independent `AIClient` orchestration loop
- [x] OpenAI, Anthropic, Gemini, and OpenAI-compatible clients
- [x] Normalized text, image, audio, file, and extension content
- [x] Session-owned message history and typed process-local state
- [x] Class and callback tools with parallel execution
- [x] Console, file, memory, callback, and POSIX syslog tracers
- [x] Mockable libcurl transport boundary
- [x] Secretless provider fixture tests
- [x] Convenience model configurations and real-provider usage examples
- [x] CMake install package and Doxygen target

## 0.3.0 — planned

- Streaming responses and cancellation
- Retry and rate-limit policy
- Structured-output helpers
- Trace schema versioning and replay
- More provider fixtures and multimodal coverage
- Package-manager manifests
- Sanitizer and static-analysis jobs

## Later

- Optional first-party OpenTelemetry adapter
- Model routing and fallback
- MCP client tools
- Embeddings, retrieval, reranking, and RAG utilities

New subsystems should prove a small runtime interface before adding convenience
layers. Templates remain ordinary type-safety helpers rather than a
metaprogramming architecture.
