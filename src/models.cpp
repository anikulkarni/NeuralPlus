// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/models.hpp"

#include <cstddef>
#include <utility>

using namespace neuralplus;

namespace {

template <typename Config>
Config configure_text_vision_model(Config config,
                                   const char* display_name,
                                   std::size_t context_window) {
    config.model.display_name = display_name;
    config.model.context_window = context_window;
    config.model.capabilities.text_input = true;
    config.model.capabilities.text_output = true;
    config.model.capabilities.image_input = true;
    config.model.capabilities.tools = true;
    config.model.capabilities.parallel_tool_calls = true;
    return config;
}

OpenAIConfig configure_openai(const char* model_id,
                              const char* display_name) {
    // Current GPT-5.6 family values:
    // input: text/image; output: text; context: 1,050,000; output: 128,000.
    // https://developers.openai.com/api/docs/models
    OpenAIConfig config =
        configure_text_vision_model(OpenAIConfig(model_id),
                                    display_name,
                                    1050000U);
    // The provider's 128,000-token output ceiling is not stored in
    // ModelDescriptor::max_output_tokens because that field is a per-call
    // default, not informational metadata.
    // The Responses API accepts direct file inputs independently of hosted
    // file-search tools.
    // https://developers.openai.com/api/docs/guides/file-inputs
    config.model.capabilities.file_input = true;
    return config;
}

AnthropicConfig configure_anthropic(const char* model_id,
                                    const char* display_name,
                                    std::size_t context_window) {
    AnthropicConfig config =
        configure_text_vision_model(AnthropicConfig(model_id),
                                    display_name,
                                    context_window);
    // Provider output ceilings remain informational in the linked catalog.
    // AnthropicConfig keeps its conservative 1,024-token request default.
    // Current Claude models accept PDFs through document content blocks.
    // https://platform.claude.com/docs/en/build-with-claude/pdf-support
    config.model.capabilities.file_input = true;
    return config;
}

GeminiConfig configure_gemini(const char* model_id,
                              const char* display_name) {
    // These stable Gemini models accept text, image, audio, video, and PDF and
    // return text with a 1,048,576-token input and 65,536-token output limit.
    // Video and PDF use normalized file content in NeuralPlus.
    // https://ai.google.dev/gemini-api/docs/models
    GeminiConfig config =
        configure_text_vision_model(GeminiConfig(model_id),
                                    display_name,
                                    1048576U);
    // The documented 65,536-token ceiling is intentionally not a generation
    // default. Applications opt into a call limit through GenerateOptions.
    config.model.capabilities.audio_input = true;
    config.model.capabilities.file_input = true;
    return config;
}

}  // namespace

OpenAIConfig neuralplus::models::openai::gpt_5_6_sol() {
    return configure_openai("gpt-5.6-sol", "GPT-5.6 Sol");
}

OpenAIConfig neuralplus::models::openai::gpt_5_6_terra() {
    return configure_openai("gpt-5.6-terra", "GPT-5.6 Terra");
}

OpenAIConfig neuralplus::models::openai::gpt_5_6_luna() {
    return configure_openai("gpt-5.6-luna", "GPT-5.6 Luna");
}

AnthropicConfig neuralplus::models::anthropic::claude_fable_5() {
    return configure_anthropic(
        "claude-fable-5", "Claude Fable 5", 1000000U);
}

AnthropicConfig neuralplus::models::anthropic::claude_opus_4_8() {
    return configure_anthropic(
        "claude-opus-4-8", "Claude Opus 4.8", 1000000U);
}

AnthropicConfig neuralplus::models::anthropic::claude_sonnet_5() {
    return configure_anthropic(
        "claude-sonnet-5", "Claude Sonnet 5", 1000000U);
}

AnthropicConfig neuralplus::models::anthropic::claude_haiku_4_5() {
    return configure_anthropic(
        "claude-haiku-4-5-20251001",
        "Claude Haiku 4.5",
        200000U);
}

GeminiConfig neuralplus::models::gemini::gemini_3_6_flash() {
    return configure_gemini("gemini-3.6-flash", "Gemini 3.6 Flash");
}

GeminiConfig neuralplus::models::gemini::gemini_3_5_flash() {
    return configure_gemini("gemini-3.5-flash", "Gemini 3.5 Flash");
}

GeminiConfig neuralplus::models::gemini::gemini_3_5_flash_lite() {
    return configure_gemini(
        "gemini-3.5-flash-lite", "Gemini 3.5 Flash-Lite");
}

GeminiConfig neuralplus::models::gemini::gemini_2_5_pro() {
    return configure_gemini("gemini-2.5-pro", "Gemini 2.5 Pro");
}
