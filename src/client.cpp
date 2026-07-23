// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/client.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <future>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

using namespace neuralplus;

namespace {

std::string make_run_id() {
    static std::atomic<unsigned long long> next_id{1};
    return "run-" +
           std::to_string(next_id.fetch_add(1, std::memory_order_relaxed));
}

TraceEvent make_event(TraceEventType type,
                      const std::string& session_id,
                      const std::string& run_id,
                      std::string name) {
    TraceEvent event;
    event.type = type;
    event.session_id = session_id;
    event.run_id = run_id;
    event.name = std::move(name);
    return event;
}

void require_json_object(const JsonValue& value, const char* field) {
    if (!value.is_object()) {
        throw ConfigurationError(std::string(field) + " must be a JSON object");
    }
}

bool valid_tool_name(const std::string& name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return (character >= 'A' && character <= 'Z') ||
               (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') ||
               character == '_' || character == '-';
    });
}

void validate_schema_definition(const JsonValue& schema,
                                const std::string& location) {
    if (!schema.is_object()) {
        throw ConfigurationError(location + " must be a JSON object");
    }

    const auto type = schema.find("type");
    if (type != schema.end()) {
        if (!type->is_string()) {
            throw ConfigurationError(location + ".type must be a string");
        }
        static const std::unordered_set<std::string> supported_types{
            "object", "array", "string", "number", "integer", "boolean", "null"};
        if (supported_types.find(type->get<std::string>()) == supported_types.end()) {
            throw ConfigurationError(location + ".type is not supported");
        }
    }

    const auto properties = schema.find("properties");
    if (properties != schema.end()) {
        if (!properties->is_object()) {
            throw ConfigurationError(location + ".properties must be an object");
        }
        for (auto iterator = properties->begin();
             iterator != properties->end();
             ++iterator) {
            validate_schema_definition(iterator.value(),
                                       location + ".properties." + iterator.key());
        }
    }

    const auto required = schema.find("required");
    if (required != schema.end()) {
        if (!required->is_array()) {
            throw ConfigurationError(location + ".required must be an array");
        }
        for (const JsonValue& key : *required) {
            if (!key.is_string() || key.get_ref<const std::string&>().empty()) {
                throw ConfigurationError(
                    location + ".required entries must be non-empty strings");
            }
        }
    }

    const auto items = schema.find("items");
    if (items != schema.end()) {
        validate_schema_definition(*items, location + ".items");
    }

    const auto additional = schema.find("additionalProperties");
    if (additional != schema.end() && !additional->is_boolean() &&
        !additional->is_object()) {
        throw ConfigurationError(
            location + ".additionalProperties must be a boolean or object");
    }
    if (additional != schema.end() && additional->is_object()) {
        validate_schema_definition(
            *additional, location + ".additionalProperties");
    }

    const auto enumeration = schema.find("enum");
    if (enumeration != schema.end() && !enumeration->is_array()) {
        throw ConfigurationError(location + ".enum must be an array");
    }
}

void validate_tool_spec(const ToolSpec& spec) {
    if (!valid_tool_name(spec.name)) {
        throw ConfigurationError(
            "tool name must contain 1-64 ASCII letters, digits, '_' or '-'");
    }
    if (spec.description.empty()) {
        throw ConfigurationError("tool description must not be empty: " + spec.name);
    }
    validate_schema_definition(spec.input_schema,
                               "tool '" + spec.name + "' input_schema");
    require_json_object(spec.provider_options, "tool provider_options");
    const auto root_type = spec.input_schema.find("type");
    if (root_type == spec.input_schema.end() ||
        root_type->get<std::string>() != "object") {
        throw ConfigurationError(
            "tool '" + spec.name + "' input_schema root type must be object");
    }
}

bool value_has_type(const JsonValue& value, const std::string& type) {
    if (type == "object") {
        return value.is_object();
    }
    if (type == "array") {
        return value.is_array();
    }
    if (type == "string") {
        return value.is_string();
    }
    if (type == "number") {
        return value.is_number();
    }
    if (type == "integer") {
        return value.is_number_integer() || value.is_number_unsigned();
    }
    if (type == "boolean") {
        return value.is_boolean();
    }
    if (type == "null") {
        return value.is_null();
    }
    return false;
}

std::string validate_value_against_schema(const JsonValue& value,
                                          const JsonValue& schema,
                                          const std::string& path) {
    // nlohmann::json is a JSON value library rather than a JSON Schema
    // implementation. The loop deliberately validates the portable subset the
    // SDK advertises (type/properties/required/items/enum/additionalProperties)
    // using its documented type predicates:
    // https://json.nlohmann.me/api/basic_json/
    const auto type = schema.find("type");
    if (type != schema.end()) {
        const std::string expected = type->get<std::string>();
        if (!value_has_type(value, expected)) {
            return path + " must be " + expected;
        }
    }

    const auto enumeration = schema.find("enum");
    if (enumeration != schema.end() &&
        std::find(enumeration->begin(), enumeration->end(), value) ==
            enumeration->end()) {
        return path + " is not one of the allowed enum values";
    }

    if (value.is_object()) {
        const auto required = schema.find("required");
        if (required != schema.end()) {
            for (const JsonValue& required_name : *required) {
                const std::string name = required_name.get<std::string>();
                if (value.find(name) == value.end()) {
                    return path + "." + name + " is required";
                }
            }
        }

        const auto properties = schema.find("properties");
        if (properties != schema.end()) {
            for (auto iterator = properties->begin();
                 iterator != properties->end();
                 ++iterator) {
                const auto argument = value.find(iterator.key());
                if (argument == value.end()) {
                    continue;
                }
                const std::string error = validate_value_against_schema(
                    *argument, iterator.value(), path + "." + iterator.key());
                if (!error.empty()) {
                    return error;
                }
            }
        }

        const auto additional = schema.find("additionalProperties");
        if (additional != schema.end()) {
            for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
                const bool declared =
                    properties != schema.end() &&
                    properties->find(iterator.key()) != properties->end();
                if (declared) {
                    continue;
                }
                if (additional->is_boolean() &&
                    !additional->get<bool>()) {
                    return path + "." + iterator.key() + " is not allowed";
                }
                if (additional->is_object()) {
                    const std::string error =
                        validate_value_against_schema(
                            iterator.value(),
                            *additional,
                            path + "." + iterator.key());
                    if (!error.empty()) {
                        return error;
                    }
                }
            }
        }
    }

    if (value.is_array()) {
        const auto items = schema.find("items");
        if (items != schema.end()) {
            for (std::size_t index = 0; index < value.size(); ++index) {
                const std::string error = validate_value_against_schema(
                    value[index],
                    *items,
                    path + "[" + std::to_string(index) + "]");
                if (!error.empty()) {
                    return error;
                }
            }
        }
    }

    return {};
}

void add_count(std::optional<std::size_t>& total,
               const std::optional<std::size_t>& value,
               bool first_value) {
    if (!value) {
        total = std::nullopt;
        return;
    }
    if (first_value) {
        total = *value;
        return;
    }
    if (!total) {
        return;
    }
    const std::size_t current = *total;
    if (*value > std::numeric_limits<std::size_t>::max() - current) {
        throw ProviderError("token usage overflow", Provider::custom, 0);
    }
    total = current + *value;
}

void add_usage(TokenUsage& total,
               const TokenUsage& value,
               bool first_value) {
    add_count(total.input_tokens, value.input_tokens, first_value);
    add_count(total.output_tokens, value.output_tokens, first_value);
    add_count(total.total_tokens, value.total_tokens, first_value);
    add_count(total.cached_input_tokens,
              value.cached_input_tokens,
              first_value);
    add_count(total.cache_creation_input_tokens,
              value.cache_creation_input_tokens,
              first_value);
    add_count(total.reasoning_tokens, value.reasoning_tokens, first_value);
}

std::string output_text(const ToolOutput& output) {
    std::string result;
    for (const Content& content : output.contents) {
        if (content.type() == ContentType::text) {
            result += content.value();
        }
    }
    return result;
}

}  // namespace

struct AIClient::ToolExecution {
    ToolCall call;
    ToolOutput output;
};

AIClient::AIClient(ModelDescriptor model, const ClientOptions& options)
    : model_(std::move(model)), options_(options) {
    if (model_.id.empty()) {
        throw ConfigurationError("model id must not be empty");
    }
    require_json_object(model_.provider_options, "model provider_options");
    if (options_.max_model_rounds == 0) {
        throw ConfigurationError("max_model_rounds must be greater than zero");
    }
    if (options_.max_parallel_tool_calls == 0) {
        throw ConfigurationError(
            "max_parallel_tool_calls must be greater than zero");
    }
    if (options_.max_tool_calls_per_generation == 0) {
        throw ConfigurationError(
            "max_tool_calls_per_generation must be greater than zero");
    }

    for (const std::shared_ptr<Tracer>& tracer : options_.tracers) {
        if (!tracer) {
            throw ConfigurationError("tracers must not contain null");
        }
    }

    tools_.reserve(options_.tools.size());
    tool_specs_.reserve(options_.tools.size());
    for (const std::shared_ptr<Tool>& tool : options_.tools) {
        if (!tool) {
            throw ConfigurationError("tools must not contain null");
        }
        const ToolSpec spec = tool->spec();
        validate_tool_spec(spec);
        const auto inserted =
            tools_.emplace(spec.name, RegisteredTool{tool, spec});
        if (!inserted.second) {
            throw ConfigurationError("duplicate tool: " + spec.name);
        }
        tool_specs_.push_back(spec);
    }

    if (!tool_specs_.empty() && !model_.capabilities.tools) {
        throw ConfigurationError(
            "tools were configured for a model that does not advertise tool support");
    }

    std::sort(tool_specs_.begin(),
              tool_specs_.end(),
              [](const ToolSpec& left, const ToolSpec& right) {
                  return left.name < right.name;
              });
}

AIClient::~AIClient() = default;

const ModelDescriptor& AIClient::model() const noexcept {
    return model_;
}

AIResponse AIClient::generate(Session& session,
                              const Message& input,
                              const GenerateOptions& options) {
    validate_input(input);
    if (options.temperature &&
        (!std::isfinite(*options.temperature) || *options.temperature < 0.0)) {
        throw std::invalid_argument(
            "temperature must be finite and greater than or equal to zero");
    }
    if (options.max_output_tokens && *options.max_output_tokens == 0) {
        throw std::invalid_argument("max_output_tokens must be greater than zero");
    }
    if (!options.provider_options.is_object()) {
        throw std::invalid_argument("provider_options must be a JSON object");
    }

    auto lease = session.acquire();
    (void)lease;
    const std::string run_id = make_run_id();

    TraceEvent generation_start =
        make_event(TraceEventType::generation_start,
                   session.id(),
                   run_id,
                   model_.id);
    generation_start.attributes["input_role"] = to_string(input.role());
    if (options_.capture_trace_payloads) {
        generation_start.payload = input.text();
    }
    trace(generation_start);

    session.append_from_client(input);

    TokenUsage cumulative_usage;
    std::size_t cumulative_tool_calls = 0;

    try {
        for (std::size_t round = 1;
             round <= options_.max_model_rounds;
             ++round) {
            AIRequest request;
            request.session_id = session.id();
            request.run_id = run_id;
            request.system_message = session.system();
            request.messages = session.messages();
            request.tools = tool_specs_;
            request.options = options;
            if (!request.options.max_output_tokens &&
                model_.max_output_tokens) {
                request.options.max_output_tokens = model_.max_output_tokens;
            }

            TraceEvent provider_start =
                make_event(TraceEventType::provider_start,
                           session.id(),
                           run_id,
                           model_.id);
            provider_start.operation_id = "round-" + std::to_string(round);
            provider_start.attributes["round"] = round;
            provider_start.attributes["message_count"] = request.messages.size();
            provider_start.attributes["tool_count"] = request.tools.size();
            trace(provider_start);

            AIResponse response = generate_once(request);
            if (response.message.role() != Role::assistant) {
                throw ProviderError(
                    "provider response message must have assistant role",
                    model_.provider,
                    0,
                    {},
                    response.provider_request_id);
            }

            const std::vector<ToolCall>& calls = response.message.tool_calls();
            if (calls.size() >
                options_.max_tool_calls_per_generation -
                    std::min(cumulative_tool_calls,
                             options_.max_tool_calls_per_generation)) {
                throw ProviderError(
                    "provider requested more tool calls than the configured "
                    "generation limit",
                    model_.provider,
                    0,
                    "tool_call_limit",
                    response.provider_request_id);
            }
            std::unordered_set<std::string> call_ids;
            for (const ToolCall& call : calls) {
                if (call.id.empty() || call.name.empty()) {
                    throw ProviderError(
                        "provider returned a tool call without id or name",
                        model_.provider,
                        0,
                        {},
                        response.provider_request_id);
                }
                if (!call_ids.emplace(call.id).second) {
                    throw ProviderError(
                        "provider returned duplicate tool call id: " + call.id,
                        model_.provider,
                        0,
                        {},
                        response.provider_request_id);
                }
            }

            add_usage(cumulative_usage, response.usage, round == 1);

            TraceEvent provider_end =
                make_event(TraceEventType::provider_end,
                           session.id(),
                           run_id,
                           model_.id);
            provider_end.operation_id = provider_start.operation_id;
            provider_end.attributes["round"] = round;
            provider_end.attributes["finish_reason"] =
                to_string(response.finish_reason);
            provider_end.attributes["tool_call_count"] = calls.size();
            provider_end.attributes["requires_continuation"] =
                response.requires_continuation;
            provider_end.attributes["provider_request_id"] =
                response.provider_request_id;
            if (options_.capture_trace_payloads) {
                provider_end.payload = response.message.text();
            }
            trace(provider_end);

            const bool needs_another_round =
                !calls.empty() || response.requires_continuation;
            if (needs_another_round &&
                round == options_.max_model_rounds) {
                // Do not retain an unresolved native tool call or continuation
                // marker. A later generate call would otherwise replay it
                // without the tool result or protocol response it requires.
                throw MaxRoundsError(
                    "maximum model rounds reached before provider "
                    "continuation could be completed");
            }

            // The normalized provider message is the replay boundary. Append it
            // unchanged so provider_metadata and multimodal parts survive later
            // rounds instead of reconstructing a lossy assistant message.
            session.append_from_client(response.message);

            if (calls.empty() && !response.requires_continuation) {
                response.usage = cumulative_usage;
                response.model_rounds = round;
                response.tool_calls = cumulative_tool_calls;

                TraceEvent generation_end =
                    make_event(TraceEventType::generation_end,
                               session.id(),
                               run_id,
                               model_.id);
                generation_end.attributes["model_rounds"] = round;
                generation_end.attributes["tool_calls"] =
                    cumulative_tool_calls;
                generation_end.attributes["finish_reason"] =
                    to_string(response.finish_reason);
                trace(generation_end);
                return response;
            }

            if (calls.empty()) {
                continue;
            }

            std::vector<ToolExecution> executions =
                execute_tools(session, run_id, calls);
            cumulative_tool_calls += executions.size();

            bool saw_error = false;
            // execute_tools always returns input order, even when workers finish
            // out of order. Appending only here keeps transcript order stable
            // and records every tool that already executed before stop-on-error.
            for (ToolExecution& execution : executions) {
                saw_error = saw_error || execution.output.is_error;
                Message tool_result =
                    Message::tool(execution.call.id,
                                  execution.call.name,
                                  std::move(execution.output.contents),
                                  execution.output.is_error);
                tool_result.set_provider_metadata(
                    execution.call.provider_metadata);
                session.append_from_client(std::move(tool_result));
            }

            if (saw_error && options_.stop_on_tool_error) {
                response.finish_reason = FinishReason::error;
                response.usage = cumulative_usage;
                response.model_rounds = round;
                response.tool_calls = cumulative_tool_calls;

                TraceEvent generation_end =
                    make_event(TraceEventType::generation_end,
                               session.id(),
                               run_id,
                               model_.id);
                generation_end.level = TraceLevel::warning;
                generation_end.attributes["model_rounds"] = round;
                generation_end.attributes["tool_calls"] =
                    cumulative_tool_calls;
                generation_end.attributes["finish_reason"] = "error";
                trace(generation_end);
                return response;
            }
        }

        throw MaxRoundsError(
            "maximum model rounds exceeded without a final response");
    } catch (const std::exception& error) {
        TraceEvent event =
            make_event(TraceEventType::error, session.id(), run_id, model_.id);
        event.level = TraceLevel::error;
        event.attributes["category"] = "exception";
        if (options_.capture_trace_payloads) {
            event.payload = error.what();
        }
        trace(event);
        throw;
    } catch (...) {
        TraceEvent event =
            make_event(TraceEventType::error, session.id(), run_id, model_.id);
        event.level = TraceLevel::error;
        event.attributes["category"] = "unknown_exception";
        if (options_.capture_trace_payloads) {
            event.payload = "unknown generation error";
        }
        trace(event);
        throw;
    }
}

AIResponse AIClient::generate(Session& session,
                              const std::string& input,
                              const GenerateOptions& options) {
    return generate(session, Message::user(input), options);
}

AIResponse AIClient::generate(const Message& input,
                              const GenerateOptions& options) {
    Session session;
    return generate(session, input, options);
}

AIResponse AIClient::generate(const std::string& input,
                              const GenerateOptions& options) {
    Session session;
    return generate(session, Message::user(input), options);
}

void AIClient::trace(const TraceEvent& event) const noexcept {
    for (const std::shared_ptr<Tracer>& tracer : options_.tracers) {
        try {
            tracer->record(event);
        } catch (...) {
            // Observability must not alter model, tool, or transcript semantics.
        }
    }
}

std::vector<AIClient::ToolExecution>
AIClient::execute_tools(Session& session,
                        const std::string& run_id,
                        const std::vector<ToolCall>& calls) const {
    std::vector<ToolExecution> results;
    results.reserve(calls.size());

    if (!options_.parallel_tool_calls || calls.size() < 2) {
        for (const ToolCall& call : calls) {
            results.push_back(execute_tool(session, run_id, call));
        }
        return results;
    }

    const std::size_t parallelism =
        std::min(options_.max_parallel_tool_calls, calls.size());
    std::vector<std::future<ToolExecution>> futures;
    futures.reserve(parallelism);

    // Provider output is untrusted. Execute bounded batches so one response
    // cannot create an unbounded number of threads. Results are collected in
    // request order before the next batch starts.
    for (std::size_t offset = 0; offset < calls.size();
         offset += parallelism) {
        futures.clear();
        const std::size_t end =
            std::min(offset + parallelism, calls.size());
        std::size_t launched = 0;
        try {
            for (std::size_t index = offset; index < end; ++index) {
                const ToolCall call = calls[index];
                futures.push_back(std::async(
                    std::launch::async,
                    [this, &session, run_id, call] {
                        return execute_tool(session, run_id, call);
                    }));
                ++launched;
            }
        } catch (const std::system_error&) {
            // Thread creation can fail under process resource pressure. Wait
            // for work that already started, then complete the unlaunched
            // calls synchronously so every execution remains observable.
            for (std::future<ToolExecution>& future : futures) {
                results.push_back(future.get());
            }
            for (std::size_t index = offset + launched;
                 index < calls.size();
                 ++index) {
                results.push_back(
                    execute_tool(session, run_id, calls[index]));
            }
            return results;
        }
        for (std::future<ToolExecution>& future : futures) {
            results.push_back(future.get());
        }
    }
    return results;
}

AIClient::ToolExecution
AIClient::execute_tool(Session& session,
                       const std::string& run_id,
                       const ToolCall& call) const {
    TraceEvent tool_start =
        make_event(TraceEventType::tool_start,
                   session.id(),
                   run_id,
                   call.name);
    tool_start.operation_id = call.id;
    trace(tool_start);

    ToolOutput output;
    std::string internal_diagnostic;
    const auto tool = tools_.find(call.name);
    if (tool == tools_.end()) {
        output = ToolOutput::error("unknown tool: " + call.name);
    } else if (!call.arguments_valid) {
        output = ToolOutput::error(
            "invalid JSON arguments for tool '" + call.name + "': " +
            call.raw_arguments);
    } else {
        const std::string validation_error = validate_value_against_schema(
            call.arguments, tool->second.spec.input_schema, "$");
        if (!validation_error.empty()) {
            output = ToolOutput::error(
                "invalid arguments for tool '" + call.name + "': " +
                validation_error);
        } else {
            try {
                ToolContext context(
                    session.id(), run_id, call.id, session.state());
                std::lock_guard<std::recursive_mutex> invocation_lock(
                    tool->second.implementation->invocation_mutex_);
                output =
                    tool->second.implementation->invoke(context, call.arguments);
                if (output.contents.empty()) {
                    output = ToolOutput::error(
                        "tool '" + call.name + "' returned no content");
                }
            } catch (const std::exception& error) {
                internal_diagnostic = error.what();
                output = ToolOutput::error(
                    "tool '" + call.name + "' failed");
            } catch (...) {
                internal_diagnostic = "unknown tool exception";
                output = ToolOutput::error(
                    "tool '" + call.name + "' failed");
            }
        }
    }

    TraceEvent tool_end =
        make_event(TraceEventType::tool_end,
                   session.id(),
                   run_id,
                   call.name);
    tool_end.operation_id = call.id;
    tool_end.level = output.is_error ? TraceLevel::warning : TraceLevel::info;
    tool_end.attributes["is_error"] = output.is_error;
    if (options_.capture_trace_payloads) {
        tool_end.payload = internal_diagnostic.empty()
                               ? output_text(output)
                               : internal_diagnostic;
    }
    trace(tool_end);

    return ToolExecution{call, std::move(output)};
}

void AIClient::validate_input(const Message& input) const {
    if (input.role() != Role::user) {
        throw std::invalid_argument("generate input must have user role");
    }
    if (input.contents().empty()) {
        throw std::invalid_argument("generate input must contain content");
    }

    for (const Content& content : input.contents()) {
        switch (content.type()) {
            case ContentType::text:
                if (!model_.capabilities.text_input) {
                    throw std::invalid_argument(
                        "model does not support text input");
                }
                if (content.value().empty()) {
                    throw std::invalid_argument(
                        "text input must not be empty");
                }
                break;
            case ContentType::image:
                if (!model_.capabilities.image_input) {
                    throw std::invalid_argument(
                        "model does not support image input");
                }
                break;
            case ContentType::audio:
                if (!model_.capabilities.audio_input) {
                    throw std::invalid_argument(
                        "model does not support audio input");
                }
                break;
            case ContentType::file:
                if (!model_.capabilities.file_input) {
                    throw std::invalid_argument(
                        "model does not support file input");
                }
                break;
            case ContentType::extension:
                if (content.provider() != to_string(model_.provider)) {
                    throw std::invalid_argument(
                        "content extension does not match the configured provider");
                }
                break;
        }
    }
}

FunctionAIClient::FunctionAIClient(ModelDescriptor model,
                                   Function function,
                                   const ClientOptions& options)
    : AIClient(std::move(model), options),
      function_(std::move(function)) {
    if (!function_) {
        throw ConfigurationError("AI client function must not be empty");
    }
}

FunctionAIClient::~FunctionAIClient() = default;

AIResponse FunctionAIClient::generate_once(const AIRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    return function_(request);
}
