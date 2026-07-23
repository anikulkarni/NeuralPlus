// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include <utility>

#include "../provider_common.hpp"

using namespace neuralplus;

namespace {

[[nodiscard]] bool has_native_compatible_message(const Message& message) {
    const auto& metadata = message.provider_metadata();
    return metadata.is_object() &&
           provider_detail::string_field(metadata, "provider") == "openai_compatible" &&
           metadata.contains("message") && metadata["message"].is_object();
}

[[nodiscard]] JsonValue compatible_content_part(const Content& content) {
    if (content.type() == ContentType::text && content.source() == ContentSource::value) {
        return JsonValue{{"type", "text"}, {"text", content.value()}};
    }

    if (content.type() == ContentType::image) {
        std::string url;
        if (content.source() == ContentSource::url) {
            url = content.url();
        } else if (content.source() == ContentSource::bytes) {
            url = provider_detail::data_url(content);
        } else {
            provider_detail::throw_unsupported_content(Provider::openai_compatible, content);
        }
        JsonValue image_url{{"url", std::move(url)}};
        if (content.options().is_object()) {
            const auto detail = content.options().find("detail");
            if (detail != content.options().end() && detail->is_string()) {
                image_url["detail"] = *detail;
            }
        }
        return JsonValue{{"type", "image_url"}, {"image_url", std::move(image_url)}};
    }

    if (content.type() == ContentType::audio &&
        content.source() == ContentSource::bytes) {
        return JsonValue{
            {"type", "input_audio"},
            {"input_audio",
             JsonValue{
                 {"data",
                  provider_detail::base64_encode(content.bytes())},
                 {"format",
                  provider_detail::chat_audio_format(
                      content, Provider::openai_compatible)}}}};
    }

    if (content.type() == ContentType::file &&
        content.source() == ContentSource::bytes) {
        JsonValue file{
            {"file_data",
             provider_detail::base64_encode(content.bytes())},
        };
        if (!content.filename().empty()) {
            file["filename"] = content.filename();
        }
        return JsonValue{
            {"type", "file"},
            {"file", std::move(file)},
        };
    }

    if (content.type() == ContentType::extension &&
        content.source() == ContentSource::provider_data &&
        content.provider() == "openai_compatible") {
        return content.provider_data();
    }

    provider_detail::throw_unsupported_content(Provider::openai_compatible, content);
}

[[nodiscard]] JsonValue compatible_message_content(const Message& message) {
    if (message.contents().size() == 1U && message.contents().front().type() == ContentType::text &&
        message.contents().front().source() == ContentSource::value) {
        return message.contents().front().value();
    }
    JsonValue content = JsonValue::array();
    for (const auto& part : message.contents()) {
        content.push_back(compatible_content_part(part));
    }
    return content;
}

[[nodiscard]] std::string compatible_system_instruction(const AIRequest& request) {
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

[[nodiscard]] std::string compatible_tool_content(
    const Message& message) {
    std::string result;
    for (const Content& content : message.contents()) {
        if (content.type() != ContentType::text ||
            content.source() != ContentSource::value) {
            provider_detail::throw_unsupported_content(
                Provider::openai_compatible, content);
        }
        result += content.value();
    }
    return result;
}

void append_compatible_history(JsonValue& messages, const Message& message) {
    if (message.role() == Role::system) {
        return;
    }
    if (message.role() == Role::assistant && has_native_compatible_message(message)) {
        JsonValue native_message = message.provider_metadata()["message"];
        const auto audio = native_message.find("audio");
        if (audio != native_message.end() && audio->is_object()) {
            const std::string id =
                provider_detail::string_field(*audio, "id");
            if (id.empty()) {
                throw ProviderError(
                    "OpenAI-compatible audio history is missing its id",
                    Provider::openai_compatible,
                    0,
                    "malformed_response");
            }
            // A response contains audio data/transcript/expiry, but follow-up
            // Chat Completions requests accept only {"audio":{"id":"..."}}.
            // https://developers.openai.com/api/reference/resources/chat
            native_message["audio"] = JsonValue{{"id", id}};
        }
        messages.push_back(std::move(native_message));
        return;
    }
    if (message.role() == Role::tool) {
        const JsonValue& metadata = message.provider_metadata();
        const auto legacy_field =
            metadata.is_object()
                ? metadata.find("legacy_function_call")
                : metadata.end();
        const bool legacy =
            metadata.is_object() &&
            legacy_field != metadata.end() &&
            legacy_field->is_boolean() &&
            legacy_field->get<bool>();
        if (legacy) {
            messages.push_back(
                JsonValue{{"role", "function"},
                          {"name", message.tool_name()},
                          {"content", compatible_tool_content(message)}});
        } else {
            messages.push_back(
                JsonValue{{"role", "tool"},
                          {"tool_call_id", message.tool_call_id()},
                          {"content", compatible_tool_content(message)}});
        }
        return;
    }

    JsonValue native_message{{"role", message.role() == Role::assistant ? "assistant" : "user"},
                             {"content", compatible_message_content(message)}};
    if (message.role() == Role::assistant && !message.tool_calls().empty()) {
        JsonValue tool_calls = JsonValue::array();
        for (const auto& call : message.tool_calls()) {
            tool_calls.push_back(JsonValue{
                {"id", call.id},
                {"type", "function"},
                {"function", JsonValue{{"name", call.name}, {"arguments", call.raw_arguments}}}});
        }
        native_message["tool_calls"] = std::move(tool_calls);
    }
    messages.push_back(std::move(native_message));
}

[[nodiscard]] JsonValue make_compatible_request(const OpenAICompatibleConfig& config,
                                                const AIRequest& request) {
    JsonValue body = provider_detail::merged_provider_options(config.model.provider_options,
                                                              request.options.provider_options);
    body["model"] = config.model.id;
    body["stream"] = false;

    JsonValue messages = JsonValue::array();
    const auto system = compatible_system_instruction(request);
    if (!system.empty()) {
        messages.push_back(JsonValue{{"role", "system"}, {"content", system}});
    }
    for (const auto& message : request.messages) {
        append_compatible_history(messages, message);
    }
    body["messages"] = std::move(messages);

    const auto configured_tools = body.find("tools");
    if (configured_tools != body.end() && !configured_tools->is_array()) {
        throw ConfigurationError(
            "OpenAI-compatible tools provider option must be an array");
    }
    if (!request.tools.empty()) {
        JsonValue tools = configured_tools == body.end()
                              ? JsonValue::array()
                              : *configured_tools;
        for (const auto& tool : request.tools) {
            JsonValue declaration = tool.provider_options;
            declaration["name"] = tool.name;
            declaration["description"] = tool.description;
            declaration["parameters"] = tool.input_schema;
            tools.push_back(
                JsonValue{{"type", "function"},
                          {"function", std::move(declaration)}});
        }
        body["tools"] = std::move(tools);
    }
    if (request.options.max_output_tokens.has_value()) {
        body["max_tokens"] = *request.options.max_output_tokens;
    }
    if (request.options.temperature.has_value()) {
        body["temperature"] = *request.options.temperature;
    }
    return body;
}

[[nodiscard]] std::string compatible_endpoint(const std::string& base_url) {
    static constexpr std::string_view suffix{"/chat/completions"};
    std::string normalized = base_url;
    while (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    if (normalized.empty()) {
        throw ConfigurationError("provider base URL cannot be empty");
    }
    if (normalized.size() >= suffix.size() &&
        normalized.compare(normalized.size() - suffix.size(), suffix.size(), suffix.data(),
                           suffix.size()) == 0) {
        return normalized;
    }
    return provider_detail::join_url(std::move(normalized), suffix);
}

[[noreturn]] void throw_compatible_http_error(const HttpResponse& response,
                                              std::string request_id) {
    const JsonValue parsed = JsonValue::parse(response.body, nullptr, false);
    std::string message;
    std::string code;
    if (!parsed.is_discarded() && parsed.is_object()) {
        const auto error = parsed.find("error");
        if (error != parsed.end() && error->is_object()) {
            message = provider_detail::string_field(*error, "message");
            code = provider_detail::string_field(*error, "code");
            if (code.empty()) {
                code = provider_detail::string_field(*error, "type");
            }
        }
    }
    provider_detail::throw_provider_error(Provider::openai_compatible, response, std::move(message),
                                          std::move(code), std::move(request_id));
}

[[nodiscard]] FinishReason compatible_finish_reason(std::string_view reason, bool has_tool_calls,
                                                    bool has_refusal) {
    if (has_tool_calls || reason == "tool_calls" || reason == "function_call") {
        return FinishReason::tool_calls;
    }
    if (has_refusal) {
        return FinishReason::refusal;
    }
    if (reason == "stop") {
        return FinishReason::stop;
    }
    if (reason == "length") {
        return FinishReason::length;
    }
    if (reason == "content_filter") {
        return FinishReason::content_filter;
    }
    return FinishReason::unknown;
}

[[nodiscard]] AIResponse parse_compatible_response(
    const HttpResponse& response, const AIRequest& request) {
    std::string request_id = provider_detail::response_request_id(response, {"x-request-id"});
    if (!provider_detail::successful_status(response.status)) {
        throw_compatible_http_error(response, std::move(request_id));
    }

    const JsonValue body =
        provider_detail::parse_response_json(response, Provider::openai_compatible, request_id);
    if (request_id.empty()) {
        request_id = provider_detail::string_field(body, "id");
    }
    const auto choices = body.find("choices");
    if (choices == body.end() || !choices->is_array() || choices->empty() ||
        !choices->front().is_object()) {
        throw ProviderError(
            "OpenAI-compatible response is missing a valid choices array",
            Provider::openai_compatible,
            response.status,
            "malformed_response",
            request_id);
    }
    const JsonValue choice = choices->front();
    const auto choice_message = choice.find("message");
    if (choice_message == choice.end() || !choice_message->is_object()) {
        throw ProviderError(
            "OpenAI-compatible response choice is missing the message object",
            Provider::openai_compatible,
            response.status,
            "malformed_response",
            request_id);
    }
    const JsonValue native_message = *choice_message;

    std::vector<Content> contents;
    const auto content = native_message.find("content");
    if (content != native_message.end() && content->is_string()) {
        contents.push_back(Content::text(content->get<std::string>()));
    } else if (content != native_message.end() && content->is_array()) {
        for (const auto& part : *content) {
            if (!part.is_object()) {
                throw ProviderError(
                    "OpenAI-compatible message content parts must be objects",
                    Provider::openai_compatible,
                    response.status,
                    "malformed_response",
                    request_id);
            }
            const auto native_type = part.find("type");
            if (native_type != part.end() && !native_type->is_string()) {
                throw ProviderError(
                    "OpenAI-compatible content part type must be a string",
                    Provider::openai_compatible,
                    response.status,
                    "malformed_response",
                    request_id);
            }
            const auto type = provider_detail::string_field(part, "type");
            if (type == "text") {
                const auto text = part.find("text");
                if (text == part.end() || !text->is_string()) {
                    throw ProviderError(
                        "OpenAI-compatible text content requires string text",
                        Provider::openai_compatible,
                        response.status,
                        "malformed_response",
                        request_id);
                }
                contents.push_back(Content::text(text->get<std::string>()));
            } else if (type == "image_url") {
                const auto image = part.find("image_url");
                if (image == part.end() || !image->is_object()) {
                    throw ProviderError(
                        "OpenAI-compatible image_url content requires an object",
                        Provider::openai_compatible,
                        response.status,
                        "malformed_response",
                        request_id);
                }
                const auto url = image->find("url");
                if (url == image->end() || !url->is_string() ||
                    url->get_ref<const std::string&>().empty()) {
                    throw ProviderError(
                        "OpenAI-compatible image_url content requires a string URL",
                        Provider::openai_compatible,
                        response.status,
                        "malformed_response",
                        request_id);
                }
                contents.push_back(Content::image_url(url->get<std::string>()));
            } else {
                contents.push_back(Content::extension("openai_compatible", part));
            }
        }
    } else if (content != native_message.end() && !content->is_null()) {
        throw ProviderError(
            "OpenAI-compatible message content must be a string, array, or null",
            Provider::openai_compatible,
            response.status,
            "malformed_response",
            request_id);
    }

    std::vector<ToolCall> calls;

    // https://developers.openai.com/api/reference/resources/chat/completions/methods/create
    // {"tool_calls":[{"id":"c1","function":{"arguments":"{\"x\":1}"}}]}
    // becomes a ToolCall while the native assistant message is retained.
    const auto tool_calls = native_message.find("tool_calls");
    if (tool_calls != native_message.end() && !tool_calls->is_null()) {
        if (!tool_calls->is_array()) {
            throw ProviderError(
                "OpenAI-compatible message tool_calls must be an array or null",
                Provider::openai_compatible,
                response.status,
                "malformed_response",
                request_id);
        }
        for (const auto& native_call : *tool_calls) {
            if (!native_call.is_object()) {
                throw ProviderError(
                    "OpenAI-compatible tool_calls entries must be objects",
                    Provider::openai_compatible,
                    response.status,
                    "malformed_response",
                    request_id);
            }

            const auto id = native_call.find("id");
            const auto type = native_call.find("type");
            const auto function = native_call.find("function");
            if (id == native_call.end() || !id->is_string() ||
                id->get_ref<const std::string&>().empty() ||
                type == native_call.end() || !type->is_string() ||
                function == native_call.end() || !function->is_object()) {
                throw ProviderError(
                    "OpenAI-compatible tool call requires a non-empty string "
                    "id, string type, and function object",
                    Provider::openai_compatible,
                    response.status,
                    "malformed_response",
                    request_id);
            }
            if (type->get_ref<const std::string&>() != "function") {
                throw ProviderError(
                    "OpenAI-compatible tool call type is not supported: " +
                        type->get<std::string>(),
                    Provider::openai_compatible,
                    response.status,
                    "unsupported_tool_call",
                    request_id);
            }

            const auto name = function->find("name");
            const auto arguments = function->find("arguments");
            if (name == function->end() || !name->is_string() ||
                name->get_ref<const std::string&>().empty() ||
                arguments == function->end() || !arguments->is_string()) {
                throw ProviderError(
                    "OpenAI-compatible function tool call requires a "
                    "non-empty string name and string arguments",
                    Provider::openai_compatible,
                    response.status,
                    "malformed_response",
                    request_id);
            }

            ToolCall call;
            call.id = id->get<std::string>();
            call.name = name->get<std::string>();
            call.raw_arguments = arguments->get<std::string>();
            call.arguments = provider_detail::parse_tool_arguments(
                call.raw_arguments, call.arguments_valid);
            call.provider_metadata = native_call;
            calls.push_back(std::move(call));
        }
    }

    const auto legacy_function_call =
        native_message.find("function_call");
    if (legacy_function_call != native_message.end() &&
        !legacy_function_call->is_null()) {
        if (!legacy_function_call->is_object()) {
            throw ProviderError(
                "OpenAI-compatible message function_call must be an object or null",
                Provider::openai_compatible,
                response.status,
                "malformed_response",
                request_id);
        }

        const auto name = legacy_function_call->find("name");
        const auto arguments = legacy_function_call->find("arguments");
        if (name == legacy_function_call->end() || !name->is_string() ||
            name->get_ref<const std::string&>().empty() ||
            arguments == legacy_function_call->end() ||
            !arguments->is_string()) {
            throw ProviderError(
                "OpenAI-compatible legacy function_call requires a non-empty "
                "string name and string arguments",
                Provider::openai_compatible,
                response.status,
                "malformed_response",
                request_id);
        }

        if (calls.empty()) {
            ToolCall call;
            call.id = "legacy_function_call_" + request.run_id + "_" +
                      std::to_string(request.messages.size());
            call.name = name->get<std::string>();
            call.raw_arguments = arguments->get<std::string>();
            call.arguments = provider_detail::parse_tool_arguments(
                call.raw_arguments, call.arguments_valid);
            call.provider_metadata =
                JsonValue{{"legacy_function_call", true},
                          {"function_call", *legacy_function_call}};
            calls.push_back(std::move(call));
        }
    }

    const bool has_refusal =
        native_message.contains("refusal") && !native_message["refusal"].is_null();
    if (has_refusal && contents.empty() && native_message["refusal"].is_string()) {
        contents.push_back(Content::text(native_message["refusal"].get<std::string>()));
    }

    const auto audio = native_message.find("audio");
    if (audio != native_message.end() && !audio->is_null()) {
        if (!audio->is_object()) {
            throw ProviderError(
                "OpenAI-compatible message audio must be an object or null",
                Provider::openai_compatible,
                response.status,
                "malformed_response",
                request_id);
        }
        const auto data = audio->find("data");
        const auto id = audio->find("id");
        const auto expires_at = audio->find("expires_at");
        const auto transcript = audio->find("transcript");
        if (id == audio->end() || !id->is_string() ||
            id->get_ref<const std::string&>().empty() ||
            data == audio->end() || !data->is_string() ||
            (expires_at != audio->end() &&
             !expires_at->is_number_integer() &&
             !expires_at->is_number_unsigned()) ||
            (transcript != audio->end() && !transcript->is_string())) {
            throw ProviderError(
                "OpenAI-compatible message audio has invalid id, data, expiry, or transcript",
                Provider::openai_compatible,
                response.status,
                "malformed_response",
                request_id);
        }
        const auto decoded = provider_detail::base64_decode(
            data->get_ref<const std::string&>());
        if (!decoded.has_value() || decoded->empty()) {
            throw ProviderError(
                "OpenAI-compatible message audio contains malformed base64",
                Provider::openai_compatible,
                response.status,
                "malformed_audio",
                request_id);
        }
        if (contents.empty() && transcript != audio->end() &&
            !transcript->get_ref<const std::string&>().empty()) {
            contents.push_back(
                Content::text(transcript->get<std::string>()));
        }
        JsonValue options = *audio;
        options.erase("data");
        contents.push_back(Content::audio_bytes(
            *decoded,
            "application/octet-stream",
            std::move(options)));
    }

    Message message = Message::assistant(std::move(contents), std::move(calls));
    message.set_provider_metadata(
        JsonValue{{"provider", "openai_compatible"}, {"message", native_message}});
    AIResponse result(std::move(message));
    result.finish_reason =
        compatible_finish_reason(provider_detail::string_field(choice, "finish_reason"),
                                 !result.message.tool_calls().empty(), has_refusal);
    result.provider_model = provider_detail::string_field(body, "model");
    result.provider_request_id = std::move(request_id);

    const auto usage = body.find("usage");
    if (usage != body.end() && usage->is_object()) {
        result.usage.input_tokens = provider_detail::size_field(*usage, "prompt_tokens");
        result.usage.output_tokens = provider_detail::size_field(*usage, "completion_tokens");
        result.usage.total_tokens = provider_detail::size_field(*usage, "total_tokens");
        const auto details = usage->find("completion_tokens_details");
        if (details != usage->end() && details->is_object()) {
            result.usage.reasoning_tokens =
                provider_detail::size_field(*details, "reasoning_tokens");
        }
        const auto prompt_details = usage->find("prompt_tokens_details");
        if (prompt_details != usage->end() && prompt_details->is_object()) {
            result.usage.cached_input_tokens =
                provider_detail::size_field(*prompt_details, "cached_tokens");
        }
    }
    result.provider_metadata = JsonValue{{"provider", "openai_compatible"}, {"response", body}};
    return result;
}

}  // namespace

class OpenAICompatibleClient::Impl {
   public:
    Impl(OpenAICompatibleConfig config, const ClientOptions& options)
        : config_(std::move(config)),
          endpoint_(compatible_endpoint(config_.base_url)),
          transport_(provider_detail::resolve_transport(options)),
          api_key_(provider_detail::resolve_optional_credential(config_.api_key,
                                                                config_.api_key_environment)) {
        provider_detail::validate_provider(config_.model, Provider::openai_compatible);
        config_.api_key.reset();
    }

    [[nodiscard]] AIResponse generate(const AIRequest& request) const {
        HttpRequest http_request;
        http_request.method = HttpMethod::post;
        http_request.url = endpoint_;
        http_request.headers = {{"Content-Type", "application/json"}};
        if (api_key_.has_value()) {
            provider_detail::set_header(http_request.headers, "Authorization",
                                        "Bearer " + *api_key_);
        }
        provider_detail::apply_extra_headers(http_request.headers, config_.extra_headers);
        http_request.body = make_compatible_request(config_, request).dump();
        const std::string_view configured_secret =
            api_key_.has_value() ? std::string_view(*api_key_)
                                 : std::string_view{};
        const auto sensitive_values = provider_detail::collect_sensitive_values(
            http_request.headers, {configured_secret});
        try {
            HttpResponse response = transport_->send(http_request);
            provider_detail::redact_sensitive_response(
                response, sensitive_values);
            return parse_compatible_response(response, request);
        } catch (const ProviderError& error) {
            provider_detail::throw_redacted_provider_error(
                error, sensitive_values);
        }
    }

   private:
    OpenAICompatibleConfig config_;
    std::string endpoint_;
    std::shared_ptr<HttpTransport> transport_;
    std::optional<std::string> api_key_;
};

OpenAICompatibleConfig::OpenAICompatibleConfig(std::string model_id,
                                               std::string compatible_base_url)
    : model(
          provider_detail::make_model_descriptor(Provider::openai_compatible, std::move(model_id))),
      base_url(std::move(compatible_base_url)) {}

OpenAICompatibleClient::OpenAICompatibleClient(OpenAICompatibleConfig config,
                                               const ClientOptions& options)
    : AIClient(config.model, options),
      impl_(std::make_unique<Impl>(std::move(config), options)) {}

OpenAICompatibleClient::~OpenAICompatibleClient() = default;

AIResponse OpenAICompatibleClient::generate_once(const AIRequest& request) {
    return impl_->generate(request);
}

std::unique_ptr<AIClient> neuralplus::make_client(OpenAICompatibleConfig config,
                                                  const ClientOptions& options) {
    return std::make_unique<OpenAICompatibleClient>(std::move(config), options);
}
