<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# Security policy

## Supported versions

Security fixes are applied to the latest 0.2.x release and the development
branch. Users should update to the newest available patch release.

| Version | Supported |
| --- | --- |
| 0.2.x | Yes |
| Earlier versions | No |

## Report a vulnerability

Do not open a public issue for a vulnerability, leaked credential, unsafe
default, or trace containing private content. Use GitHub private vulnerability
reporting for this repository. If it is unavailable, contact the repository
owner privately through the contact method on the GitHub profile.

Include the affected commit, a minimal reproduction, impact, whether
credentials or personal data are involved, and a proposed fix when available.
Do not attach working credentials or unsanitized production traces.

## Security defaults

- Provider keys resolve at client construction. Built-in clients exact-redact
  the resolved key and sensitive authentication-header values from HTTP
  responses before provider parsing and from `ProviderError` fields. Common
  credential header names are recognized automatically; custom credential
  headers must set `HttpHeader::sensitive`.
- The built-in libcurl transport verifies TLS, restricts protocols to HTTP(S),
  does not follow redirects, and bounds response bodies and headers.
- Trace events are metadata-only unless the client enables
  `capture_trace_payloads`. In-memory, callback, and custom tracers then receive
  the payload directly. `FileTracer` requires its separate
  `include_payloads` option; console and syslog remain metadata-only.
- Newly created POSIX trace files use owner-only mode `0600`; supported POSIX
  platforms also reject final-component symbolic-link destinations.
- Tool declarations are validated and snapshotted at client construction.
- Tool names and arguments are validated before invocation; provider rounds,
  total tool calls, and concurrent callbacks have configurable hard limits.
- Provider tests use mock transport and sanitized fixtures, without secrets.

Payload capture can expose prompts, model output, tool arguments, personal
data, or credentials returned by a tool. If enabled, apply access controls,
retention limits, encryption, and redaction appropriate to the application.
`FileTracer::max_bytes` is a size guard, not rotation or retention management.
Exact credential redaction cannot recognize transformed values. An injected
transport is responsible for keeping request credentials out of its own logs
and non-`ProviderError` exceptions. `MockHttpTransport` intentionally records
complete requests for tests, including authentication headers, so use only
synthetic credentials and sanitized fixtures.

The transport and generation limits are resource guards, not full request
quotas, rate limiting, cancellation, or application-level authorization.

The project has not received an independent security audit. Consumers remain
responsible for threat modeling and review before sensitive deployment.
