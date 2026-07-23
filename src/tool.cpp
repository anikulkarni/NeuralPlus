// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/tool.hpp"

#include <stdexcept>
#include <utility>

using namespace neuralplus;

namespace {

bool is_portable_tool_name(const std::string& name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (const unsigned char character : name) {
        const bool ascii_alphanumeric =
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9');
        if (!ascii_alphanumeric && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

void validate_required(const JsonValue& required) {
    if (!required.is_array()) {
        throw std::invalid_argument(
            "tool input_schema.required must be a JSON array");
    }
    for (const JsonValue& name : required) {
        if (!name.is_string() || name.get_ref<const std::string&>().empty()) {
            throw std::invalid_argument(
                "tool input_schema.required entries must be non-empty strings");
        }
    }
}

void validate_tool_spec(const ToolSpec& spec) {
    if (!is_portable_tool_name(spec.name)) {
        throw std::invalid_argument(
            "tool name must contain 1-64 ASCII letters, digits, '_' or '-'");
    }
    if (spec.description.empty()) {
        throw std::invalid_argument("tool description must not be empty");
    }

    // nlohmann::json intentionally provides JSON values, not a full JSON
    // Schema engine. These common-shape checks rely on the documented object
    // lookup/type API and leave advanced draft keywords to provider validators:
    // https://json.nlohmann.me/features/element_access/
    if (!spec.input_schema.is_object()) {
        throw std::invalid_argument("tool input_schema must be a JSON object");
    }
    if (!spec.provider_options.is_object()) {
        throw std::invalid_argument(
            "tool provider_options must be a JSON object");
    }

    const auto type = spec.input_schema.find("type");
    if (type == spec.input_schema.end() ||
        !type->is_string() ||
        type->get<std::string>() != "object") {
        throw std::invalid_argument(
            "tool input_schema root type must be 'object'");
    }

    const auto properties = spec.input_schema.find("properties");
    if (properties != spec.input_schema.end() && !properties->is_object()) {
        throw std::invalid_argument(
            "tool input_schema.properties must be a JSON object");
    }

    const auto required = spec.input_schema.find("required");
    if (required != spec.input_schema.end()) {
        validate_required(*required);
    }
}

}  // namespace

ToolContext::ToolContext(std::string session_id,
                         std::string run_id,
                         std::string call_id,
                         SessionState& state)
    : session_id_(std::move(session_id)),
      run_id_(std::move(run_id)),
      call_id_(std::move(call_id)),
      state_(&state) {}

const std::string& ToolContext::session_id() const noexcept {
    return session_id_;
}

const std::string& ToolContext::run_id() const noexcept {
    return run_id_;
}

const std::string& ToolContext::call_id() const noexcept {
    return call_id_;
}

SessionState& ToolContext::state() const noexcept {
    return *state_;
}

ToolOutput ToolOutput::text(std::string value) {
    ToolOutput output;
    output.contents.push_back(Content::text(std::move(value)));
    return output;
}

ToolOutput ToolOutput::json(JsonValue value) {
    return text(value.dump());
}

ToolOutput ToolOutput::error(std::string value) {
    ToolOutput output = text(std::move(value));
    output.is_error = true;
    return output;
}

Tool::~Tool() = default;

FunctionTool::FunctionTool(ToolSpec spec, Function function)
    : spec_(std::move(spec)), function_(std::move(function)) {
    validate_tool_spec(spec_);
    if (!function_) {
        throw std::invalid_argument("tool function must not be empty");
    }
}

FunctionTool::~FunctionTool() = default;

const ToolSpec& FunctionTool::spec() const noexcept {
    return spec_;
}

ToolOutput FunctionTool::invoke(ToolContext& context,
                                const JsonValue& arguments) {
    if (!arguments.is_object()) {
        throw std::invalid_argument("tool arguments must be a JSON object");
    }
    return function_(context, arguments);
}
