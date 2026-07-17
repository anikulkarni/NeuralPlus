#pragma once

#include "neuralplus/core/model.hpp"
#include "neuralplus/core/observer.hpp"
#include "neuralplus/core/state_store.hpp"
#include "neuralplus/core/tool.hpp"
#include "neuralplus/core/types.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace neuralplus {

struct AgentOptions {
    std::size_t max_model_rounds{8};
    bool parallel_tool_calls{true};
    bool stop_on_tool_error{false};
};

class Session {
public:
    explicit Session(std::string id = {});

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const std::vector<Message>& messages() const noexcept;
    [[nodiscard]] std::vector<Message>& messages() noexcept;
    [[nodiscard]] const std::shared_ptr<StateStore>& state() const noexcept;

    void add(Message message);
    void clear_messages();

private:
    std::string id_;
    std::vector<Message> messages_;
    std::shared_ptr<StateStore> state_;
};

class Agent {
public:
    Agent(std::shared_ptr<Model> model,
          std::shared_ptr<ToolRegistry> tools = std::make_shared<ToolRegistry>(),
          AgentOptions options = {});

    void add_observer(std::shared_ptr<RunObserver> observer);

    AgentResult run(Session& session, std::string user_input);

    [[nodiscard]] const std::shared_ptr<Model>& model() const noexcept;
    [[nodiscard]] const std::shared_ptr<ToolRegistry>& tools() const noexcept;

private:
    ToolResult execute_tool(Session& session, const ToolCall& call) const;
    std::vector<ToolResult> execute_tools(Session& session,
                                          const std::vector<ToolCall>& calls) const;

    void notify_model_start(const Session& session, const ModelRequest& request) const;
    void notify_model_end(const Session& session, const ModelResponse& response) const;
    void notify_tool_start(const Session& session, const ToolCall& call) const;
    void notify_tool_end(const Session& session, const ToolResult& result) const;

    std::shared_ptr<Model> model_;
    std::shared_ptr<ToolRegistry> tools_;
    AgentOptions options_;
    std::vector<std::shared_ptr<RunObserver>> observers_;
};

}  // namespace neuralplus
