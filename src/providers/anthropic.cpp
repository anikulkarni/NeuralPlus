// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include <limits>
#include <utility>

#include "../provider_common.hpp"

using namespace neuralplus;

namespace {

[[nodiscard]] bool has_native_anthropic_content(const Message& message) {
    const auto& metadata = message.provider_metadata();
    return metadata.is_object() &&
           provider_detail::string_field(metadata, "provider") == "anthropic" &&
           metadata.contains("content") && metadata["content"].is_array();
}

[[nodiscard]] JsonValue anthropic_content_part(const Content& content) {
    if (content.type() == ContentType::text && content.source() == ContentSource::value) {
        return JsonValue{{"type", "text"}, {"text", content.value()}};
    }

    if (content.type() == ContentType::image) {
        if (content.source() == ContentSource::url) {
            return JsonValue{{"type", "image"},
                             {"source", JsonValue{{"type", "url"}, {"url", content.url()}}}};
        }
        if (content.source() == ContentSource::bytes) {
            return JsonValue{
                {"type", "image"},
                {"source", JsonValue{{"type", "base64"},
                                     {"media_type", content.media_type()},
                                     {"data", provider_detail::base64_encode(content.bytes())}}}};
        }
        provider_detail::throw_unsupported_content(Provider::anthropic, content);
    }

    if (content.type() == ContentType::file) {
        JsonValue source;
        if (content.source() == ContentSource::url) {
            source = JsonValue{
                {"type", "url"},
                {"url", content.url()},
            };
        } else if (content.source() == ContentSource::bytes &&
                   content.media_type() == "application/pdf") {
            source = JsonValue{
                {"type", "base64"},
                {"media_type", "application/pdf"},
                {"data",
                 provider_detail::base64_encode(content.bytes())},
            };
        } else {
            throw ProviderError(
                "Anthropic file input supports document URLs and inline PDFs",
                Provider::anthropic,
                0,
                "unsupported_document");
        }

        JsonValue block{
            {"type", "document"},
            {"source", std::move(source)},
        };
        if (!content.filename().empty()) {
            block["title"] = content.filename();
        }
        return block;
    }

    if (content.type() == ContentType::extension &&
        content.source() == ContentSource::provider_data && content.provider() == "anthropic") {
        return content.provider_data();
    }

    provider_detail::throw_unsupported_content(Provider::anthropic, content);
}

[[nodiscard]] JsonValue anthropic_content(const Message& message) {
    JsonValue blocks = JsonValue::array();
    for (const auto& content : message.contents()) {
        blocks.push_back(anthropic_content_part(content));
    }
    return blocks;
}

void append_anthropic_turn(JsonValue& messages, std::string_view role, JsonValue blocks) {
    if (!messages.empty() && messages.back().is_object() &&
        provider_detail::string_field(messages.back(), "role") == role &&
        messages.back().contains("content") && messages.back()["content"].is_array()) {
        for (auto& block : blocks) {
            messages.back()["content"].push_back(std::move(block));
        }
        return;
    }
    messages.push_back(JsonValue{{"role", role}, {"content", std::move(blocks)}});
}

void append_anthropic_history(JsonValue& messages, const Message& message) {
    if (message.role() == Role::system) {
        return;
    }

    if (message.role() == Role::assistant && has_native_anthropic_content(message)) {
        append_anthropic_turn(messages, "assistant", message.provider_metadata()["content"]);
        return;
    }

    if (message.role() == Role::tool) {
        JsonValue block{{"type", "tool_result"},
                        {"tool_use_id", message.tool_call_id()},
                        {"content", anthropic_content(message)}};
        if (message.is_tool_error()) {
            block["is_error"] = true;
        }
        JsonValue blocks = JsonValue::array();
        blocks.push_back(std::move(block));
        append_anthropic_turn(messages, "user", std::move(blocks));
        return;
    }

    JsonValue blocks = anthropic_content(message);
    if (message.role() == Role::assistant) {
        for (const auto& call : message.tool_calls()) {
            blocks.push_back(JsonValue{{"type", "tool_use"},
                                       {"id", call.id},
                                       {"name", call.name},
                                       {"input", call.arguments}});
        }
        append_anthropic_turn(messages, "assistant", std::move(blocks));
        return;
    }
    append_anthropic_turn(messages, "user", std::move(blocks));
}

[[nodiscard]] std::string anthropic_system_instruction(const AIRequest& request) {
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

[[nodiscard]] JsonValue make_anthropic_request(const AnthropicConfig& config,
                                               const AIRequest& request) {
    JsonValue body = provider_detail::merged_provider_options(config.model.provider_options,
                                                              request.options.provider_options);
    body["model"] = config.model.id;
    // The normalized client consumes one complete JSON response. Anthropic
    // streaming uses server-sent events and is intentionally a separate
    // protocol shape.
    // https://docs.anthropic.com/en/api/messages
    body["stream"] = false;
    body["max_tokens"] =
        request.options.max_output_tokens.value_or(config.default_max_output_tokens);

    JsonValue messages = JsonValue::array();
    for (const auto& message : request.messages) {
        append_anthropic_history(messages, message);
    }
    body["messages"] = std::move(messages);

    const auto system = anthropic_system_instruction(request);
    if (!system.empty()) {
        body["system"] = system;
    }
    const auto configured_tools = body.find("tools");
    if (configured_tools != body.end() && !configured_tools->is_array()) {
        throw ConfigurationError(
            "Anthropic tools provider option must be an array");
    }
    if (!request.tools.empty()) {
        JsonValue tools = configured_tools == body.end()
                              ? JsonValue::array()
                              : *configured_tools;
        for (const auto& tool : request.tools) {
            JsonValue declaration = tool.provider_options;
            declaration["name"] = tool.name;
            declaration["description"] = tool.description;
            declaration["input_schema"] = tool.input_schema;
            tools.push_back(std::move(declaration));
        }
        body["tools"] = std::move(tools);
    }
    if (request.options.temperature.has_value()) {
        body["temperature"] = *request.options.temperature;
    }
    return body;
}

[[noreturn]] void throw_anthropic_http_error(const HttpResponse& response, std::string request_id) {
    const JsonValue parsed = JsonValue::parse(response.body, nullptr, false);
    std::string message;
    std::string code;
    if (!parsed.is_discarded() && parsed.is_object()) {
        if (request_id.empty()) {
            request_id = provider_detail::string_field(parsed, "request_id");
        }
        const auto error = parsed.find("error");
        if (error != parsed.end() && error->is_object()) {
            message = provider_detail::string_field(*error, "message");
            code = provider_detail::string_field(*error, "type");
        }
    }
    provider_detail::throw_provider_error(Provider::anthropic, response, std::move(message),
                                          std::move(code), std::move(request_id));
}

[[nodiscard]] FinishReason anthropic_finish_reason(std::string_view reason, bool has_tool_calls) {
    if (has_tool_calls || reason == "tool_use") {
        return FinishReason::tool_calls;
    }
    if (reason == "end_turn" || reason == "stop_sequence") {
        return FinishReason::stop;
    }
    if (reason == "max_tokens" || reason == "model_context_window_exceeded") {
        return FinishReason::length;
    }
    if (reason == "refusal") {
        return FinishReason::refusal;
    }
    return FinishReason::unknown;
}

[[nodiscard]] AIResponse parse_anthropic_response(const HttpResponse& response) {
    std::string request_id = provider_detail::response_request_id(response, {"request-id"});
    if (!provider_detail::successful_status(response.status)) {
        throw_anthropic_http_error(response, std::move(request_id));
    }

    const JsonValue body =
        provider_detail::parse_response_json(response, Provider::anthropic, request_id);
    if (request_id.empty()) {
        request_id = provider_detail::string_field(body, "id");
    }
    const auto body_content = body.find("content");
    if (body_content == body.end() || !body_content->is_array()) {
        throw ProviderError(
            "Anthropic response is missing the content array",
            Provider::anthropic,
            response.status,
            "malformed_response",
            request_id);
    }
    const JsonValue native_content = *body_content;

    std::vector<Content> contents;
    std::vector<ToolCall> calls;

    // https://platform.claude.com/docs/en/agents-and-tools/tool-use/overview
    // {"type":"tool_use","id":"t1","input":{"city":"Paris"}} becomes a
    // ToolCall; the original block array remains attached for exact replay.
    for (const auto& block : native_content) {
        if (!block.is_object()) {
            throw ProviderError(
                "Anthropic response content blocks must be objects",
                Provider::anthropic,
                response.status,
                "malformed_response",
                request_id);
        }
        const auto native_type = block.find("type");
        if (native_type != block.end() && !native_type->is_string()) {
            throw ProviderError(
                "Anthropic response content block type must be a string",
                Provider::anthropic,
                response.status,
                "malformed_response",
                request_id);
        }
        const auto type = provider_detail::string_field(block, "type");
        if (type == "text") {
            const auto text = block.find("text");
            if (text == block.end() || !text->is_string()) {
                throw ProviderError(
                    "Anthropic text content requires string text",
                    Provider::anthropic,
                    response.status,
                    "malformed_response",
                    request_id);
            }
            contents.push_back(Content::text(text->get<std::string>()));
            continue;
        }
        if (type == "tool_use") {
            const auto id = block.find("id");
            const auto name = block.find("name");
            const auto input = block.find("input");
            if (id == block.end() || !id->is_string() ||
                id->get_ref<const std::string&>().empty() ||
                name == block.end() || !name->is_string() ||
                name->get_ref<const std::string&>().empty() ||
                input == block.end() || !input->is_object()) {
                throw ProviderError(
                    "Anthropic tool_use content requires non-empty string id "
                    "and name and object input",
                    Provider::anthropic,
                    response.status,
                    "malformed_response",
                    request_id);
            }
            ToolCall call;
            call.id = id->get<std::string>();
            call.name = name->get<std::string>();
            call.arguments = *input;
            call.raw_arguments = call.arguments.dump();
            call.arguments_valid = true;
            call.provider_metadata = block;
            calls.push_back(std::move(call));
            continue;
        }
        contents.push_back(Content::extension("anthropic", block));
    }

    Message message = Message::assistant(std::move(contents), std::move(calls));
    message.set_provider_metadata(
        JsonValue{{"provider", "anthropic"}, {"content", native_content}});
    AIResponse result(std::move(message));
    const std::string stop_reason =
        provider_detail::string_field(body, "stop_reason");
    result.finish_reason = anthropic_finish_reason(
        stop_reason, !result.message.tool_calls().empty());
    // https://platform.claude.com/docs/en/build-with-claude/handling-stop-reasons
    // pause_turn means a server-tool loop reached its per-request limit. The
    // assistant content must be replayed unchanged in another provider round.
    result.requires_continuation = stop_reason == "pause_turn";
    result.provider_model = provider_detail::string_field(body, "model");
    result.provider_request_id = std::move(request_id);

    const auto usage = body.find("usage");
    if (usage != body.end() && usage->is_object()) {
        const auto uncached_input =
            provider_detail::size_field(*usage, "input_tokens");
        const auto cache_creation = provider_detail::size_field(
            *usage, "cache_creation_input_tokens");
        const auto cache_read = provider_detail::size_field(
            *usage, "cache_read_input_tokens");
        result.usage.output_tokens = provider_detail::size_field(*usage, "output_tokens");
        result.usage.cached_input_tokens = cache_read;
        result.usage.cache_creation_input_tokens = cache_creation;

        if (uncached_input.has_value()) {
            std::size_t total_input = *uncached_input;
            for (const auto& count : {cache_creation, cache_read}) {
                if (count.has_value()) {
                    if (*count >
                        std::numeric_limits<std::size_t>::max() -
                            total_input) {
                        throw ProviderError(
                            "Anthropic token usage overflow",
                            Provider::anthropic,
                            response.status,
                            "usage_overflow",
                            result.provider_request_id);
                    }
                    total_input += *count;
                }
            }
            result.usage.input_tokens = total_input;
        }

        if (result.usage.input_tokens.has_value() &&
            result.usage.output_tokens.has_value() &&
            *result.usage.output_tokens <=
                std::numeric_limits<std::size_t>::max() -
                    *result.usage.input_tokens) {
            result.usage.total_tokens =
                *result.usage.input_tokens + *result.usage.output_tokens;
        }

        const auto output_details = usage->find("output_tokens_details");
        if (output_details != usage->end() &&
            output_details->is_object()) {
            result.usage.reasoning_tokens = provider_detail::size_field(
                *output_details, "thinking_tokens");
        }
    }
    result.provider_metadata = JsonValue{{"provider", "anthropic"}, {"response", body}};
    return result;
}

}  // namespace

class AnthropicClient::Impl {
   public:
    Impl(AnthropicConfig config, const ClientOptions& options)
        : config_(std::move(config)),
          endpoint_(provider_detail::join_url(config_.base_url, "/v1/messages")),
          transport_(provider_detail::resolve_transport(options)),
          api_key_(provider_detail::resolve_required_credential(
              config_.api_key, {"ANTHROPIC_API_KEY"}, "Anthropic")) {
        provider_detail::validate_provider(config_.model, Provider::anthropic);
        config_.api_key.reset();
        if (config_.api_version.empty()) {
            throw ConfigurationError("Anthropic API version cannot be empty");
        }
        if (config_.default_max_output_tokens == 0U) {
            throw ConfigurationError(
                "Anthropic default_max_output_tokens must be greater than zero");
        }
    }

    [[nodiscard]] AIResponse generate(const AIRequest& request) const {
        HttpRequest http_request;
        http_request.method = HttpMethod::post;
        http_request.url = endpoint_;
        http_request.headers = {
            {"x-api-key", api_key_},
            {"anthropic-version", config_.api_version},
            {"Content-Type", "application/json"},
        };
        provider_detail::apply_extra_headers(http_request.headers, config_.extra_headers);
        http_request.body = make_anthropic_request(config_, request).dump();
        const auto sensitive_values = provider_detail::collect_sensitive_values(
            http_request.headers, {api_key_});
        try {
            HttpResponse response = transport_->send(http_request);
            provider_detail::redact_sensitive_response(
                response, sensitive_values);
            return parse_anthropic_response(response);
        } catch (const ProviderError& error) {
            provider_detail::throw_redacted_provider_error(
                error, sensitive_values);
        }
    }

   private:
    AnthropicConfig config_;
    std::string endpoint_;
    std::shared_ptr<HttpTransport> transport_;
    std::string api_key_;
};

AnthropicConfig::AnthropicConfig(std::string model_id)
    : model(provider_detail::make_model_descriptor(Provider::anthropic, std::move(model_id))) {}

AnthropicClient::AnthropicClient(AnthropicConfig config, const ClientOptions& options)
    : AIClient(config.model, options),
      impl_(std::make_unique<Impl>(std::move(config), options)) {}

AnthropicClient::~AnthropicClient() = default;

AIResponse AnthropicClient::generate_once(const AIRequest& request) {
    return impl_->generate(request);
}

std::unique_ptr<AIClient> neuralplus::make_client(AnthropicConfig config,
                                                  const ClientOptions& options) {
    return std::make_unique<AnthropicClient>(std::move(config), options);
}
