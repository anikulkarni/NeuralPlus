// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "example_support.hpp"

#include <iostream>
#include <utility>

using namespace neuralplus;
using namespace neuralplus_examples;

namespace {

void usage() {
    std::cout
        << "Custom Anthropic model/configuration using the real Messages API\n"
        << "Usage: neuralplus_anthropic_custom_config --model ID [options]\n"
        << "  --model ID      Custom, preview, or pinned Claude model ID\n"
        << "  --api-key KEY   Override ANTHROPIC_API_KEY\n"
        << "  --base-url URL  Override https://api.anthropic.com\n"
        << "  --prompt TEXT   Prompt to send\n"
        << "  --help          Show this message\n";
}

}  // namespace

int main(int argc, const char* const* argv) {
    try {
        const Arguments arguments(argc, argv);
        if (arguments.help()) {
            usage();
            return 0;
        }

        AnthropicConfig config(arguments.require("--model"));
        config.model.display_name = "Application-selected Claude model";
        config.model.capabilities.image_input = true;
        config.model.capabilities.file_input = true;
        config.model.capabilities.tools = true;
        config.model.capabilities.parallel_tool_calls = true;
        config.default_max_output_tokens = 1024U;
        config.model.provider_options["top_p"] = 0.9;
        if (arguments.has("--base-url")) {
            config.base_url = arguments.require("--base-url");
        }
        apply_api_key(arguments, config.api_key);

        auto client = make_client(std::move(config));
        GenerateOptions generation;
        generation.max_output_tokens = 512U;
        const AIResponse response = client->generate(
            arguments.value_or(
                "--prompt", "Explain what this custom model is good at."),
            generation);
        std::cout << response.message.text() << '\n';
        return 0;
    } catch (const std::exception& error) {
        return print_exception(error);
    }
}
