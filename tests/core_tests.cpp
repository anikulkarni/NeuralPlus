#include "neuralplus/neuralplus.hpp"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class EchoTool final : public neuralplus::Tool {
public:
    [[nodiscard]] std::string name() const override { return "echo"; }
    [[nodiscard]] std::string description() const override { return "Echoes its input."; }

    std::string invoke(neuralplus::ToolContext& context,
                       std::string_view arguments_json) override {
        const auto count = context.state->update<int>("echo_count", 0, [](int value) {
            return value + 1;
        });
        return std::string(arguments_json) + ":" + std::to_string(count);
    }
};

class OneToolRoundModel final : public neuralplus::Model {
public:
    [[nodiscard]] std::string name() const override { return "test-model"; }

    neuralplus::ModelResponse generate(const neuralplus::ModelRequest& request) override {
        for (const auto& message : request.messages) {
            if (message.role == neuralplus::Role::tool) {
                return {message.content, {}, neuralplus::FinishReason::stop, "request-2"};
            }
        }
        return {"", {{"1", "echo", R"({"value":"hello"})"}},
                neuralplus::FinishReason::tool_calls, "request-1"};
    }
};

void test_state_store() {
    neuralplus::StateStore state;
    state.set("answer", 40);
    const auto updated = state.update<int>("answer", 0, [](int value) { return value + 2; });
    assert(updated == 42);
    assert(state.get_copy<int>("answer").value() == 42);
}

void test_registry_rejects_duplicates() {
    neuralplus::ToolRegistry registry;
    registry.add(std::make_shared<EchoTool>());
    bool threw = false;
    try {
        registry.add(std::make_shared<EchoTool>());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

void test_agent_tool_loop() {
    auto registry = std::make_shared<neuralplus::ToolRegistry>();
    registry->add(std::make_shared<EchoTool>());

    neuralplus::Agent agent(std::make_shared<OneToolRoundModel>(), registry);
    neuralplus::Session session("test-session");
    const auto result = agent.run(session, "hello");

    assert(result.model_rounds == 2);
    assert(result.tool_calls == 1);
    assert(result.text == "{\"value\":\"hello\"}:1");
    assert(session.state()->get_copy<int>("echo_count").value() == 1);
}

}  // namespace

int main() {
    test_state_store();
    test_registry_rejects_duplicates();
    test_agent_tool_loop();
    return 0;
}
