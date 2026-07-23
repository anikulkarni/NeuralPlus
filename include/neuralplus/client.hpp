// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

/// @file
/// Provider-independent AI client interface and callback-backed implementation.

#pragma once

#include "neuralplus/export.hpp"
#include "neuralplus/session.hpp"
#include "neuralplus/tool.hpp"
#include "neuralplus/tracing.hpp"
#include "neuralplus/transport.hpp"
#include "neuralplus/types.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace neuralplus {

/// Dependencies and behavior shared by all AIClient implementations.
struct ClientOptions {
    /// Immutable set of tools advertised to the configured model.
    Tools tools;

    /// Zero or more tracing/logging destinations.
    Tracers tracers;

    /// Injectable transport; null selects CurlHttpTransport.
    std::shared_ptr<HttpTransport> transport;

    /// Hard limit for provider requests in one generation.
    std::size_t max_model_rounds{8};

    /// Executes calls from one provider round concurrently when true.
    bool parallel_tool_calls{true};

    /// Maximum number of tool callbacks executing concurrently.
    std::size_t max_parallel_tool_calls{4};

    /// Maximum tool callbacks allowed across one generate call.
    std::size_t max_tool_calls_per_generation{64};

    /// Returns after recording a failed tool batch instead of asking the model again.
    bool stop_on_tool_error{false};

    /// Adds prompt/response/tool text to TraceEvent::payload when true.
    bool capture_trace_payloads{false};
};

/// Provider-independent client and complete conversation/tool loop.
///
/// Applications normally use a built-in provider from providers.hpp. Custom
/// providers implement only `generate_once`; session ownership, tracing, and
/// tool execution remain consistent in this base class. One client may serve
/// different Session objects concurrently. A custom `generate_once`
/// implementation must therefore be thread-safe.
class NEURALPLUS_API AIClient {
public:
    virtual ~AIClient();

    /// Returns the immutable model description used by this client.
    [[nodiscard]] const ModelDescriptor& model() const noexcept;

    /// Generates from one input message and updates the supplied Session.
    AIResponse generate(Session& session,
                        const Message& input,
                        const GenerateOptions& options = {});

    /// Convenience overload for text input.
    AIResponse generate(Session& session,
                        const std::string& input,
                        const GenerateOptions& options = {});

    /// One-shot convenience overload that uses an internal Session.
    AIResponse generate(const Message& input,
                        const GenerateOptions& options = {});

    /// One-shot convenience overload for text input.
    AIResponse generate(const std::string& input,
                        const GenerateOptions& options = {});

protected:
    /// Creates a provider client for a model with shared tools, tracing, and transport.
    AIClient(ModelDescriptor model, const ClientOptions& options = {});

    /// Performs exactly one provider request without mutating a Session.
    virtual AIResponse generate_once(const AIRequest& request) = 0;

    /// Emits a custom event through this client's exception-isolated tracer fan-out.
    void trace(const TraceEvent& event) const noexcept;

private:
    struct ToolExecution;
    struct RegisteredTool {
        std::shared_ptr<Tool> implementation;
        ToolSpec spec;
    };

    [[nodiscard]] std::vector<ToolExecution>
    execute_tools(Session& session,
                  const std::string& run_id,
                  const std::vector<ToolCall>& calls) const;

    [[nodiscard]] ToolExecution
    execute_tool(Session& session,
                 const std::string& run_id,
                 const ToolCall& call) const;

    void validate_input(const Message& input) const;

    AIClient(const AIClient&) = delete;
    AIClient& operator=(const AIClient&) = delete;

    ModelDescriptor model_;
    ClientOptions options_;
    std::unordered_map<std::string, RegisteredTool> tools_;
    std::vector<ToolSpec> tool_specs_;
};

/// Callback-backed AIClient useful for custom in-process models and tests.
///
/// Calls to the supplied callback are serialized so ordinary application
/// callbacks do not need their own synchronization.
class NEURALPLUS_API FunctionAIClient final : public AIClient {
public:
    /// Callback signature for one provider-independent model round.
    using Function = std::function<AIResponse(const AIRequest&)>;

    /// Creates a client that delegates each model round to `function`.
    FunctionAIClient(ModelDescriptor model,
                     Function function,
                     const ClientOptions& options = {});
    ~FunctionAIClient() override;

private:
    FunctionAIClient(const FunctionAIClient&) = delete;
    FunctionAIClient& operator=(const FunctionAIClient&) = delete;

    AIResponse generate_once(const AIRequest& request) override;

    std::mutex mutex_;
    Function function_;
};

}  // namespace neuralplus
