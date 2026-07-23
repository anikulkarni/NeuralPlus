// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/neuralplus.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace neuralplus;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Function>
void require_throws(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

ModelDescriptor test_model(bool tools = false) {
    ModelDescriptor model;
    model.provider = Provider::custom;
    model.id = "offline-test-model";
    model.display_name = "Offline test model";
    model.capabilities.tools = tools;
    model.capabilities.parallel_tool_calls = tools;
    return model;
}

ToolSpec integer_tool_spec(std::string name) {
    ToolSpec spec;
    spec.name = std::move(name);
    spec.description = "Processes one integer value.";
    spec.input_schema = JsonValue::object();
    spec.input_schema["type"] = "object";
    spec.input_schema["properties"] = JsonValue::object();
    spec.input_schema["properties"]["value"] = JsonValue{{"type", "integer"}};
    spec.input_schema["required"] = JsonValue::array({"value"});
    spec.input_schema["additionalProperties"] = false;
    return spec;
}

ToolCall call(std::string id, std::string name, JsonValue arguments) {
    ToolCall result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.arguments = std::move(arguments);
    result.raw_arguments = result.arguments.dump();
    return result;
}

class RecordingTracer final : public Tracer {
public:
    void record(const TraceEvent& event) override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(event);
    }

    std::vector<TraceEvent> events() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<TraceEvent> events_;
};

class ThrowingTracer final : public Tracer {
public:
    void record(const TraceEvent&) override {
        throw std::runtime_error("tracer failure");
    }
};

class MutableSpecTool final : public Tool {
public:
    explicit MutableSpecTool(std::atomic<int>& invocation_count)
        : spec_(integer_tool_spec("mutable_spec")),
          invocation_count_(&invocation_count) {}

    [[nodiscard]] const ToolSpec& spec() const noexcept override {
        return spec_;
    }

    ToolOutput invoke(ToolContext&, const JsonValue&) override {
        invocation_count_->fetch_add(1, std::memory_order_relaxed);
        return ToolOutput::text("ok");
    }

    void require_string_value() {
        spec_.input_schema["properties"]["value"]["type"] = "string";
    }

private:
    MutableSpecTool(const MutableSpecTool&) = delete;
    MutableSpecTool& operator=(const MutableSpecTool&) = delete;

    ToolSpec spec_;
    std::atomic<int>* invocation_count_;
};

class ScopedTestFile final {
public:
    explicit ScopedTestFile(std::string path) : path_(std::move(path)) {
        (void)std::remove(path_.c_str());
    }

    ~ScopedTestFile() {
        (void)std::remove(path_.c_str());
    }

    [[nodiscard]] const std::string& path() const noexcept {
        return path_;
    }

private:
    ScopedTestFile(const ScopedTestFile&) = delete;
    ScopedTestFile& operator=(const ScopedTestFile&) = delete;

    std::string path_;
};

#if !defined(_WIN32)
class LoopbackHttpServer final {
public:
    explicit LoopbackHttpServer(std::vector<std::string> responses)
        : descriptor_(::socket(AF_INET, SOCK_STREAM, 0)) {
        if (descriptor_ == -1) {
            throw std::runtime_error("loopback server socket creation failed");
        }

        int reuse_address = 1;
        (void)::setsockopt(descriptor_, SOL_SOCKET, SO_REUSEADDR,
                           &reuse_address, sizeof(reuse_address));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(descriptor_,
                   reinterpret_cast<const sockaddr*>(&address),
                   sizeof(address)) != 0 ||
            ::listen(descriptor_, 16) != 0) {
            ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("loopback server bind/listen failed");
        }

        socklen_t address_size = sizeof(address);
        if (::getsockname(descriptor_,
                          reinterpret_cast<sockaddr*>(&address),
                          &address_size) != 0) {
            ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("loopback server port lookup failed");
        }
        port_ = ntohs(address.sin_port);

        const int listener = descriptor_;
        thread_ = std::thread(
            [this, listener, responses = std::move(responses)] {
                for (const std::string& response : responses) {
                    const int connection =
                        ::accept(listener, nullptr, nullptr);
                    if (connection == -1) {
                        return;
                    }
#if defined(SO_NOSIGPIPE)
                    int no_sigpipe = 1;
                    (void)::setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE,
                                       &no_sigpipe, sizeof(no_sigpipe));
#endif
                    std::size_t sent = 0;
                    while (sent < response.size()) {
#if defined(MSG_NOSIGNAL)
                        constexpr int send_flags = MSG_NOSIGNAL;
#else
                        constexpr int send_flags = 0;
#endif
                        const auto result = ::send(
                            connection,
                            response.data() + sent,
                            response.size() - sent,
                            send_flags);
                        if (result <= 0) {
                            break;
                        }
                        sent += static_cast<std::size_t>(result);
                    }
                    (void)::shutdown(connection, SHUT_RDWR);
                    (void)::close(connection);
                }
                completed_.store(true, std::memory_order_release);
            });
    }

    ~LoopbackHttpServer() {
        if (descriptor_ != -1) {
            (void)::shutdown(descriptor_, SHUT_RDWR);
            (void)::close(descriptor_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_) + "/";
    }

    [[nodiscard]] bool completed() const noexcept {
        return completed_.load(std::memory_order_acquire);
    }

private:
    LoopbackHttpServer(const LoopbackHttpServer&) = delete;
    LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

    int descriptor_;
    std::uint16_t port_{0};
    std::thread thread_;
    std::atomic<bool> completed_{false};
};

std::string loopback_response(
    const std::string& body,
    const std::vector<std::string>& headers = {}) {
    std::string response = "HTTP/1.1 200 OK\r\n";
    for (const std::string& header : headers) {
        response += header + "\r\n";
    }
    response += "Content-Length: " + std::to_string(body.size()) +
                "\r\nConnection: close\r\n\r\n" + body;
    return response;
}

void require_transport_error_contains(
    const std::function<void()>& function,
    const std::string& expected,
    const std::string& message) {
    try {
        function();
    } catch (const TransportError& error) {
        require(std::string(error.what()).find(expected) !=
                    std::string::npos,
                message);
        return;
    }
    throw std::runtime_error(message);
}
#endif

void test_content_and_message_values() {
    const Content image =
        Content::image_bytes({1, 2, 3}, "image/png", JsonValue{{"detail", "low"}});
    require(image.type() == ContentType::image, "image content type");
    require(image.source() == ContentSource::bytes, "image content source");
    require(image.bytes().size() == 3, "image byte storage");
    require(image.options().at("detail") == "low", "image options");

    require_throws<std::invalid_argument>(
        [] { (void)Content::image_bytes({}, "image/png"); },
        "empty image bytes must fail");
    require_throws<std::invalid_argument>(
        [] { (void)Content::extension("", JsonValue::object()); },
        "empty extension provider must fail");

    Message message = Message::assistant(
        Message::Contents{Content::text("hello"), Content::text(" world")});
    message.set_provider_metadata(JsonValue{{"continuation", "opaque"}});
    require(message.text() == "hello world", "message text concatenation");
    require(message.provider_metadata().at("continuation") == "opaque",
            "provider metadata");

    const Message tool_message =
        Message::tool_json("call-1", "lookup", JsonValue{{"ok", true}}, true);
    require(tool_message.role() == Role::tool, "tool role");
    require(tool_message.tool_call_id() == "call-1", "tool call id");
    require(tool_message.tool_name() == "lookup", "tool name");
    require(tool_message.is_tool_error(), "tool error flag");
    require(JsonValue::parse(tool_message.text()).at("ok").get<bool>(),
            "JSON tool result");

    ProviderError error("bad response", Provider::openai, 429, "rate_limit", "r1");
    require(error.provider() == Provider::openai, "provider error provider");
    require(error.status() == 429, "provider error status");
    require(error.code() == "rate_limit", "provider error code");
    require(error.request_id() == "r1", "provider error request id");
    require(std::string(to_string(FinishReason::refusal)) == "refusal",
            "finish reason spelling");
}

void test_session_history_and_state() {
    SessionOptions options;
    options.id = "restored-session";
    options.system_message = "Be concise.";
    options.messages.push_back(Message::user("restored"));
    Session session(std::move(options));

    require(session.id() == "restored-session", "explicit session id");
    require(session.system().value() == "Be concise.", "system message");
    require(session.messages().size() == 1, "restored message");

    session.state().set("answer", 40);
    const int updated =
        session.state().update<int>("answer", 0, [](int value) {
            return value + 2;
        });
    require(updated == 42, "atomic state update");
    require(session.state().get<int>("answer").value() == 42, "state get");
    require(session.state().contains("answer"), "state contains");
    require_throws<std::bad_any_cast>(
        [&] { (void)session.state().get<std::string>("answer"); },
        "state type mismatch");
    session.state().erase("answer");
    require(!session.state().contains("answer"), "state erase");

    require_throws<std::logic_error>(
        [&] { session.set_system("too late"); },
        "system cannot change after history");
    require_throws<std::invalid_argument>(
        [&] { session.append(Message::system("not allowed")); },
        "system cannot enter ordinary history");

    session.state().set("retained", 7);
    session.clear_messages();
    require(session.messages().empty(), "clear history");
    require(session.system().value() == "Be concise.", "system retained");
    require(session.state().get<int>("retained").value() == 7,
            "state retained");
    session.set_system("Begin a fresh conversation.");
    require(session.system().value() == "Begin a fresh conversation.",
            "clearing history reopens system configuration");

    Session first;
    Session second;
    require(first.id() != second.id(), "generated session ids differ");

    SessionOptions invalid;
    invalid.messages.push_back(Message::system("duplicate system channel"));
    require_throws<std::invalid_argument>(
        [&] {
            Session rejected(std::move(invalid));
            (void)rejected;
        },
        "system messages in restored history must fail");
}

void test_function_tool_and_schema_validation() {
    FunctionTool tool(
        integer_tool_spec("increment"),
        [](ToolContext& context, const JsonValue& arguments) {
            const int value = arguments.at("value").get<int>();
            const int total = context.state().update<int>(
                "total", 0, [value](int current) { return current + value; });
            return ToolOutput::json(JsonValue{{"total", total}});
        });

    Session session;
    ToolContext context(session.id(), "run-1", "call-1", session.state());
    const ToolOutput output = tool.invoke(context, JsonValue{{"value", 3}});
    require(!output.is_error, "function tool success");
    require(JsonValue::parse(output.contents.front().value()).at("total") == 3,
            "function tool result");
    require(context.session_id() == session.id(), "tool context session id");
    require(context.run_id() == "run-1", "tool context run id");
    require(context.call_id() == "call-1", "tool context call id");

    ToolSpec invalid_name = integer_tool_spec("bad name");
    require_throws<std::invalid_argument>(
        [&] {
            FunctionTool rejected(
                invalid_name,
                [](ToolContext&, const JsonValue&) {
                    return ToolOutput::text("unused");
                });
            (void)rejected;
        },
        "invalid tool name");

    ToolSpec invalid_schema = integer_tool_spec("bad_schema");
    invalid_schema.input_schema = JsonValue::array();
    require_throws<std::invalid_argument>(
        [&] {
            FunctionTool rejected(
                invalid_schema,
                [](ToolContext&, const JsonValue&) {
                    return ToolOutput::text("unused");
                });
            (void)rejected;
        },
        "invalid tool schema");

    require_throws<std::invalid_argument>(
        [&] { (void)tool.invoke(context, JsonValue::array()); },
        "tool arguments must be an object");
}

void test_full_parallel_tool_loop_and_tracing() {
    std::mutex completion_mutex;
    std::condition_variable completion_condition;
    bool second_may_finish = false;
    std::vector<int> completion_order;

    const FunctionTool::Function record =
        [&](ToolContext& context, const JsonValue& arguments) {
            const int value = arguments.at("value").get<int>();
            if (value == 1) {
                std::unique_lock<std::mutex> lock(completion_mutex);
                completion_condition.wait(lock, [&] { return second_may_finish; });
            }
            context.state().update<int>(
                "count", 0, [](int current) { return current + 1; });
            {
                std::lock_guard<std::mutex> lock(completion_mutex);
                completion_order.push_back(value);
                if (value == 2) {
                    second_may_finish = true;
                }
            }
            completion_condition.notify_all();
            return ToolOutput::json(JsonValue{{"value", value}});
        };
    auto first_tool = std::make_shared<FunctionTool>(
        integer_tool_spec("record_first"), record);
    auto second_tool = std::make_shared<FunctionTool>(
        integer_tool_spec("record_second"), record);

    auto throwing_tracer = std::make_shared<ThrowingTracer>();
    auto recording_tracer = std::make_shared<RecordingTracer>();

    ClientOptions options;
    options.tools = {first_tool, second_tool};
    options.tracers = {throwing_tracer, recording_tracer};
    options.parallel_tool_calls = true;

    ModelDescriptor model = test_model(true);
    model.max_output_tokens = 77;
    std::vector<AIRequest> requests;
    FunctionAIClient client(
        std::move(model),
        [&](const AIRequest& request) {
            requests.push_back(request);
            if (requests.size() == 1) {
                std::vector<ToolCall> calls;
                calls.push_back(call(
                    "first", "record_first", JsonValue{{"value", 1}}));
                calls.push_back(call(
                    "second", "record_second", JsonValue{{"value", 2}}));
                Message message = Message::assistant("working", std::move(calls));
                message.set_provider_metadata(JsonValue{{"turn", 1}});
                AIResponse response(std::move(message));
                response.finish_reason = FinishReason::tool_calls;
                response.usage.input_tokens = 10;
                response.usage.output_tokens = 2;
                response.usage.total_tokens = 12;
                response.usage.cached_input_tokens = 1;
                response.usage.cache_creation_input_tokens = 0;
                response.usage.reasoning_tokens = 0;
                response.provider_request_id = "provider-1";
                return response;
            }

            Message message = Message::assistant("done");
            message.set_provider_metadata(JsonValue{{"turn", 2}});
            AIResponse response(std::move(message));
            response.finish_reason = FinishReason::stop;
            response.usage.input_tokens = 4;
            response.usage.output_tokens = 3;
            response.usage.total_tokens = 7;
            response.usage.cached_input_tokens = 0;
            response.usage.cache_creation_input_tokens = 0;
            response.usage.reasoning_tokens = 1;
            response.provider_request_id = "provider-2";
            return response;
        },
        options);

    SessionOptions session_options;
    session_options.id = "tool-loop";
    session_options.system_message = "Use tools.";
    Session session(std::move(session_options));
    const AIResponse result = client.generate(session, "start");

    require(result.message.text() == "done", "final response");
    require(result.message.provider_metadata().at("turn") == 2,
            "final provider metadata");
    require(result.model_rounds == 2, "cumulative model rounds");
    require(result.tool_calls == 2, "cumulative tool calls");
    require(result.usage.input_tokens.value() == 14, "cumulative input usage");
    require(result.usage.output_tokens.value() == 5, "cumulative output usage");
    require(result.usage.total_tokens.value() == 19, "cumulative total usage");
    require(result.usage.cached_input_tokens.value() == 1,
            "cumulative cached usage");
    require(result.usage.cache_creation_input_tokens.value() == 0,
            "cumulative cache creation usage");
    require(result.usage.reasoning_tokens.value() == 1,
            "cumulative reasoning usage");
    require(session.state().get<int>("count").value() == 2,
            "parallel state updates");

    const std::vector<Message> messages = session.messages();
    require(messages.size() == 5, "complete transcript");
    require(messages[0].role() == Role::user, "user transcript role");
    require(messages[1].role() == Role::assistant, "assistant transcript role");
    require(messages[1].provider_metadata().at("turn") == 1,
            "intermediate provider metadata preserved");
    require(messages[2].role() == Role::tool, "first tool transcript role");
    require(messages[2].tool_call_id() == "first", "stable first call id");
    require(messages[2].tool_name() == "record_first",
            "stable first tool name");
    require(messages[3].tool_call_id() == "second", "stable second call id");
    require(messages[4].provider_metadata().at("turn") == 2,
            "final provider metadata preserved in transcript");

    require(completion_order.size() == 2, "both tools completed");
    require(completion_order[0] == 2,
            "test establishes completion order differs from transcript order");
    require(requests.size() == 2, "two provider requests");
    require(requests[0].system_message.value() == "Use tools.",
            "system sent separately");
    require(requests[0].options.max_output_tokens.value() == 77,
            "model descriptor supplies default max output tokens");
    require(requests[0].messages.size() == 1, "first request history");
    require(requests[1].messages.size() == 4, "second request history");
    require(requests[1].messages[2].tool_call_id() == "first",
            "provider sees stable tool order");

    const std::vector<TraceEvent> events = recording_tracer->events();
    require(!events.empty(), "recording tracer receives events");
    require(events.front().type == TraceEventType::generation_start,
            "generation start trace");
    require(events.back().type == TraceEventType::generation_end,
            "generation end trace");
}

void test_shared_tool_is_serialized_across_clients() {
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::atomic<int> invocations{0};
    auto shared_tool = std::make_shared<FunctionTool>(
        integer_tool_spec("shared"),
        [&](ToolContext&, const JsonValue&) {
            const int current =
                active.fetch_add(1, std::memory_order_relaxed) + 1;
            int observed_peak = peak.load(std::memory_order_relaxed);
            while (current > observed_peak &&
                   !peak.compare_exchange_weak(
                       observed_peak,
                       current,
                       std::memory_order_relaxed)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            invocations.fetch_add(1, std::memory_order_relaxed);
            active.fetch_sub(1, std::memory_order_relaxed);
            return ToolOutput::text("done");
        });

    ClientOptions options;
    options.tools = {shared_tool};
    const auto model_round = [](const AIRequest& request) {
        if (request.messages.size() == 1) {
            AIResponse response(Message::assistant(
                "",
                {call("shared-call", "shared", JsonValue{{"value", 1}})}));
            response.finish_reason = FinishReason::tool_calls;
            return response;
        }
        AIResponse response(Message::assistant("complete"));
        response.finish_reason = FinishReason::stop;
        return response;
    };

    FunctionAIClient first_client(test_model(true), model_round, options);
    FunctionAIClient second_client(test_model(true), model_round, options);
    Session first_session;
    Session second_session;
    auto first = std::async(std::launch::async, [&] {
        return first_client.generate(first_session, "first");
    });
    auto second = std::async(std::launch::async, [&] {
        return second_client.generate(second_session, "second");
    });

    require(first.get().message.text() == "complete",
            "first shared-tool generation completes");
    require(second.get().message.text() == "complete",
            "second shared-tool generation completes");
    require(invocations.load(std::memory_order_relaxed) == 2,
            "shared tool executes once per client");
    require(peak.load(std::memory_order_relaxed) == 1,
            "shared tool object is serialized across clients");
}

void test_invalid_unknown_and_malformed_tool_calls() {
    std::atomic<int> invocation_count{0};
    auto tool = std::make_shared<FunctionTool>(
        integer_tool_spec("validated"),
        [&](ToolContext&, const JsonValue&) {
            invocation_count.fetch_add(1, std::memory_order_relaxed);
            return ToolOutput::text("should not run");
        });

    ClientOptions options;
    options.tools = {tool};

    std::size_t round = 0;
    FunctionAIClient client(
        test_model(true),
        [&](const AIRequest& request) {
            ++round;
            if (round == 1) {
                ToolCall wrong_type =
                    call("wrong-type", "validated", JsonValue{{"value", "x"}});
                ToolCall malformed;
                malformed.id = "malformed";
                malformed.name = "validated";
                malformed.arguments_valid = false;
                malformed.raw_arguments = "{broken";
                ToolCall unknown =
                    call("unknown-id", "missing", JsonValue::object());
                AIResponse response(Message::assistant(
                    "", {wrong_type, malformed, unknown}));
                response.finish_reason = FinishReason::tool_calls;
                return response;
            }
            require(request.messages.size() == 5,
                    "all invalid tool results reach next provider round");
            AIResponse response(Message::assistant("handled"));
            response.finish_reason = FinishReason::stop;
            return response;
        },
        options);

    Session session;
    const AIResponse result = client.generate(session, "validate");
    require(result.message.text() == "handled", "invalid calls recover");
    require(invocation_count.load(std::memory_order_relaxed) == 0,
            "invalid calls never invoke tool");

    const std::vector<Message> messages = session.messages();
    require(messages.size() == 6, "invalid call transcript");
    require(messages[2].tool_call_id() == "wrong-type",
            "wrong-type call id preserved");
    require(messages[2].tool_name() == "validated",
            "wrong-type name preserved");
    require(messages[2].is_tool_error(), "wrong-type error");
    require(messages[3].tool_call_id() == "malformed",
            "malformed call id preserved");
    require(messages[3].is_tool_error(), "malformed error");
    require(messages[4].tool_call_id() == "unknown-id",
            "unknown call id preserved");
    require(messages[4].tool_name() == "missing",
            "unknown tool name preserved");
    require(messages[4].is_tool_error(), "unknown tool error");
}

void test_stop_on_error_records_every_execution() {
    std::atomic<int> successful_calls{0};
    auto failing = std::make_shared<FunctionTool>(
        integer_tool_spec("fails"),
        [](ToolContext&, const JsonValue&) {
            return ToolOutput::error("expected failure");
        });
    auto succeeding = std::make_shared<FunctionTool>(
        integer_tool_spec("succeeds"),
        [&](ToolContext&, const JsonValue&) {
            successful_calls.fetch_add(1, std::memory_order_relaxed);
            return ToolOutput::text("ok");
        });

    ClientOptions options;
    options.tools = {failing, succeeding};
    options.parallel_tool_calls = true;
    options.stop_on_tool_error = true;

    FunctionAIClient client(
        test_model(true),
        [](const AIRequest&) {
            AIResponse response(Message::assistant(
                "",
                {call("failure-id", "fails", JsonValue{{"value", 1}}),
                 call("success-id", "succeeds", JsonValue{{"value", 2}})}));
            response.finish_reason = FinishReason::tool_calls;
            return response;
        },
        options);

    Session session;
    const AIResponse result = client.generate(session, "run both");
    require(result.finish_reason == FinishReason::error, "stop-on-error finish");
    require(result.model_rounds == 1, "stop-on-error rounds");
    require(result.tool_calls == 2, "all executed tools counted");
    require(result.message.role() == Role::assistant,
            "stop-on-error returns provider assistant turn");
    require(result.message.tool_calls().size() == 2,
            "stop-on-error preserves provider tool calls");
    require(successful_calls.load(std::memory_order_relaxed) == 1,
            "later tool still executes in launched batch");

    const std::vector<Message> messages = session.messages();
    require(messages.size() == 4, "all executed results recorded");
    require(messages[2].tool_call_id() == "failure-id", "failure result order");
    require(messages[2].is_tool_error(), "failure result status");
    require(messages[3].tool_call_id() == "success-id", "success result order");
    require(!messages[3].is_tool_error(), "success result status");
}

void test_session_exclusive_use_and_release() {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    std::atomic<int> calls{0};

    FunctionAIClient client(
        test_model(),
        [&](const AIRequest&) {
            if (calls.fetch_add(1, std::memory_order_relaxed) == 0) {
                std::unique_lock<std::mutex> lock(mutex);
                entered = true;
                condition.notify_all();
                condition.wait(lock, [&] { return release; });
            }
            AIResponse response(Message::assistant("ok"));
            response.finish_reason = FinishReason::stop;
            return response;
        });

    Session session;
    std::exception_ptr worker_error;
    std::thread worker([&] {
        try {
            (void)client.generate(session, "first");
        } catch (...) {
            worker_error = std::current_exception();
        }
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return entered; });
    }

    require(session.messages().size() == 1,
            "snapshot remains available during generation");
    require_throws<SessionInUseError>(
        [&] { (void)client.generate(session, "second"); },
        "second generation must fail immediately");
    require_throws<SessionInUseError>(
        [&] { session.append(Message::user("external")); },
        "external append must fail during generation");
    require_throws<SessionInUseError>(
        [&] { session.clear_messages(); },
        "clear must fail during generation");

    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    worker.join();
    if (worker_error) {
        std::rethrow_exception(worker_error);
    }

    const AIResponse second = client.generate(session, "after release");
    require(second.message.text() == "ok", "lease releases after success");
    require(session.messages().size() == 4, "two completed turns");

    std::atomic<bool> first{true};
    FunctionAIClient throwing_client(
        test_model(),
        [&](const AIRequest&) {
            if (first.exchange(false, std::memory_order_relaxed)) {
                throw std::runtime_error("provider exploded");
            }
            AIResponse response(Message::assistant("recovered"));
            response.finish_reason = FinishReason::stop;
            return response;
        });
    Session throwing_session;
    require_throws<std::runtime_error>(
        [&] { (void)throwing_client.generate(throwing_session, "fail"); },
        "provider exception propagates");
    const AIResponse recovered =
        throwing_client.generate(throwing_session, "retry");
    require(recovered.message.text() == "recovered",
            "lease releases after exception");
}

void test_parallel_state_stress_and_max_rounds() {
    std::atomic<int> active_tools{0};
    std::atomic<int> peak_active_tools{0};
    const FunctionTool::Function count =
        [&](ToolContext& context, const JsonValue&) {
            const int active =
                active_tools.fetch_add(1, std::memory_order_relaxed) + 1;
            int peak = peak_active_tools.load(std::memory_order_relaxed);
            while (active > peak &&
                   !peak_active_tools.compare_exchange_weak(
                       peak, active, std::memory_order_relaxed)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            const int total = context.state().update<int>(
                "parallel-count", 0, [](int current) { return current + 1; });
            active_tools.fetch_sub(1, std::memory_order_relaxed);
            return ToolOutput::json(JsonValue{{"count", total}});
        };
    auto counter_zero = std::make_shared<FunctionTool>(
        integer_tool_spec("counter_0"), count);
    auto counter_one = std::make_shared<FunctionTool>(
        integer_tool_spec("counter_1"), count);
    auto counter_two = std::make_shared<FunctionTool>(
        integer_tool_spec("counter_2"), count);

    ClientOptions options;
    options.tools = {counter_zero, counter_one, counter_two};
    options.parallel_tool_calls = true;
    options.max_parallel_tool_calls = 3;

    std::size_t round = 0;
    FunctionAIClient client(
        test_model(true),
        [&](const AIRequest&) {
            ++round;
            if (round == 1) {
                std::vector<ToolCall> calls;
                for (int index = 0; index < 32; ++index) {
                    calls.push_back(call(
                        "call-" + std::to_string(index),
                        "counter_" + std::to_string(index % 3),
                        JsonValue{{"value", index}}));
                }
                AIResponse response(
                    Message::assistant("", std::move(calls)));
                response.finish_reason = FinishReason::tool_calls;
                return response;
            }
            AIResponse response(Message::assistant("complete"));
            response.finish_reason = FinishReason::stop;
            return response;
        },
        options);

    Session session;
    const AIResponse result = client.generate(session, "stress");
    require(result.tool_calls == 32, "parallel tool count");
    require(session.state().get<int>("parallel-count").value() == 32,
            "parallel state is atomic");
    require(peak_active_tools.load(std::memory_order_relaxed) > 1,
            "parallel execution overlaps");
    require(peak_active_tools.load(std::memory_order_relaxed) <= 3,
            "parallel execution respects configured bound");
    const std::vector<Message> messages = session.messages();
    for (int index = 0; index < 32; ++index) {
        require(messages[static_cast<std::size_t>(index) + 2].tool_call_id() ==
                    "call-" + std::to_string(index),
                "parallel results preserve request order");
    }

    ClientOptions one_round_options;
    one_round_options.tools = {counter_zero};
    one_round_options.max_model_rounds = 1;
    FunctionAIClient endless(
        test_model(true),
        [](const AIRequest&) {
            AIResponse response(Message::assistant(
                "",
                {call("again", "counter_0", JsonValue{{"value", 1}})}));
            response.finish_reason = FinishReason::tool_calls;
            return response;
        },
        one_round_options);
    Session endless_session;
    require_throws<MaxRoundsError>(
        [&] { (void)endless.generate(endless_session, "loop"); },
        "max rounds must fail");
    require(!endless_session.state().contains("parallel-count"),
            "final allowed round must not execute tool side effects");
    require(endless_session.messages().size() == 1,
            "terminal tool call is not retained without its result");
    require_throws<MaxRoundsError>(
        [&] { (void)endless.generate(endless_session, "lease released"); },
        "max-round exception must release session");
    require(endless_session.messages().size() == 2,
            "retry history contains only complete user inputs");

    ClientOptions continuation_options;
    continuation_options.max_model_rounds = 1;
    FunctionAIClient continuation(
        test_model(),
        [](const AIRequest&) {
            AIResponse response(Message::assistant("partial"));
            response.requires_continuation = true;
            return response;
        },
        continuation_options);
    Session continuation_session;
    require_throws<MaxRoundsError>(
        [&] { (void)continuation.generate(continuation_session, "continue"); },
        "terminal protocol continuation must fail");
    require(continuation_session.messages().size() == 1,
            "terminal continuation marker is not retained");

    std::atomic<int> limited_invocations{0};
    auto limited_tool = std::make_shared<FunctionTool>(
        integer_tool_spec("limited"),
        [&](ToolContext&, const JsonValue&) {
            limited_invocations.fetch_add(1, std::memory_order_relaxed);
            return ToolOutput::text("unexpected");
        });
    ClientOptions limited_options;
    limited_options.tools = {limited_tool};
    limited_options.max_tool_calls_per_generation = 2;
    FunctionAIClient limited_client(
        test_model(true),
        [](const AIRequest&) {
            AIResponse response(Message::assistant(
                "",
                {call("limit-1", "limited", JsonValue{{"value", 1}}),
                 call("limit-2", "limited", JsonValue{{"value", 2}}),
                 call("limit-3", "limited", JsonValue{{"value", 3}})}));
            response.finish_reason = FinishReason::tool_calls;
            response.provider_request_id = "limit-response";
            return response;
        },
        limited_options);
    Session limited_session;
    try {
        (void)limited_client.generate(limited_session, "too many");
        throw std::runtime_error("tool-call limit must fail");
    } catch (const ProviderError& error) {
        require(error.code() == "tool_call_limit",
                "tool-call limit error code");
        require(error.request_id() == "limit-response",
                "tool-call limit request id");
    }
    require(limited_invocations.load(std::memory_order_relaxed) == 0,
            "tool-call limit rejects before side effects");
}

void test_tool_exception_sanitization_and_spec_snapshot() {
    auto tracer = std::make_shared<InMemoryTracer>();
    auto throwing_tool = std::make_shared<FunctionTool>(
        integer_tool_spec("throws"),
        [](ToolContext&, const JsonValue&) -> ToolOutput {
            throw std::runtime_error("database password=do-not-expose");
        });
    ClientOptions throwing_options;
    throwing_options.tools = {throwing_tool};
    throwing_options.tracers = {tracer};

    std::size_t round = 0;
    FunctionAIClient throwing_client(
        test_model(true),
        [&](const AIRequest& request) {
            ++round;
            if (round == 1) {
                AIResponse response(Message::assistant(
                    "",
                    {call("throw-1", "throws",
                          JsonValue{{"value", 1}})}));
                response.finish_reason = FinishReason::tool_calls;
                return response;
            }
            require(request.messages.back().role() == Role::tool,
                    "tool exception creates tool result");
            require(request.messages.back().text() ==
                        "tool 'throws' failed",
                    "tool exception is sanitized for the model");
            require(request.messages.back().text().find("password") ==
                        std::string::npos,
                    "tool diagnostic is absent from model content");
            AIResponse response(Message::assistant("recovered"));
            response.finish_reason = FinishReason::stop;
            return response;
        },
        throwing_options);
    const AIResponse recovered = throwing_client.generate("run");
    require(recovered.message.text() == "recovered",
            "tool exception loop recovers");
    for (const TraceEvent& event : tracer->events()) {
        require(event.payload.find("password") == std::string::npos,
                "default traces exclude tool diagnostic");
    }

    std::atomic<int> invocation_count{0};
    auto mutable_tool =
        std::make_shared<MutableSpecTool>(invocation_count);
    ClientOptions snapshot_options;
    snapshot_options.tools = {mutable_tool};
    std::size_t snapshot_round = 0;
    FunctionAIClient snapshot_client(
        test_model(true),
        [&](const AIRequest& request) {
            ++snapshot_round;
            require(request.tools.front()
                        .input_schema.at("properties")
                        .at("value")
                        .at("type") == "integer",
                    "advertised tool schema is an immutable snapshot");
            if (snapshot_round == 1) {
                AIResponse response(Message::assistant(
                    "",
                    {call("snapshot-1", "mutable_spec",
                          JsonValue{{"value", 5}})}));
                response.finish_reason = FinishReason::tool_calls;
                return response;
            }
            AIResponse response(Message::assistant("done"));
            response.finish_reason = FinishReason::stop;
            return response;
        },
        snapshot_options);
    mutable_tool->require_string_value();
    (void)snapshot_client.generate("use snapshot");
    require(invocation_count.load(std::memory_order_relaxed) == 1,
            "runtime validation uses snapshotted tool schema");
}

void test_additional_property_schemas() {
    std::atomic<int> invocations{0};

    ToolSpec deny_all;
    deny_all.name = "deny_all";
    deny_all.description = "Accepts no properties.";
    deny_all.input_schema =
        JsonValue{{"type", "object"},
                  {"additionalProperties", false}};

    ToolSpec typed_additional;
    typed_additional.name = "typed_additional";
    typed_additional.description = "Accepts integer values.";
    typed_additional.input_schema =
        JsonValue{{"type", "object"},
                  {"additionalProperties",
                   JsonValue{{"type", "integer"}}}};

    const auto function = [&](ToolContext&, const JsonValue&) {
        invocations.fetch_add(1, std::memory_order_relaxed);
        return ToolOutput::text("unexpected");
    };
    ClientOptions options;
    options.tools = {
        std::make_shared<FunctionTool>(deny_all, function),
        std::make_shared<FunctionTool>(typed_additional, function),
    };

    std::size_t round = 0;
    FunctionAIClient client(
        test_model(true),
        [&](const AIRequest& request) {
            ++round;
            if (round == 1) {
                AIResponse response(Message::assistant(
                    "",
                    {call("deny", "deny_all",
                          JsonValue{{"unexpected", 1}}),
                     call("typed", "typed_additional",
                          JsonValue{{"value", "wrong"}})}));
                response.finish_reason = FinishReason::tool_calls;
                return response;
            }
            require(request.messages[2].is_tool_error(),
                    "false additionalProperties rejects unknown key");
            require(request.messages[3].is_tool_error(),
                    "schema-valued additionalProperties validates value");
            AIResponse response(Message::assistant("validated"));
            response.finish_reason = FinishReason::stop;
            return response;
        },
        options);
    require(client.generate("validate").message.text() == "validated",
            "additional property errors return to model");
    require(invocations.load(std::memory_order_relaxed) == 0,
            "invalid additional properties do not invoke tools");
}

void test_partial_usage_remains_unknown() {
    auto tool = std::make_shared<FunctionTool>(
        integer_tool_spec("usage_tool"),
        [](ToolContext&, const JsonValue&) {
            return ToolOutput::text("ok");
        });
    ClientOptions options;
    options.tools = {tool};

    std::size_t round = 0;
    FunctionAIClient client(
        test_model(true),
        [&](const AIRequest&) {
            ++round;
            if (round == 1) {
                AIResponse response(Message::assistant(
                    "",
                    {call("usage-1", "usage_tool",
                          JsonValue{{"value", 1}})}));
                response.finish_reason = FinishReason::tool_calls;
                response.usage.input_tokens = 10;
                response.usage.output_tokens = 2;
                response.usage.total_tokens = 12;
                return response;
            }
            AIResponse response(Message::assistant("done"));
            response.finish_reason = FinishReason::stop;
            response.usage.output_tokens = 3;
            return response;
        },
        options);

    const AIResponse response = client.generate("usage");
    require(!response.usage.input_tokens.has_value(),
            "missing round keeps cumulative input unknown");
    require(response.usage.output_tokens.value() == 5,
            "fully reported output remains cumulative");
    require(!response.usage.total_tokens.has_value(),
            "missing round keeps cumulative total unknown");
}

void test_error_trace_payload_policy() {
    auto metadata_only = std::make_shared<RecordingTracer>();
    ClientOptions metadata_options;
    metadata_options.tracers = {metadata_only};

    FunctionAIClient metadata_client(
        test_model(),
        [](const AIRequest&) -> AIResponse {
            throw std::runtime_error("sensitive provider diagnostic");
        },
        metadata_options);
    Session metadata_session;
    require_throws<std::runtime_error>(
        [&] { (void)metadata_client.generate(metadata_session, "fail"); },
        "provider diagnostic propagates to caller");

    const std::vector<TraceEvent> metadata_events = metadata_only->events();
    require(metadata_events.back().type == TraceEventType::error,
            "provider exception emits error event");
    require(metadata_events.back().attributes.at("category") == "exception",
            "metadata trace keeps generic category");
    require(metadata_events.back().attributes.find("message") ==
                metadata_events.back().attributes.end(),
            "metadata trace excludes exception message");
    require(metadata_events.back().payload.empty(),
            "metadata trace excludes diagnostic payload");

    auto payload_tracer = std::make_shared<RecordingTracer>();
    ClientOptions payload_options;
    payload_options.tracers = {payload_tracer};
    payload_options.capture_trace_payloads = true;
    FunctionAIClient payload_client(
        test_model(),
        [](const AIRequest&) -> AIResponse {
            throw std::runtime_error("opt-in diagnostic");
        },
        payload_options);
    Session payload_session;
    require_throws<std::runtime_error>(
        [&] { (void)payload_client.generate(payload_session, "fail"); },
        "opt-in provider diagnostic propagates");
    require(payload_tracer->events().back().payload == "opt-in diagnostic",
            "payload capture opt-in includes diagnostic");
}

void test_default_tracers() {
    TraceEvent event;
    event.level = TraceLevel::warning;
    event.type = TraceEventType::tool_end;
    event.session_id = "trace-session";
    event.run_id = "trace-run";
    event.operation_id = "trace-operation";
    event.name = "trace-tool";
    event.attributes["attempt"] = 2;
    event.payload = "sensitive payload";

    InMemoryTracer memory;
    memory.record(event);
    require(memory.events().size() == 1, "in-memory tracer records event");
    require(memory.events().front().attributes.at("attempt") == 2,
            "in-memory tracer preserves attributes");
    memory.clear();
    require(memory.events().empty(), "in-memory tracer clear");

    std::vector<TraceEvent> callback_events;
    FunctionTracer callback(
        [&](const TraceEvent& value) { callback_events.push_back(value); });
    callback.record(event);
    require(callback_events.size() == 1, "function tracer records event");
    require(callback_events.front().operation_id == "trace-operation",
            "function tracer preserves operation id");

    std::atomic<int> active_callbacks{0};
    std::atomic<int> peak_callbacks{0};
    FunctionTracer serialized_callback(
        [&](const TraceEvent&) {
            const int active =
                active_callbacks.fetch_add(1, std::memory_order_relaxed) + 1;
            int peak = peak_callbacks.load(std::memory_order_relaxed);
            while (active > peak &&
                   !peak_callbacks.compare_exchange_weak(
                       peak, active, std::memory_order_relaxed)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            active_callbacks.fetch_sub(1, std::memory_order_relaxed);
        });
    std::vector<std::thread> callback_threads;
    for (int index = 0; index < 4; ++index) {
        callback_threads.emplace_back(
            [&] { serialized_callback.record(event); });
    }
    for (std::thread& thread : callback_threads) {
        thread.join();
    }
    require(peak_callbacks.load(std::memory_order_relaxed) == 1,
            "function tracer serializes its callback");

    TraceEvent untrusted_event = event;
    untrusted_event.name = "tool\nforged-line";
    std::ostringstream console_output;
    std::streambuf* previous_buffer =
        std::cerr.rdbuf(console_output.rdbuf());
    try {
        ConsoleTracer console;
        console.record(untrusted_event);
    } catch (...) {
        std::cerr.rdbuf(previous_buffer);
        throw;
    }
    std::cerr.rdbuf(previous_buffer);
    const std::string console_line = console_output.str();
    require(console_line.find("\\n") != std::string::npos,
            "console tracer escapes embedded newlines");
    require(console_line.find('\n') == console_line.rfind('\n'),
            "console tracer emits one physical log line");

    ScopedTestFile trace_file{"neuralplus-core-tracer-test.jsonl"};
    FileTracerOptions options;
    options.append = false;
    options.flush_each_event = true;
    {
        FileTracer file(trace_file.path(), options);
        file.record(event);
    }

    std::ifstream input(trace_file.path(), std::ios::binary);
    std::string line;
    std::getline(input, line);
    require(!line.empty(), "file tracer writes one JSON line");
    const JsonValue parsed = JsonValue::parse(line);
    require(parsed.at("type") == "tool_end", "file tracer event type");
    require(parsed.at("attributes").at("attempt") == 2,
            "file tracer attributes");
    require(parsed.find("payload") == parsed.end(),
            "file tracer excludes payload by default");

#if !defined(_WIN32)
    struct stat metadata {};
    require(::stat(trace_file.path().c_str(), &metadata) == 0,
            "trace file metadata is available");
    require((metadata.st_mode & 0777) == 0600,
            "new trace file uses private POSIX permissions");

#if defined(O_NOFOLLOW)
    ScopedTestFile symlink_target{
        "neuralplus-core-tracer-symlink-target.jsonl"};
    ScopedTestFile symlink_path{
        "neuralplus-core-tracer-symlink.jsonl"};
    {
        std::ofstream target(symlink_target.path(), std::ios::binary);
        target << "unchanged";
    }
    require(::symlink(
                symlink_target.path().c_str(),
                symlink_path.path().c_str()) == 0,
            "trace symlink fixture is created");
    require_throws<ConfigurationError>(
        [&] {
            FileTracer rejected(symlink_path.path());
            (void)rejected;
        },
        "file tracer rejects a symbolic-link destination");
#endif
#endif
}

void test_curl_transport_validation() {
    HttpTransportOptions invalid_limits;
    invalid_limits.max_response_body_bytes = 0;
    require_throws<ConfigurationError>(
        [&] {
            CurlHttpTransport rejected(invalid_limits);
            (void)rejected;
        },
        "zero response body limit");

    HttpTransportOptions invalid_timeout;
    invalid_timeout.connect_timeout = std::chrono::milliseconds{0};
    require_throws<ConfigurationError>(
        [&] {
            CurlHttpTransport rejected(invalid_timeout);
            (void)rejected;
        },
        "zero connect timeout");

    CurlHttpTransport transport;
    HttpRequest request;
    request.url = "https://example.invalid";
    request.timeout = std::chrono::milliseconds{0};
    require_throws<ConfigurationError>(
        [&] { (void)transport.send(request); },
        "zero request timeout");

    request.timeout = std::chrono::milliseconds{100};
    request.method = HttpMethod::get;
    request.body = "unexpected";
    require_throws<ConfigurationError>(
        [&] { (void)transport.send(request); },
        "GET body rejected before transfer");

    request.method = HttpMethod::post;
    request.body.clear();
    request.headers = {{"Bad Header", "value"}};
    require_throws<ConfigurationError>(
        [&] { (void)transport.send(request); },
        "invalid header name");

    request.headers = {{"X-Test", "value\x01"}};
    require_throws<ConfigurationError>(
        [&] { (void)transport.send(request); },
        "invalid header control character");

#if !defined(_WIN32)
    {
        LoopbackHttpServer server(
            {loopback_response("hello", {"X-NeuralPlus: ready"})});
        HttpTransportOptions options;
        options.max_response_body_bytes = 5;
        options.max_response_header_bytes = 1024;
        CurlHttpTransport loopback_transport(options);
        HttpRequest loopback_request;
        loopback_request.method = HttpMethod::get;
        loopback_request.url = server.url();
        loopback_request.timeout = std::chrono::milliseconds{2000};
        const HttpResponse response =
            loopback_transport.send(loopback_request);
        require(response.status == 200, "loopback HTTP response status");
        require(response.body == "hello", "loopback HTTP response body");
        require(response.header("X-NeuralPlus") ==
                    std::optional<std::string>{"ready"},
                "loopback HTTP response header");
    }

    {
        LoopbackHttpServer server({loopback_response("12345")});
        HttpTransportOptions options;
        options.max_response_body_bytes = 4;
        CurlHttpTransport limited_transport(options);
        HttpRequest limited_request;
        limited_request.method = HttpMethod::get;
        limited_request.url = server.url();
        limited_request.timeout = std::chrono::milliseconds{2000};
        require_transport_error_contains(
            [&] { (void)limited_transport.send(limited_request); },
            "size limit",
            "response body limit classification");
    }

    {
        LoopbackHttpServer server(
            {loopback_response("", {"X-Padding: " + std::string(256, 'x')})});
        HttpTransportOptions options;
        options.max_response_header_bytes = 64;
        CurlHttpTransport limited_transport(options);
        HttpRequest limited_request;
        limited_request.method = HttpMethod::get;
        limited_request.url = server.url();
        limited_request.timeout = std::chrono::milliseconds{2000};
        require_transport_error_contains(
            [&] { (void)limited_transport.send(limited_request); },
            "size limit",
            "response header limit classification");
    }

    {
        constexpr std::size_t request_count = 4;
        LoopbackHttpServer server(
            std::vector<std::string>(
                request_count, loopback_response("parallel")));
        auto shared_transport =
            std::make_shared<CurlHttpTransport>();
        std::vector<std::future<HttpResponse>> responses;
        for (std::size_t index = 0; index < request_count; ++index) {
            responses.push_back(std::async(
                std::launch::async,
                [shared_transport, url = server.url()] {
                    HttpRequest concurrent_request;
                    concurrent_request.method = HttpMethod::get;
                    concurrent_request.url = url;
                    concurrent_request.timeout =
                        std::chrono::milliseconds{2000};
                    return shared_transport->send(concurrent_request);
                }));
        }
        for (std::future<HttpResponse>& response : responses) {
            require(response.get().body == "parallel",
                    "concurrent loopback response body");
        }
        require(server.completed(),
                "loopback server handled all concurrent requests");
    }
#endif
}

void test_configuration_and_input_validation() {
    ModelDescriptor invalid_model = test_model();
    invalid_model.id.clear();
    require_throws<ConfigurationError>(
        [&] {
            FunctionAIClient rejected(
                invalid_model,
                [](const AIRequest&) {
                    return AIResponse(Message::assistant("unused"));
                });
            (void)rejected;
        },
        "empty model id");

    ClientOptions zero_rounds;
    zero_rounds.max_model_rounds = 0;
    require_throws<ConfigurationError>(
        [&] {
            FunctionAIClient rejected(
                test_model(),
                [](const AIRequest&) {
                    return AIResponse(Message::assistant("unused"));
                },
                zero_rounds);
            (void)rejected;
        },
        "zero max rounds");

    auto first = std::make_shared<FunctionTool>(
        integer_tool_spec("duplicate"),
        [](ToolContext&, const JsonValue&) {
            return ToolOutput::text("one");
        });
    auto second = std::make_shared<FunctionTool>(
        integer_tool_spec("duplicate"),
        [](ToolContext&, const JsonValue&) {
            return ToolOutput::text("two");
        });
    ClientOptions duplicates;
    duplicates.tools = {first, second};
    require_throws<ConfigurationError>(
        [&] {
            FunctionAIClient rejected(
                test_model(true),
                [](const AIRequest&) {
                    return AIResponse(Message::assistant("unused"));
                },
                duplicates);
            (void)rejected;
        },
        "duplicate tools");

    FunctionAIClient client(
        test_model(),
        [](const AIRequest&) {
            AIResponse response(Message::assistant("ok"));
            response.finish_reason = FinishReason::stop;
            return response;
        });
    Session session;
    require_throws<std::invalid_argument>(
        [&] { (void)client.generate(session, Message::assistant("wrong role")); },
        "assistant input rejected");
    require_throws<std::invalid_argument>(
        [&] { (void)client.generate(session, ""); },
        "empty input rejected");
    require_throws<std::invalid_argument>(
        [&] {
            Message::Contents contents;
            contents.push_back(Content::image_url("https://example.test/a.png"));
            (void)client.generate(session, Message::user(std::move(contents)));
        },
        "unsupported image input");

    GenerateOptions invalid_options;
    invalid_options.max_output_tokens = 0;
    require_throws<std::invalid_argument>(
        [&] { (void)client.generate(session, "hello", invalid_options); },
        "zero output token option");
}

}  // namespace

int main() {
    try {
        test_content_and_message_values();
        test_session_history_and_state();
        test_function_tool_and_schema_validation();
        test_full_parallel_tool_loop_and_tracing();
        test_shared_tool_is_serialized_across_clients();
        test_invalid_unknown_and_malformed_tool_calls();
        test_stop_on_error_records_every_execution();
        test_session_exclusive_use_and_release();
        test_parallel_state_stress_and_max_rounds();
        test_tool_exception_sanitization_and_spec_snapshot();
        test_additional_property_schemas();
        test_partial_usage_remains_unknown();
        test_error_trace_payload_policy();
        test_default_tracers();
        test_curl_transport_validation();
        test_configuration_and_input_validation();
    } catch (const std::exception& error) {
        std::cerr << "core test failure: " << error.what() << '\n';
        return 1;
    }

    std::cout << "all core tests passed\n";
    return 0;
}
