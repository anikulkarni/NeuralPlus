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
        << "OpenAI chatbot using the real Responses API\n"
        << "Usage: neuralplus_openai_chatbot [options]\n"
        << "  --api-key KEY   Override OPENAI_API_KEY\n"
        << "  --model ID      Override gpt-5.6-terra\n"
        << "  --system TEXT   Set the system instruction\n"
        << "  --prompt TEXT   Run one prompt instead of interactive mode\n"
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

        OpenAIConfig config = models::openai::gpt_5_6_terra();
        if (arguments.has("--model")) {
            config.model.id = arguments.require("--model");
            config.model.display_name = config.model.id;
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
