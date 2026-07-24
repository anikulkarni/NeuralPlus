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
        << "Chatbot for a real OpenAI-compatible Chat Completions server\n"
        << "Usage: neuralplus_openai_compatible_chatbot [options]\n"
        << "  --base-url URL      Server root, default http://localhost:8000/v1\n"
        << "  --model ID          Server model ID, default local-model\n"
        << "  --api-key KEY       Optional bearer token\n"
        << "  --api-key-env NAME  Read bearer token from this environment variable\n"
        << "  --system TEXT       Set the system instruction\n"
        << "  --prompt TEXT       Run one prompt instead of interactive mode\n"
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
        require_secure_base_url(config.base_url);
        apply_api_key(arguments, config.api_key);
        config.api_key_environment =
            arguments.optional("--api-key-env");

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
