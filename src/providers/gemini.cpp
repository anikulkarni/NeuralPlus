// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <string_view>
#include <utility>

#include "../provider_common.hpp"

using namespace neuralplus;

namespace {

[[nodiscard]] bool has_native_gemini_content(const Message& message) {
    const auto& metadata = message.provider_metadata();
    return metadata.is_object() &&
           provider_detail::string_field(metadata, "provider") == "gemini" &&
           metadata.contains("content") && metadata["content"].is_object();
}

[[nodiscard]] JsonValue gemini_part(const Content& content) {
    if (content.type() == ContentType::text && content.source() == ContentSource::value) {
        return JsonValue{{"text", content.value()}};
    }

    if (content.type() == ContentType::image ||
        content.type() == ContentType::audio ||
        content.type() == ContentType::file) {
        if (content.source() == ContentSource::bytes) {
            JsonValue inline_data{
                {"mimeType", content.media_type()},
                {"data",
                 provider_detail::base64_encode(content.bytes())},
            };
            return JsonValue{
                {"inlineData",
                 std::move(inline_data)}};
        }
        if (content.source() == ContentSource::url) {
            // https://ai.google.dev/api/files
            // fileData.fileUri identifies a Gemini-uploaded/Google file; the
            // generateContent endpoint does not fetch arbitrary web media.
            static constexpr std::string_view gemini_file_prefix{
                "https://generativelanguage.googleapis.com/"};
            static constexpr std::string_view google_storage_prefix{"gs://"};
            const bool is_gemini_file =
                content.url().compare(0, gemini_file_prefix.size(), gemini_file_prefix.data(),
                                      gemini_file_prefix.size()) == 0;
            const bool is_google_storage =
                content.url().compare(0, google_storage_prefix.size(), google_storage_prefix.data(),
                                      google_storage_prefix.size()) == 0;
            if (!is_gemini_file && !is_google_storage) {
                throw ProviderError(
                    "Gemini media URLs must be uploaded File API or gs:// URIs; "
                    "use inline bytes for arbitrary media data",
                    Provider::gemini, 0, "unsupported_media_url");
            }
            JsonValue file_data{{"fileUri", content.url()}};
            if (!content.media_type().empty()) {
                file_data["mimeType"] = content.media_type();
            }
            return JsonValue{{"fileData", std::move(file_data)}};
        }
        provider_detail::throw_unsupported_content(Provider::gemini, content);
    }

    if (content.type() == ContentType::extension &&
        content.source() == ContentSource::provider_data && content.provider() == "gemini") {
        return content.provider_data();
    }

    provider_detail::throw_unsupported_content(Provider::gemini, content);
}

[[nodiscard]] JsonValue gemini_parts(const Message& message) {
    JsonValue parts = JsonValue::array();
    for (const auto& content : message.contents()) {
        parts.push_back(gemini_part(content));
    }
    return parts;
}

void append_gemini_turn(JsonValue& contents, std::string_view role, JsonValue parts) {
    if (!contents.empty() && contents.back().is_object() &&
        provider_detail::string_field(contents.back(), "role") == role &&
        contents.back().contains("parts") && contents.back()["parts"].is_array()) {
        for (auto& part : parts) {
            contents.back()["parts"].push_back(std::move(part));
        }
        return;
    }
    contents.push_back(JsonValue{{"role", role}, {"parts", std::move(parts)}});
}

[[nodiscard]] JsonValue gemini_tool_response(const Message& message) {
    const auto text = message.text();
    JsonValue value = JsonValue::parse(text, nullptr, false);
    JsonValue response = JsonValue::object();
    if (!value.is_discarded() && value.is_object()) {
        response = std::move(value);
    } else if (!value.is_discarded()) {
        response["result"] = std::move(value);
    } else {
        response["result"] = text;
    }
    if (message.is_tool_error()) {
        response["is_error"] = true;
    }
    return response;
}

[[nodiscard]] JsonValue gemini_tool_response_parts(
    const Message& message) {
    JsonValue parts = JsonValue::array();
    for (const Content& content : message.contents()) {
        if (content.type() == ContentType::text) {
            continue;
        }
        if ((content.type() == ContentType::image ||
             content.type() == ContentType::audio ||
             content.type() == ContentType::file) &&
            content.source() == ContentSource::bytes) {
            JsonValue inline_data{
                {"mimeType", content.media_type()},
                {"data",
                 provider_detail::base64_encode(content.bytes())},
            };
            parts.push_back(
                JsonValue{{"inlineData", std::move(inline_data)}});
            continue;
        }
        if (content.type() == ContentType::extension &&
            content.source() == ContentSource::provider_data &&
            content.provider() == "gemini") {
            parts.push_back(content.provider_data());
            continue;
        }
        provider_detail::throw_unsupported_content(
            Provider::gemini, content);
    }
    return parts;
}

void append_gemini_history(JsonValue& contents, const Message& message) {
    if (message.role() == Role::system) {
        return;
    }

    if (message.role() == Role::assistant && has_native_gemini_content(message)) {
        const JsonValue& native = message.provider_metadata()["content"];
        const auto role = provider_detail::string_field(native, "role");
        const auto parts = native.find("parts");
        if (parts != native.end() && parts->is_array()) {
            append_gemini_turn(contents, role.empty() ? "model" : role, *parts);
        }
        return;
    }

    if (message.role() == Role::tool) {
        JsonValue function_response{{"name", message.tool_name()},
                                    {"response", gemini_tool_response(message)}};
        const JsonValue& metadata = message.provider_metadata();
        const bool provider_supplied_id =
            metadata.is_object() &&
            metadata.contains("functionCall") &&
            metadata["functionCall"].is_object() &&
            !provider_detail::string_field(
                 metadata["functionCall"], "id")
                 .empty();
        if (provider_supplied_id) {
            function_response["id"] = message.tool_call_id();
        }
        JsonValue response_parts = gemini_tool_response_parts(message);
        if (!response_parts.empty()) {
            function_response["parts"] = std::move(response_parts);
        }
        JsonValue parts = JsonValue::array();
        parts.push_back(JsonValue{{"functionResponse", std::move(function_response)}});
        append_gemini_turn(contents, "user", std::move(parts));
        return;
    }

    JsonValue parts = gemini_parts(message);
    if (message.role() == Role::assistant) {
        for (const auto& call : message.tool_calls()) {
            if (call.provider_metadata.is_object() &&
                call.provider_metadata.contains("functionCall")) {
                parts.push_back(call.provider_metadata);
                continue;
            }
            JsonValue function_call{{"name", call.name}, {"args", call.arguments}};
            if (!call.id.empty()) {
                function_call["id"] = call.id;
            }
            parts.push_back(JsonValue{{"functionCall", std::move(function_call)}});
        }
        append_gemini_turn(contents, "model", std::move(parts));
        return;
    }
    append_gemini_turn(contents, "user", std::move(parts));
}

[[nodiscard]] std::string gemini_system_instruction(const AIRequest& request) {
    std::string system = request.system_message.value_or("");
    for (const auto& message : request.messages) {
        if (message.role() != Role::system || message.text().empty()) {
            continue;
        }
        if (!system.empty()) {
            system.push_back('\n');
        }
        system += message.text();
    }
    return system;
}

[[nodiscard]] JsonValue make_gemini_request(const GeminiConfig& config, const AIRequest& request) {
    JsonValue body = provider_detail::merged_provider_options(config.model.provider_options,
                                                              request.options.provider_options);

    JsonValue contents = JsonValue::array();
    for (const auto& message : request.messages) {
        append_gemini_history(contents, message);
    }
    body["contents"] = std::move(contents);

    const auto system = gemini_system_instruction(request);
    if (!system.empty()) {
        body["systemInstruction"] =
            JsonValue{{"parts", JsonValue::array({JsonValue{{"text", std::move(system)}}})}};
    }
    const auto configured_tools = body.find("tools");
    if (configured_tools != body.end() && !configured_tools->is_array()) {
        throw ConfigurationError(
            "Gemini tools provider option must be an array");
    }
    if (!request.tools.empty()) {
        JsonValue declarations = JsonValue::array();
        for (const auto& tool : request.tools) {
            JsonValue declaration = tool.provider_options;
            declaration["name"] = tool.name;
            declaration["description"] = tool.description;
            declaration["parameters"] = tool.input_schema;
            declarations.push_back(std::move(declaration));
        }
        JsonValue tools = configured_tools == body.end()
                              ? JsonValue::array()
                              : *configured_tools;
        tools.push_back(
            JsonValue{{"functionDeclarations", std::move(declarations)}});
        body["tools"] = std::move(tools);
    }
    if (request.options.max_output_tokens.has_value() || request.options.temperature.has_value()) {
        auto generation_config = body.find("generationConfig");
        if (generation_config == body.end()) {
            body["generationConfig"] = JsonValue::object();
            generation_config = body.find("generationConfig");
        }
        if (!generation_config->is_object()) {
            throw ConfigurationError("Gemini generationConfig provider option must be an object");
        }
        if (request.options.max_output_tokens.has_value()) {
            (*generation_config)["maxOutputTokens"] = *request.options.max_output_tokens;
        }
        if (request.options.temperature.has_value()) {
            (*generation_config)["temperature"] = *request.options.temperature;
        }
    }
    return body;
}

[[nodiscard]] std::string gemini_model_path(std::string model_id) {
    static constexpr std::string_view prefix{"models/"};
    if (model_id.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0) {
        model_id.erase(0, prefix.size());
    }
    const bool valid =
        !model_id.empty() && std::all_of(model_id.begin(), model_id.end(), [](char character) {
            const auto value = static_cast<unsigned char>(character);
            const bool ascii_alphanumeric =
                (value >= static_cast<unsigned char>('A') &&
                 value <= static_cast<unsigned char>('Z')) ||
                (value >= static_cast<unsigned char>('a') &&
                 value <= static_cast<unsigned char>('z')) ||
                (value >= static_cast<unsigned char>('0') &&
                 value <= static_cast<unsigned char>('9'));
            return ascii_alphanumeric || character == '-' ||
                   character == '_' || character == '.';
        });
    if (!valid) {
        throw ConfigurationError("Gemini model id must be one URL-safe model path segment");
    }
    return "/models/" + model_id + ":generateContent";
}

[[noreturn]] void throw_gemini_http_error(const HttpResponse& response, std::string request_id) {
    const JsonValue parsed = JsonValue::parse(response.body, nullptr, false);
    std::string message;
    std::string code;
    if (!parsed.is_discarded() && parsed.is_object()) {
        const auto error = parsed.find("error");
        if (error != parsed.end() && error->is_object()) {
            message = provider_detail::string_field(*error, "message");
            code = provider_detail::string_field(*error, "status");
        }
    }
    provider_detail::throw_provider_error(Provider::gemini, response, std::move(message),
                                          std::move(code), std::move(request_id));
}

[[nodiscard]] FinishReason gemini_finish_reason(std::string_view reason, bool has_tool_calls,
                                                bool prompt_blocked) {
    if (has_tool_calls) {
        return FinishReason::tool_calls;
    }
    if (prompt_blocked) {
        return FinishReason::content_filter;
    }
    if (reason == "STOP") {
        return FinishReason::stop;
    }
    if (reason == "MAX_TOKENS") {
        return FinishReason::length;
    }
    if (reason == "SAFETY" || reason == "RECITATION" || reason == "LANGUAGE" ||
        reason == "BLOCKLIST" || reason == "PROHIBITED_CONTENT" || reason == "SPII" ||
        reason == "IMAGE_SAFETY" || reason == "IMAGE_PROHIBITED_CONTENT" ||
        reason == "IMAGE_RECITATION" || reason == "ESCALATION") {
        return FinishReason::content_filter;
    }
    if (reason == "MALFORMED_FUNCTION_CALL" || reason == "UNEXPECTED_TOOL_CALL" ||
        reason == "TOO_MANY_TOOL_CALLS" || reason == "MISSING_THOUGHT_SIGNATURE" ||
        reason == "MALFORMED_RESPONSE" || reason == "NO_IMAGE") {
        return FinishReason::error;
    }
    return FinishReason::unknown;
}

[[nodiscard]] AIResponse parse_gemini_response(
    const HttpResponse& response, const AIRequest& request) {
    std::string request_id = provider_detail::response_request_id(response, {"x-request-id"});
    if (!provider_detail::successful_status(response.status)) {
        throw_gemini_http_error(response, std::move(request_id));
    }

    const JsonValue body =
        provider_detail::parse_response_json(response, Provider::gemini, request_id);
    if (request_id.empty()) {
        request_id = provider_detail::string_field(body, "responseId");
    }
    bool prompt_blocked = false;
    const auto feedback = body.find("promptFeedback");
    if (feedback != body.end() && feedback->is_object()) {
        prompt_blocked = !provider_detail::string_field(*feedback, "blockReason").empty();
    }

    JsonValue candidate = JsonValue::object();
    bool has_candidate = false;
    const auto candidates = body.find("candidates");
    if (candidates == body.end() || (candidates->is_array() && candidates->empty())) {
        if (!prompt_blocked) {
            throw ProviderError(
                "Gemini response is missing the candidates array",
                Provider::gemini,
                response.status,
                "malformed_response",
                request_id);
        }
    } else if (!candidates->is_array() || !candidates->front().is_object()) {
        throw ProviderError(
            "Gemini response has an invalid candidates array",
            Provider::gemini,
            response.status,
            "malformed_response",
            request_id);
    } else {
        candidate = candidates->front();
        has_candidate = true;
    }

    JsonValue native_content = JsonValue::object();
    const auto candidate_content = candidate.find("content");
    const std::string candidate_finish_reason =
        provider_detail::string_field(candidate, "finishReason");
    if (candidate_content != candidate.end() && !candidate_content->is_object()) {
        throw ProviderError(
            "Gemini response candidate has invalid content",
            Provider::gemini,
            response.status,
            "malformed_response",
            request_id);
    }
    if (candidate_content != candidate.end()) {
        native_content = *candidate_content;
    }
    JsonValue parts = JsonValue::array();
    const auto native_parts = native_content.find("parts");
    if (native_parts != native_content.end() && !native_parts->is_array()) {
        throw ProviderError(
            "Gemini response candidate has invalid content parts",
            Provider::gemini,
            response.status,
            "malformed_response",
            request_id);
    }
    if (native_parts != native_content.end()) {
        parts = *native_parts;
    } else if (has_candidate && candidate_finish_reason.empty()) {
        throw ProviderError(
            "Gemini response candidate is missing content parts and finishReason",
            Provider::gemini,
            response.status,
            "malformed_response",
            request_id);
    }

    std::vector<Content> contents;
    std::vector<ToolCall> calls;

    // https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures
    // {"functionCall":{"name":"f","args":{"x":1}},"thoughtSignature":"..."}
    // becomes a ToolCall, while the unmodified Part stays on Message so the
    // encrypted thoughtSignature is replayed on the next generateContent call.
    std::size_t call_index = 0;
    for (const auto& part : parts) {
        if (!part.is_object()) {
            throw ProviderError(
                "Gemini response content parts must be objects",
                Provider::gemini,
                response.status,
                "malformed_response",
                request_id);
        }

        const auto text = part.find("text");
        if (text != part.end() && !text->is_string()) {
            throw ProviderError(
                "Gemini response text part must contain a string",
                Provider::gemini,
                response.status,
                "malformed_response",
                request_id);
        }
        const auto thought = part.find("thought");
        if (thought != part.end() && !thought->is_boolean()) {
            throw ProviderError(
                "Gemini response thought marker must be a boolean",
                Provider::gemini,
                response.status,
                "malformed_response",
                request_id);
        }
        const auto thought_signature = part.find("thoughtSignature");
        if (thought_signature != part.end() &&
            !thought_signature->is_string()) {
            throw ProviderError(
                "Gemini response thought signature must be a string",
                Provider::gemini,
                response.status,
                "malformed_response",
                request_id);
        }

        const auto inline_data = part.find("inlineData");
        if (inline_data != part.end()) {
            if (!inline_data->is_object()) {
                throw ProviderError(
                    "Gemini response inlineData must be an object",
                    Provider::gemini,
                    response.status,
                    "malformed_response",
                    request_id);
            }
            for (const std::string_view field :
                 {"mimeType", "data", "displayName"}) {
                const auto value = inline_data->find(field);
                if (value != inline_data->end() && !value->is_string()) {
                    throw ProviderError(
                        "Gemini response inlineData string field has an invalid type",
                        Provider::gemini,
                        response.status,
                        "malformed_response",
                        request_id);
                }
            }
        }

        const auto file_data = part.find("fileData");
        if (file_data != part.end()) {
            if (!file_data->is_object()) {
                throw ProviderError(
                    "Gemini response fileData must be an object",
                    Provider::gemini,
                    response.status,
                    "malformed_response",
                    request_id);
            }
            for (const std::string_view field :
                 {"mimeType", "fileUri", "displayName"}) {
                const auto value = file_data->find(field);
                if (value != file_data->end() && !value->is_string()) {
                    throw ProviderError(
                        "Gemini response fileData string field has an invalid type",
                        Provider::gemini,
                        response.status,
                        "malformed_response",
                        request_id);
                }
            }
        }

        const auto function_call = part.find("functionCall");
        if (function_call != part.end()) {
            if (!function_call->is_object()) {
                throw ProviderError(
                    "Gemini response functionCall must be an object",
                    Provider::gemini,
                    response.status,
                    "malformed_response",
                    request_id);
            }
            const auto id = function_call->find("id");
            const auto name = function_call->find("name");
            const auto arguments = function_call->find("args");
            if ((id != function_call->end() && !id->is_string()) ||
                name == function_call->end() || !name->is_string() ||
                name->get_ref<const std::string&>().empty() ||
                (arguments != function_call->end() &&
                 !arguments->is_object())) {
                throw ProviderError(
                    "Gemini response functionCall has invalid id, non-empty "
                    "name, or args",
                    Provider::gemini,
                    response.status,
                    "malformed_response",
                    request_id);
            }
        }

        std::size_t payload_fields = 0;
        for (const std::string_view field :
             {"text",
              "inlineData",
              "fileData",
              "functionCall",
              "functionResponse",
              "executableCode",
              "codeExecutionResult"}) {
            if (part.find(field) != part.end()) {
                ++payload_fields;
            }
        }
        if (payload_fields > 1U) {
            throw ProviderError(
                "Gemini response Part contains multiple mutually exclusive "
                "payload fields",
                Provider::gemini,
                response.status,
                "malformed_response",
                request_id);
        }

        // Gemini can return a visible answer and a separate thought summary:
        // {"text":"Reasoning summary","thought":true}
        // {"text":"Final answer"}
        // Keep the first Part provider-native so Message::text() exposes only
        // the final answer while the exact Part remains available and replayable.
        if (text != part.end() && thought != part.end() &&
            thought->get<bool>()) {
            contents.push_back(Content::extension("gemini", part));
            continue;
        }
        if (text != part.end()) {
            contents.push_back(Content::text(text->get<std::string>()));
            continue;
        }
        if (inline_data != part.end()) {
            const std::string media_type =
                provider_detail::string_field(*inline_data, "mimeType");
            const std::string encoded =
                provider_detail::string_field(*inline_data, "data");
            const auto decoded =
                provider_detail::base64_decode(encoded);
            if (media_type.empty() || !decoded.has_value() ||
                decoded->empty()) {
                throw ProviderError(
                    "Gemini returned malformed inline data",
                    Provider::gemini,
                    response.status,
                    "malformed_inline_data",
                    request_id);
            }
            if (media_type.compare(0, 6, "image/") == 0) {
                contents.push_back(
                    Content::image_bytes(*decoded, media_type));
            } else if (media_type.compare(0, 6, "audio/") == 0) {
                contents.push_back(
                    Content::audio_bytes(*decoded, media_type));
            } else {
                contents.push_back(Content::file_bytes(
                    *decoded,
                    media_type,
                    provider_detail::string_field(
                        *inline_data, "displayName")));
            }
            continue;
        }
        if (file_data != part.end()) {
            const std::string media_type =
                provider_detail::string_field(*file_data, "mimeType");
            const std::string uri =
                provider_detail::string_field(*file_data, "fileUri");
            if (uri.empty()) {
                throw ProviderError(
                    "Gemini returned file data without a URI",
                    Provider::gemini,
                    response.status,
                    "malformed_file_data",
                    request_id);
            }
            if (media_type.compare(0, 6, "image/") == 0) {
                contents.push_back(
                    Content::image_url(uri, media_type));
            } else if (media_type.compare(0, 6, "audio/") == 0) {
                contents.push_back(
                    Content::audio_url(uri, media_type));
            } else {
                contents.push_back(Content::file_url(
                    uri,
                    media_type,
                    provider_detail::string_field(
                        *file_data, "displayName")));
            }
            continue;
        }
        if (function_call != part.end()) {
            ToolCall call;
            call.id = provider_detail::string_field(*function_call, "id");
            if (call.id.empty()) {
                call.id = "gemini_call_" + request.run_id + "_" +
                          std::to_string(request.messages.size()) + "_" +
                          std::to_string(call_index);
            }
            call.name = provider_detail::string_field(*function_call, "name");
            const auto arguments = function_call->find("args");
            if (arguments == function_call->end()) {
                call.arguments = JsonValue::object();
            } else {
                call.arguments = *arguments;
            }
            call.raw_arguments = call.arguments.dump();
            call.arguments_valid = true;
            call.provider_metadata = part;
            calls.push_back(std::move(call));
            ++call_index;
            continue;
        }
        contents.push_back(Content::extension("gemini", part));
    }

    Message message = Message::assistant(std::move(contents), std::move(calls));
    message.set_provider_metadata(JsonValue{{"provider", "gemini"}, {"content", native_content}});
    AIResponse result(std::move(message));

    result.finish_reason =
        gemini_finish_reason(candidate_finish_reason,
                             !result.message.tool_calls().empty(), prompt_blocked);
    result.provider_model = provider_detail::string_field(body, "modelVersion");
    result.provider_request_id = std::move(request_id);

    const auto usage = body.find("usageMetadata");
    if (usage != body.end() && usage->is_object()) {
        result.usage.input_tokens = provider_detail::size_field(*usage, "promptTokenCount");
        result.usage.output_tokens = provider_detail::size_field(*usage, "candidatesTokenCount");
        result.usage.total_tokens = provider_detail::size_field(*usage, "totalTokenCount");
        result.usage.cached_input_tokens =
            provider_detail::size_field(*usage, "cachedContentTokenCount");
        result.usage.reasoning_tokens = provider_detail::size_field(*usage, "thoughtsTokenCount");
    }
    result.provider_metadata = JsonValue{{"provider", "gemini"}, {"response", body}};
    return result;
}

}  // namespace

class GeminiClient::Impl {
   public:
    Impl(GeminiConfig config, const ClientOptions& options)
        : config_(std::move(config)),
          endpoint_(
              provider_detail::join_url(config_.base_url, gemini_model_path(config_.model.id))),
          transport_(provider_detail::resolve_transport(options)),
          api_key_(provider_detail::resolve_required_credential(
              config_.api_key, {"GEMINI_API_KEY", "GOOGLE_API_KEY"}, "Gemini")) {
        provider_detail::validate_provider(config_.model, Provider::gemini);
        config_.api_key.reset();
    }

    [[nodiscard]] AIResponse generate(const AIRequest& request) const {
        HttpRequest http_request;
        http_request.method = HttpMethod::post;
        http_request.url = endpoint_;
        http_request.headers = {
            {"x-goog-api-key", api_key_},
            {"Content-Type", "application/json"},
        };
        provider_detail::apply_extra_headers(http_request.headers, config_.extra_headers);
        http_request.body = make_gemini_request(config_, request).dump();
        const auto sensitive_values = provider_detail::collect_sensitive_values(
            http_request.headers, {api_key_});
        try {
            HttpResponse response = transport_->send(http_request);
            provider_detail::redact_sensitive_response(
                response, sensitive_values);
            return parse_gemini_response(response, request);
        } catch (const ProviderError& error) {
            provider_detail::throw_redacted_provider_error(
                error, sensitive_values);
        }
    }

   private:
    GeminiConfig config_;
    std::string endpoint_;
    std::shared_ptr<HttpTransport> transport_;
    std::string api_key_;
};

GeminiConfig::GeminiConfig(std::string model_id)
    : model(provider_detail::make_model_descriptor(Provider::gemini, std::move(model_id))) {}

GeminiClient::GeminiClient(GeminiConfig config, const ClientOptions& options)
    : AIClient(config.model, options),
      impl_(std::make_unique<Impl>(std::move(config), options)) {}

GeminiClient::~GeminiClient() = default;

AIResponse GeminiClient::generate_once(const AIRequest& request) {
    return impl_->generate(request);
}

std::unique_ptr<AIClient> neuralplus::make_client(GeminiConfig config,
                                                  const ClientOptions& options) {
    return std::make_unique<GeminiClient>(std::move(config), options);
}
