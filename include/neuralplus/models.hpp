// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

/// @file
/// Ready-to-use configurations for commonly selected provider models.

#pragma once

#include "neuralplus/export.hpp"
#include "neuralplus/providers.hpp"

namespace neuralplus {
namespace models {

/// OpenAI Responses API model configurations.
namespace openai {

/// Frontier GPT-5.6 configuration for complex professional work.
///
/// @see https://developers.openai.com/api/docs/models/gpt-5.6-sol
[[nodiscard]] NEURALPLUS_API OpenAIConfig gpt_5_6_sol();

/// Balanced GPT-5.6 configuration for quality- and cost-sensitive work.
///
/// @see https://developers.openai.com/api/docs/models
[[nodiscard]] NEURALPLUS_API OpenAIConfig gpt_5_6_terra();

/// Efficient GPT-5.6 configuration for high-volume workloads.
///
/// @see https://developers.openai.com/api/docs/models
[[nodiscard]] NEURALPLUS_API OpenAIConfig gpt_5_6_luna();

}  // namespace openai

/// Anthropic Messages API model configurations.
namespace anthropic {

/// Highest-capability generally available Claude configuration.
///
/// @see https://platform.claude.com/docs/en/about-claude/models/overview
[[nodiscard]] NEURALPLUS_API AnthropicConfig claude_fable_5();

/// Claude configuration for complex agentic and enterprise work.
///
/// @see https://platform.claude.com/docs/en/about-claude/models/overview
[[nodiscard]] NEURALPLUS_API AnthropicConfig claude_opus_4_8();

/// Claude configuration balancing speed and frontier intelligence.
///
/// @see https://platform.claude.com/docs/en/about-claude/models/overview
[[nodiscard]] NEURALPLUS_API AnthropicConfig claude_sonnet_5();

/// Fast Claude configuration for latency-sensitive workloads.
///
/// @see https://platform.claude.com/docs/en/about-claude/models/overview
[[nodiscard]] NEURALPLUS_API AnthropicConfig claude_haiku_4_5();

}  // namespace anthropic

/// Google Gemini generateContent model configurations.
namespace gemini {

/// Current stable Gemini Flash configuration for agentic multimodal work.
///
/// @see https://ai.google.dev/gemini-api/docs/models/gemini-3.6-flash
[[nodiscard]] NEURALPLUS_API GeminiConfig gemini_3_6_flash();

/// Stable Gemini Flash configuration for sustained agentic workloads.
///
/// @see https://ai.google.dev/gemini-api/docs/models/gemini-3.5-flash
[[nodiscard]] NEURALPLUS_API GeminiConfig gemini_3_5_flash();

/// Cost-efficient stable Gemini Flash-Lite configuration.
///
/// @see https://ai.google.dev/gemini-api/docs/models/gemini-3.5-flash-lite
[[nodiscard]] NEURALPLUS_API GeminiConfig gemini_3_5_flash_lite();

/// Stable Gemini Pro configuration for complex reasoning and long context.
///
/// @see https://ai.google.dev/gemini-api/docs/models/gemini-2.5-pro
[[nodiscard]] NEURALPLUS_API GeminiConfig gemini_2_5_pro();

}  // namespace gemini

}  // namespace models
}  // namespace neuralplus
