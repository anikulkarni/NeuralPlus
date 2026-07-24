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
        << "Multimodal input using a real OpenAI-compatible server\n"
        << "Usage: neuralplus_openai_compatible_multimodal [options]\n"
        << "  --base-url URL      Server root, default http://localhost:8000/v1\n"
        << "  --model ID          Server model ID, default local-model\n"
        << "  --image PATH        Add an inline image\n"
        << "  --audio PATH        Add inline MP3 or WAV audio\n"
        << "  --file PATH         Add an inline file supported by the server\n"
        << "  --api-key KEY       Optional bearer token\n"
        << "  --api-key-env NAME  Read bearer token from this environment variable\n"
        << "  --prompt TEXT       Question about the supplied media\n"
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
        if (!arguments.has("--image") &&
            !arguments.has("--audio") &&
            !arguments.has("--file")) {
            throw std::invalid_argument(
                "provide --image, --audio, or --file PATH");
        }

        OpenAICompatibleConfig config(
            arguments.value_or("--model", "local-model"),
            arguments.value_or(
                "--base-url", "http://localhost:8000/v1"));
        require_secure_base_url(config.base_url);
        config.model.capabilities.image_input = arguments.has("--image");
        config.model.capabilities.audio_input = arguments.has("--audio");
        config.model.capabilities.file_input = arguments.has("--file");
        apply_api_key(arguments, config.api_key);
        config.api_key_environment =
            arguments.optional("--api-key-env");

        Message::Contents contents;
        contents.push_back(Content::text(arguments.value_or(
            "--prompt", "Describe and summarize the supplied content.")));
        if (arguments.has("--image")) {
            const std::string path = arguments.require("--image");
            contents.push_back(
                Content::image_bytes(read_bytes(path), media_type(path)));
        }
        if (arguments.has("--audio")) {
            const std::string path = arguments.require("--audio");
            contents.push_back(
                Content::audio_bytes(read_bytes(path), media_type(path)));
        }
        if (arguments.has("--file")) {
            const std::string path = arguments.require("--file");
            contents.push_back(Content::file_bytes(
                read_bytes(path), media_type(path), filename(path)));
        }

        auto client = make_client(std::move(config));
        const AIResponse response =
            client->generate(Message::user(std::move(contents)));
        std::cout << response.message.text() << '\n';
        return 0;
    } catch (const std::exception& error) {
        return print_exception(error);
    }
}
