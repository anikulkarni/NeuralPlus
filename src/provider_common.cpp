// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "provider_common.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

using namespace neuralplus;

namespace neuralplus::provider_detail {
namespace {

[[nodiscard]] unsigned char ascii_lower(unsigned char character) noexcept {
    if (character >= static_cast<unsigned char>('A') &&
        character <= static_cast<unsigned char>('Z')) {
        return static_cast<unsigned char>(
            character - static_cast<unsigned char>('A') +
            static_cast<unsigned char>('a'));
    }
    return character;
}

[[nodiscard]] bool ascii_equal_case_insensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    return std::equal(left.begin(), left.end(), right.begin(), [](char lhs, char rhs) {
        const auto left_value = static_cast<unsigned char>(lhs);
        const auto right_value = static_cast<unsigned char>(rhs);
        return ascii_lower(left_value) == ascii_lower(right_value);
    });
}

[[nodiscard]] bool sensitive_header_name(std::string_view name) {
    std::string normalized_name(name);
    std::transform(
        normalized_name.begin(), normalized_name.end(),
        normalized_name.begin(), [](char character) {
            return static_cast<char>(
                ascii_lower(static_cast<unsigned char>(character)));
        });
    const bool token_suffix =
        normalized_name.size() >= 6U &&
        normalized_name.compare(normalized_name.size() - 6U, 6U,
                                "-token") == 0;
    return normalized_name.find("authorization") != std::string::npos ||
           normalized_name.find("api-key") != std::string::npos ||
           normalized_name.find("apikey") != std::string::npos ||
           normalized_name.find("secret") != std::string::npos ||
           normalized_name.find("credential") != std::string::npos ||
           token_suffix || normalized_name == "cookie";
}

[[nodiscard]] bool authorization_header_name(std::string_view name) {
    std::string normalized_name(name);
    std::transform(
        normalized_name.begin(), normalized_name.end(),
        normalized_name.begin(), [](char character) {
            return static_cast<char>(
                ascii_lower(static_cast<unsigned char>(character)));
        });
    return normalized_name.find("authorization") != std::string::npos;
}

void append_unique_secret(SensitiveValues& values, std::string_view value) {
    if (value.empty()) {
        return;
    }
    const auto duplicate = std::find(values.begin(), values.end(), value);
    if (duplicate == values.end()) {
        values.emplace_back(value);
    }
}

[[nodiscard]] std::string redact_sensitive_values(
    std::string_view input, const SensitiveValues& sensitive_values) {
    SensitiveValues ordered_values = sensitive_values;
    std::sort(ordered_values.begin(), ordered_values.end(),
              [](const std::string& left, const std::string& right) {
                  return left.size() > right.size();
              });

    static constexpr std::string_view replacement{"[REDACTED]"};
    std::string result;
    result.reserve(input.size());
    // Match against the original input once so a secret cannot alter or match
    // text in the replacement marker itself.
    for (std::size_t index = 0; index < input.size();) {
        const auto match = std::find_if(
            ordered_values.begin(), ordered_values.end(),
            [input, index](const std::string& secret) {
                return !secret.empty() && index + secret.size() <= input.size() &&
                       input.compare(index, secret.size(), secret) == 0;
            });
        if (match == ordered_values.end()) {
            result.push_back(input[index]);
            ++index;
            continue;
        }
        result.append(replacement.data(), replacement.size());
        index += match->size();
    }
    return result;
}

void redact_json_strings(
    JsonValue& value, const SensitiveValues& sensitive_values) {
    if (value.is_string()) {
        value = redact_sensitive_values(
            value.get_ref<const std::string&>(), sensitive_values);
        return;
    }
    if (value.is_array()) {
        for (JsonValue& element : value) {
            redact_json_strings(element, sensitive_values);
        }
        return;
    }
    if (!value.is_object()) {
        return;
    }

    // JSON member names can also be controlled by a provider. Rebuilding the
    // object ensures a credential echoed as a key cannot survive in opaque
    // provider metadata.
    JsonValue redacted = JsonValue::object();
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
        JsonValue member = iterator.value();
        redact_json_strings(member, sensitive_values);
        redacted[redact_sensitive_values(iterator.key(), sensitive_values)] =
            std::move(member);
    }
    value = std::move(redacted);
}

[[nodiscard]] std::optional<std::string> environment_value(std::string_view name) {
    if (name.empty()) {
        return std::nullopt;
    }
    const std::string owned_name{name};
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t value_size = 0;
    if (_dupenv_s(&value, &value_size, owned_name.c_str()) != 0 ||
        value == nullptr) {
        std::free(value);
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
#else
    const char* value = std::getenv(owned_name.c_str());
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

void require_object(const JsonValue& value, std::string_view description) {
    if (!value.is_object()) {
        throw ConfigurationError(std::string(description) + " must be a JSON object");
    }
}

}  // namespace

ModelDescriptor make_model_descriptor(Provider provider, std::string model_id) {
    if (model_id.empty()) {
        throw ConfigurationError("model id cannot be empty");
    }

    ModelDescriptor descriptor;
    descriptor.provider = provider;
    descriptor.id = std::move(model_id);
    descriptor.display_name = descriptor.id;
    descriptor.capabilities.text_input = true;
    descriptor.capabilities.text_output = true;
    if (provider != Provider::openai_compatible) {
        // First-party provider defaults cover the common text, vision, and
        // function-calling surface. Less universal audio/file features and all
        // generic compatible-server features remain explicit model choices.
        descriptor.capabilities.image_input = true;
        descriptor.capabilities.tools = true;
        descriptor.capabilities.parallel_tool_calls = true;
    }
    return descriptor;
}

void validate_provider(const ModelDescriptor& model, Provider expected) {
    if (model.provider != expected) {
        throw ConfigurationError(
            "model descriptor provider does not match client protocol: expected " +
            std::string(to_string(expected)) + ", got " + std::string(to_string(model.provider)));
    }
    if (model.id.empty()) {
        throw ConfigurationError("model id cannot be empty");
    }
}

std::shared_ptr<HttpTransport> resolve_transport(const ClientOptions& options) {
    if (options.transport) {
        return options.transport;
    }
    return make_default_http_transport();
}

std::string resolve_required_credential(const std::optional<std::string>& explicit_value,
                                        std::initializer_list<std::string_view> environment_names,
                                        std::string_view provider_name) {
    if (explicit_value.has_value()) {
        if (explicit_value->empty()) {
            throw ConfigurationError(std::string(provider_name) + " API key cannot be empty");
        }
        return *explicit_value;
    }

    for (const auto name : environment_names) {
        if (const auto value = environment_value(name)) {
            return *value;
        }
    }

    std::string message(provider_name);
    message += " API key is missing";
    if (!environment_names.size()) {
        throw ConfigurationError(message);
    }
    message += "; set it explicitly or through ";
    bool first = true;
    for (const auto name : environment_names) {
        if (!first) {
            message += " or ";
        }
        message += std::string(name);
        first = false;
    }
    throw ConfigurationError(message);
}

std::optional<std::string> resolve_optional_credential(
    const std::optional<std::string>& explicit_value,
    const std::optional<std::string>& environment_name) {
    if (explicit_value.has_value()) {
        if (explicit_value->empty()) {
            throw ConfigurationError("API key cannot be empty");
        }
        return explicit_value;
    }
    if (!environment_name.has_value()) {
        return std::nullopt;
    }
    if (environment_name->empty()) {
        throw ConfigurationError("API key environment variable name cannot be empty");
    }
    const auto value = environment_value(*environment_name);
    if (!value.has_value()) {
        throw ConfigurationError("API key environment variable is not set: " + *environment_name);
    }
    return value;
}

std::string join_url(std::string base_url, std::string_view path) {
    if (base_url.empty()) {
        throw ConfigurationError("provider base URL cannot be empty");
    }
    while (!base_url.empty() && base_url.back() == '/') {
        base_url.pop_back();
    }
    if (path.empty()) {
        return base_url;
    }
    if (path.front() != '/') {
        base_url.push_back('/');
    }
    base_url.append(path.data(), path.size());
    return base_url;
}

void set_header(std::vector<HttpHeader>& headers,
                std::string name,
                std::string value,
                bool sensitive) {
    const auto iterator =
        std::find_if(headers.begin(), headers.end(), [&name](const HttpHeader& header) {
            return ascii_equal_case_insensitive(header.name, name);
        });
    if (iterator == headers.end()) {
        headers.push_back(
            {std::move(name), std::move(value), sensitive});
        return;
    }
    iterator->name = std::move(name);
    iterator->value = std::move(value);
    iterator->sensitive = sensitive;
}

void apply_extra_headers(std::vector<HttpHeader>& headers,
    const std::vector<HttpHeader>& extra_headers) {
    for (const auto& header : extra_headers) {
        set_header(
            headers, header.name, header.value, header.sensitive);
    }
}

SensitiveValues collect_sensitive_values(
    const std::vector<HttpHeader>& headers,
    std::initializer_list<std::string_view> configured_secrets) {
    SensitiveValues result;
    for (const auto secret : configured_secrets) {
        append_unique_secret(result, secret);
    }
    for (const auto& header : headers) {
        if (!header.sensitive &&
            !sensitive_header_name(header.name)) {
            continue;
        }
        append_unique_secret(result, header.value);
        if (!authorization_header_name(header.name)) {
            continue;
        }
        // An error service might echo either "Bearer token" or only "token".
        const auto separator = header.value.find_first_of(" \t");
        if (separator == std::string::npos) {
            continue;
        }
        const auto token_begin = header.value.find_first_not_of(" \t", separator);
        if (token_begin != std::string::npos) {
            append_unique_secret(
                result, std::string_view(header.value).substr(token_begin));
        }
    }
    return result;
}

void redact_sensitive_response(
    HttpResponse& response, const SensitiveValues& sensitive_values) {
    if (sensitive_values.empty()) {
        return;
    }
    for (HttpHeader& header : response.headers) {
        header.value =
            redact_sensitive_values(header.value, sensitive_values);
    }

    // Successful and unsuccessful endpoints are both untrusted. Scrub a valid
    // JSON tree so escaped strings are covered; retain malformed response text
    // only after applying literal exact-value redaction.
    JsonValue body = JsonValue::parse(response.body, nullptr, false);
    if (body.is_discarded()) {
        response.body =
            redact_sensitive_values(response.body, sensitive_values);
        return;
    }
    redact_json_strings(body, sensitive_values);
    response.body = body.dump();
}

void throw_redacted_provider_error(
    const ProviderError& error, const SensitiveValues& sensitive_values) {
    throw ProviderError(
        redact_sensitive_values(error.what(), sensitive_values),
        error.provider(),
        error.status(),
        redact_sensitive_values(error.code(), sensitive_values),
        redact_sensitive_values(error.request_id(), sensitive_values));
}

std::string base64_encode(const std::vector<std::uint8_t>& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2U) / 3U) * 4U);

    std::size_t index = 0;
    while (index + 2U < bytes.size()) {
        const auto value = (static_cast<std::uint32_t>(bytes[index]) << 16U) |
                           (static_cast<std::uint32_t>(bytes[index + 1U]) << 8U) |
                           static_cast<std::uint32_t>(bytes[index + 2U]);
        encoded.push_back(alphabet[(value >> 18U) & 0x3FU]);
        encoded.push_back(alphabet[(value >> 12U) & 0x3FU]);
        encoded.push_back(alphabet[(value >> 6U) & 0x3FU]);
        encoded.push_back(alphabet[value & 0x3FU]);
        index += 3U;
    }

    const auto remaining = bytes.size() - index;
    if (remaining == 1U) {
        const auto value = static_cast<std::uint32_t>(bytes[index]) << 16U;
        encoded.push_back(alphabet[(value >> 18U) & 0x3FU]);
        encoded.push_back(alphabet[(value >> 12U) & 0x3FU]);
        encoded.append("==");
    } else if (remaining == 2U) {
        const auto value = (static_cast<std::uint32_t>(bytes[index]) << 16U) |
                           (static_cast<std::uint32_t>(bytes[index + 1U]) << 8U);
        encoded.push_back(alphabet[(value >> 18U) & 0x3FU]);
        encoded.push_back(alphabet[(value >> 12U) & 0x3FU]);
        encoded.push_back(alphabet[(value >> 6U) & 0x3FU]);
        encoded.push_back('=');
    }
    return encoded;
}

std::optional<std::vector<std::uint8_t>>
base64_decode(std::string_view encoded) {
    std::string compact;
    compact.reserve(encoded.size());
    for (const char character : encoded) {
        const bool ascii_whitespace =
            character == ' ' || character == '\t' || character == '\r' ||
            character == '\n' || character == '\f' || character == '\v';
        if (!ascii_whitespace) {
            compact.push_back(character);
        }
    }
    if (compact.empty() || compact.size() % 4U != 0U) {
        return std::nullopt;
    }

    const auto decode_character = [](char character) -> int {
        if (character >= 'A' && character <= 'Z') {
            return character - 'A';
        }
        if (character >= 'a' && character <= 'z') {
            return character - 'a' + 26;
        }
        if (character >= '0' && character <= '9') {
            return character - '0' + 52;
        }
        if (character == '+') {
            return 62;
        }
        if (character == '/') {
            return 63;
        }
        return -1;
    };

    std::vector<std::uint8_t> decoded;
    decoded.reserve((compact.size() / 4U) * 3U);
    for (std::size_t index = 0; index < compact.size(); index += 4U) {
        const int first = decode_character(compact[index]);
        const int second = decode_character(compact[index + 1U]);
        const bool third_padding = compact[index + 2U] == '=';
        const bool fourth_padding = compact[index + 3U] == '=';
        const int third =
            third_padding ? 0 : decode_character(compact[index + 2U]);
        const int fourth =
            fourth_padding ? 0 : decode_character(compact[index + 3U]);
        const bool last_group = index + 4U == compact.size();

        if (first < 0 || second < 0 || third < 0 || fourth < 0 ||
            (third_padding && !fourth_padding) ||
            ((third_padding || fourth_padding) && !last_group) ||
            (third_padding && (second & 0x0F) != 0) ||
            (fourth_padding && !third_padding && (third & 0x03) != 0)) {
            return std::nullopt;
        }

        decoded.push_back(static_cast<std::uint8_t>(
            (first << 2) | (second >> 4)));
        if (!third_padding) {
            decoded.push_back(static_cast<std::uint8_t>(
                ((second & 0x0F) << 4) | (third >> 2)));
        }
        if (!fourth_padding) {
            decoded.push_back(static_cast<std::uint8_t>(
                ((third & 0x03) << 6) | fourth));
        }
    }
    return decoded;
}

std::string data_url(const Content& content) {
    const std::string media_type =
        content.media_type().empty() ? "application/octet-stream" : content.media_type();
    return "data:" + media_type + ";base64," + base64_encode(content.bytes());
}

std::string chat_audio_format(const Content& content, Provider provider) {
    const auto configured_format = content.options().find("format");
    if (configured_format != content.options().end()) {
        if (!configured_format->is_string() ||
            configured_format->get_ref<const std::string&>().empty()) {
            throw ProviderError(
                "audio format option must be a non-empty string",
                provider,
                0,
                "invalid_audio_format");
        }
        return configured_format->get<std::string>();
    }

    if (content.media_type() == "audio/mpeg" ||
        content.media_type() == "audio/mp3") {
        return "mp3";
    }
    if (content.media_type() == "audio/wav" ||
        content.media_type() == "audio/wave" ||
        content.media_type() == "audio/x-wav") {
        return "wav";
    }
    throw ProviderError(
        "audio input requires MP3/WAV media type or options.format",
        provider,
        0,
        "unsupported_audio_format");
}

JsonValue merged_provider_options(const JsonValue& model_options,
                                  const JsonValue& request_options) {
    require_object(model_options, "model provider_options");
    require_object(request_options, "request provider_options");
    JsonValue merged = model_options;
    merged.update(request_options);
    return merged;
}

JsonValue parse_response_json(const HttpResponse& response, Provider provider,
                              std::string_view request_id) {
    // Example: {"id":"r1"} becomes an object available to the provider
    // adapter; "<html>" becomes ProviderError{code="malformed_json"}.
    // https://json.nlohmann.me/api/basic_json/parse/
    JsonValue parsed = JsonValue::parse(response.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw ProviderError("provider returned malformed JSON", provider, response.status,
                            "malformed_json", std::string(request_id));
    }
    return parsed;
}

bool successful_status(int status) noexcept {
    return status >= 200 && status < 300;
}

std::string response_request_id(const HttpResponse& response,
                                std::initializer_list<std::string_view> header_names) {
    for (const auto header_name : header_names) {
        if (const auto value = response.header(header_name)) {
            return *value;
        }
    }
    return {};
}

std::string string_field(const JsonValue& object, std::string_view key) {
    if (!object.is_object()) {
        return {};
    }
    const auto iterator = object.find(std::string(key));
    if (iterator == object.end() || !iterator->is_string()) {
        return {};
    }
    return iterator->get<std::string>();
}

std::optional<std::size_t> size_field(const JsonValue& object, std::string_view key) {
    if (!object.is_object()) {
        return std::nullopt;
    }
    const auto iterator = object.find(std::string(key));
    if (iterator == object.end()) {
        return std::nullopt;
    }
    if (iterator->is_number_unsigned()) {
        const auto value = iterator->get<std::uint64_t>();
        if (value <= std::numeric_limits<std::size_t>::max()) {
            return static_cast<std::size_t>(value);
        }
    } else if (iterator->is_number_integer()) {
        const auto value = iterator->get<std::int64_t>();
        if (value >= 0 &&
            static_cast<std::uint64_t>(value) <= std::numeric_limits<std::size_t>::max()) {
            return static_cast<std::size_t>(value);
        }
    }
    return std::nullopt;
}

JsonValue parse_tool_arguments(std::string raw, bool& valid) {
    // Example: "{\"city\":\"Paris\"}" becomes {"city":"Paris"} with
    // valid=true; "{broken" becomes {} with valid=false so AIClient can return
    // a deterministic tool error without invoking application code.
    JsonValue parsed = JsonValue::parse(raw, nullptr, false);
    valid = !parsed.is_discarded();
    if (!valid) {
        return JsonValue::object();
    }
    return parsed;
}

void throw_provider_error(Provider provider, const HttpResponse& response, std::string message,
                          std::string code, std::string request_id) {
    if (message.empty()) {
        message = "provider request failed with HTTP status " + std::to_string(response.status);
    }
    throw ProviderError(std::move(message), provider, response.status, std::move(code),
                        std::move(request_id));
}

void throw_unsupported_content(Provider provider, const Content& content) {
    throw ProviderError("content type/source is not supported by " +
                            std::string(to_string(provider)) +
                            ": type=" + std::to_string(static_cast<int>(content.type())) +
                            ", source=" + std::to_string(static_cast<int>(content.source())),
                        provider, 0, "unsupported_content");
}

}  // namespace neuralplus::provider_detail
