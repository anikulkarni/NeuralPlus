// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

/// @file
/// Configurations, concrete clients, and factories for built-in providers.

#pragma once

#include "neuralplus/client.hpp"
#include "neuralplus/export.hpp"
#include "neuralplus/transport.hpp"
#include "neuralplus/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace neuralplus {

/// OpenAI Responses API configuration.
///
/// @see https://developers.openai.com/api/reference/resources/responses/methods/create
struct NEURALPLUS_API OpenAIConfig {
    /// Creates OpenAI configuration for `model_id`.
    explicit OpenAIConfig(std::string model_id);

    /// Model ID, capabilities, limits, and data-driven provider options.
    ModelDescriptor model;

    /// Explicit API key. When empty, OPENAI_API_KEY is read once.
    std::optional<std::string> api_key;

    /// Responses API root including its version segment.
    std::string base_url{"https://api.openai.com/v1"};

    /// Optional OpenAI organization identifier.
    std::string organization;

    /// Optional OpenAI project identifier.
    std::string project;

    /// Additional or replacement headers; mark credential values sensitive.
    std::vector<HttpHeader> extra_headers;
};

/// Anthropic Messages API configuration.
///
/// @see https://platform.claude.com/docs/en/api/messages/create
struct NEURALPLUS_API AnthropicConfig {
    /// Creates Anthropic configuration for `model_id`.
    explicit AnthropicConfig(std::string model_id);

    /// Model ID, capabilities, limits, and data-driven provider options.
    ModelDescriptor model;

    /// Explicit API key. When empty, ANTHROPIC_API_KEY is read once.
    std::optional<std::string> api_key;

    /// Anthropic API origin.
    std::string base_url{"https://api.anthropic.com"};

    /// Value of the required anthropic-version header.
    std::string api_version{"2023-06-01"};

    /// Fallback max_tokens value required by the Messages API.
    std::size_t default_max_output_tokens{1024};

    /// Additional or replacement headers; mark credential values sensitive.
    std::vector<HttpHeader> extra_headers;
};

/// Google Gemini generateContent API configuration.
///
/// @see https://ai.google.dev/api/generate-content
struct NEURALPLUS_API GeminiConfig {
    /// Creates Gemini configuration for `model_id`.
    explicit GeminiConfig(std::string model_id);

    /// Model ID, capabilities, limits, and data-driven provider options.
    ModelDescriptor model;

    /// Explicit key, otherwise GEMINI_API_KEY then GOOGLE_API_KEY is read once.
    std::optional<std::string> api_key;

    /// Gemini REST API root including its version segment.
    std::string base_url{"https://generativelanguage.googleapis.com/v1beta"};

    /// Additional or replacement headers; mark credential values sensitive.
    std::vector<HttpHeader> extra_headers;
};

/// Conservative OpenAI Chat Completions compatibility configuration.
///
/// @see https://developers.openai.com/api/reference/resources/chat/completions/methods/create
struct NEURALPLUS_API OpenAICompatibleConfig {
    /// Creates compatible-server configuration for a model and server root.
    OpenAICompatibleConfig(std::string model_id, std::string base_url);

    /// Model ID, capabilities, limits, and data-driven provider options.
    ModelDescriptor model;

    /// Optional explicit bearer token.
    std::optional<std::string> api_key;

    /// Optional environment-variable name used when api_key is absent.
    std::optional<std::string> api_key_environment;

    /// Server root, normally including `/v1`.
    std::string base_url;

    /// Additional or replacement headers; mark credential values sensitive.
    std::vector<HttpHeader> extra_headers;
};

/// OpenAI Responses API client.
class NEURALPLUS_API OpenAIClient final : public AIClient {
public:
    /// Creates an OpenAI Responses API client.
    explicit OpenAIClient(OpenAIConfig config,
                          const ClientOptions& options = {});
    ~OpenAIClient() override;

private:
    OpenAIClient(const OpenAIClient&) = delete;
    OpenAIClient& operator=(const OpenAIClient&) = delete;

    AIResponse generate_once(const AIRequest& request) override;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

/// Anthropic Messages API client.
class NEURALPLUS_API AnthropicClient final : public AIClient {
public:
    /// Creates an Anthropic Messages API client.
    explicit AnthropicClient(AnthropicConfig config,
                             const ClientOptions& options = {});
    ~AnthropicClient() override;

private:
    AnthropicClient(const AnthropicClient&) = delete;
    AnthropicClient& operator=(const AnthropicClient&) = delete;

    AIResponse generate_once(const AIRequest& request) override;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

/// Google Gemini generateContent API client.
class NEURALPLUS_API GeminiClient final : public AIClient {
public:
    /// Creates a Google Gemini generateContent client.
    explicit GeminiClient(GeminiConfig config,
                          const ClientOptions& options = {});
    ~GeminiClient() override;

private:
    GeminiClient(const GeminiClient&) = delete;
    GeminiClient& operator=(const GeminiClient&) = delete;

    AIResponse generate_once(const AIRequest& request) override;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

/// Generic OpenAI-compatible Chat Completions client.
class NEURALPLUS_API OpenAICompatibleClient final : public AIClient {
public:
    /// Creates an OpenAI-compatible Chat Completions client.
    explicit OpenAICompatibleClient(OpenAICompatibleConfig config,
                                    const ClientOptions& options = {});
    ~OpenAICompatibleClient() override;

private:
    OpenAICompatibleClient(const OpenAICompatibleClient&) = delete;
    OpenAICompatibleClient& operator=(const OpenAICompatibleClient&) = delete;

    AIResponse generate_once(const AIRequest& request) override;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

/// Creates an OpenAI client. Credentials resolve as explicit value, environment, error.
[[nodiscard]] NEURALPLUS_API std::unique_ptr<AIClient>
make_client(OpenAIConfig config, const ClientOptions& options = {});

/// Creates an Anthropic client. Credentials resolve as explicit value, environment, error.
[[nodiscard]] NEURALPLUS_API std::unique_ptr<AIClient>
make_client(AnthropicConfig config, const ClientOptions& options = {});

/// Creates a Gemini client. `GEMINI_API_KEY` precedes `GOOGLE_API_KEY`.
[[nodiscard]] NEURALPLUS_API std::unique_ptr<AIClient>
make_client(GeminiConfig config, const ClientOptions& options = {});

/// Creates an OpenAI-compatible client; unauthenticated local endpoints are allowed.
[[nodiscard]] NEURALPLUS_API std::unique_ptr<AIClient>
make_client(OpenAICompatibleConfig config,
            const ClientOptions& options = {});

}  // namespace neuralplus
