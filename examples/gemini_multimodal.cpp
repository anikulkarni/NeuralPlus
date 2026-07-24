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
        << "Gemini image/audio/video/file input using real generateContent\n"
        << "Usage: neuralplus_gemini_multimodal [options]\n"
        << "  --image PATH    Add an inline image\n"
        << "  --audio PATH    Add inline audio\n"
        << "  --video PATH    Add an inline video file\n"
        << "  --file PATH     Add an inline PDF or text file\n"
        << "  --api-key KEY   Override GEMINI_API_KEY/GOOGLE_API_KEY\n"
        << "  --model ID      Override gemini-3.6-flash\n"
        << "  --prompt TEXT   Question about the supplied media\n"
        << "  --help          Show this message\n";
}

void add_file(const Arguments& arguments,
              const std::string& argument,
              Message::Contents& contents) {
    if (!arguments.has(argument)) {
        return;
    }
    const std::string path = arguments.require(argument);
    contents.push_back(Content::file_bytes(
        read_bytes(path), media_type(path), filename(path)));
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
            !arguments.has("--video") &&
            !arguments.has("--file")) {
            throw std::invalid_argument(
                "provide --image, --audio, --video, or --file PATH");
        }

        GeminiConfig config = models::gemini::gemini_3_6_flash();
        if (arguments.has("--model")) {
            config.model.id = arguments.require("--model");
            config.model.display_name = config.model.id;
        }
        apply_api_key(arguments, config.api_key);

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
        add_file(arguments, "--video", contents);
        add_file(arguments, "--file", contents);

        auto client = make_client(std::move(config));
        const AIResponse response =
            client->generate(Message::user(std::move(contents)));
        std::cout << response.message.text() << '\n';
        return 0;
    } catch (const std::exception& error) {
        return print_exception(error);
    }
}
