#pragma once

#include "neuralplus/core/state_store.hpp"
#include "neuralplus/core/types.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace neuralplus {

struct ToolContext {
    std::string session_id;
    std::string call_id;
    std::shared_ptr<StateStore> state;
};

class Tool {
public:
    virtual ~Tool() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string description() const = 0;
    [[nodiscard]] virtual std::string input_schema_json() const {
        return R"({"type":"object"})";
    }

    virtual std::string invoke(ToolContext& context, std::string_view arguments_json) = 0;

    [[nodiscard]] ToolSpec spec() const {
        return ToolSpec{name(), description(), input_schema_json()};
    }
};

/// Typed extension point for tools. Subclasses define parsing, execution, and serialization.
template <typename Input, typename Output>
class TypedTool : public Tool {
public:
    std::string invoke(ToolContext& context, std::string_view arguments_json) final {
        return serialize(execute(context, parse(arguments_json)));
    }

protected:
    [[nodiscard]] virtual Input parse(std::string_view arguments_json) const = 0;
    virtual Output execute(ToolContext& context, const Input& input) = 0;
    [[nodiscard]] virtual std::string serialize(const Output& output) const = 0;
};

class ToolRegistry {
public:
    void add(std::shared_ptr<Tool> tool) {
        if (!tool) {
            throw std::invalid_argument("tool must not be null");
        }
        const auto tool_name = tool->name();
        if (tool_name.empty()) {
            throw std::invalid_argument("tool name must not be empty");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        const auto [_, inserted] = tools_.emplace(tool_name, std::move(tool));
        if (!inserted) {
            throw std::invalid_argument("duplicate tool: " + tool_name);
        }
    }

    [[nodiscard]] std::shared_ptr<Tool> find(std::string_view name) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto it = tools_.find(std::string(name));
        return it == tools_.end() ? nullptr : it->second;
    }

    [[nodiscard]] std::vector<ToolSpec> specs() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<ToolSpec> result;
        result.reserve(tools_.size());
        for (const auto& [_, tool] : tools_) {
            result.push_back(tool->spec());
        }
        std::sort(result.begin(), result.end(), [](const ToolSpec& left, const ToolSpec& right) {
            return left.name < right.name;
        });
        return result;
    }

    [[nodiscard]] std::size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return tools_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Tool>> tools_;
};

}  // namespace neuralplus
