// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

/// @file
/// Type-safe tool extension interface and callback-backed default tool.

#pragma once

#include "neuralplus/export.hpp"
#include "neuralplus/session.hpp"
#include "neuralplus/types.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace neuralplus {

class AIClient;

/// Context available to one tool invocation.
///
/// The context is valid only for the duration of `Tool::invoke` and must not be
/// retained by a tool.
class NEURALPLUS_API ToolContext final {
public:
    /// Creates the context for one correlated tool invocation.
    ToolContext(std::string session_id,
                std::string run_id,
                std::string call_id,
                SessionState& state);

    /// Returns the session identifier associated with this invocation.
    [[nodiscard]] const std::string& session_id() const noexcept;

    /// Returns the generation identifier associated with this invocation.
    [[nodiscard]] const std::string& run_id() const noexcept;

    /// Returns the provider tool-call identifier.
    [[nodiscard]] const std::string& call_id() const noexcept;

    /// Returns the session-scoped state available to the tool.
    [[nodiscard]] SessionState& state() const noexcept;

private:
    ToolContext(const ToolContext&) = delete;
    ToolContext& operator=(const ToolContext&) = delete;

    std::string session_id_;
    std::string run_id_;
    std::string call_id_;
    SessionState* state_;
};

/// Normalized multimodal result from a tool.
struct NEURALPLUS_API ToolOutput {
    /// Text, media, file, or provider-extension parts returned by the tool.
    std::vector<Content> contents;

    /// Whether the result represents an application/tool failure.
    bool is_error{false};

    /// Creates a successful text result.
    [[nodiscard]] static ToolOutput text(std::string value);

    /// Creates a successful JSON result serialized as text content.
    [[nodiscard]] static ToolOutput json(JsonValue value);

    /// Creates an unsuccessful text result.
    [[nodiscard]] static ToolOutput error(std::string value);
};

/// Type-safe extension point for application tools.
///
/// AIClient serializes invocations of the same Tool object, including when
/// that shared instance is installed in more than one client. Different Tool
/// objects may still execute concurrently.
class NEURALPLUS_API Tool {
public:
    virtual ~Tool();

    /// Returns the immutable declaration advertised to providers.
    [[nodiscard]] virtual const ToolSpec& spec() const noexcept = 0;

    /// Executes the tool with validated JSON arguments.
    virtual ToolOutput invoke(ToolContext& context,
                              const JsonValue& arguments) = 0;

protected:
    Tool() = default;

private:
    friend class AIClient;

    Tool(const Tool&) = delete;
    Tool& operator=(const Tool&) = delete;

    mutable std::recursive_mutex invocation_mutex_;
};

/// Callback-backed Tool for applications that do not need a subclass.
class NEURALPLUS_API FunctionTool final : public Tool {
public:
    /// Callback signature for one tool invocation.
    using Function = std::function<ToolOutput(ToolContext&, const JsonValue&)>;

    /// Creates a tool from its declaration and callback.
    FunctionTool(ToolSpec spec, Function function);
    ~FunctionTool() override;

    /// Returns the immutable declaration supplied at construction.
    [[nodiscard]] const ToolSpec& spec() const noexcept override;

    /// Delegates one invocation to the configured callback.
    ToolOutput invoke(ToolContext& context,
                      const JsonValue& arguments) override;

private:
    FunctionTool(const FunctionTool&) = delete;
    FunctionTool& operator=(const FunctionTool&) = delete;

    ToolSpec spec_;
    Function function_;
};

/// Shared tool collection accepted by ClientOptions.
using Tools = std::vector<std::shared_ptr<Tool>>;

}  // namespace neuralplus
