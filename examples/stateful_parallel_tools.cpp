#include "neuralplus/neuralplus.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

int parse_delta(std::string_view json) {
    const auto colon = json.find(':');
    if (colon == std::string_view::npos) {
        throw std::invalid_argument("expected JSON object containing delta");
    }

    std::size_t begin = colon + 1;
    while (begin < json.size() && !std::isdigit(static_cast<unsigned char>(json[begin])) &&
           json[begin] != '-') {
        ++begin;
    }
    if (begin == json.size()) {
        throw std::invalid_argument("delta must be an integer");
    }

    std::size_t end = begin + 1;
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) {
        ++end;
    }
    return std::stoi(std::string(json.substr(begin, end - begin)));
}

class CounterTool final : public neuralplus::TypedTool<int, int> {
public:
    [[nodiscard]] std::string name() const override {
        return "increment_counter";
    }

    [[nodiscard]] std::string description() const override {
        return "Atomically increments a session-scoped counter.";
    }

    [[nodiscard]] std::string input_schema_json() const override {
        return R"({"type":"object","properties":{"delta":{"type":"integer"}},"required":["delta"]})";
    }

protected:
    [[nodiscard]] int parse(std::string_view arguments_json) const override {
        return parse_delta(arguments_json);
    }

    int execute(neuralplus::ToolContext& context, const int& delta) override {
        return context.state->update<int>("counter", 0, [delta](int current) {
            return current + delta;
        });
    }

    [[nodiscard]] std::string serialize(const int& output) const override {
        return std::string{"{\"counter\":"} + std::to_string(output) + "}";
    }
};

class DemoModel final : public neuralplus::Model {
public:
    [[nodiscard]] std::string name() const override {
        return "demo-model";
    }

    neuralplus::ModelResponse generate(const neuralplus::ModelRequest& request) override {
        bool has_tool_result = false;
        std::string last_result;
        for (const auto& message : request.messages) {
            if (message.role == neuralplus::Role::tool) {
                has_tool_result = true;
                last_result = message.content;
            }
        }

        if (!has_tool_result) {
            return neuralplus::ModelResponse{
                "I will update the counter in parallel.",
                {
                    {"call-1", "increment_counter", R"({"delta":2})"},
                    {"call-2", "increment_counter", R"({"delta":3})"},
                },
                neuralplus::FinishReason::tool_calls,
                "demo-request-1",
            };
        }

        return neuralplus::ModelResponse{
            "Tool calls completed. Final observed result: " + last_result,
            {},
            neuralplus::FinishReason::stop,
            "demo-request-2",
        };
    }
};

}  // namespace

int main() {
    auto tools = std::make_shared<neuralplus::ToolRegistry>();
    tools->add(std::make_shared<CounterTool>());

    neuralplus::Agent agent(std::make_shared<DemoModel>(), tools);
    neuralplus::Session session("example-session");

    const auto result = agent.run(session, "Increment the counter by two and three.");
    const auto counter = session.state()->get_copy<int>("counter").value_or(-1);

    std::cout << result.text << '\n';
    std::cout << "Session counter: " << counter << '\n';
    std::cout << "Model rounds: " << result.model_rounds
              << ", tool calls: " << result.tool_calls << '\n';

    return counter == 5 ? EXIT_SUCCESS : EXIT_FAILURE;
}
