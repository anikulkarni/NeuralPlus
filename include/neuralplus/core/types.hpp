#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace neuralplus {

enum class Role {
    system,
    user,
    assistant,
    tool,
};

enum class FinishReason {
    stop,
    tool_calls,
    length,
    error,
    unknown,
};

struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments_json{"{}"};
};

struct Message {
    Role role{Role::user};
    std::string content;
    std::string name;
    std::string tool_call_id;
    std::vector<ToolCall> tool_calls;

    static Message system(std::string text) {
        return Message{Role::system, std::move(text), {}, {}, {}};
    }

    static Message user(std::string text) {
        return Message{Role::user, std::move(text), {}, {}, {}};
    }

    static Message assistant(std::string text, std::vector<ToolCall> calls = {}) {
        return Message{Role::assistant, std::move(text), {}, {}, std::move(calls)};
    }

    static Message tool(std::string call_id, std::string tool_name, std::string text) {
        return Message{Role::tool, std::move(text), std::move(tool_name), std::move(call_id), {}};
    }
};

struct ToolSpec {
    std::string name;
    std::string description;
    std::string input_schema_json{"{\"type\":\"object\"}"};
};

struct ModelRequest {
    std::vector<Message> messages;
    std::vector<ToolSpec> tools;
    std::optional<std::string> model;
    std::optional<double> temperature;
    std::optional<std::size_t> max_output_tokens;
    bool allow_parallel_tool_calls{true};
};

struct ModelResponse {
    std::string text;
    std::vector<ToolCall> tool_calls;
    FinishReason finish_reason{FinishReason::unknown};
    std::string provider_request_id;
};

struct ToolResult {
    std::string call_id;
    std::string tool_name;
    std::string content;
    bool is_error{false};
};

struct AgentResult {
    std::string text;
    std::size_t model_rounds{0};
    std::size_t tool_calls{0};
};

}  // namespace neuralplus
