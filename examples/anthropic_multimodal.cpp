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
        << "Claude image/PDF input using the real Messages API\n"
        << "Usage: neuralplus_anthropic_multimodal [options]\n"
        << "  --image PATH    Add an inline image\n"
        << "  --pdf PATH      Add an inline PDF\n"
        << "  --api-key KEY   Override ANTHROPIC_API_KEY\n"
        << "  --model ID      Override claude-sonnet-5\n"
        << "  --prompt TEXT   Question about the supplied media\n"
        << "  --help          Show this message\n";
}

}  // namespace

int main(int argc, const char* const* argv) {
    try {
        const Arguments arguments(
            argc,
            argv,
            {"--image", "--pdf", "--api-key", "--model", "--prompt"});
        if (arguments.help()) {
            usage();
            return 0;
        }
        if (!arguments.has("--image") && !arguments.has("--pdf")) {
            throw std::invalid_argument(
                "provide --image PATH, --pdf PATH, or both");
        }

        AnthropicConfig config =
            arguments.has("--model")
                ? AnthropicConfig(arguments.require("--model"))
                : models::anthropic::claude_sonnet_5();
        config.model.capabilities = ModelCapabilities{};
        config.model.capabilities.text_input = true;
        config.model.capabilities.text_output = true;
        config.model.capabilities.image_input =
            arguments.has("--image");
        config.model.capabilities.file_input =
            arguments.has("--pdf");
        apply_api_key(arguments, config.api_key);

        Message::Contents contents;
        contents.push_back(Content::text(arguments.value_or(
            "--prompt", "Describe and summarize the supplied content.")));
        if (arguments.has("--image")) {
            const std::string path = arguments.require("--image");
            contents.push_back(
                Content::image_bytes(read_bytes(path), media_type(path)));
        }
        if (arguments.has("--pdf")) {
            const std::string path = arguments.require("--pdf");
            contents.push_back(Content::file_bytes(
                read_bytes(path), "application/pdf", filename(path)));
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
