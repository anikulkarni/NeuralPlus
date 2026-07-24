<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# Provider examples

The provider examples use the production `OpenAIClient`, `AnthropicClient`,
`GeminiClient`, or `OpenAICompatibleClient` and the normal libcurl transport.
They do not replace HTTP with `MockHttpTransport`.

Build them with:

```bash
cmake --preset dev
cmake --build --preset dev
```

Run any example with `--help` to see all options. Prefer environment variables
for credentials:

```bash
export OPENAI_API_KEY="..."
export ANTHROPIC_API_KEY="..."
export GEMINI_API_KEY="..."
```

The `--api-key` option is convenient for temporary local use, but an
environment variable or process secret store is safer because command-line
arguments may be visible to other local processes. Never commit a key or
`.env` file.

## Chatbots

Each chatbot supports an interactive conversation, an optional system
instruction, a model override, and a one-shot `--prompt`.

```bash
./build/dev/neuralplus_openai_chatbot \
  --system "Answer as a concise C++ tutor."

./build/dev/neuralplus_anthropic_chatbot

./build/dev/neuralplus_gemini_chatbot \
  --prompt "Explain move semantics in one paragraph."

./build/dev/neuralplus_openai_compatible_chatbot \
  --base-url http://localhost:8000/v1 \
  --model local-model
```

The first three read their standard provider environment variables when
`--api-key` is absent. The compatible example permits an unauthenticated local
endpoint or `--api-key-env NAME` for a server-specific variable.

## Custom model and configuration

These programs show how to use a model ID that is not in the convenience
catalog, set provider options, and override a provider endpoint:

```bash
./build/dev/neuralplus_openai_custom_config \
  --model MODEL_ID \
  --prompt "What workloads suit this model?"

./build/dev/neuralplus_anthropic_custom_config \
  --model MODEL_ID

./build/dev/neuralplus_gemini_custom_config \
  --model MODEL_ID

./build/dev/neuralplus_openai_compatible_custom_config \
  --base-url http://localhost:8000/v1 \
  --model MODEL_ID
```

Inspect the source before copying its example provider options. A field
accepted by one model may be rejected by another.

## Images, audio, video, and documents

The multimodal examples accept only forms mapped by the corresponding
NeuralPlus adapter:

| Program | Demonstrated inputs |
|---|---|
| `neuralplus_openai_multimodal` | Image and file |
| `neuralplus_anthropic_multimodal` | Image and PDF |
| `neuralplus_gemini_multimodal` | Image, audio, video, PDF, or another inline file |
| `neuralplus_openai_compatible_multimodal` | Image, MP3/WAV audio, and file when the target server supports them |

Examples:

```bash
./build/dev/neuralplus_openai_multimodal \
  --image photo.png \
  --prompt "What is in this image?"

./build/dev/neuralplus_anthropic_multimodal \
  --pdf report.pdf \
  --prompt "Summarize the conclusions."

./build/dev/neuralplus_gemini_multimodal \
  --video demo.mp4 \
  --prompt "Describe the sequence of events."

./build/dev/neuralplus_openai_compatible_multimodal \
  --base-url http://localhost:8000/v1 \
  --model vision-model \
  --image photo.jpg
```

These small examples inline local bytes so the C++ data flow is visible.
Provider upload APIs are preferable for large media and repeated use. Check
current size, format, and model constraints in the
[OpenAI file-input guide](https://developers.openai.com/api/docs/guides/file-inputs),
[Anthropic PDF guide](https://platform.claude.com/docs/en/build-with-claude/pdf-support),
and [Gemini video guide](https://ai.google.dev/gemini-api/docs/video-understanding).

## What CI verifies

CI compiles every example against the real provider client and production
transport. It invokes each provider example only with `--help`, which exits
before credential resolution or network access. This confirms the public API
and command-line path on every primary platform without storing provider keys,
spending provider credits, or making a mocked example look production-ready.

Protocol behavior is covered separately by deterministic unit tests that
inject `MockHttpTransport`. Live provider calls are an explicit local
integration step because public-repository pull requests must not receive
repository secrets.
