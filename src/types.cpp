// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/types.hpp"

#include <stdexcept>
#include <utility>

using namespace neuralplus;

namespace {

void require_non_empty(const std::string& value, const char* field) {
    if (value.empty()) {
        throw std::invalid_argument(std::string(field) + " must not be empty");
    }
}

void require_object(const JsonValue& value, const char* field) {
    // nlohmann::json distinguishes objects, arrays, strings, and scalars at
    // runtime; its official type-predicate reference is useful when extending
    // these provider-neutral validation rules:
    // https://json.nlohmann.me/api/basic_json/is_object/
    if (!value.is_object()) {
        throw std::invalid_argument(std::string(field) + " must be a JSON object");
    }
}

void append_text_content(Message::Contents& contents, std::string text) {
    if (!text.empty()) {
        contents.push_back(Content::text(std::move(text)));
    }
}

}  // namespace

Content::Content(ContentType type, ContentSource source)
    : type_(type), source_(source) {}

Content Content::text(std::string value) {
    Content content(ContentType::text, ContentSource::value);
    content.value_ = std::move(value);
    return content;
}

Content Content::image_url(std::string url,
                           std::string media_type,
                           JsonValue options) {
    require_non_empty(url, "image URL");
    require_object(options, "image options");

    Content content(ContentType::image, ContentSource::url);
    content.url_ = std::move(url);
    content.media_type_ = std::move(media_type);
    content.options_ = std::move(options);
    return content;
}

Content Content::image_bytes(std::vector<std::uint8_t> bytes,
                             std::string media_type,
                             JsonValue options) {
    if (bytes.empty()) {
        throw std::invalid_argument("image bytes must not be empty");
    }
    require_non_empty(media_type, "image media type");
    require_object(options, "image options");

    Content content(ContentType::image, ContentSource::bytes);
    content.bytes_ = std::move(bytes);
    content.media_type_ = std::move(media_type);
    content.options_ = std::move(options);
    return content;
}

Content Content::audio_url(std::string url,
                           std::string media_type,
                           JsonValue options) {
    require_non_empty(url, "audio URL");
    require_object(options, "audio options");

    Content content(ContentType::audio, ContentSource::url);
    content.url_ = std::move(url);
    content.media_type_ = std::move(media_type);
    content.options_ = std::move(options);
    return content;
}

Content Content::audio_bytes(std::vector<std::uint8_t> bytes,
                             std::string media_type,
                             JsonValue options) {
    if (bytes.empty()) {
        throw std::invalid_argument("audio bytes must not be empty");
    }
    require_non_empty(media_type, "audio media type");
    require_object(options, "audio options");

    Content content(ContentType::audio, ContentSource::bytes);
    content.bytes_ = std::move(bytes);
    content.media_type_ = std::move(media_type);
    content.options_ = std::move(options);
    return content;
}

Content Content::file_url(std::string url,
                          std::string media_type,
                          std::string filename,
                          JsonValue options) {
    require_non_empty(url, "file URL");
    require_object(options, "file options");

    Content content(ContentType::file, ContentSource::url);
    content.url_ = std::move(url);
    content.media_type_ = std::move(media_type);
    content.filename_ = std::move(filename);
    content.options_ = std::move(options);
    return content;
}

Content Content::file_bytes(std::vector<std::uint8_t> bytes,
                            std::string media_type,
                            std::string filename,
                            JsonValue options) {
    if (bytes.empty()) {
        throw std::invalid_argument("file bytes must not be empty");
    }
    require_non_empty(media_type, "file media type");
    require_object(options, "file options");

    Content content(ContentType::file, ContentSource::bytes);
    content.bytes_ = std::move(bytes);
    content.media_type_ = std::move(media_type);
    content.filename_ = std::move(filename);
    content.options_ = std::move(options);
    return content;
}

Content Content::extension(std::string provider, JsonValue provider_data) {
    require_non_empty(provider, "extension provider");
    require_object(provider_data, "extension provider data");

    Content content(ContentType::extension, ContentSource::provider_data);
    content.provider_ = std::move(provider);
    content.provider_data_ = std::move(provider_data);
    return content;
}

ContentType Content::type() const noexcept {
    return type_;
}

ContentSource Content::source() const noexcept {
    return source_;
}

const std::string& Content::value() const noexcept {
    return value_;
}

const std::string& Content::url() const noexcept {
    return url_;
}

const std::vector<std::uint8_t>& Content::bytes() const noexcept {
    return bytes_;
}

const std::string& Content::media_type() const noexcept {
    return media_type_;
}

const std::string& Content::filename() const noexcept {
    return filename_;
}

const std::string& Content::provider() const noexcept {
    return provider_;
}

const JsonValue& Content::options() const noexcept {
    return options_;
}

const JsonValue& Content::provider_data() const noexcept {
    return provider_data_;
}

Message::Message(Role role) : role_(role) {}

Message Message::system(std::string text) {
    require_non_empty(text, "system message");
    Message message(Role::system);
    message.contents_.push_back(Content::text(std::move(text)));
    return message;
}

Message Message::user(std::string text) {
    Message message(Role::user);
    append_text_content(message.contents_, std::move(text));
    return message;
}

Message Message::user(Contents contents) {
    Message message(Role::user);
    message.contents_ = std::move(contents);
    return message;
}

Message Message::assistant(std::string text,
                           std::vector<ToolCall> tool_calls) {
    Message message(Role::assistant);
    append_text_content(message.contents_, std::move(text));
    message.tool_calls_ = std::move(tool_calls);
    return message;
}

Message Message::assistant(Contents contents,
                           std::vector<ToolCall> tool_calls) {
    Message message(Role::assistant);
    message.contents_ = std::move(contents);
    message.tool_calls_ = std::move(tool_calls);
    return message;
}

Message Message::tool(std::string call_id,
                      std::string tool_name,
                      Contents contents,
                      bool is_error) {
    require_non_empty(call_id, "tool call id");
    require_non_empty(tool_name, "tool name");

    Message message(Role::tool);
    message.tool_call_id_ = std::move(call_id);
    message.tool_name_ = std::move(tool_name);
    message.contents_ = std::move(contents);
    message.tool_error_ = is_error;
    return message;
}

Message Message::tool_json(std::string call_id,
                           std::string tool_name,
                           JsonValue value,
                           bool is_error) {
    Contents contents;
    contents.push_back(Content::text(value.dump()));
    return tool(std::move(call_id),
                std::move(tool_name),
                std::move(contents),
                is_error);
}

Role Message::role() const noexcept {
    return role_;
}

const Message::Contents& Message::contents() const noexcept {
    return contents_;
}

const std::vector<ToolCall>& Message::tool_calls() const noexcept {
    return tool_calls_;
}

const std::string& Message::tool_call_id() const noexcept {
    return tool_call_id_;
}

const std::string& Message::tool_name() const noexcept {
    return tool_name_;
}

bool Message::is_tool_error() const noexcept {
    return tool_error_;
}

std::string Message::text() const {
    std::string result;
    for (const Content& content : contents_) {
        if (content.type() == ContentType::text) {
            result += content.value();
        }
    }
    return result;
}

const JsonValue& Message::provider_metadata() const noexcept {
    return provider_metadata_;
}

Message& Message::set_provider_metadata(JsonValue metadata) {
    provider_metadata_ = std::move(metadata);
    return *this;
}

AIResponse::AIResponse(Message message_value)
    : message(std::move(message_value)) {}

ProviderError::ProviderError(std::string message,
                             Provider provider_value,
                             int status_value,
                             std::string code_value,
                             std::string request_id_value)
    : Error(std::move(message)),
      provider_(provider_value),
      status_(status_value),
      code_(std::move(code_value)),
      request_id_(std::move(request_id_value)) {}

Provider ProviderError::provider() const noexcept {
    return provider_;
}

int ProviderError::status() const noexcept {
    return status_;
}

const std::string& ProviderError::code() const noexcept {
    return code_;
}

const std::string& ProviderError::request_id() const noexcept {
    return request_id_;
}

const char* neuralplus::to_string(Provider provider) noexcept {
    switch (provider) {
        case Provider::openai:
            return "openai";
        case Provider::anthropic:
            return "anthropic";
        case Provider::gemini:
            return "gemini";
        case Provider::openai_compatible:
            return "openai_compatible";
        case Provider::custom:
            return "custom";
    }
    return "custom";
}

const char* neuralplus::to_string(Role role) noexcept {
    switch (role) {
        case Role::system:
            return "system";
        case Role::user:
            return "user";
        case Role::assistant:
            return "assistant";
        case Role::tool:
            return "tool";
    }
    return "user";
}

const char* neuralplus::to_string(FinishReason reason) noexcept {
    switch (reason) {
        case FinishReason::stop:
            return "stop";
        case FinishReason::tool_calls:
            return "tool_calls";
        case FinishReason::length:
            return "length";
        case FinishReason::content_filter:
            return "content_filter";
        case FinishReason::refusal:
            return "refusal";
        case FinishReason::error:
            return "error";
        case FinishReason::unknown:
            return "unknown";
    }
    return "unknown";
}
