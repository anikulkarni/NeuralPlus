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
        << "Custom OpenAI-compatible server configuration\n"
        << "Usage: neuralplus_openai_compatible_custom_config [options]\n"
        << "  --base-url URL      Server root, default http://localhost:8000/v1\n"
        << "  --model ID          Server model ID, default local-model\n"
        << "  --api-key KEY       Optional bearer token\n"
        << "  --api-key-env NAME  Read bearer token from this environment variable\n"
        << "  --prompt TEXT       Prompt to send\n"
        << "  --help              Show this message\n";
}

}  // namespace

int main(int argc, const char* const* argv) {
    try {
        const Arguments arguments(argc, argv);
        if (arguments.help()) {
            usage();
            return 0;
        }

        OpenAICompatibleConfig config(
            arguments.value_or("--model", "local-model"),
            arguments.value_or(
                "--base-url", "http://localhost:8000/v1"));
        config.model.display_name = "Application-selected compatible model";
        config.model.capabilities.tools = true;
        config.model.capabilities.parallel_tool_calls = true;
        config.model.provider_options["frequency_penalty"] = 0.1;
        apply_api_key(arguments, config.api_key);
        config.api_key_environment =
            arguments.optional("--api-key-env");

        auto client = make_client(std::move(config));
        GenerateOptions generation;
        generation.max_output_tokens = 512U;
        const AIResponse response = client->generate(
            arguments.value_or(
                "--prompt", "Explain what this server model is good at."),
            generation);
        std::cout << response.message.text() << '\n';
        return 0;
    } catch (const std::exception& error) {
        return print_exception(error);
    }
}
