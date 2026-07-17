#pragma once

#include "neuralplus/core/types.hpp"

#include <string_view>

namespace neuralplus {

/// Lightweight lifecycle hooks. Implementations must be thread-safe because parallel
/// tool callbacks can occur concurrently.
class RunObserver {
public:
    virtual ~RunObserver() = default;

    virtual void on_model_start(std::string_view, const ModelRequest&) {}
    virtual void on_model_end(std::string_view, const ModelResponse&) {}
    virtual void on_tool_start(std::string_view, const ToolCall&) {}
    virtual void on_tool_end(std::string_view, const ToolResult&) {}
};

}  // namespace neuralplus
