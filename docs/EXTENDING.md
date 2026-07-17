# Extending NeuralPlus

## Adding a tool

Tools are runtime-polymorphic classes. A tool should:

- Use a stable, provider-compatible name
- Describe one clear capability
- Provide a JSON Schema input description
- Validate all external input
- Return concise machine-readable output when another model round will consume it
- Avoid storing session data in global variables

For stateful behavior, read and modify `ToolContext::state`. Namespace keys, for example `calendar.last_query` rather than `last_query`.

## Adding a provider

A provider adapter should derive from `Model` and keep provider-specific objects private to its module.

Recommended internal separation:

```text
ProviderModel
  -> RequestMapper
  -> HttpTransport
  -> ResponseMapper
  -> ProviderErrorMapper
```

Provider adapters should not implement the agent loop. They translate one `ModelRequest` into one provider generation operation and return one `ModelResponse`.

Tests should use an injectable transport and sanitized fixtures. Never require live credentials for the default test suite.

## Adding model behavior

Cross-provider behavior such as retries, routing, caching, and rate limiting should generally be implemented as a `ModelDecorator` rather than copied into each provider.

A decorator can call `next().generate(request)` and transform behavior before or after delegation.

## Adding tracing or metrics

Implement `RunObserver` for lightweight lifecycle integration. Remember that tool callbacks may happen concurrently.

Persistent tracing backends should keep serialization and storage outside the observer callback's critical path where possible.

## Adding a future subsystem

New public modules should:

- Define the smallest stable interface first
- Keep implementation dependencies private
- Include one focused example
- Include deterministic tests
- Document threading and ownership expectations
- Avoid coupling core model/tool APIs to the new subsystem
