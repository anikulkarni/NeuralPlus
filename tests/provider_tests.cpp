// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neuralplus/neuralplus.hpp"

using namespace neuralplus;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Function>
void require_throws(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

bool ascii_equal_case_insensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    return std::equal(left.begin(), left.end(), right.begin(), [](char lhs, char rhs) {
        const auto ascii_lower = [](unsigned char character) {
            if (character >= static_cast<unsigned char>('A') &&
                character <= static_cast<unsigned char>('Z')) {
                return static_cast<unsigned char>(
                    character - static_cast<unsigned char>('A') +
                    static_cast<unsigned char>('a'));
            }
            return character;
        };
        return ascii_lower(static_cast<unsigned char>(lhs)) ==
               ascii_lower(static_cast<unsigned char>(rhs));
    });
}

std::optional<std::string> header_value(const HttpRequest& request, std::string_view name) {
    for (const auto& header : request.headers) {
        if (ascii_equal_case_insensitive(header.name, name)) {
            return header.value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> environment_value(const std::string& name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t value_size = 0;
    if (_dupenv_s(&value, &value_size, name.c_str()) != 0 ||
        value == nullptr) {
        std::free(value);
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name.c_str());
    return value == nullptr ? std::nullopt
                            : std::optional<std::string>(value);
#endif
}

void set_environment(const std::string& name,
                     const std::optional<std::string>& value) {
#if defined(_WIN32)
    if (_putenv_s(name.c_str(), value.value_or("").c_str()) != 0) {
        throw std::runtime_error("could not update test environment");
    }
#else
    const int result =
        value.has_value()
            ? ::setenv(name.c_str(), value->c_str(), 1)
            : ::unsetenv(name.c_str());
    if (result != 0) {
        throw std::runtime_error("could not update test environment");
    }
#endif
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(std::string name, std::string value)
        : name_(std::move(name)),
          original_(environment_value(name_)) {
        set_environment(name_, std::move(value));
    }

    ~ScopedEnvironment() {
        try {
            set_environment(name_, original_);
        } catch (...) {
        }
    }

private:
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    std::string name_;
    std::optional<std::string> original_;
};

ToolSpec weather_spec() {
    ToolSpec spec;
    spec.name = "weather";
    spec.description = "Gets the temperature for a city.";
    spec.input_schema =
        JsonValue{{"type", "object"},
                  {"properties", JsonValue{{"city", JsonValue{{"type", "string"}}}}},
                  {"required", JsonValue::array({"city"})},
                  {"additionalProperties", false}};
    return spec;
}

std::shared_ptr<FunctionTool> weather_tool(
    int temperature,
    JsonValue provider_options = JsonValue::object()) {
    ToolSpec spec = weather_spec();
    spec.provider_options = std::move(provider_options);
    return std::make_shared<FunctionTool>(
        std::move(spec),
        [temperature](ToolContext&, const JsonValue& arguments) {
            require(arguments.at("city").is_string(), "weather city argument");
            return ToolOutput::json(JsonValue{{"temp_c", temperature}});
        });
}

HttpResponse json_response(int status, JsonValue body, std::vector<HttpHeader> headers = {}) {
    HttpResponse response;
    response.status = status;
    response.headers = std::move(headers);
    response.body = std::move(body).dump();
    return response;
}

void test_openai_multimodal_text_and_usage() {
    auto transport = std::make_shared<MockHttpTransport>([](const HttpRequest& request) {
        require(request.method == HttpMethod::post, "OpenAI POST");
        require(request.url == "https://api.openai.com/v1/responses", "OpenAI Responses URL");
        require(header_value(request, "Authorization").value() == "Bearer explicit-openai",
                "OpenAI explicit credential");

        const JsonValue body = JsonValue::parse(request.body);
        require(body.at("model") == "gpt-test", "OpenAI model");
        require(body.at("stream") == false, "OpenAI streaming is disabled");
        require(body.at("background") == false,
                "OpenAI background mode is disabled");
        require(body.at("instructions") == "Describe briefly.", "OpenAI instructions");
        const JsonValue& content = body.at("input").at(0).at("content");
        require(
            content.at(0) == JsonValue{{"type", "input_text"}, {"text", "What is in this image?"}},
            "OpenAI text input part");
        require(content.at(1).at("type") == "input_image", "OpenAI image input part");
        require(content.at(1).at("image_url") == "data:image/png;base64,iVBORw==",
                "OpenAI image base64");
        require(content.at(1).at("detail") == "low", "OpenAI image detail");

        return json_response(
            200,
            JsonValue{
                {"id", "resp_1"},
                {"model", "gpt-test-2026-01-01"},
                {"status", "completed"},
                {"output",
                 JsonValue::array(
                     {JsonValue{
                          {"type", "message"},
                          {"role", "assistant"},
                          {"status", "completed"},
                          {"unknown_future_field", true},
                          {"content",
                           JsonValue::array(
                               {JsonValue{
                                   {"type", "output_text"},
                                   {"text", "A tiny image."},
                                   {"annotations", JsonValue::array()}}})}},
                      JsonValue{{"type", "image_generation_call"},
                                {"id", "image-1"},
                                {"status", "completed"},
                                {"result", "iVBORw=="}}})},
                {"usage", JsonValue{{"input_tokens", 9},
                                    {"input_tokens_details", JsonValue{{"cached_tokens", 2}}},
                                    {"output_tokens", 4},
                                    {"output_tokens_details", JsonValue{{"reasoning_tokens", 1}}},
                                    {"total_tokens", 13}}}},
            {{"x-request-id", "req-openai-1"}});
    });

    OpenAIConfig config("gpt-test");
    config.api_key = "explicit-openai";
    config.model.provider_options["stream"] = true;
    config.model.provider_options["background"] = true;
    config.model.capabilities.image_output = true;
    ClientOptions options;
    options.transport = transport;
    OpenAIClient client(std::move(config), options);

    SessionOptions session_options;
    session_options.system_message = "Describe briefly.";
    Session session(std::move(session_options));
    Message input = Message::user({Content::text("What is in this image?"),
                                   Content::image_bytes({0x89, 0x50, 0x4e, 0x47}, "image/png",
                                                        JsonValue{{"detail", "low"}})});
    const AIResponse response = client.generate(session, input);

    require(response.message.text() == "A tiny image.", "OpenAI output text");
    require(response.finish_reason == FinishReason::stop, "OpenAI completed finish");
    require(response.provider_request_id == "req-openai-1", "OpenAI request id");
    require(response.provider_model == "gpt-test-2026-01-01", "OpenAI response model");
    require(response.usage.input_tokens.value() == 9, "OpenAI input usage");
    require(response.usage.output_tokens.value() == 4, "OpenAI output usage");
    require(response.usage.cached_input_tokens.value() == 2, "OpenAI cached usage");
    require(response.usage.reasoning_tokens.value() == 1, "OpenAI reasoning usage");
    require(response.message.contents().size() == 2,
            "OpenAI multimodal output count");
    require(response.message.contents().at(1).type() == ContentType::image,
            "OpenAI image output type");
    require(response.message.contents().at(1).bytes() ==
                std::vector<std::uint8_t>({0x89, 0x50, 0x4e, 0x47}),
            "OpenAI image output bytes");
}

void test_openai_native_tool_continuation() {
    const JsonValue reasoning_item{{"type", "reasoning"},
                                   {"id", "rs_1"},
                                   {"encrypted_content", "opaque-reasoning"},
                                   {"summary", JsonValue::array()}};
    const JsonValue function_item{{"type", "function_call"},
                                  {"id", "fc_1"},
                                  {"call_id", "call_1"},
                                  {"name", "weather"},
                                  {"arguments", R"({"city":"Paris"})"},
                                  {"status", "completed"}};

    std::size_t round = 0;
    auto transport = std::make_shared<MockHttpTransport>([&](const HttpRequest& request) {
        const JsonValue body = JsonValue::parse(request.body);
        require(body.at("stream") == false,
                "Anthropic streaming is disabled");
        ++round;
        if (round == 1U) {
            require(body.at("tools").size() == 2,
                    "OpenAI native and client tools are combined");
            require(body.at("tools").at(0).at("type") == "web_search_preview",
                    "OpenAI native tool is preserved");
            require(body.at("tools").at(1).at("name") == "weather",
                    "OpenAI stable tool name wins");
            require(body.at("tools").at(1).at("strict") == true,
                    "OpenAI tool provider option is preserved");
            return json_response(
                200, JsonValue{{"id", "resp_tool_1"},
                               {"model", "gpt-test"},
                               {"status", "completed"},
                               {"output", JsonValue::array({reasoning_item, function_item})},
                               {"usage", JsonValue{{"input_tokens", 3},
                                                   {"output_tokens", 2},
                                                   {"total_tokens", 5}}}});
        }

        const JsonValue& input = body.at("input");
        require(input.size() == 4, "OpenAI continuation item count");
        require(input.at(1) == reasoning_item, "OpenAI reasoning item replayed exactly");
        require(input.at(2) == function_item, "OpenAI function item replayed exactly");
        require(input.at(3).at("type") == "function_call_output", "OpenAI function output type");
        require(input.at(3).at("call_id") == "call_1", "OpenAI function output call id");
        require(JsonValue::parse(input.at(3).at("output").get<std::string>()).at("temp_c") == 20,
                "OpenAI function output value");
        return json_response(
            200,
            JsonValue{
                {"id", "resp_tool_2"},
                {"model", "gpt-test"},
                {"status", "completed"},
                {"output", JsonValue::array({JsonValue{
                               {"type", "message"},
                               {"role", "assistant"},
                               {"content", JsonValue::array({JsonValue{{"type", "output_text"},
                                                                       {"text", "20 C"}}})}}})},
                {"usage",
                 JsonValue{{"input_tokens", 5}, {"output_tokens", 2}, {"total_tokens", 7}}}});
    });

    OpenAIConfig config("gpt-test");
    config.api_key = "openai-key";
    config.model.provider_options["tools"] = JsonValue::array(
        {JsonValue{{"type", "web_search_preview"}}});
    ClientOptions options;
    options.transport = transport;
    options.tools = {
        weather_tool(20, JsonValue{{"strict", true},
                                   {"name", "ignored-name"}})};
    OpenAIClient client(std::move(config), options);
    const AIResponse response = client.generate("Weather in Paris?");

    require(round == 2, "OpenAI tool loop rounds");
    require(response.message.text() == "20 C", "OpenAI tool final text");
    require(response.model_rounds == 2, "OpenAI normalized model rounds");
    require(response.tool_calls == 1, "OpenAI normalized tool calls");
    require(response.usage.total_tokens.value() == 12, "OpenAI cumulative usage");
}

void test_anthropic_multimodal_tool_continuation() {
    const JsonValue thinking_block{
        {"type", "thinking"}, {"thinking", "opaque"}, {"signature", "anthropic-signature"}};
    const JsonValue tool_block{{"type", "tool_use"},
                               {"id", "toolu_1"},
                               {"name", "weather"},
                               {"input", JsonValue{{"city", "Paris"}}}};

    std::size_t round = 0;
    auto transport = std::make_shared<MockHttpTransport>([&](const HttpRequest& request) {
        require(request.url == "https://api.anthropic.com/v1/messages", "Anthropic URL");
        require(header_value(request, "x-api-key").value() == "explicit-anthropic",
                "Anthropic explicit credential");
        require(header_value(request, "anthropic-version").value() == "2023-06-01",
                "Anthropic version");
        const JsonValue body = JsonValue::parse(request.body);
        ++round;
        if (round == 1U) {
            require(body.at("system") == "Use tools.", "Anthropic system");
            require(body.at("max_tokens") == 1024, "Anthropic max tokens");
            const JsonValue& content = body.at("messages").at(0).at("content");
            require(content.at(0).at("type") == "text", "Anthropic text part");
            require(content.at(1).at("source").at("type") == "base64", "Anthropic image source");
            require(content.at(1).at("source").at("data") == "iVBORw==", "Anthropic image data");
            require(body.at("tools").size() == 2,
                    "Anthropic native and client tools are combined");
            require(body.at("tools").at(0).at("type") ==
                        "web_search_20250305",
                    "Anthropic native tool is preserved");
            require(body.at("tools").at(1).at("name") == "weather",
                    "Anthropic stable tool name wins");
            require(body.at("tools").at(1).at("cache_control").at("type") ==
                        "ephemeral",
                    "Anthropic tool provider option is preserved");
            require(body.at("tools").at(1).at("input_schema").at("type") == "object",
                    "Anthropic tool schema");
            return json_response(
                200,
                JsonValue{{"id", "msg_1"},
                          {"type", "message"},
                          {"role", "assistant"},
                          {"model", "claude-test"},
                          {"content", JsonValue::array({thinking_block, tool_block})},
                          {"stop_reason", "tool_use"},
                          {"usage", JsonValue{{"input_tokens", 3},
                                              {"cache_creation_input_tokens", 0},
                                              {"cache_read_input_tokens", 0},
                                              {"output_tokens", 2},
                                              {"output_tokens_details",
                                               JsonValue{{"thinking_tokens", 0}}}}}},
                {{"request-id", "req-anthropic-1"}});
        }

        const JsonValue& messages = body.at("messages");
        require(messages.size() == 3, "Anthropic continuation turn count");
        require(messages.at(1).at("role") == "assistant", "Anthropic assistant replay role");
        require(messages.at(1).at("content") == JsonValue::array({thinking_block, tool_block}),
                "Anthropic native blocks replayed exactly");
        const JsonValue& result_block = messages.at(2).at("content").at(0);
        require(result_block.at("type") == "tool_result", "Anthropic tool result type");
        require(result_block.at("tool_use_id") == "toolu_1", "Anthropic tool result id");
        require(JsonValue::parse(result_block.at("content").at(0).at("text").get<std::string>())
                        .at("temp_c") == 19,
                "Anthropic tool result");
        return json_response(
            200, JsonValue{
                     {"id", "msg_2"},
                     {"type", "message"},
                     {"role", "assistant"},
                     {"model", "claude-test"},
                     {"content", JsonValue::array({JsonValue{{"type", "text"}, {"text", "19 C"}}})},
                     {"stop_reason", "end_turn"},
                     {"usage", JsonValue{{"input_tokens", 4},
                                         {"cache_creation_input_tokens", 1},
                                         {"output_tokens", 1},
                                         {"cache_read_input_tokens", 2},
                                         {"output_tokens_details",
                                          JsonValue{{"thinking_tokens", 1}}}}}});
    });

    AnthropicConfig config("claude-test");
    config.api_key = "explicit-anthropic";
    config.model.provider_options["stream"] = true;
    config.model.provider_options["tools"] = JsonValue::array(
        {JsonValue{{"type", "web_search_20250305"},
                   {"name", "web_search"}}});
    ClientOptions options;
    options.transport = transport;
    options.tools = {
        weather_tool(19,
                     JsonValue{{"cache_control",
                                JsonValue{{"type", "ephemeral"}}},
                               {"name", "ignored-name"}})};
    AnthropicClient client(std::move(config), options);

    SessionOptions session_options;
    session_options.system_message = "Use tools.";
    Session session(std::move(session_options));
    const AIResponse response = client.generate(
        session, Message::user({Content::text("Weather?"),
                                Content::image_bytes({0x89, 0x50, 0x4e, 0x47}, "image/png")}));

    require(round == 2, "Anthropic tool rounds");
    require(response.message.text() == "19 C", "Anthropic final text");
    require(response.finish_reason == FinishReason::stop, "Anthropic finish reason");
    require(response.usage.input_tokens.value() == 10,
            "Anthropic cumulative input usage");
    require(response.usage.total_tokens.value() == 13,
            "Anthropic calculated total usage");
    require(response.usage.cached_input_tokens.value() == 2, "Anthropic cached usage");
    require(response.usage.cache_creation_input_tokens.value() == 1,
            "Anthropic cache creation usage");
    require(response.usage.reasoning_tokens.value() == 1,
            "Anthropic reasoning usage");
}

void test_anthropic_pause_turn_continuation() {
    const JsonValue paused_content = JsonValue::array(
        {JsonValue{{"type", "server_tool_use"},
                   {"id", "server-tool-1"},
                   {"name", "web_search"},
                   {"input", JsonValue{{"query", "C++17"}}}},
         JsonValue{{"type", "web_search_tool_result"},
                   {"tool_use_id", "server-tool-1"},
                   {"content", JsonValue::array()}}});

    std::size_t round = 0;
    auto transport =
        std::make_shared<MockHttpTransport>(
            [&](const HttpRequest& request) {
                const JsonValue body = JsonValue::parse(request.body);
                ++round;
                require(body.at("tools").at(0).at("type") ==
                            "web_search_20250305",
                        "Anthropic server tool persists across pause");
                if (round == 1) {
                    return json_response(
                        200,
                        JsonValue{
                            {"id", "pause-1"},
                            {"model", "claude-test"},
                            {"content", paused_content},
                            {"stop_reason", "pause_turn"},
                            {"usage",
                             JsonValue{
                                 {"input_tokens", 2},
                                 {"cache_creation_input_tokens", 0},
                                 {"cache_read_input_tokens", 0},
                                 {"output_tokens", 3}}}});
                }

                const JsonValue& messages = body.at("messages");
                require(messages.size() == 2,
                        "Anthropic pause continuation turn count");
                require(messages.at(1).at("role") == "assistant",
                        "Anthropic paused assistant role");
                require(messages.at(1).at("content") == paused_content,
                        "Anthropic paused content replayed exactly");
                return json_response(
                    200,
                    JsonValue{
                        {"id", "pause-2"},
                        {"model", "claude-test"},
                        {"content",
                         JsonValue::array(
                             {JsonValue{{"type", "text"},
                                        {"text", "Search complete."}}})},
                        {"stop_reason", "end_turn"},
                        {"usage",
                         JsonValue{
                             {"input_tokens", 4},
                             {"cache_creation_input_tokens", 0},
                             {"cache_read_input_tokens", 0},
                             {"output_tokens", 2}}}});
            });

    AnthropicConfig config("claude-test");
    config.api_key = "key";
    config.model.provider_options["tools"] =
        JsonValue::array(
            {JsonValue{{"type", "web_search_20250305"},
                       {"name", "web_search"}}});
    ClientOptions options;
    options.transport = transport;
    AnthropicClient client(std::move(config), options);

    Session session;
    const AIResponse response =
        client.generate(session, "Search for C++17.");
    require(round == 2, "Anthropic pause performs another model round");
    require(response.model_rounds == 2,
            "Anthropic pause normalized round count");
    require(response.message.text() == "Search complete.",
            "Anthropic pause final response");
    require(session.messages().size() == 3,
            "Anthropic paused response remains in transcript");
}

void test_gemini_thought_signature_continuation() {
    const JsonValue thought_part{{"thought", true}, {"thoughtSignature", "thought-signature"}};
    const JsonValue function_part{
        {"functionCall",
         JsonValue{
             {"id", "gemini-1"}, {"name", "weather"}, {"args", JsonValue{{"city", "Paris"}}}}},
        {"thoughtSignature", "function-signature"}};
    const JsonValue trailing_part{{"text", "Checking."}};

    std::size_t round = 0;
    auto transport = std::make_shared<MockHttpTransport>([&](const HttpRequest& request) {
        require(request.url ==
                    "https://generativelanguage.googleapis.com/v1beta/"
                    "models/gemini-test:generateContent",
                "Gemini URL");
        require(header_value(request, "x-goog-api-key").value() == "explicit-gemini",
                "Gemini explicit credential");
        const JsonValue body = JsonValue::parse(request.body);
        ++round;
        if (round == 1U) {
            require(body.at("systemInstruction").at("parts").at(0).at("text") == "Use tools.",
                    "Gemini system instruction");
            const JsonValue& parts = body.at("contents").at(0).at("parts");
            require(parts.at(0).at("text") == "Weather?", "Gemini text input");
            require(parts.at(1).at("inlineData").at("mimeType") == "image/png",
                    "Gemini image MIME type");
            require(parts.at(1).at("inlineData").at("data") == "iVBORw==", "Gemini image data");
            require(body.at("tools").size() == 2,
                    "Gemini native and client tools are combined");
            require(body.at("tools").at(0).contains("googleSearch"),
                    "Gemini native tool is preserved");
            require(body.at("tools")
                        .at(1)
                        .at("functionDeclarations")
                        .at(0)
                        .at("name") == "weather",
                    "Gemini function declaration");
            require(body.at("tools")
                        .at(1)
                        .at("functionDeclarations")
                        .at(0)
                        .at("behavior") == "BLOCKING",
                    "Gemini tool provider option is preserved");
            return json_response(
                200,
                JsonValue{{"candidates",
                           JsonValue::array({JsonValue{
                               {"content",
                                JsonValue{{"role", "model"},
                                          {"parts", JsonValue::array({thought_part, function_part,
                                                                      trailing_part})}}},
                               {"finishReason", "STOP"},
                               {"futureCandidateField", true}}})},
                          {"usageMetadata", JsonValue{{"promptTokenCount", 4},
                                                      {"candidatesTokenCount", 2},
                                                      {"cachedContentTokenCount", 0},
                                                      {"thoughtsTokenCount", 1},
                                                      {"totalTokenCount", 7}}},
                          {"modelVersion", "gemini-test-001"},
                          {"responseId", "gemini-response-1"}});
        }

        const JsonValue& contents = body.at("contents");
        require(contents.size() == 3, "Gemini continuation turn count");
        require(contents.at(1).at("role") == "model", "Gemini model replay role");
        require(contents.at(1).at("parts") ==
                    JsonValue::array({thought_part, function_part, trailing_part}),
                "Gemini thought signatures replayed exactly");
        const JsonValue& function_response =
            contents.at(2).at("parts").at(0).at("functionResponse");
        require(function_response.at("id") == "gemini-1", "Gemini function response id");
        require(function_response.at("name") == "weather", "Gemini function response name");
        require(function_response.at("response").at("temp_c") == 21,
                "Gemini function response value");
        return json_response(
            200,
            JsonValue{{"candidates",
                       JsonValue::array({JsonValue{
                           {"content",
                            JsonValue{{"role", "model"},
                                      {"parts",
                                       JsonValue::array(
                                           {JsonValue{{"text", "21 C"}},
                                            JsonValue{
                                                {"inlineData",
                                                 JsonValue{
                                                     {"mimeType", "image/png"},
                                                     {"data", "AQID"}}}}})}}},
                           {"finishReason", "STOP"}}})},
                      {"usageMetadata", JsonValue{{"promptTokenCount", 5},
                                                  {"candidatesTokenCount", 1},
                                                  {"cachedContentTokenCount", 2},
                                                  {"thoughtsTokenCount", 0},
                                                  {"totalTokenCount", 6}}},
                      {"modelVersion", "gemini-test-001"},
                      {"responseId", "gemini-response-2"}});
    });

    GeminiConfig config("models/gemini-test");
    config.api_key = "explicit-gemini";
    config.model.capabilities.image_output = true;
    config.model.provider_options["tools"] =
        JsonValue::array({JsonValue{{"googleSearch", JsonValue::object()}}});
    ClientOptions options;
    options.transport = transport;
    options.tools = {
        weather_tool(21, JsonValue{{"behavior", "BLOCKING"},
                                   {"name", "ignored-name"}})};
    GeminiClient client(std::move(config), options);

    SessionOptions session_options;
    session_options.system_message = "Use tools.";
    Session session(std::move(session_options));
    const AIResponse response = client.generate(
        session, Message::user({Content::text("Weather?"),
                                Content::image_bytes({0x89, 0x50, 0x4e, 0x47}, "image/png")}));

    require(round == 2, "Gemini tool rounds");
    require(response.message.text() == "21 C", "Gemini final text");
    require(response.finish_reason == FinishReason::stop, "Gemini finish reason");
    require(response.provider_request_id == "gemini-response-2", "Gemini response id");
    require(response.usage.total_tokens.value() == 13, "Gemini cumulative usage");
    require(response.usage.reasoning_tokens.value() == 1, "Gemini reasoning usage");
    require(response.usage.cached_input_tokens.value() == 2, "Gemini cached usage");
    require(response.message.contents().size() == 2,
            "Gemini multimodal output count");
    require(response.message.contents().at(1).type() == ContentType::image,
            "Gemini image output type");
    require(response.message.contents().at(1).bytes() ==
                std::vector<std::uint8_t>({1, 2, 3}),
            "Gemini image output bytes");
}

void test_gemini_multimodal_tool_result_and_synthetic_id() {
    ToolSpec spec;
    spec.name = "render";
    spec.description = "Renders an image.";
    std::string synthetic_call_id;
    auto tool = std::make_shared<FunctionTool>(
        std::move(spec),
        [&](ToolContext& context, const JsonValue&) {
            synthetic_call_id = context.call_id();
            ToolOutput output;
            output.contents = {
                Content::text(R"({"caption":"chart"})"),
                Content::image_bytes({1, 2, 3}, "image/png"),
            };
            return output;
        });

    std::size_t round = 0;
    auto transport =
        std::make_shared<MockHttpTransport>(
            [&](const HttpRequest& request) {
                const JsonValue body = JsonValue::parse(request.body);
                ++round;
                if (round == 1) {
                    return json_response(
                        200,
                        JsonValue{
                            {"candidates",
                             JsonValue::array({JsonValue{
                                 {"content",
                                  JsonValue{
                                      {"role", "model"},
                                      {"parts",
                                       JsonValue::array(
                                           {JsonValue{
                                               {"functionCall",
                                                JsonValue{
                                                    {"name", "render"},
                                                    {"args",
                                                     JsonValue::object()}}}}})}}},
                                 {"finishReason", "STOP"}}})},
                            {"usageMetadata",
                             JsonValue{{"promptTokenCount", 1},
                                       {"candidatesTokenCount", 1},
                                       {"totalTokenCount", 2}}}});
                }

                const JsonValue& response = body.at("contents")
                                                .at(2)
                                                .at("parts")
                                                .at(0)
                                                .at("functionResponse");
                require(!response.contains("id"),
                        "synthetic Gemini call id is not sent to provider");
                require(response.at("response").at("caption") == "chart",
                        "Gemini structured tool response");
                const JsonValue& inline_data =
                    response.at("parts").at(0).at("inlineData");
                require(inline_data.at("mimeType") == "image/png",
                        "Gemini tool image MIME type");
                require(inline_data.at("data") == "AQID",
                        "Gemini tool image bytes");
                require(!inline_data.contains("displayName"),
                        "Gemini tool image uses the documented Blob fields");
                return json_response(
                    200,
                    JsonValue{
                        {"candidates",
                         JsonValue::array({JsonValue{
                             {"content",
                              JsonValue{
                                  {"role", "model"},
                                  {"parts",
                                   JsonValue::array(
                                       {JsonValue{{"text", "Rendered."}}})}}},
                             {"finishReason", "STOP"}}})},
                        {"usageMetadata",
                         JsonValue{{"promptTokenCount", 2},
                                   {"candidatesTokenCount", 1},
                                   {"totalTokenCount", 3}}}});
            });

    GeminiConfig config("gemini-test");
    config.api_key = "key";
    ClientOptions options;
    options.transport = transport;
    options.tools = {tool};
    GeminiClient client(std::move(config), options);

    const AIResponse response = client.generate("Render a chart.");
    require(response.message.text() == "Rendered.",
            "Gemini multimodal tool final response");
    require(response.tool_calls == 1,
            "Gemini synthetic call is executed once");
    require(synthetic_call_id.find("gemini_call_run-") == 0,
            "Gemini synthetic call id is scoped to the generation");
    require(synthetic_call_id.size() >= 4U &&
                synthetic_call_id.compare(
                    synthetic_call_id.size() - 4U, 4U, "_1_0") == 0,
            "Gemini synthetic call id includes history and call index");
}

void test_sparse_openai_compatible_response() {
    auto transport = std::make_shared<MockHttpTransport>([](const HttpRequest& request) {
        require(request.url == "http://localhost:8080/v1/chat/completions", "compatible URL");
        require(!header_value(request, "Authorization").has_value(),
                "compatible local endpoint remains unauthenticated");
        const JsonValue body = JsonValue::parse(request.body);
        require(body.at("stream") == false, "compatible non-streaming");
        require(body.at("messages").at(0).at("role") == "system", "compatible system role");
        const JsonValue& content = body.at("messages").at(1).at("content");
        require(content.at(0).at("type") == "text", "compatible text part");
        require(content.at(1).at("type") == "image_url", "compatible image part");
        require(content.at(1).at("image_url").at("url") == "https://example.test/image.png",
                "compatible image URL");
        const JsonValue response_content =
            JsonValue::array(
                {JsonValue{{"type", "text"}, {"text", "ok"}},
                 JsonValue{
                     {"type", "image_url"},
                     {"image_url",
                      JsonValue{
                          {"url", "https://example.test/output.png"}}}}});
        return json_response(
            200, JsonValue{{"id", "chatcmpl_1"},
                           {"model", "local-test"},
                           {"choices",
                            JsonValue::array(
                                {JsonValue{
                                    {"index", 0},
                                    {"message",
                                     JsonValue{
                                         {"role", "assistant"},
                                         {"content", response_content},
                                         {"future_field", 7}}},
                                    {"finish_reason",
                                     "future_finish_reason"}}})},
                           {"future_top_level", true}});
    });

    OpenAICompatibleConfig config("local-test", "http://localhost:8080/v1/chat/completions/");
    config.model.capabilities.image_input = true;
    config.model.capabilities.image_output = true;
    ClientOptions options;
    options.transport = transport;
    OpenAICompatibleClient client(std::move(config), options);

    SessionOptions session_options;
    session_options.system_message = "Be brief.";
    Session session(std::move(session_options));
    const AIResponse response = client.generate(
        session,
        Message::user({Content::text("Describe."),
                       Content::image_url("https://example.test/image.png", "image/png")}));

    require(response.message.text() == "ok", "compatible output");
    require(response.finish_reason == FinishReason::unknown, "compatible unknown finish tolerated");
    require(!response.usage.total_tokens.has_value(), "compatible missing usage tolerated");
    require(response.message.provider_metadata().at("message").at("future_field") == 7,
            "compatible unknown message field retained");
    require(response.message.contents().at(1).type() == ContentType::image,
            "compatible image output type");
    require(response.message.contents().at(1).url() ==
                "https://example.test/output.png",
            "compatible image output URL");
}

void test_audio_and_file_content_mappings() {
    {
        auto transport = std::make_shared<MockHttpTransport>(
            [](const HttpRequest& request) {
                const JsonValue body = JsonValue::parse(request.body);
                const JsonValue& content =
                    body.at("input")
                        .at(0)
                        .at("content");
                require(content.at(1).at("type") == "input_file",
                        "OpenAI inline file type");
                require(content.at(1).at("file_data") ==
                            "data:application/pdf;base64,AwQ=",
                        "OpenAI inline file data");
                require(content.at(1).at("filename") == "inline.pdf",
                        "OpenAI inline file name");
                require(content.at(2).at("file_url") ==
                            "https://example.test/reference.pdf",
                        "OpenAI file URL");
                return json_response(
                    200,
                    JsonValue{
                        {"id", "openai-media"},
                        {"status", "completed"},
                        {"output",
                         JsonValue::array(
                             {JsonValue{
                                 {"type", "message"},
                                 {"role", "assistant"},
                                 {"content",
                                  JsonValue::array(
                                      {JsonValue{
                                          {"type", "output_text"},
                                          {"text", "file reply"}}})}}})}});
            });

        OpenAIConfig config("gpt-media");
        config.api_key = "key";
        config.model.capabilities.file_input = true;
        ClientOptions options;
        options.transport = transport;
        OpenAIClient client(std::move(config), options);
        const AIResponse response = client.generate(
            Message::user(
                {Content::text("Inspect these inputs."),
                 Content::file_bytes(
                     {3, 4}, "application/pdf", "inline.pdf"),
                 Content::file_url(
                     "https://example.test/reference.pdf",
                     "application/pdf",
                     "reference.pdf")}));
        require(response.message.text() == "file reply",
                "OpenAI file response");
    }

    {
        std::size_t round = 0;
        auto transport = std::make_shared<MockHttpTransport>(
            [&](const HttpRequest& request) {
                const JsonValue body = JsonValue::parse(request.body);
                ++round;
                if (round == 2) {
                    const JsonValue& prior_audio =
                        body.at("messages").at(1).at("audio");
                    require(prior_audio ==
                                JsonValue{{"id", "audio-1"}},
                            "compatible audio history replays only its id");
                    return json_response(
                        200,
                        JsonValue{
                            {"id", "compatible-follow-up"},
                            {"model", "compatible-media"},
                            {"choices",
                             JsonValue::array(
                                 {JsonValue{
                                     {"message",
                                      JsonValue{
                                          {"role", "assistant"},
                                          {"content", "continued"}}},
                                     {"finish_reason", "stop"}}})}});
                }
                const JsonValue& content =
                    body.at("messages")
                        .at(0)
                        .at("content");
                require(content.at(1).at("type") == "document",
                        "Anthropic URL document type");
                require(content.at(1).at("source").at("type") == "url",
                        "Anthropic URL document source");
                require(content.at(1).at("source").at("url") ==
                            "https://example.test/reference.pdf",
                        "Anthropic document URL");
                require(content.at(2).at("source").at("type") ==
                            "base64",
                        "Anthropic inline document source");
                require(content.at(2).at("source").at("media_type") ==
                            "application/pdf",
                        "Anthropic inline document media type");
                require(content.at(2).at("source").at("data") == "AQID",
                        "Anthropic inline document data");
                require(content.at(2).at("title") == "inline.pdf",
                        "Anthropic document title");
                return json_response(
                    200,
                    JsonValue{
                        {"id", "anthropic-media"},
                        {"model", "claude-media"},
                        {"content", JsonValue::array()},
                        {"stop_reason", "end_turn"}});
            });

        AnthropicConfig config("claude-media");
        config.api_key = "key";
        config.model.capabilities.file_input = true;
        ClientOptions options;
        options.transport = transport;
        AnthropicClient client(std::move(config), options);
        const AIResponse response = client.generate(
            Message::user(
                {Content::text("Compare these documents."),
                 Content::file_url(
                     "https://example.test/reference.pdf",
                     "application/pdf",
                     "reference.pdf"),
                 Content::file_bytes(
                     {1, 2, 3},
                     "application/pdf",
                     "inline.pdf")}));
        require(response.finish_reason == FinishReason::stop,
                "Anthropic file response");
    }

    {
        auto transport = std::make_shared<MockHttpTransport>(
            [](const HttpRequest& request) {
                const JsonValue body = JsonValue::parse(request.body);
                const JsonValue& parts =
                    body.at("contents")
                        .at(0)
                        .at("parts");
                require(parts.at(1).at("inlineData").at("mimeType") ==
                            "audio/mpeg",
                        "Gemini inline audio media type");
                require(parts.at(1).at("inlineData").at("data") == "AQI=",
                        "Gemini inline audio data");
                require(parts.at(2).at("inlineData").at("mimeType") ==
                            "application/pdf",
                        "Gemini inline file media type");
                require(!parts.at(2).at("inlineData").contains("displayName"),
                        "Gemini inline file uses the documented Blob fields");
                require(parts.at(3).at("fileData").at("fileUri") ==
                            "https://generativelanguage.googleapis.com/v1beta/files/file-1",
                        "Gemini file URI");
                return json_response(
                    200,
                    JsonValue{
                        {"candidates",
                         JsonValue::array(
                             {JsonValue{
                                 {"content",
                                  JsonValue{
                                      {"role", "model"},
                                      {"parts",
                                       JsonValue::array(
                                           {JsonValue{
                                                {"inlineData",
                                                 JsonValue{
                                                     {"mimeType",
                                                      "audio/mpeg"},
                                                     {"data", "AQID"}}}},
                                            JsonValue{
                                                {"fileData",
                                                 JsonValue{
                                                     {"mimeType",
                                                      "application/pdf"},
                                                     {"fileUri",
                                                      "gs://bucket/result.pdf"},
                                                     {"displayName",
                                                      "result.pdf"}}}}})}}},
                                 {"finishReason", "STOP"}}})}});
            });

        GeminiConfig config("gemini-media");
        config.api_key = "key";
        config.model.capabilities.audio_input = true;
        config.model.capabilities.audio_output = true;
        config.model.capabilities.file_input = true;
        ClientOptions options;
        options.transport = transport;
        GeminiClient client(std::move(config), options);
        const AIResponse response = client.generate(
            Message::user(
                {Content::text("Inspect these media."),
                 Content::audio_bytes({1, 2}, "audio/mpeg"),
                 Content::file_bytes(
                     {3, 4}, "application/pdf", "inline.pdf"),
                 Content::file_url(
                     "https://generativelanguage.googleapis.com/v1beta/files/file-1",
                     "application/pdf",
                     "uploaded.pdf")}));
        require(response.message.contents().size() == 2,
                "Gemini audio and file output count");
        require(response.message.contents().at(0).type() ==
                    ContentType::audio,
                "Gemini audio output type");
        require(response.message.contents().at(1).type() ==
                    ContentType::file,
                "Gemini file output type");
        require(response.message.contents().at(1).url() ==
                    "gs://bucket/result.pdf",
                "Gemini file output URI");
    }

    {
        std::size_t round = 0;
        auto transport = std::make_shared<MockHttpTransport>(
            [&](const HttpRequest& request) {
                const JsonValue body = JsonValue::parse(request.body);
                ++round;
                if (round == 2) {
                    const JsonValue& prior_audio =
                        body.at("messages").at(1).at("audio");
                    require(prior_audio ==
                                JsonValue{{"id", "audio-1"}},
                            "compatible audio history replays only its id");
                    return json_response(
                        200,
                        JsonValue{
                            {"id", "compatible-follow-up"},
                            {"model", "compatible-media"},
                            {"choices",
                             JsonValue::array(
                                 {JsonValue{
                                     {"message",
                                      JsonValue{
                                          {"role", "assistant"},
                                          {"content", "continued"}}},
                                     {"finish_reason", "stop"}}})}});
                }
                const JsonValue& content =
                    body.at("messages")
                        .at(0)
                        .at("content");
                require(content.at(1).at("type") == "input_audio",
                        "compatible audio input type");
                require(content.at(1).at("input_audio").at("format") ==
                            "mp3",
                        "compatible audio input format");
                require(content.at(2).at("type") == "file",
                        "compatible file input type");
                require(content.at(2).at("file").at("file_data") ==
                            "AwQ=",
                        "compatible file input data");
                require(content.at(2).at("file").at("filename") ==
                            "notes.txt",
                        "compatible file input name");
                return json_response(
                    200,
                    JsonValue{
                        {"id", "compatible-media"},
                        {"model", "compatible-media"},
                        {"choices",
                         JsonValue::array(
                             {JsonValue{
                                 {"message",
                                  JsonValue{
                                      {"role", "assistant"},
                                      {"content", nullptr},
                                      {"audio",
                                       JsonValue{
                                           {"id", "audio-1"},
                                           {"data", "AQID"},
                                           {"expires_at", 2000000000},
                                           {"transcript",
                                            "audio reply"}}}}},
                                 {"finish_reason", "stop"}}})}});
            });

        OpenAICompatibleConfig config(
            "compatible-media", "http://localhost:8080/v1");
        config.model.capabilities.audio_input = true;
        config.model.capabilities.audio_output = true;
        config.model.capabilities.file_input = true;
        ClientOptions options;
        options.transport = transport;
        OpenAICompatibleClient client(std::move(config), options);
        Session session;
        const AIResponse response = client.generate(
            session,
            Message::user(
                {Content::text("Inspect these media."),
                 Content::audio_bytes({1, 2}, "audio/mpeg"),
                 Content::file_bytes(
                     {3, 4}, "text/plain", "notes.txt")}));
        require(response.message.text() == "audio reply",
                "compatible audio transcript");
        require(response.message.contents().size() == 2,
                "compatible audio output count");
        require(response.message.contents().at(1).type() ==
                    ContentType::audio,
                "compatible audio output type");
        require(response.message.contents().at(1).bytes() ==
                    std::vector<std::uint8_t>({1, 2, 3}),
                "compatible audio output bytes");
        const AIResponse follow_up =
            client.generate(session, "Continue.");
        require(follow_up.message.text() == "continued",
                "compatible audio continuation response");
    }
}

void test_openai_compatible_legacy_function_call() {
    std::size_t round = 0;
    auto transport =
        std::make_shared<MockHttpTransport>(
            [&](const HttpRequest& request) {
                const JsonValue body = JsonValue::parse(request.body);
                ++round;
                require(body.at("tools").size() == 2,
                        "compatible native and client tools are combined");
                require(body.at("tools").at(0).at("type") == "custom",
                        "compatible native tool is preserved");
                require(body.at("tools")
                            .at(1)
                            .at("function")
                            .at("strict") == true,
                        "compatible function provider option is preserved");
                require(body.at("tools")
                            .at(1)
                            .at("function")
                            .at("name") == "weather",
                        "compatible stable tool name wins");

                if (round == 1) {
                    return json_response(
                        200,
                        JsonValue{
                            {"id", "legacy-1"},
                            {"model", "legacy-model"},
                            {"choices",
                             JsonValue::array({JsonValue{
                                 {"message",
                                  JsonValue{
                                      {"role", "assistant"},
                                      {"content", nullptr},
                                      {"function_call",
                                       JsonValue{
                                           {"name", "weather"},
                                           {"arguments",
                                            R"({"city":"Paris"})"}}}}},
                                 {"finish_reason", "function_call"}}})}});
                }

                const JsonValue& messages = body.at("messages");
                require(messages.size() == 3,
                        "compatible legacy continuation turn count");
                require(messages.at(2).at("role") == "function",
                        "compatible legacy result role");
                require(messages.at(2).at("name") == "weather",
                        "compatible legacy result name");
                require(!messages.at(2).contains("tool_call_id"),
                        "compatible legacy result omits synthetic id");
                require(JsonValue::parse(
                            messages.at(2).at("content").get<std::string>())
                            .at("temp_c") == 18,
                        "compatible legacy result content");
                return json_response(
                    200,
                    JsonValue{
                        {"id", "legacy-2"},
                        {"model", "legacy-model"},
                        {"choices",
                         JsonValue::array({JsonValue{
                             {"message",
                              JsonValue{{"role", "assistant"},
                                        {"content", "18 C"}}},
                             {"finish_reason", "stop"}}})}});
            });

    OpenAICompatibleConfig config(
        "legacy-model", "http://localhost:8080/v1");
    config.model.capabilities.tools = true;
    config.model.capabilities.parallel_tool_calls = true;
    config.model.provider_options["tools"] = JsonValue::array(
        {JsonValue{{"type", "custom"},
                   {"custom", JsonValue{{"name", "native"}}}}});
    ClientOptions options;
    options.transport = transport;
    options.tools = {
        weather_tool(18,
                     JsonValue{{"strict", true},
                               {"name", "ignored-name"}})};
    OpenAICompatibleClient client(std::move(config), options);

    Session session;
    const AIResponse response = client.generate(session, "Weather?");
    require(round == 2, "compatible legacy tool loop rounds");
    require(response.message.text() == "18 C",
            "compatible legacy final response");
    require(response.tool_calls == 1,
            "compatible legacy normalized tool count");
    const std::string synthetic_call_id =
        session.messages().at(1).tool_calls().at(0).id;
    require(synthetic_call_id.find("legacy_function_call_run-") == 0,
            "compatible legacy synthetic id is scoped to the generation");
    require(synthetic_call_id.size() >= 2U &&
                synthetic_call_id.compare(
                    synthetic_call_id.size() - 2U, 2U, "_1") == 0,
            "compatible legacy synthetic id includes request history");
}

void test_openai_compatible_rejects_multimodal_tool_result() {
    ToolSpec spec;
    spec.name = "image_result";
    spec.description = "Returns an image.";
    auto tool = std::make_shared<FunctionTool>(
        std::move(spec),
        [](ToolContext&, const JsonValue&) {
            ToolOutput output;
            output.contents.push_back(
                Content::image_bytes({1, 2, 3}, "image/png"));
            return output;
        });

    auto transport =
        std::make_shared<MockHttpTransport>(
            [](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{
                        {"id", "multimodal-compatible"},
                        {"choices",
                         JsonValue::array({JsonValue{
                             {"message",
                              JsonValue{
                                  {"role", "assistant"},
                                  {"content", nullptr},
                                  {"tool_calls",
                                   JsonValue::array({JsonValue{
                                       {"id", "image-call"},
                                       {"type", "function"},
                                       {"function",
                                        JsonValue{
                                            {"name", "image_result"},
                                            {"arguments", "{}"}}}}})}}},
                             {"finish_reason", "tool_calls"}}})}});
            });

    OpenAICompatibleConfig config(
        "local-model", "http://localhost:8080/v1");
    config.model.capabilities.tools = true;
    ClientOptions options;
    options.transport = transport;
    options.tools = {tool};
    OpenAICompatibleClient client(std::move(config), options);

    require_throws<ProviderError>(
        [&] { (void)client.generate("Return an image."); },
        "compatible multimodal tool result must fail explicitly");
    require(transport->requests().size() == 1,
            "unsupported compatible tool result is not silently sent");
}

void test_missing_required_tool_call_ids() {
    {
        auto transport =
            std::make_shared<MockHttpTransport>(
                [](const HttpRequest&) {
                    return json_response(
                        200,
                        JsonValue{
                            {"id", "missing-openai-id"},
                            {"status", "completed"},
                            {"output",
                             JsonValue::array({JsonValue{
                                 {"type", "function_call"},
                                 {"name", "weather"},
                                 {"arguments", "{}"}}})}});
                });
        OpenAIConfig config("gpt-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        OpenAIClient client(std::move(config), options);
        require_throws<ProviderError>(
            [&] { (void)client.generate("call"); },
            "OpenAI missing call id must fail");
    }
    {
        auto transport =
            std::make_shared<MockHttpTransport>(
                [](const HttpRequest&) {
                    return json_response(
                        200,
                        JsonValue{
                            {"id", "missing-anthropic-id"},
                            {"content",
                             JsonValue::array({JsonValue{
                                 {"type", "tool_use"},
                                 {"name", "weather"},
                                 {"input", JsonValue::object()}}})},
                            {"stop_reason", "tool_use"}});
                });
        AnthropicConfig config("claude-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        AnthropicClient client(std::move(config), options);
        require_throws<ProviderError>(
            [&] { (void)client.generate("call"); },
            "Anthropic missing call id must fail");
    }
    {
        auto transport =
            std::make_shared<MockHttpTransport>(
                [](const HttpRequest&) {
                    return json_response(
                        200,
                        JsonValue{
                            {"id", "missing-compatible-id"},
                            {"choices",
                             JsonValue::array({JsonValue{
                                 {"message",
                                  JsonValue{
                                      {"role", "assistant"},
                                      {"tool_calls",
                                       JsonValue::array({JsonValue{
                                           {"type", "function"},
                                           {"function",
                                            JsonValue{{"name", "weather"},
                                                      {"arguments", "{}"}}}}})}}},
                                 {"finish_reason", "tool_calls"}}})}});
                });
        OpenAICompatibleConfig config(
            "local-model", "http://localhost:8080/v1");
        ClientOptions options;
        options.transport = transport;
        OpenAICompatibleClient client(std::move(config), options);
        require_throws<ProviderError>(
            [&] { (void)client.generate("call"); },
            "compatible missing call id must fail");
    }
}

void test_environment_credential_resolution() {
    ScopedEnvironment environment(
        "NEURALPLUS_TEST_API_KEY", "environment-test-key");

    std::size_t request_count = 0;
    auto transport =
        std::make_shared<MockHttpTransport>(
            [&](const HttpRequest& request) {
                ++request_count;
                const std::string expected =
                    request_count == 1
                        ? "Bearer environment-test-key"
                        : "Bearer explicit-test-key";
                require(header_value(request, "Authorization").value() ==
                            expected,
                        "credential resolution order");
                return json_response(
                    200,
                    JsonValue{
                        {"id", "credential-response"},
                        {"choices",
                         JsonValue::array({JsonValue{
                             {"message",
                              JsonValue{{"role", "assistant"},
                                        {"content", "ok"}}},
                             {"finish_reason", "stop"}}})}});
            });

    ClientOptions options;
    options.transport = transport;

    OpenAICompatibleConfig environment_config(
        "local-model", "http://localhost:8080/v1");
    environment_config.api_key_environment =
        "NEURALPLUS_TEST_API_KEY";
    OpenAICompatibleClient environment_client(
        std::move(environment_config), options);
    require(environment_client.generate("first").message.text() == "ok",
            "environment credential request");

    OpenAICompatibleConfig explicit_config(
        "local-model", "http://localhost:8080/v1");
    explicit_config.api_key = "explicit-test-key";
    explicit_config.api_key_environment =
        "NEURALPLUS_TEST_API_KEY";
    OpenAICompatibleClient explicit_client(
        std::move(explicit_config), options);
    require(explicit_client.generate("second").message.text() == "ok",
            "explicit credential request");
}

template <typename Function>
void require_provider_error(Function&& function, Provider provider, int status,
                            const std::string& code, const std::string& request_id) {
    try {
        function();
    } catch (const ProviderError& error) {
        require(error.provider() == provider, "provider error family");
        require(error.status() == status, "provider error HTTP status");
        require(error.code() == code, "provider error code");
        require(error.request_id() == request_id, "provider error request id");
        return;
    }
    throw std::runtime_error("expected ProviderError");
}

template <typename Function>
void require_redacted_provider_error(
    Function&& function, Provider provider, int status,
    const std::vector<std::string>& secrets) {
    try {
        function();
    } catch (const ProviderError& error) {
        require(error.provider() == provider,
                "redacted provider error family");
        require(error.status() == status,
                "redacted provider error HTTP status");
        const std::vector<std::string> exposed_fields{
            error.what(), error.code(), error.request_id()};
        for (const auto& secret : secrets) {
            for (const auto& field : exposed_fields) {
                require(field.find(secret) == std::string::npos,
                        "provider error must not expose a credential");
            }
        }
        require(std::string(error.what()).find("diagnostic-marker") !=
                    std::string::npos,
                "provider error preserves nonsecret message diagnostics");
        require(error.code().find("code-marker") != std::string::npos,
                "provider error preserves nonsecret code diagnostics");
        require(error.request_id().find("request-marker") !=
                    std::string::npos,
                "provider error preserves nonsecret request diagnostics");
        require(std::string(error.what()).find("[REDACTED]") !=
                    std::string::npos,
                "provider error message marks redaction");
        require(error.code().find("[REDACTED]") != std::string::npos,
                "provider error code marks redaction");
        require(error.request_id().find("[REDACTED]") !=
                    std::string::npos,
                "provider error request id marks redaction");
        return;
    }
    throw std::runtime_error("expected redacted ProviderError");
}

void test_provider_error_credential_redaction() {
    {
        const std::string configured_secret =
            "openai-configured-secret-7c93";
        const std::string override_secret =
            "openai-extra-header-secret-a184";
        const std::string custom_secret =
            "openai-custom-header-secret-d672";
        auto transport = std::make_shared<MockHttpTransport>(
            [configured_secret, override_secret, custom_secret](
                const HttpRequest& request) {
                require(
                    header_value(request, "Authorization").value() ==
                        "Bearer " + override_secret,
                    "OpenAI extra Authorization header overrides default");
                require(
                    header_value(request, "X-Client-Metadata").value() ==
                        custom_secret,
                    "OpenAI arbitrary extra header is sent");
                return json_response(
                    401,
                    JsonValue{
                        {"error",
                         JsonValue{
                             {"message",
                              "diagnostic-marker " +
                                  configured_secret + " " +
                                  override_secret + " " +
                                  custom_secret},
                             {"code",
                              "code-marker-" + override_secret + "-" +
                                  configured_secret + "-" +
                                  custom_secret}}}},
                    {{"x-request-id",
                      "request-marker-" + configured_secret + "-" +
                          override_secret + "-" + custom_secret}});
            });
        OpenAIConfig config("gpt-test");
        config.api_key = configured_secret;
        config.extra_headers = {
            {"Authorization", "Bearer " + override_secret},
            {"X-Client-Metadata", custom_secret, true}};
        ClientOptions options;
        options.transport = transport;
        OpenAIClient client(std::move(config), options);
        require_redacted_provider_error(
            [&] { (void)client.generate("hello"); },
            Provider::openai,
            401,
            {configured_secret, override_secret,
             "Bearer " + override_secret, custom_secret});
    }
    {
        const std::string secret =
            "anthropic-configured-secret-f251";
        auto transport = std::make_shared<MockHttpTransport>(
            [secret](const HttpRequest&) {
                return json_response(
                    429,
                    JsonValue{
                        {"type", "error"},
                        {"error",
                         JsonValue{
                             {"message",
                              "diagnostic-marker " + secret},
                             {"type", "code-marker-" + secret}}},
                        {"request_id",
                         "request-marker-" + secret}});
            });
        AnthropicConfig config("claude-test");
        config.api_key = secret;
        ClientOptions options;
        options.transport = transport;
        AnthropicClient client(std::move(config), options);
        require_redacted_provider_error(
            [&] { (void)client.generate("hello"); },
            Provider::anthropic,
            429,
            {secret});
    }
    {
        const std::string secret =
            "gemini-configured-secret-90be";
        auto transport = std::make_shared<MockHttpTransport>(
            [secret](const HttpRequest&) {
                return json_response(
                    403,
                    JsonValue{
                        {"error",
                         JsonValue{
                             {"message",
                              "diagnostic-marker " + secret},
                             {"status", "code-marker-" + secret}}}},
                    {{"x-request-id",
                      "request-marker-" + secret}});
            });
        GeminiConfig config("gemini-test");
        config.api_key = secret;
        ClientOptions options;
        options.transport = transport;
        GeminiClient client(std::move(config), options);
        require_redacted_provider_error(
            [&] { (void)client.generate("hello"); },
            Provider::gemini,
            403,
            {secret});
    }
    {
        const std::string secret =
            "compatible-configured-secret-b602";
        auto transport = std::make_shared<MockHttpTransport>(
            [secret](const HttpRequest&) {
                return json_response(
                    401,
                    JsonValue{
                        {"error",
                         JsonValue{
                             {"message",
                              "diagnostic-marker " + secret},
                             {"type", "code-marker-" + secret}}}},
                    {{"x-request-id",
                      "request-marker-" + secret}});
            });
        OpenAICompatibleConfig config(
            "local-test", "http://localhost:8080/v1");
        config.api_key = secret;
        ClientOptions options;
        options.transport = transport;
        OpenAICompatibleClient client(std::move(config), options);
        require_redacted_provider_error(
            [&] { (void)client.generate("hello"); },
            Provider::openai_compatible,
            401,
            {secret});
    }
}

void test_successful_response_credential_redaction() {
    const std::string secret =
        "compatible-success-secret-2e71";
    const std::string custom_secret =
        "compatible-custom-header-secret-410c";
    auto transport = std::make_shared<MockHttpTransport>(
        [secret, custom_secret](const HttpRequest& request) {
            require(header_value(request, "X-Client-Metadata") ==
                        std::optional<std::string>{custom_secret},
                    "compatible arbitrary extra header is sent");
            require(header_value(request, "X-Version") ==
                        std::optional<std::string>{"1"},
                    "compatible ordinary extra header is sent");
            return json_response(
                200,
                JsonValue{
                    {"id", "body-marker-" + secret + "-" + custom_secret},
                    {"model", "model-marker-" + custom_secret},
                    {"choices",
                     JsonValue::array(
                         {JsonValue{
                             {"message",
                              JsonValue{
                                  {"role", "assistant"},
                                  {"content",
                                   "content-marker-" + secret + "-" +
                                       custom_secret},
                                  {"extension",
                                   JsonValue{{"echo", custom_secret},
                                             {"ordinary", "version 1"}}}}},
                             {"finish_reason", "stop"}}})}},
                {{"x-request-id",
                  "request-marker-" + secret + "-" + custom_secret}});
        });
    auto tracer = std::make_shared<InMemoryTracer>();
    OpenAICompatibleConfig config(
        "local-test", "http://localhost:8080/v1");
    config.api_key = secret;
    config.extra_headers = {
        {"X-Client-Metadata", custom_secret, true},
        {"X-Version", "1"}};
    ClientOptions options;
    options.transport = transport;
    options.tracers = {tracer};
    options.capture_trace_payloads = true;
    OpenAICompatibleClient client(std::move(config), options);

    const AIResponse response = client.generate("hello");
    for (const std::string& sensitive_value : {secret, custom_secret}) {
        require(response.provider_request_id.find(sensitive_value) ==
                    std::string::npos,
                "successful request id must not expose a credential");
        require(response.provider_model.find(sensitive_value) ==
                    std::string::npos,
                "successful model id must not expose a credential");
        require(response.message.text().find(sensitive_value) ==
                    std::string::npos,
                "successful content must not expose an echoed credential");
        require(response.message.provider_metadata().dump().find(
                    sensitive_value) == std::string::npos,
                "message metadata must not expose an echoed credential");
        require(response.provider_metadata.dump().find(sensitive_value) ==
                    std::string::npos,
                "response metadata must not expose an echoed credential");
        for (const TraceEvent& event : tracer->events()) {
            require(event.attributes.dump().find(sensitive_value) ==
                        std::string::npos,
                    "trace attributes must not expose an echoed credential");
            require(event.payload.find(sensitive_value) ==
                        std::string::npos,
                    "trace payload must not expose an echoed credential");
        }
    }
    require(response.provider_metadata.dump().find("version 1") !=
                std::string::npos,
            "ordinary extra-header values do not corrupt responses");
}

void test_malformed_success_envelopes() {
    {
        auto transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{{"id", "malformed-openai-id"}});
            });
        OpenAIConfig config("gpt-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        OpenAIClient client(std::move(config), options);
        require_provider_error(
            [&] { (void)client.generate("hello"); },
            Provider::openai,
            200,
            "malformed_response",
            "malformed-openai-id");
    }
    {
        auto transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{{"id", "malformed-anthropic-id"}});
            });
        AnthropicConfig config("claude-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        AnthropicClient client(std::move(config), options);
        require_provider_error(
            [&] { (void)client.generate("hello"); },
            Provider::anthropic,
            200,
            "malformed_response",
            "malformed-anthropic-id");
    }
    {
        auto transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{{"responseId", "malformed-gemini-id"}});
            });
        GeminiConfig config("gemini-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        GeminiClient client(std::move(config), options);
        require_provider_error(
            [&] { (void)client.generate("hello"); },
            Provider::gemini,
            200,
            "malformed_response",
            "malformed-gemini-id");
    }
    {
        auto transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{{"id", "malformed-compatible-id"}});
            });
        OpenAICompatibleConfig config(
            "local-test", "http://localhost:8080/v1");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        OpenAICompatibleClient client(std::move(config), options);
        require_provider_error(
            [&] { (void)client.generate("hello"); },
            Provider::openai_compatible,
            200,
            "malformed_response",
            "malformed-compatible-id");
    }
}

void test_malformed_success_inner_content() {
    {
        const std::vector<JsonValue> malformed_outputs = {
            JsonValue::array({JsonValue(7)}),
            JsonValue::array(
                {JsonValue{{"type", "message"}, {"content", "not-an-array"}}}),
            JsonValue::array(
                {JsonValue{{"type", "message"},
                           {"content", JsonValue::array({JsonValue(false)})}}}),
            JsonValue::array(
                {JsonValue{{"type", "message"},
                           {"content",
                            JsonValue::array({JsonValue{
                                {"type", "output_text"}, {"text", 7}}})}}}),
            JsonValue::array(
                {JsonValue{{"type", "function_call"},
                           {"call_id", 7},
                           {"name", "weather"},
                           {"arguments", "{}"}}}),
            JsonValue::array(
                {JsonValue{{"type", "function_call"},
                           {"call_id", "call-1"},
                           {"arguments", "{}"}}}),
            JsonValue::array(
                {JsonValue{{"type", "function_call"},
                           {"call_id", "call-1"},
                           {"name", "weather"}}}),
            JsonValue::array(
                {JsonValue{{"type", "function_call"},
                           {"call_id", "call-1"},
                           {"name", "weather"},
                           {"arguments", JsonValue::object()}}}),
            JsonValue::array(
                {JsonValue{{"type", "function_call"},
                           {"id", 11},
                           {"call_id", "call-1"},
                           {"name", "weather"},
                           {"arguments", "{}"}}}),
        };
        for (const JsonValue& output : malformed_outputs) {
            auto transport = std::make_shared<MockHttpTransport>(
                [output](const HttpRequest&) {
                    return json_response(
                        200,
                        JsonValue{{"status", "completed"}, {"output", output}},
                        {{"x-request-id", "inner-openai-id"}});
                });
            OpenAIConfig config("gpt-test");
            config.api_key = "key";
            ClientOptions options;
            options.transport = transport;
            OpenAIClient client(std::move(config), options);
            require_provider_error(
                [&] { (void)client.generate("hello"); },
                Provider::openai,
                200,
                "malformed_response",
                "inner-openai-id");
        }

        auto empty_image_transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{
                        {"status", "completed"},
                        {"output",
                         JsonValue::array(
                             {JsonValue{{"type", "image_generation_call"},
                                        {"result", ""}}})}},
                    {{"x-request-id", "empty-openai-image-id"}});
            });
        OpenAIConfig empty_image_config("gpt-test");
        empty_image_config.api_key = "key";
        ClientOptions empty_image_options;
        empty_image_options.transport = empty_image_transport;
        OpenAIClient empty_image_client(
            std::move(empty_image_config), empty_image_options);
        require_provider_error(
            [&] { (void)empty_image_client.generate("hello"); },
            Provider::openai,
            200,
            "malformed_image",
            "empty-openai-image-id");
    }
    {
        const std::vector<JsonValue> malformed_content = {
            JsonValue::array({JsonValue(false)}),
            JsonValue::array(
                {JsonValue{{"type", "text"}, {"text", 7}}}),
            JsonValue::array(
                {JsonValue{{"type", "tool_use"},
                           {"id", "call-1"},
                           {"name", "weather"},
                           {"input", JsonValue::array()}}}),
            JsonValue::array(
                {JsonValue{{"type", "tool_use"},
                           {"id", ""},
                           {"name", "weather"},
                           {"input", JsonValue::object()}}}),
            JsonValue::array(
                {JsonValue{{"type", "tool_use"},
                           {"id", "call-1"},
                           {"name", ""},
                           {"input", JsonValue::object()}}}),
        };
        for (const JsonValue& content : malformed_content) {
            auto transport = std::make_shared<MockHttpTransport>(
                [content](const HttpRequest&) {
                    return json_response(
                        200,
                        JsonValue{{"content", content},
                                  {"stop_reason", "end_turn"}},
                        {{"request-id", "inner-anthropic-id"}});
                });
            AnthropicConfig config("claude-test");
            config.api_key = "key";
            ClientOptions options;
            options.transport = transport;
            AnthropicClient client(std::move(config), options);
            require_provider_error(
                [&] { (void)client.generate("hello"); },
                Provider::anthropic,
                200,
                "malformed_response",
                "inner-anthropic-id");
        }
    }
    {
        const std::vector<JsonValue> malformed_parts = {
            JsonValue::array({JsonValue(3)}),
            JsonValue::array({JsonValue{{"text", false}}}),
            JsonValue::array(
                {JsonValue{{"inlineData", JsonValue::array()}}}),
            JsonValue::array(
                {JsonValue{{"functionCall",
                            JsonValue{{"name", "weather"},
                                      {"args", JsonValue::array()}}}}}),
            JsonValue::array(
                {JsonValue{{"functionCall",
                            JsonValue{{"name", ""},
                                      {"args", JsonValue::object()}}}}}),
            JsonValue::array(
                {JsonValue{{"text", "ignored"},
                           {"functionCall",
                            JsonValue{{"name", "weather"},
                                      {"args", JsonValue::object()}}}}}),
        };
        for (const JsonValue& parts : malformed_parts) {
            auto transport = std::make_shared<MockHttpTransport>(
                [parts](const HttpRequest&) {
                    return json_response(
                        200,
                        JsonValue{
                            {"candidates",
                             JsonValue::array(
                                 {JsonValue{
                                     {"content", JsonValue{{"parts", parts}}},
                                     {"finishReason", "STOP"}}})}},
                        {{"x-request-id", "inner-gemini-id"}});
                });
            GeminiConfig config("gemini-test");
            config.api_key = "key";
            ClientOptions options;
            options.transport = transport;
            GeminiClient client(std::move(config), options);
            require_provider_error(
                [&] { (void)client.generate("hello"); },
                Provider::gemini,
                200,
                "malformed_response",
                "inner-gemini-id");
        }

        auto empty_inline_transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{
                        {"candidates",
                         JsonValue::array(
                             {JsonValue{
                                 {"content",
                                  JsonValue{
                                      {"parts",
                                       JsonValue::array(
                                           {JsonValue{
                                               {"inlineData",
                                                JsonValue{
                                                    {"mimeType", "image/png"},
                                                    {"data", ""}}}}})}}},
                                 {"finishReason", "STOP"}}})}},
                    {{"x-request-id", "empty-gemini-inline-id"}});
            });
        GeminiConfig empty_inline_config("gemini-test");
        empty_inline_config.api_key = "key";
        ClientOptions empty_inline_options;
        empty_inline_options.transport = empty_inline_transport;
        GeminiClient empty_inline_client(
            std::move(empty_inline_config), empty_inline_options);
        require_provider_error(
            [&] { (void)empty_inline_client.generate("hello"); },
            Provider::gemini,
            200,
            "malformed_inline_data",
            "empty-gemini-inline-id");
    }
    {
        const std::vector<JsonValue> malformed_content = {
            JsonValue(12),
            JsonValue::array({JsonValue(false)}),
            JsonValue::array(
                {JsonValue{{"type", "text"}, {"text", false}}}),
            JsonValue::array(
                {JsonValue{{"type", "image_url"},
                           {"image_url", JsonValue{{"url", 7}}}}}),
            JsonValue::array(
                {JsonValue{{"type", "image_url"},
                           {"image_url", JsonValue{{"url", ""}}}}}),
        };
        for (const JsonValue& content : malformed_content) {
            auto transport = std::make_shared<MockHttpTransport>(
                [content](const HttpRequest&) {
                    return json_response(
                        200,
                        JsonValue{
                            {"choices",
                             JsonValue::array(
                                 {JsonValue{
                                     {"message",
                                      JsonValue{{"role", "assistant"},
                                                {"content", content}}},
                                     {"finish_reason", "stop"}}})}},
                        {{"x-request-id", "inner-compatible-id"}});
                });
            OpenAICompatibleConfig config(
                "local-test", "http://localhost:8080/v1");
            config.api_key = "key";
            ClientOptions options;
            options.transport = transport;
            OpenAICompatibleClient client(std::move(config), options);
            require_provider_error(
                [&] { (void)client.generate("hello"); },
                Provider::openai_compatible,
                200,
                "malformed_response",
                "inner-compatible-id");
        }

        const std::vector<JsonValue> malformed_messages = {
            JsonValue{{"role", "assistant"},
                      {"content", nullptr},
                      {"tool_calls", JsonValue::object()}},
            JsonValue{{"role", "assistant"},
                      {"content", nullptr},
                      {"tool_calls", JsonValue::array({JsonValue(false)})}},
            JsonValue{
                {"role", "assistant"},
                {"content", nullptr},
                {"tool_calls",
                 JsonValue::array({JsonValue{
                     {"id", "call-1"},
                     {"type", false},
                     {"function",
                      JsonValue{{"name", "weather"}, {"arguments", "{}"}}}}})}},
            JsonValue{
                {"role", "assistant"},
                {"content", nullptr},
                {"tool_calls",
                 JsonValue::array({JsonValue{
                     {"id", "call-1"},
                     {"type", "function"},
                     {"function", JsonValue::array()}}})}},
            JsonValue{
                {"role", "assistant"},
                {"content", nullptr},
                {"tool_calls",
                 JsonValue::array({JsonValue{
                     {"id", "call-1"},
                     {"type", "function"},
                     {"function", JsonValue{{"arguments", "{}"}}}}})}},
            JsonValue{
                {"role", "assistant"},
                {"content", nullptr},
                {"tool_calls",
                 JsonValue::array({JsonValue{
                     {"id", "call-1"},
                     {"type", "function"},
                     {"function",
                      JsonValue{{"name", "weather"},
                                {"arguments", JsonValue::object()}}}}})}},
            JsonValue{{"role", "assistant"},
                      {"content", nullptr},
                      {"function_call", JsonValue::array()}},
            JsonValue{{"role", "assistant"},
                      {"content", nullptr},
                      {"function_call", JsonValue{{"arguments", "{}"}}}},
            JsonValue{{"role", "assistant"},
                      {"content", nullptr},
                      {"function_call",
                       JsonValue{{"name", "weather"},
                                 {"arguments", JsonValue::object()}}}},
        };
        for (const JsonValue& message : malformed_messages) {
            auto transport = std::make_shared<MockHttpTransport>(
                [message](const HttpRequest&) {
                    return json_response(
                        200,
                        JsonValue{
                            {"choices",
                             JsonValue::array(
                                 {JsonValue{
                                     {"message", message},
                                     {"finish_reason", "tool_calls"}}})}},
                        {{"x-request-id", "inner-compatible-tool-id"}});
                });
            OpenAICompatibleConfig config(
                "local-test", "http://localhost:8080/v1");
            config.api_key = "key";
            ClientOptions options;
            options.transport = transport;
            OpenAICompatibleClient client(std::move(config), options);
            require_provider_error(
                [&] { (void)client.generate("hello"); },
                Provider::openai_compatible,
                200,
                "malformed_response",
                "inner-compatible-tool-id");
        }

        auto empty_audio_transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{
                        {"choices",
                         JsonValue::array(
                             {JsonValue{
                                 {"message",
                                  JsonValue{
                                      {"role", "assistant"},
                                      {"content", nullptr},
                                      {"audio",
                                       JsonValue{{"id", "audio-empty"},
                                                 {"data", ""}}}}},
                                 {"finish_reason", "stop"}}})}},
                    {{"x-request-id", "empty-compatible-audio-id"}});
            });
        OpenAICompatibleConfig empty_audio_config(
            "local-test", "http://localhost:8080/v1");
        empty_audio_config.api_key = "key";
        ClientOptions empty_audio_options;
        empty_audio_options.transport = empty_audio_transport;
        OpenAICompatibleClient empty_audio_client(
            std::move(empty_audio_config), empty_audio_options);
        require_provider_error(
            [&] { (void)empty_audio_client.generate("hello"); },
            Provider::openai_compatible,
            200,
            "malformed_audio",
            "empty-compatible-audio-id");
    }
}

void test_valid_empty_and_blocked_success_envelopes() {
    {
        auto transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{{"id", "empty-openai"},
                              {"status", "completed"},
                              {"output", JsonValue::array()}});
            });
        OpenAIConfig config("gpt-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        OpenAIClient client(std::move(config), options);
        require(client.generate("hello").message.contents().empty(),
                "OpenAI empty output array remains valid");
    }
    {
        auto transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{{"id", "empty-anthropic"},
                              {"content", JsonValue::array()},
                              {"stop_reason", "end_turn"}});
            });
        AnthropicConfig config("claude-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        AnthropicClient client(std::move(config), options);
        require(client.generate("hello").message.contents().empty(),
                "Anthropic empty content array remains valid");
    }
    {
        auto transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{
                        {"id", "empty-compatible"},
                        {"choices",
                         JsonValue::array({JsonValue{
                             {"message",
                              JsonValue{{"role", "assistant"},
                                        {"content", JsonValue::array()}}},
                             {"finish_reason", "stop"}}})}});
            });
        OpenAICompatibleConfig config(
            "local-test", "http://localhost:8080/v1");
        ClientOptions options;
        options.transport = transport;
        OpenAICompatibleClient client(std::move(config), options);
        require(client.generate("hello").message.contents().empty(),
                "compatible empty content array remains valid");
    }
    {
        auto transport =
            std::make_shared<MockHttpTransport>([](const HttpRequest&) {
                return json_response(
                    200,
                    JsonValue{
                        {"promptFeedback",
                         JsonValue{{"blockReason", "SAFETY"}}}});
            });
        GeminiConfig config("gemini-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        GeminiClient client(std::move(config), options);
        const AIResponse response = client.generate("hello");
        require(response.message.contents().empty(),
                "Gemini prompt block has no synthetic content");
        require(response.finish_reason == FinishReason::content_filter,
                "Gemini prompt block remains a content filter response");
    }
}

void test_provider_error_envelopes() {
    {
        auto transport = std::make_shared<MockHttpTransport>([](const HttpRequest&) {
            return json_response(400,
                                 JsonValue{{"error", JsonValue{{"message", "bad input"},
                                                               {"type", "invalid_request_error"},
                                                               {"param", "input"},
                                                               {"code", "invalid_input"}}}},
                                 {{"x-request-id", "openai-error-id"}});
        });
        OpenAIConfig config("gpt-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        OpenAIClient client(std::move(config), options);
        require_provider_error([&] { (void)client.generate("hello"); }, Provider::openai, 400,
                               "invalid_input", "openai-error-id");
    }
    {
        auto transport = std::make_shared<MockHttpTransport>([](const HttpRequest&) {
            return json_response(
                529,
                JsonValue{{"type", "error"},
                          {"error", JsonValue{{"type", "overloaded_error"}, {"message", "busy"}}},
                          {"request_id", "anthropic-body-id"}});
        });
        AnthropicConfig config("claude-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        AnthropicClient client(std::move(config), options);
        require_provider_error([&] { (void)client.generate("hello"); }, Provider::anthropic, 529,
                               "overloaded_error", "anthropic-body-id");
    }
    {
        auto transport = std::make_shared<MockHttpTransport>([](const HttpRequest&) {
            return json_response(429,
                                 JsonValue{{"error", JsonValue{{"code", 429},
                                                               {"message", "quota"},
                                                               {"status", "RESOURCE_EXHAUSTED"},
                                                               {"details", JsonValue::array()}}}},
                                 {{"x-request-id", "gemini-error-id"}});
        });
        GeminiConfig config("gemini-test");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        GeminiClient client(std::move(config), options);
        require_provider_error([&] { (void)client.generate("hello"); }, Provider::gemini, 429,
                               "RESOURCE_EXHAUSTED", "gemini-error-id");
    }
    {
        auto transport = std::make_shared<MockHttpTransport>([](const HttpRequest&) {
            return json_response(401,
                                 JsonValue{{"error", JsonValue{{"message", "unauthorized"},
                                                               {"type", "authentication_error"},
                                                               {"param", nullptr},
                                                               {"code", nullptr}}}},
                                 {{"x-request-id", "compatible-error-id"}});
        });
        OpenAICompatibleConfig config("local-test", "http://localhost:8080/v1");
        config.api_key = "key";
        ClientOptions options;
        options.transport = transport;
        OpenAICompatibleClient client(std::move(config), options);
        require_provider_error([&] { (void)client.generate("hello"); }, Provider::openai_compatible,
                               401, "authentication_error", "compatible-error-id");
    }
}

void test_provider_descriptor_mismatch_is_rejected() {
    OpenAIConfig config("gpt-test");
    config.api_key = "key";
    config.model.provider = Provider::anthropic;
    ClientOptions options;
    options.transport =
        std::make_shared<MockHttpTransport>([](const HttpRequest&) { return HttpResponse{}; });
    require_throws<ConfigurationError>([&] { OpenAIClient rejected(config, options); },
                                       "provider descriptor mismatch");

    require_throws<ConfigurationError>(
        [&] {
            GeminiConfig invalid_model("bad/model");
            invalid_model.api_key = "key";
            GeminiClient rejected(std::move(invalid_model), options);
        },
        "Gemini model path segment validation");

    GeminiConfig invalid_image_config("gemini-test");
    invalid_image_config.api_key = "key";
    GeminiClient gemini_invalid_image(std::move(invalid_image_config), options);
    require_throws<ProviderError>(
        [&] {
            (void)gemini_invalid_image.generate(Message::user(
                {Content::image_url("https://example.test/not-uploaded.png", "image/png")}));
        },
        "Gemini arbitrary web image URL rejection");

    require_throws<ConfigurationError>(
        [&] {
            AnthropicConfig invalid("claude-test");
            invalid.api_key = "key";
            invalid.api_version.clear();
            AnthropicClient rejected(std::move(invalid), options);
        },
        "Anthropic empty API version");
    require_throws<ConfigurationError>(
        [&] {
            OpenAICompatibleConfig invalid("local-test", "");
            OpenAICompatibleClient rejected(std::move(invalid), options);
        },
        "compatible empty base URL");
}

}  // namespace

int main() {
    test_openai_multimodal_text_and_usage();
    test_openai_native_tool_continuation();
    test_anthropic_multimodal_tool_continuation();
    test_anthropic_pause_turn_continuation();
    test_gemini_thought_signature_continuation();
    test_gemini_multimodal_tool_result_and_synthetic_id();
    test_sparse_openai_compatible_response();
    test_audio_and_file_content_mappings();
    test_openai_compatible_legacy_function_call();
    test_openai_compatible_rejects_multimodal_tool_result();
    test_missing_required_tool_call_ids();
    test_environment_credential_resolution();
    test_malformed_success_envelopes();
    test_malformed_success_inner_content();
    test_valid_empty_and_blocked_success_envelopes();
    test_provider_error_envelopes();
    test_provider_error_credential_redaction();
    test_successful_response_credential_redaction();
    test_provider_descriptor_mismatch_is_rejected();
    return 0;
}
