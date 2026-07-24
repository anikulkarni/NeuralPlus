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
        << "Anthropic chatbot using the real Messages API\n"
        << "Usage: neuralplus_anthropic_chatbot [options]\n"
        << "  --api-key KEY   Override ANTHROPIC_API_KEY\n"
        << "  --model ID      Override claude-sonnet-5\n"
        << "  --system TEXT   Set the system instruction\n"
        << "  --prompt TEXT   Run one prompt instead of interactive mode\n"
        << "  --help          Show this message\n";
}

}  // namespace

int main(int argc, const char* const* argv) {
    try {
        const Arguments arguments(
            argc,
            argv,
            {"--api-key", "--model", "--system", "--prompt"});
        if (arguments.help()) {
            usage();
            return 0;
        }

        AnthropicConfig config =
            arguments.has("--model")
                ? AnthropicConfig(arguments.require("--model"))
                : models::anthropic::claude_sonnet_5();
        if (arguments.has("--model")) {
            config.model.capabilities = ModelCapabilities{};
            config.model.capabilities.text_input = true;
            config.model.capabilities.text_output = true;
        }
        apply_api_key(arguments, config.api_key);

        auto client = make_client(std::move(config));
        SessionOptions options;
        options.system_message = arguments.value_or(
            "--system", "Be helpful, accurate, and concise.");
        Session session(std::move(options));
        return run_chatbot(
            *client, session, arguments.optional("--prompt"));
    } catch (const std::exception& error) {
        return print_exception(error);
    }
}
