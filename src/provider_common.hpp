// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "neuralplus/client.hpp"
#include "neuralplus/providers.hpp"
#include "neuralplus/transport.hpp"
#include "neuralplus/types.hpp"

namespace neuralplus::provider_detail {

using SensitiveValues = std::vector<std::string>;

[[nodiscard]] ModelDescriptor make_model_descriptor(Provider provider, std::string model_id);

void validate_provider(const ModelDescriptor& model, Provider expected);

[[nodiscard]] std::shared_ptr<HttpTransport> resolve_transport(const ClientOptions& options);

[[nodiscard]] std::string resolve_required_credential(
    const std::optional<std::string>& explicit_value,
    std::initializer_list<std::string_view> environment_names, std::string_view provider_name);

[[nodiscard]] std::optional<std::string> resolve_optional_credential(
    const std::optional<std::string>& explicit_value,
    const std::optional<std::string>& environment_name);

[[nodiscard]] std::string join_url(std::string base_url, std::string_view path);

void set_header(std::vector<HttpHeader>& headers,
                std::string name,
                std::string value,
                bool sensitive = false);

void apply_extra_headers(std::vector<HttpHeader>& headers,
                         const std::vector<HttpHeader>& extra_headers);

[[nodiscard]] SensitiveValues collect_sensitive_values(
    const std::vector<HttpHeader>& headers,
    std::initializer_list<std::string_view> configured_secrets = {});

/// Removes exact credential/header values from a provider response before parsing.
void redact_sensitive_response(
    HttpResponse& response, const SensitiveValues& sensitive_values);

[[noreturn]] void throw_redacted_provider_error(
    const ProviderError& error, const SensitiveValues& sensitive_values);

[[nodiscard]] std::string base64_encode(const std::vector<std::uint8_t>& bytes);

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
base64_decode(std::string_view encoded);

[[nodiscard]] std::string data_url(const Content& content);

[[nodiscard]] std::string chat_audio_format(
    const Content& content, Provider provider);

[[nodiscard]] JsonValue merged_provider_options(const JsonValue& model_options,
                                                const JsonValue& request_options);

[[nodiscard]] JsonValue parse_response_json(const HttpResponse& response, Provider provider,
                                            std::string_view request_id);

[[nodiscard]] bool successful_status(int status) noexcept;

[[nodiscard]] std::string response_request_id(const HttpResponse& response,
                                              std::initializer_list<std::string_view> header_names);

[[nodiscard]] std::string string_field(const JsonValue& object, std::string_view key);

[[nodiscard]] std::optional<std::size_t> size_field(const JsonValue& object, std::string_view key);

[[nodiscard]] JsonValue parse_tool_arguments(std::string raw, bool& valid);

[[noreturn]] void throw_provider_error(Provider provider, const HttpResponse& response,
                                       std::string message, std::string code,
                                       std::string request_id);

[[noreturn]] void throw_unsupported_content(Provider provider, const Content& content);

}  // namespace neuralplus::provider_detail
