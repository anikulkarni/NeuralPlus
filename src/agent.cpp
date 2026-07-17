#include "neuralplus/core/agent.hpp"

#include <atomic>
#include <exception>
#include <future>
#include <stdexcept>
#include <utility>

namespace neuralplus {
namespace {

std::string make_session_id() {
    static std::atomic<unsigned long long> next_id{1};
    return "session-" + std::to_string(next_id.fetch_add(1));
}

}  // namespace

Session::Session(std::string id)
    : id_(id.empty() ? make_session_id() : std::move(id)),
      state_(std::make_shared<StateStore>()) {}

const std::string& Session::id() const noexcept {
    return id_;
}

const std::vector<Message>& Session::messages() const noexcept {
    return messages_;
}

std::vector<Message>& Session::messages() noexcept {
    return messages_;
}

const std::shared_ptr<StateStore>& Session::state() const noexcept {
    return state_;
}

void Session::add(Message message) {
    messages_.push_back(std::move(message));
}

void Session::clear_messages() {
    messages_.clear();
}

Agent::Agent(std::shared_ptr<Model> model,
             std::shared_ptr<ToolRegistry> tools,
             AgentOptions options)
    : model_(std::move(model)), tools_(std::move(tools)), options_(options) {
    if (!model_) {
        throw std::invalid_argument("model must not be null");
    }
    if (!tools_) {
        throw std::invalid_argument("tool registry must not be null");
    }
    if (options_.max_model_rounds == 0) {
        throw std::invalid_argument("max_model_rounds must be greater than zero");
    }
}

void Agent::add_observer(std::shared_ptr<RunObserver> observer) {
    if (!observer) {
        throw std::invalid_argument("observer must not be null");
    }
    observers_.push_back(std::move(observer));
}

AgentResult Agent::run(Session& session, std::string user_input) {
    session.add(Message::user(std::move(user_input)));

    AgentResult result;
    for (std::size_t round = 0; round < options_.max_model_rounds; ++round) {
        ModelRequest request;
        request.messages = session.messages();
        request.tools = tools_->specs();
        request.allow_parallel_tool_calls = options_.parallel_tool_calls;

        notify_model_start(session, request);
        auto response = model_->generate(request);
        notify_model_end(session, response);

        ++result.model_rounds;
        session.add(Message::assistant(response.text, response.tool_calls));

        if (response.tool_calls.empty()) {
            result.text = std::move(response.text);
            return result;
        }

        auto tool_results = execute_tools(session, response.tool_calls);
        result.tool_calls += tool_results.size();

        for (const auto& tool_result : tool_results) {
            session.add(Message::tool(tool_result.call_id,
                                      tool_result.tool_name,
                                      tool_result.content));
            if (options_.stop_on_tool_error && tool_result.is_error) {
                result.text = "Tool execution failed: " + tool_result.tool_name +
                              ": " + tool_result.content;
                return result;
            }
        }
    }

    throw std::runtime_error("agent exceeded max_model_rounds without a final response");
}

const std::shared_ptr<Model>& Agent::model() const noexcept {
    return model_;
}

const std::shared_ptr<ToolRegistry>& Agent::tools() const noexcept {
    return tools_;
}

ToolResult Agent::execute_tool(Session& session, const ToolCall& call) const {
    notify_tool_start(session, call);

    ToolResult result{call.id, call.name, {}, false};
    const auto tool = tools_->find(call.name);
    if (!tool) {
        result.is_error = true;
        result.content = "unknown tool: " + call.name;
        notify_tool_end(session, result);
        return result;
    }

    try {
        ToolContext context{session.id(), call.id, session.state()};
        result.content = tool->invoke(context, call.arguments_json);
    } catch (const std::exception& ex) {
        result.is_error = true;
        result.content = ex.what();
    } catch (...) {
        result.is_error = true;
        result.content = "unknown tool error";
    }

    notify_tool_end(session, result);
    return result;
}

std::vector<ToolResult> Agent::execute_tools(Session& session,
                                             const std::vector<ToolCall>& calls) const {
    std::vector<ToolResult> results;
    results.reserve(calls.size());

    if (!options_.parallel_tool_calls || calls.size() < 2) {
        for (const auto& call : calls) {
            results.push_back(execute_tool(session, call));
        }
        return results;
    }

    std::vector<std::future<ToolResult>> futures;
    futures.reserve(calls.size());
    for (const auto& call : calls) {
        futures.push_back(std::async(std::launch::async, [this, &session, call] {
            return execute_tool(session, call);
        }));
    }

    for (auto& future : futures) {
        results.push_back(future.get());
    }
    return results;
}

void Agent::notify_model_start(const Session& session, const ModelRequest& request) const {
    for (const auto& observer : observers_) {
        try {
            observer->on_model_start(session.id(), request);
        } catch (...) {
            // Observability must not change model execution semantics.
        }
    }
}

void Agent::notify_model_end(const Session& session, const ModelResponse& response) const {
    for (const auto& observer : observers_) {
        try {
            observer->on_model_end(session.id(), response);
        } catch (...) {
            // Observability must not change model execution semantics.
        }
    }
}

void Agent::notify_tool_start(const Session& session, const ToolCall& call) const {
    for (const auto& observer : observers_) {
        try {
            observer->on_tool_start(session.id(), call);
        } catch (...) {
            // Observability must not change tool execution semantics.
        }
    }
}

void Agent::notify_tool_end(const Session& session, const ToolResult& result) const {
    for (const auto& observer : observers_) {
        try {
            observer->on_tool_end(session.id(), result);
        } catch (...) {
            // Observability must not change tool execution semantics.
        }
    }
}

}  // namespace neuralplus
