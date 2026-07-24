// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/neuralplus.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace neuralplus;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_common(const ModelDescriptor& model,
                    Provider provider,
                    const std::string& id,
                    std::size_t context_window) {
    require(model.provider == provider, id + " provider");
    require(model.id == id, id + " id");
    require(!model.display_name.empty(), id + " display name");
    require(model.context_window == context_window, id + " context window");
    require(!model.max_output_tokens.has_value(),
            id + " does not turn a provider ceiling into a call default");
    require(model.capabilities.text_input, id + " text input");
    require(model.capabilities.text_output, id + " text output");
    require(model.capabilities.image_input, id + " image input");
    require(model.capabilities.tools, id + " tools");
    require(model.capabilities.parallel_tool_calls,
            id + " parallel tool calls");
    require(model.provider_options.is_object(), id + " provider options");
}

void test_openai_models() {
    ModelCapabilities text_only;
    text_only = ModelCapabilities{};
    require(text_only.text_input,
            "reset model capabilities preserve text input");
    require(text_only.text_output,
            "reset model capabilities preserve text output");

    const OpenAIConfig sol = models::openai::gpt_5_6_sol();
    require_common(sol.model,
                   Provider::openai,
                   "gpt-5.6-sol",
                   1050000U);
    require(sol.model.capabilities.file_input, "GPT-5.6 Sol file input");
    require(!sol.api_key.has_value(), "GPT-5.6 Sol key remains unset");

    const OpenAIConfig terra = models::openai::gpt_5_6_terra();
    require_common(terra.model,
                   Provider::openai,
                   "gpt-5.6-terra",
                   1050000U);

    const OpenAIConfig luna = models::openai::gpt_5_6_luna();
    require_common(luna.model,
                   Provider::openai,
                   "gpt-5.6-luna",
                   1050000U);
}

void test_anthropic_models() {
    const AnthropicConfig fable =
        models::anthropic::claude_fable_5();
    require_common(fable.model,
                   Provider::anthropic,
                   "claude-fable-5",
                   1000000U);
    require(fable.model.capabilities.file_input,
            "Claude Fable 5 file input");
    require(fable.default_max_output_tokens == 1024U,
            "Claude safe default output remains bounded");

    const AnthropicConfig opus =
        models::anthropic::claude_opus_4_8();
    require_common(opus.model,
                   Provider::anthropic,
                   "claude-opus-4-8",
                   1000000U);

    const AnthropicConfig sonnet =
        models::anthropic::claude_sonnet_5();
    require_common(sonnet.model,
                   Provider::anthropic,
                   "claude-sonnet-5",
                   1000000U);

    const AnthropicConfig haiku =
        models::anthropic::claude_haiku_4_5();
    require_common(haiku.model,
                   Provider::anthropic,
                   "claude-haiku-4-5-20251001",
                   200000U);
}

void require_gemini(const GeminiConfig& config,
                    const std::string& id) {
    require_common(config.model,
                   Provider::gemini,
                   id,
                   1048576U);
    require(config.model.capabilities.audio_input,
            id + " audio input");
    require(config.model.capabilities.file_input,
            id + " video, PDF, and file input");
}

void test_gemini_models() {
    require_gemini(models::gemini::gemini_3_6_flash(),
                   "gemini-3.6-flash");
    require_gemini(models::gemini::gemini_3_5_flash(),
                   "gemini-3.5-flash");
    require_gemini(models::gemini::gemini_3_5_flash_lite(),
                   "gemini-3.5-flash-lite");
    require_gemini(models::gemini::gemini_2_5_pro(),
                   "gemini-2.5-pro");
}

}  // namespace

int main() {
    try {
        test_openai_models();
        test_anthropic_models();
        test_gemini_models();
        std::cout << "model tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "model test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
