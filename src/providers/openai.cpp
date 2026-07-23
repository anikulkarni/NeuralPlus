// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include <utility>

#include "../provider_common.hpp"

using namespace neuralplus;

namespace {

[[nodiscard]] bool has_native_openai_output(const Message& message) {
    const auto& metadata = message.provider_metadata();
    return metadata.is_object() &&
           provider_detail::string_field(metadata, "provider") == "openai" &&
           metadata.contains("output") && metadata["output"].is_array();
}

[[nodiscard]] JsonValue openai_input_part(const Content& content) {
    if (content.type() == ContentType::text && content.source() == ContentSource::value) {
        return JsonValue{{"type", "input_text"}, {"text", content.value()}};
    }

    if (content.type() == ContentType::image) {
        JsonValue part{{"type", "input_image"}};
        if (content.source() == ContentSource::url) {
            part["image_url"] = content.url();
        } else if (content.source() == ContentSource::bytes) {
            part["image_url"] = provider_detail::data_url(content);
        } else {
            provider_detail::throw_unsupported_content(Provider::openai, content);
        }
        if (content.options().is_object()) {
            const auto detail = content.options().find("detail");
            if (detail != content.options().end() && detail->is_string()) {
                part["detail"] = *detail;
            }
        }
        return part;
    }

    if (content.type() == ContentType::file) {
        JsonValue part{{"type", "input_file"}};
        if (content.source() == ContentSource::url) {
            part["file_url"] = content.url();
        } else if (content.source() == ContentSource::bytes) {
            // OpenAI file inputs use a complete data URL rather than bare
            // Base64: data:application/pdf;base64,JVBERi0x...
            // https://developers.openai.com/api/docs/guides/file-inputs
            part["file_data"] = provider_detail::data_url(content);
        } else {
            provider_detail::throw_unsupported_content(
                Provider::openai, content);
        }
        if (!content.filename().empty()) {
            part["filename"] = content.filename();
        }
        return part;
    }

    if (content.type() == ContentType::extension &&
        content.source() == ContentSource::provider_data && content.provider() == "openai") {
        return content.provider_data();
    }

    provider_detail::throw_unsupported_content(Provider::openai, content);
}

[[nodiscard]] JsonValue openai_message_content(const Message& message) {
    JsonValue content = JsonValue::array();
    for (const auto& part : message.contents()) {
        content.push_back(openai_input_part(part));
    }
    return content;
}

[[nodiscard]] JsonValue openai_tool_output(const Message& message) {
    if (message.contents().empty()) {
        return "";
    }
    if (message.contents().size() == 1U && message.contents().front().type() == ContentType::text &&
        message.contents().front().source() == ContentSource::value) {
        return message.contents().front().value();
    }
    return openai_message_content(message);
}

void append_openai_history(JsonValue& input, const Message& message) {
    if (message.role() == Role::assistant && has_native_openai_output(message)) {
        for (const auto& item : message.provider_metadata()["output"]) {
            input.push_back(item);
        }
        return;
    }

    if (message.role() == Role::tool) {
        input.push_back(JsonValue{{"type", "function_call_output"},
                                  {"call_id", message.tool_call_id()},
                                  {"output", openai_tool_output(message)}});
        return;
    }

    if (message.role() == Role::assistant) {
        if (!message.contents().empty()) {
            input.push_back(
                JsonValue{{"role", "assistant"}, {"content", openai_message_content(message)}});
        }
        for (const auto& call : message.tool_calls()) {
            JsonValue item{{"type", "function_call"},
                           {"call_id", call.id},
                           {"name", call.name},
                           {"arguments", call.raw_arguments}};
            if (!call.provider_metadata.is_object() || !call.provider_metadata.contains("id")) {
                input.push_back(std::move(item));
                continue;
            }
            const auto& id = call.provider_metadata["id"];
            if (id.is_string()) {
                item["id"] = id;
            }
            input.push_back(std::move(item));
        }
        return;
    }

    const char* role = message.role() == Role::system ? "developer" : "user";
    input.push_back(JsonValue{{"role", role}, {"content", openai_message_content(message)}});
}

[[nodiscard]] JsonValue make_openai_request(const OpenAIConfig& config, const AIRequest& request) {
    JsonValue body = provider_detail::merged_provider_options(config.model.provider_options,
                                                              request.options.provider_options);
    body["model"] = config.model.id;
    // AIClient::generate is a synchronous request/response API. Streaming and
    // background Responses have different wire lifecycles, so provider
    // options cannot switch those modes on.
    // https://developers.openai.com/api/reference/resources/responses/methods/create
    body["stream"] = false;
    body["background"] = false;

    JsonValue input = JsonValue::array();
    for (const auto& message : request.messages) {
        append_openai_history(input, message);
    }
    body["input"] = std::move(input);

    if (request.system_message.has_value()) {
        body["instructions"] = *request.system_message;
    }
    const auto configured_tools = body.find("tools");
    if (configured_tools != body.end() && !configured_tools->is_array()) {
        throw ConfigurationError(
            "OpenAI tools provider option must be an array");
    }
    if (!request.tools.empty()) {
        JsonValue tools = configured_tools == body.end()
                              ? JsonValue::array()
                              : *configured_tools;
        for (const auto& tool : request.tools) {
            JsonValue declaration = tool.provider_options;
            declaration["type"] = "function";
            declaration["name"] = tool.name;
            declaration["description"] = tool.description;
            declaration["parameters"] = tool.input_schema;
            tools.push_back(std::move(declaration));
        }
        body["tools"] = std::move(tools);
    }
    if (request.options.max_output_tokens.has_value()) {
        body["max_output_tokens"] = *request.options.max_output_tokens;
    }
    if (request.options.temperature.has_value()) {
        body["temperature"] = *request.options.temperature;
    }
    return body;
}

[[noreturn]] void throw_openai_http_error(const HttpResponse& response,
                                          const std::string& request_id) {
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
    provider_detail::throw_provider_error(Provider::openai, response, std::move(message),
                                          std::move(code), request_id);
}

[[nodiscard]] FinishReason openai_finish_reason(const JsonValue& body, bool has_tool_calls,
                                                bool has_refusal) {
    if (has_tool_calls) {
        return FinishReason::tool_calls;
    }
    if (has_refusal) {
        return FinishReason::refusal;
    }

    const auto status = provider_detail::string_field(body, "status");
    if (status == "failed") {
        return FinishReason::error;
    }
    if (status == "completed") {
        return FinishReason::stop;
    }
    if (status == "incomplete") {
        const auto details = body.find("incomplete_details");
        if (details != body.end() && details->is_object()) {
            const auto reason = provider_detail::string_field(*details, "reason");
            if (reason == "max_output_tokens") {
                return FinishReason::length;
            }
            if (reason == "content_filter") {
                return FinishReason::content_filter;
            }
        }
    }
    return FinishReason::unknown;
}

[[nodiscard]] std::string openai_image_media_type(
    const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() >= 4U && bytes[0] == 0x89U &&
        bytes[1] == 0x50U && bytes[2] == 0x4EU &&
        bytes[3] == 0x47U) {
        return "image/png";
    }
    if (bytes.size() >= 3U && bytes[0] == 0xFFU &&
        bytes[1] == 0xD8U && bytes[2] == 0xFFU) {
        return "image/jpeg";
    }
    if (bytes.size() >= 12U && bytes[0] == 'R' &&
        bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
        bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' &&
        bytes[11] == 'P') {
        return "image/webp";
    }
    return "application/octet-stream";
}

[[nodiscard]] AIResponse parse_openai_response(const HttpResponse& response) {
    std::string request_id = provider_detail::response_request_id(response, {"x-request-id"});
    if (!provider_detail::successful_status(response.status)) {
        throw_openai_http_error(response, request_id);
    }

    const JsonValue body =
        provider_detail::parse_response_json(response, Provider::openai, request_id);
    if (request_id.empty()) {
        request_id = provider_detail::string_field(body, "id");
    }
    const auto output_iterator = body.find("output");
    if (output_iterator == body.end() || !output_iterator->is_array()) {
        throw ProviderError(
            "OpenAI response is missing the output array",
            Provider::openai,
            response.status,
            "malformed_response",
            request_id);
    }

    std::vector<Content> contents;
    std::vector<ToolCall> calls;
    bool has_refusal = false;
    const JsonValue output = *output_iterator;

    // https://developers.openai.com/api/reference/resources/responses/methods/create
    // {"type":"function_call","call_id":"c1","arguments":"{\"x\":1}"}
    // becomes ToolCall{"c1","...",{"x":1}} while the entire output array is
    // retained on Message for reasoning-item and tool-call continuation replay.
    for (const auto& item : output) {
        if (!item.is_object()) {
            throw ProviderError(
                "OpenAI response output items must be objects",
                Provider::openai,
                response.status,
                "malformed_response",
                request_id);
        }
        const auto native_type = item.find("type");
        if (native_type != item.end() && !native_type->is_string()) {
            throw ProviderError(
                "OpenAI response output item type must be a string",
                Provider::openai,
                response.status,
                "malformed_response",
                request_id);
        }
        const auto type = provider_detail::string_field(item, "type");
        if (type == "message") {
            const auto native_content = item.find("content");
            if (native_content == item.end() || !native_content->is_array()) {
                throw ProviderError(
                    "OpenAI response message content must be an array",
                    Provider::openai,
                    response.status,
                    "malformed_response",
                    request_id);
            }
            for (const auto& part : *native_content) {
                if (!part.is_object()) {
                    throw ProviderError(
                        "OpenAI response message content parts must be objects",
                        Provider::openai,
                        response.status,
                        "malformed_response",
                        request_id);
                }
                const auto native_part_type = part.find("type");
                if (native_part_type != part.end() &&
                    !native_part_type->is_string()) {
                    throw ProviderError(
                        "OpenAI response message content part type must be a string",
                        Provider::openai,
                        response.status,
                        "malformed_response",
                        request_id);
                }
                const auto part_type = provider_detail::string_field(part, "type");
                if (part_type == "output_text") {
                    const auto text = part.find("text");
                    if (text == part.end() || !text->is_string()) {
                        throw ProviderError(
                            "OpenAI output_text content requires string text",
                            Provider::openai,
                            response.status,
                            "malformed_response",
                            request_id);
                    }
                    contents.push_back(Content::text(text->get<std::string>()));
                } else if (part_type == "refusal") {
                    const auto refusal = part.find("refusal");
                    if (refusal == part.end() || !refusal->is_string()) {
                        throw ProviderError(
                            "OpenAI refusal content requires a string refusal",
                            Provider::openai,
                            response.status,
                            "malformed_response",
                            request_id);
                    }
                    has_refusal = true;
                    contents.push_back(Content::text(refusal->get<std::string>()));
                } else {
                    contents.push_back(
                        Content::extension("openai", part));
                }
            }
            continue;
        }
        if (type == "image_generation_call") {
            const auto result = item.find("result");
            if (result == item.end() || !result->is_string()) {
                throw ProviderError(
                    "OpenAI image generation output requires a string result",
                    Provider::openai,
                    response.status,
                    "malformed_response",
                    request_id);
            }
            const std::string encoded = result->get<std::string>();
            const auto decoded =
                provider_detail::base64_decode(encoded);
            if (!decoded.has_value() || decoded->empty()) {
                throw ProviderError(
                    "OpenAI returned malformed base64 image data",
                    Provider::openai,
                    response.status,
                    "malformed_image",
                    request_id);
            }
            contents.push_back(
                Content::image_bytes(
                    *decoded, openai_image_media_type(*decoded)));
            continue;
        }
        if (type != "function_call") {
            continue;
        }

        const auto call_id = item.find("call_id");
        const auto name = item.find("name");
        const auto arguments = item.find("arguments");
        if (call_id == item.end() || !call_id->is_string() ||
            call_id->get_ref<const std::string&>().empty() ||
            name == item.end() || !name->is_string() ||
            name->get_ref<const std::string&>().empty() ||
            arguments == item.end() || !arguments->is_string()) {
            throw ProviderError(
                "OpenAI function_call requires non-empty string call_id and "
                "name fields and a string arguments field",
                Provider::openai,
                response.status,
                "malformed_response",
                request_id);
        }

        const auto id = item.find("id");
        if (id != item.end() && !id->is_string()) {
            throw ProviderError(
                "OpenAI function_call id must be a string when present",
                Provider::openai,
                response.status,
                "malformed_response",
                request_id);
        }

        ToolCall call;
        call.id = call_id->get<std::string>();
        call.name = name->get<std::string>();
        call.raw_arguments = arguments->get<std::string>();
        call.arguments =
            provider_detail::parse_tool_arguments(call.raw_arguments, call.arguments_valid);
        call.provider_metadata = item;
        calls.push_back(std::move(call));
    }

    if (contents.empty()) {
        const auto error = body.find("error");
        if (error != body.end() && error->is_object()) {
            const auto message = provider_detail::string_field(*error, "message");
            if (!message.empty()) {
                contents.push_back(Content::text(message));
            }
        }
    }

    Message message = Message::assistant(std::move(contents), std::move(calls));
    message.set_provider_metadata(JsonValue{{"provider", "openai"}, {"output", output}});
    AIResponse result(std::move(message));
    result.finish_reason =
        openai_finish_reason(body, !result.message.tool_calls().empty(), has_refusal);
    result.provider_model = provider_detail::string_field(body, "model");
    result.provider_request_id = std::move(request_id);

    const auto usage = body.find("usage");
    if (usage != body.end() && usage->is_object()) {
        result.usage.input_tokens = provider_detail::size_field(*usage, "input_tokens");
        result.usage.output_tokens = provider_detail::size_field(*usage, "output_tokens");
        result.usage.total_tokens = provider_detail::size_field(*usage, "total_tokens");
        const auto input_details = usage->find("input_tokens_details");
        if (input_details != usage->end() && input_details->is_object()) {
            result.usage.cached_input_tokens =
                provider_detail::size_field(*input_details, "cached_tokens");
        }
        const auto output_details = usage->find("output_tokens_details");
        if (output_details != usage->end() && output_details->is_object()) {
            result.usage.reasoning_tokens =
                provider_detail::size_field(*output_details, "reasoning_tokens");
        }
    }
    result.provider_metadata = JsonValue{{"provider", "openai"}, {"response", body}};
    return result;
}

}  // namespace

class OpenAIClient::Impl {
   public:
    Impl(OpenAIConfig config, const ClientOptions& options)
        : config_(std::move(config)),
          endpoint_(provider_detail::join_url(config_.base_url, "/responses")),
          transport_(provider_detail::resolve_transport(options)),
          api_key_(provider_detail::resolve_required_credential(config_.api_key, {"OPENAI_API_KEY"},
                                                                "OpenAI")) {
        provider_detail::validate_provider(config_.model, Provider::openai);
        config_.api_key.reset();
    }

    [[nodiscard]] AIResponse generate(const AIRequest& request) const {
        HttpRequest http_request;
        http_request.method = HttpMethod::post;
        http_request.url = endpoint_;
        http_request.headers = {
            {"Authorization", "Bearer " + api_key_},
            {"Content-Type", "application/json"},
        };
        if (!config_.organization.empty()) {
            provider_detail::set_header(http_request.headers, "OpenAI-Organization",
                                        config_.organization);
        }
        if (!config_.project.empty()) {
            provider_detail::set_header(http_request.headers, "OpenAI-Project", config_.project);
        }
        provider_detail::apply_extra_headers(http_request.headers, config_.extra_headers);
        http_request.body = make_openai_request(config_, request).dump();
        const auto sensitive_values = provider_detail::collect_sensitive_values(
            http_request.headers, {api_key_});
        try {
            HttpResponse response = transport_->send(http_request);
            provider_detail::redact_sensitive_response(
                response, sensitive_values);
            return parse_openai_response(response);
        } catch (const ProviderError& error) {
            provider_detail::throw_redacted_provider_error(
                error, sensitive_values);
        }
    }

   private:
    OpenAIConfig config_;
    std::string endpoint_;
    std::shared_ptr<HttpTransport> transport_;
    std::string api_key_;
};

OpenAIConfig::OpenAIConfig(std::string model_id)
    : model(provider_detail::make_model_descriptor(Provider::openai, std::move(model_id))) {}

OpenAIClient::OpenAIClient(OpenAIConfig config, const ClientOptions& options)
    : AIClient(config.model, options),
      impl_(std::make_unique<Impl>(std::move(config), options)) {}

OpenAIClient::~OpenAIClient() = default;

AIResponse OpenAIClient::generate_once(const AIRequest& request) {
    return impl_->generate(request);
}

std::unique_ptr<AIClient> neuralplus::make_client(OpenAIConfig config,
                                                  const ClientOptions& options) {
    return std::make_unique<OpenAIClient>(std::move(config), options);
}
