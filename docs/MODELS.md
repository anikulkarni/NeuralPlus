<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# Model configurations

NeuralPlus keeps model names as configuration data. The SDK therefore has one
client class per provider protocol, not one C++ class per marketed model.

For common choices, `neuralplus/models.hpp` provides ready-to-use typed
configurations:

```cpp
#include <neuralplus/neuralplus.hpp>

#include <utility>

auto config = neuralplus::models::openai::gpt_5_6_terra();
auto client = neuralplus::make_client(std::move(config));
```

The result is an ordinary `OpenAIConfig`, `AnthropicConfig`, or
`GeminiConfig`. Applications can still set credentials, URLs, headers,
capabilities, and provider options before passing it to `make_client`.

## Included configurations

The catalog was checked against provider documentation on July 23, 2026.
Provider availability, aliases, limits, and pricing can change independently
of an SDK release, so check the linked source before selecting a production
model.

| Provider | C++ configuration | Model ID |
|---|---|---|
| OpenAI | `models::openai::gpt_5_6_sol()` | `gpt-5.6-sol` |
| OpenAI | `models::openai::gpt_5_6_terra()` | `gpt-5.6-terra` |
| OpenAI | `models::openai::gpt_5_6_luna()` | `gpt-5.6-luna` |
| Anthropic | `models::anthropic::claude_fable_5()` | `claude-fable-5` |
| Anthropic | `models::anthropic::claude_opus_4_8()` | `claude-opus-4-8` |
| Anthropic | `models::anthropic::claude_sonnet_5()` | `claude-sonnet-5` |
| Anthropic | `models::anthropic::claude_haiku_4_5()` | `claude-haiku-4-5-20251001` |
| Gemini | `models::gemini::gemini_3_6_flash()` | `gemini-3.6-flash` |
| Gemini | `models::gemini::gemini_3_5_flash()` | `gemini-3.5-flash` |
| Gemini | `models::gemini::gemini_3_5_flash_lite()` | `gemini-3.5-flash-lite` |
| Gemini | `models::gemini::gemini_2_5_pro()` | `gemini-2.5-pro` |

Sources:

- [OpenAI model catalog](https://developers.openai.com/api/docs/models)
- [Anthropic model overview](https://platform.claude.com/docs/en/about-claude/models/overview)
- [Gemini model catalog](https://ai.google.dev/gemini-api/docs/models)

The catalog describes capabilities used by NeuralPlus before a request is
sent. It does not add a capability that the provider or account does not have.

## Use a custom or newly released model

No catalog update is required to use another model:

```cpp
neuralplus::OpenAIConfig config{"gpt-new-or-fine-tuned-id"};
config.model.display_name = "My application model";
config.model.capabilities.image_input = true;
config.model.capabilities.file_input = true;
config.model.capabilities.tools = true;
config.model.provider_options["reasoning"] = {{"effort", "medium"}};

auto client = neuralplus::make_client(std::move(config));
```

The same pattern works with `AnthropicConfig` and `GeminiConfig`.
`OpenAICompatibleConfig` always uses an application-supplied server URL and
model ID because compatible servers do not share one authoritative catalog.

Use typed `GenerateOptions` for portable controls. Use
`ModelDescriptor::provider_options` for a model-wide provider field and
`GenerateOptions::provider_options` for a one-call override. A new wire
protocol belongs in its provider adapter; a new marketed model normally needs
configuration only.
