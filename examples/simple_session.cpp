// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/neuralplus.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

using namespace neuralplus;

namespace {

AIResponse demo_model(const AIRequest& request) {
    std::string latest_tool_output;
    for (const Message& message : request.messages) {
        if (message.role() == Role::tool) {
            latest_tool_output = message.text();
        }
    }
    if (!latest_tool_output.empty()) {
        AIResponse response{Message::assistant(
            "The final tool output is " + latest_tool_output + ".")};
        response.finish_reason = FinishReason::stop;
        response.provider_model = "local-demo";
        return response;
    }

    ToolCall first;
    first.id = "increment-1";
    first.name = "increment";
    first.arguments = JsonValue{{"delta", 2}};

    ToolCall second;
    second.id = "increment-2";
    second.name = "increment";
    second.arguments = JsonValue{{"delta", 3}};

    AIResponse response{
        Message::assistant("I will update the counter.", {first, second})};
    response.finish_reason = FinishReason::tool_calls;
    response.provider_model = "local-demo";
    return response;
}

}  // namespace

int main() {
    ToolSpec spec;
    spec.name = "increment";
    spec.description = "Atomically increment this session's counter.";
    spec.input_schema = {
        {"type", "object"},
        {"properties", {{"delta", {{"type", "integer"}}}}},
        {"required", {"delta"}},
    };

    auto increment = std::make_shared<FunctionTool>(
        std::move(spec),
        [](ToolContext& context, const JsonValue& arguments) {
            const int delta = arguments.at("delta").get<int>();
            const int value = context.state().update<int>(
                "counter", 0,
                [delta](int current) { return current + delta; });
            return ToolOutput::json({{"counter", value}});
        });

    ClientOptions options;
    options.tools = {increment};
    options.tracers = {std::make_shared<ConsoleTracer>()};

    ModelDescriptor model;
    model.provider = Provider::custom;
    model.id = "local-demo";
    model.display_name = "Local demo";
    model.capabilities.tools = true;
    model.capabilities.parallel_tool_calls = true;

    FunctionAIClient client{std::move(model), demo_model, options};
    Session session;
    const AIResponse response =
        client.generate(session, "Increment the counter by two and three.");

    const int counter = session.state().get<int>("counter").value_or(-1);
    std::cout << response.message.text() << '\n';
    std::cout << "Session counter: " << counter << '\n';
    std::cout << "Model rounds: " << response.model_rounds
              << ", tool calls: " << response.tool_calls << '\n';

    return counter == 5 ? EXIT_SUCCESS : EXIT_FAILURE;
}
