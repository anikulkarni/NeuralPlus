// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "neuralplus/neuralplus.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace neuralplus_examples {

class Arguments final {
public:
    Arguments(int argc, const char* const* argv) {
        for (int index = 1; index < argc; ++index) {
            const std::string name = argv[index];
            if (name == "--help" || name == "-h") {
                help_ = true;
                continue;
            }
            if (name.compare(0, 2, "--") != 0) {
                throw std::invalid_argument(
                    "unexpected positional argument: " + name);
            }
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "missing value for argument: " + name);
            }
            const std::string value = argv[++index];
            if (!values_.emplace(name, value).second) {
                throw std::invalid_argument(
                    "duplicate argument: " + name);
            }
        }
    }

    [[nodiscard]] bool help() const noexcept {
        return help_;
    }

    [[nodiscard]] bool has(const std::string& name) const {
        return values_.find(name) != values_.end();
    }

    [[nodiscard]] std::optional<std::string> optional(
        const std::string& name) const {
        const auto value = values_.find(name);
        if (value == values_.end()) {
            return std::nullopt;
        }
        return value->second;
    }

    [[nodiscard]] std::string value_or(
        const std::string& name,
        std::string fallback) const {
        const auto value = values_.find(name);
        if (value == values_.end()) {
            return fallback;
        }
        return value->second;
    }

    [[nodiscard]] std::string require(const std::string& name) const {
        const auto value = values_.find(name);
        if (value == values_.end() || value->second.empty()) {
            throw std::invalid_argument(
                "required argument is missing: " + name);
        }
        return value->second;
    }

private:
    bool help_{false};
    std::unordered_map<std::string, std::string> values_;
};

inline void apply_api_key(const Arguments& arguments,
                          std::optional<std::string>& api_key) {
    const auto explicit_key = arguments.optional("--api-key");
    if (explicit_key.has_value()) {
        api_key = *explicit_key;
    }
}

inline std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input file: " + path);
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0) {
        throw std::runtime_error("input file is empty: " + path);
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(size));
    if (!input) {
        throw std::runtime_error("cannot read input file: " + path);
    }
    return bytes;
}

inline std::string media_type(const std::string& path) {
    const auto dot = path.find_last_of('.');
    std::string extension =
        dot == std::string::npos ? std::string{} : path.substr(dot);
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if (extension == ".gif") {
        return "image/gif";
    }
    if (extension == ".webp") {
        return "image/webp";
    }
    if (extension == ".pdf") {
        return "application/pdf";
    }
    if (extension == ".mp3") {
        return "audio/mpeg";
    }
    if (extension == ".wav") {
        return "audio/wav";
    }
    if (extension == ".mp4") {
        return "video/mp4";
    }
    if (extension == ".webm") {
        return "video/webm";
    }
    if (extension == ".txt") {
        return "text/plain";
    }
    return "application/octet-stream";
}

inline std::string filename(const std::string& path) {
    const auto separator = path.find_last_of("/\\");
    return separator == std::string::npos
               ? path
               : path.substr(separator + 1U);
}

inline int run_chatbot(neuralplus::AIClient& client,
                       neuralplus::Session& session,
                       const std::optional<std::string>& initial_prompt) {
    if (initial_prompt.has_value()) {
        const neuralplus::AIResponse response =
            client.generate(session, *initial_prompt);
        std::cout << response.message.text() << '\n';
        return 0;
    }

    std::cout << "Enter /quit to exit.\n";
    std::string prompt;
    while (true) {
        std::cout << "you> " << std::flush;
        if (!std::getline(std::cin, prompt) ||
            prompt == "/quit" || prompt == "/exit") {
            break;
        }
        if (prompt.empty()) {
            continue;
        }
        const neuralplus::AIResponse response =
            client.generate(session, prompt);
        std::cout << "assistant> " << response.message.text() << '\n';
    }
    return 0;
}

inline int print_exception(const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
}

}  // namespace neuralplus_examples
