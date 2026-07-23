<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# Changelog

NeuralPlus follows semantic versioning after its first stable release. During
pre-alpha development, minor versions may contain breaking changes.

## Unreleased

- Bounded provider rounds, tool-call totals, parallel callbacks, response
  bodies, and response headers.
- Immutable validated tool-declaration snapshots, provider-specific tool
  options, and composition with native server tools.
- Protocol continuation without local tool calls and conservative cumulative
  token-usage accounting.
- Expanded normalized multimodal provider input and output handling, fixtures,
  and documentation.
- Documentation and portability hardening.

## 0.2.0

### Changed

- Replaced the bootstrap `Agent`/`Model`/registry design with one `AIClient`
  orchestration base and small provider clients.
- Made `Session` the owner of normalized messages and generic tool state.
- Replaced observer and decorator layers with a direct multi-tracer collection.
- Moved from the MIT License to the Apache License 2.0.
- Added mandatory libcurl and nlohmann/json dependencies.

### Added

- OpenAI Responses, Anthropic Messages, Gemini generateContent, and
  OpenAI-compatible Chat Completions clients.
- Normalized multimodal messages, tool calls, usage, and provider errors.
- `FunctionTool` and `FunctionAIClient` convenience adapters.
- Console, JSON Lines file, in-memory, callback, and POSIX syslog tracers.
- Production libcurl transport and deterministic mock transport.
- Mock-only provider tests, Doxygen target, install package, and expanded
  platform CI.

### Removed

- `Agent`, `Model`, `ModelDecorator`, `ToolRegistry`, `TypedTool`,
  `StateStore`, and `RunObserver`.
