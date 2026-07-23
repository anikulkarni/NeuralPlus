// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

/// @file
/// Provider-independent messages, model metadata, requests, responses, and errors.

#pragma once

#include "neuralplus/export.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace neuralplus {

/// JSON value used by provider options, tool schemas, and tool arguments.
///
/// @see https://json.nlohmann.me/features/arbitrary_types/
using JsonValue = nlohmann::json;

// Object defaults below intentionally use `= JsonValue::object()`.
// With nlohmann/json, `JsonValue{JsonValue::object()}` is a one-element array,
// not an empty object: []-style initializer-list construction wins.

/// Provider family associated with a model descriptor.
enum class Provider {
    openai,
    anthropic,
    gemini,
    openai_compatible,
    custom,
};

/// Conversation role of a message.
enum class Role {
    system,
    user,
    assistant,
    tool,
};

/// Normalized reason why generation stopped.
enum class FinishReason {
    stop,
    tool_calls,
    length,
    content_filter,
    refusal,
    error,
    unknown,
};

/// Kind of a multimodal content part.
enum class ContentType {
    text,
    image,
    audio,
    file,
    extension,
};

/// Storage or reference form of a content part.
enum class ContentSource {
    value,
    url,
    bytes,
    provider_data,
};

/// One text, image, audio, file, or provider-extension part.
class NEURALPLUS_API Content final {
public:
    /// Creates a UTF-8 text part.
    [[nodiscard]] static Content text(std::string value);

    /// Creates an image referenced by URL.
    [[nodiscard]] static Content image_url(std::string url,
                                           std::string media_type = {},
                                           JsonValue options = JsonValue::object());

    /// Creates an inline image. The provider adapter performs base64 encoding.
    [[nodiscard]] static Content image_bytes(std::vector<std::uint8_t> bytes,
                                             std::string media_type,
                                             JsonValue options = JsonValue::object());

    /// Creates audio referenced by URL.
    [[nodiscard]] static Content audio_url(std::string url,
                                           std::string media_type = {},
                                           JsonValue options = JsonValue::object());

    /// Creates inline audio.
    [[nodiscard]] static Content audio_bytes(std::vector<std::uint8_t> bytes,
                                             std::string media_type,
                                             JsonValue options = JsonValue::object());

    /// Creates a file referenced by URL.
    [[nodiscard]] static Content file_url(std::string url,
                                          std::string media_type = {},
                                          std::string filename = {},
                                          JsonValue options = JsonValue::object());

    /// Creates an inline file.
    [[nodiscard]] static Content file_bytes(std::vector<std::uint8_t> bytes,
                                            std::string media_type,
                                            std::string filename = {},
                                            JsonValue options = JsonValue::object());

    /// Creates a provider-specific escape hatch.
    ///
    /// The provider name is matched by its adapter, for example `openai`.
    /// Other adapters reject the part instead of silently changing semantics.
    [[nodiscard]] static Content extension(std::string provider,
                                           JsonValue provider_data);

    /// Returns the normalized kind of this content part.
    [[nodiscard]] ContentType type() const noexcept;

    /// Returns whether this part stores a value, URL, bytes, or provider data.
    [[nodiscard]] ContentSource source() const noexcept;

    /// Returns the text value, or an empty string for non-text parts.
    [[nodiscard]] const std::string& value() const noexcept;

    /// Returns the referenced URL, or an empty string for non-URL parts.
    [[nodiscard]] const std::string& url() const noexcept;

    /// Returns inline bytes, or an empty vector for non-inline parts.
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept;

    /// Returns the media type supplied for this part.
    [[nodiscard]] const std::string& media_type() const noexcept;

    /// Returns the optional filename supplied for a file part.
    [[nodiscard]] const std::string& filename() const noexcept;

    /// Returns the target provider name for an extension part.
    [[nodiscard]] const std::string& provider() const noexcept;

    /// Returns provider-neutral options associated with this part.
    [[nodiscard]] const JsonValue& options() const noexcept;

    /// Returns the opaque payload of a provider extension part.
    [[nodiscard]] const JsonValue& provider_data() const noexcept;

private:
    Content(ContentType type, ContentSource source);

    ContentType type_;
    ContentSource source_;
    std::string value_;
    std::string url_;
    std::vector<std::uint8_t> bytes_;
    std::string media_type_;
    std::string filename_;
    std::string provider_;
    JsonValue options_ = JsonValue::object();
    JsonValue provider_data_ = JsonValue::object();
};

/// A normalized provider-requested tool call.
struct ToolCall {
    /// Provider call identifier used to correlate the tool result.
    std::string id;

    /// Tool name requested by the provider.
    std::string name;

    /// Parsed arguments. This is an empty object when parsing failed.
    JsonValue arguments = JsonValue::object();

    /// Original provider argument text, retained even when it is malformed.
    std::string raw_arguments{"{}"};

    /// Whether raw_arguments parsed successfully.
    bool arguments_valid{true};

    /// Opaque provider fields needed for exact continuation replay.
    JsonValue provider_metadata = JsonValue::object();
};

/// Description and JSON Schema advertised to a model.
struct ToolSpec {
    /// Portable function name.
    std::string name;

    /// Human-readable instruction for deciding when to call the tool.
    std::string description;

    /// JSON Schema whose root type must be `object`.
    JsonValue input_schema =
        JsonValue{{"type", "object"}, {"properties", JsonValue::object()}};

    /// Provider-native function-declaration fields.
    ///
    /// Provider adapters merge this object first, then apply the stable name,
    /// description, and input schema so portable fields cannot be replaced.
    JsonValue provider_options = JsonValue::object();
};

/// A valid conversation message with one normalized role.
class NEURALPLUS_API Message final {
public:
    /// Ordered content-part collection used by multimodal messages.
    using Contents = std::vector<Content>;

    /// Creates a system message. Sessions normally use `Session::set_system`.
    [[nodiscard]] static Message system(std::string text);

    /// Creates a text user message.
    [[nodiscard]] static Message user(std::string text);

    /// Creates a multimodal user message.
    [[nodiscard]] static Message user(Contents contents);

    /// Creates a text assistant message, optionally containing tool calls.
    [[nodiscard]] static Message assistant(std::string text,
                                           std::vector<ToolCall> tool_calls = {});

    /// Creates a multimodal assistant message.
    [[nodiscard]] static Message assistant(Contents contents,
                                           std::vector<ToolCall> tool_calls = {});

    /// Creates a tool result message.
    [[nodiscard]] static Message tool(std::string call_id,
                                      std::string tool_name,
                                      Contents contents,
                                      bool is_error = false);

    /// Creates a JSON tool result serialized as one text content part.
    [[nodiscard]] static Message tool_json(std::string call_id,
                                           std::string tool_name,
                                           JsonValue value,
                                           bool is_error = false);

    /// Returns the normalized role of this message.
    [[nodiscard]] Role role() const noexcept;

    /// Returns the ordered multimodal content parts.
    [[nodiscard]] const Contents& contents() const noexcept;

    /// Returns tool calls requested by an assistant message.
    [[nodiscard]] const std::vector<ToolCall>& tool_calls() const noexcept;

    /// Returns the correlated call ID for a tool-result message.
    [[nodiscard]] const std::string& tool_call_id() const noexcept;

    /// Returns the invoked tool name for a tool-result message.
    [[nodiscard]] const std::string& tool_name() const noexcept;

    /// Returns whether a tool-result message represents a failure.
    [[nodiscard]] bool is_tool_error() const noexcept;

    /// Concatenates text parts without discarding non-text parts from the message.
    [[nodiscard]] std::string text() const;

    /// Returns opaque data needed to replay provider-native continuation state.
    [[nodiscard]] const JsonValue& provider_metadata() const noexcept;

    /// Attaches provider-native continuation data and returns this message.
    Message& set_provider_metadata(JsonValue metadata);

private:
    explicit Message(Role role);

    Role role_;
    Contents contents_;
    std::vector<ToolCall> tool_calls_;
    std::string tool_call_id_;
    std::string tool_name_;
    bool tool_error_{false};
    JsonValue provider_metadata_ = JsonValue::object();
};

/// Model features used for early validation and feature discovery.
struct ModelCapabilities {
    /// Accepts UTF-8 text input.
    bool text_input{true};

    /// Can return normalized text.
    bool text_output{true};

    /// Accepts at least one supported image source.
    bool image_input{false};

    /// Can return normalized image content.
    bool image_output{false};

    /// Accepts at least one supported audio source.
    bool audio_input{false};

    /// Can return normalized audio content.
    bool audio_output{false};

    /// Accepts at least one supported file source.
    bool file_input{false};

    /// Can return normalized file content.
    bool file_output{false};

    /// Supports function/tool declarations.
    bool tools{false};

    /// May request more than one tool in a provider round.
    bool parallel_tool_calls{false};
};

/// Data-driven model description; no C++ subclass is needed per model name.
struct ModelDescriptor {
    /// Provider protocol used by the configured client.
    Provider provider{Provider::custom};

    /// Provider model identifier.
    std::string id;

    /// Optional user-interface label.
    std::string display_name;

    /// Discoverable features used for early validation.
    ModelCapabilities capabilities;

    /// Informational context-window size, or zero when unknown.
    std::size_t context_window{0};

    /// Model-profile output limit applied when a call does not override it.
    std::optional<std::size_t> max_output_tokens;

    /// Provider request fields for model-specific or forward-compatible behavior.
    JsonValue provider_options = JsonValue::object();
};

/// Per-call generation settings shared by all providers.
struct GenerateOptions {
    /// Sampling temperature, when the provider/model accepts it.
    std::optional<double> temperature;

    /// Maximum generated tokens for this call.
    std::optional<std::size_t> max_output_tokens;

    /// Provider request fields merged before stable SDK fields are applied.
    JsonValue provider_options = JsonValue::object();
};

/// Normalized token counts. Providers may leave unavailable values empty.
struct TokenUsage {
    /// Total tokens consumed from input, including provider cache reads/writes.
    std::optional<std::size_t> input_tokens;

    /// Tokens generated as output.
    std::optional<std::size_t> output_tokens;

    /// Provider-reported or safely calculated total.
    std::optional<std::size_t> total_tokens;

    /// Cached portion of input tokens, when reported.
    std::optional<std::size_t> cached_input_tokens;

    /// Input tokens written to a provider cache, when reported separately.
    std::optional<std::size_t> cache_creation_input_tokens;

    /// Reasoning/thinking tokens, when reported.
    std::optional<std::size_t> reasoning_tokens;
};

/// Immutable snapshot passed to one provider round.
struct AIRequest {
    /// Stable session identifier.
    std::string session_id;

    /// Identifier shared by all rounds of one generate call.
    std::string run_id;

    /// Provider-independent system instruction.
    std::optional<std::string> system_message;

    /// Complete normalized transcript snapshot.
    std::vector<Message> messages;

    /// Tools available for this round.
    std::vector<ToolSpec> tools;

    /// Normalized and provider-specific call options.
    GenerateOptions options;
};

/// Normalized response returned by both one provider round and the full tool loop.
struct NEURALPLUS_API AIResponse {
    /// Creates a normalized response around one assistant message.
    explicit AIResponse(Message message);

    /// Assistant message returned by the provider.
    Message message;

    /// Normalized provider stop reason.
    FinishReason finish_reason{FinishReason::unknown};

    /// Per-round usage from generate_once or cumulative usage from generate.
    TokenUsage usage;

    /// Provider request/response correlation identifier.
    std::string provider_request_id;

    /// Exact model identifier reported by the provider.
    std::string provider_model;

    /// Provider rounds completed by the public generate call.
    std::size_t model_rounds{1};

    /// Tool invocations completed by the public generate call.
    std::size_t tool_calls{0};

    /// Requests another provider round after replaying this assistant message.
    ///
    /// Provider adapters use this for protocol-level continuation such as
    /// Anthropic `pause_turn`; applications normally inspect only the final
    /// response returned by `AIClient::generate`.
    bool requires_continuation{false};

    /// Opaque provider response data available to advanced applications.
    JsonValue provider_metadata = JsonValue::object();
};

/// Base error for SDK failures.
class NEURALPLUS_API Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Invalid or missing configuration.
class NEURALPLUS_API ConfigurationError final : public Error {
public:
    using Error::Error;
};

/// A Session was already being used by another generation.
class NEURALPLUS_API SessionInUseError final : public Error {
public:
    using Error::Error;
};

/// The configured number of provider requests was exhausted.
class NEURALPLUS_API MaxRoundsError final : public Error {
public:
    using Error::Error;
};

/// An HTTP transport could not complete a request.
class NEURALPLUS_API TransportError final : public Error {
public:
    using Error::Error;
};

/// A provider returned an unsuccessful or malformed response.
class NEURALPLUS_API ProviderError final : public Error {
public:
    /// Creates an error with normalized provider and HTTP correlation details.
    ProviderError(std::string message,
                  Provider provider,
                  int status,
                  std::string code = {},
                  std::string request_id = {});

    /// Returns the provider that produced the error.
    [[nodiscard]] Provider provider() const noexcept;

    /// Returns the HTTP status, or zero when no status was available.
    [[nodiscard]] int status() const noexcept;

    /// Returns the provider-specific error code.
    [[nodiscard]] const std::string& code() const noexcept;

    /// Returns the provider request identifier when supplied.
    [[nodiscard]] const std::string& request_id() const noexcept;

private:
    Provider provider_;
    int status_;
    std::string code_;
    std::string request_id_;
};

/// Returns the stable SDK spelling of an enum value.
[[nodiscard]] NEURALPLUS_API const char* to_string(Provider provider) noexcept;

/// Returns the stable SDK spelling of an enum value.
[[nodiscard]] NEURALPLUS_API const char* to_string(Role role) noexcept;

/// Returns the stable SDK spelling of an enum value.
[[nodiscard]] NEURALPLUS_API const char* to_string(FinishReason reason) noexcept;

}  // namespace neuralplus
