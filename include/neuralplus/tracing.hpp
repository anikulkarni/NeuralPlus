// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

/// @file
/// Structured trace events and built-in logging destinations.

#pragma once

#include "neuralplus/export.hpp"
#include "neuralplus/types.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace neuralplus {

/// Severity used by structured tracing and optional logging.
enum class TraceLevel {
    debug,
    info,
    warning,
    error,
};

/// Stable lifecycle event categories emitted by AIClient.
enum class TraceEventType {
    generation_start,
    generation_end,
    provider_start,
    provider_end,
    tool_start,
    tool_end,
    error,
};

/// One metadata-first trace record.
struct TraceEvent {
    /// Event severity.
    TraceLevel level{TraceLevel::info};

    /// Lifecycle category.
    TraceEventType type{TraceEventType::generation_start};

    /// Wall-clock timestamp captured when the event is created.
    std::chrono::system_clock::time_point timestamp{
        std::chrono::system_clock::now()};

    /// Session correlation identifier.
    std::string session_id;

    /// Generation correlation identifier.
    std::string run_id;

    /// Provider round or tool-call identifier.
    std::string operation_id;

    /// Model or tool name.
    std::string name;

    /// Metadata safe for the configured tracing policy.
    JsonValue attributes = JsonValue::object();

    /// Optional sensitive payload, empty unless explicitly enabled.
    std::string payload;
};

/// Thread-safe destination for tracing and optional logging.
class NEURALPLUS_API Tracer {
public:
    virtual ~Tracer();

    /// Records one structured lifecycle event.
    virtual void record(const TraceEvent& event) = 0;

protected:
    Tracer() = default;

private:
    Tracer(const Tracer&) = delete;
    Tracer& operator=(const Tracer&) = delete;
};

/// Human-readable stderr tracer.
class NEURALPLUS_API ConsoleTracer final : public Tracer {
public:
    ConsoleTracer();
    ~ConsoleTracer() override;

    /// Writes one human-readable event to standard error.
    void record(const TraceEvent& event) override;

private:
    ConsoleTracer(const ConsoleTracer&) = delete;
    ConsoleTracer& operator=(const ConsoleTracer&) = delete;

    std::mutex mutex_;
};

/// JSON Lines file tracer settings.
struct FileTracerOptions {
    /// Append to an existing file instead of truncating it.
    bool append{true};

    /// Flush the stream after every event.
    bool flush_each_event{false};

    /// Persist TraceEvent::payload in addition to metadata.
    bool include_payloads{false};

    /// Stop accepting events at this many bytes; zero means unlimited.
    std::size_t max_bytes{0};
};

/// Thread-safe JSON Lines tracer with metadata-only output by default.
///
/// POSIX builds open the destination once with owner-only creation permissions
/// and reject final-component symbolic links when the platform provides
/// `O_NOFOLLOW`.
class NEURALPLUS_API FileTracer final : public Tracer {
public:
    /// Opens a JSON Lines trace file at `path`.
    explicit FileTracer(std::string path,
                        FileTracerOptions options = {});
    ~FileTracer() override;

    /// Appends one event subject to the configured payload and size policy.
    void record(const TraceEvent& event) override;

private:
    FileTracer(const FileTracer&) = delete;
    FileTracer& operator=(const FileTracer&) = delete;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

/// Thread-safe tracer that retains events for tests and embedding applications.
class NEURALPLUS_API InMemoryTracer final : public Tracer {
public:
    InMemoryTracer();
    ~InMemoryTracer() override;

    /// Retains a copy of one event.
    void record(const TraceEvent& event) override;

    /// Returns a thread-safe snapshot of retained events.
    [[nodiscard]] std::vector<TraceEvent> events() const;

    /// Removes all retained events.
    void clear();

private:
    InMemoryTracer(const InMemoryTracer&) = delete;
    InMemoryTracer& operator=(const InMemoryTracer&) = delete;

    mutable std::mutex mutex_;
    std::vector<TraceEvent> events_;
};

/// Callback-backed tracer, including a dependency-neutral OpenTelemetry bridge.
///
/// Concurrent `record` calls are serialized before invoking the callback.
///
/// @see https://opentelemetry.io/docs/languages/cpp/instrumentation/
class NEURALPLUS_API FunctionTracer final : public Tracer {
public:
    /// Callback signature for one structured event.
    using Function = std::function<void(const TraceEvent&)>;

    /// Creates a tracer that forwards events to `function`.
    explicit FunctionTracer(Function function);
    ~FunctionTracer() override;

    /// Delegates one event to the configured callback.
    void record(const TraceEvent& event) override;

private:
    FunctionTracer(const FunctionTracer&) = delete;
    FunctionTracer& operator=(const FunctionTracer&) = delete;

    std::mutex mutex_;
    Function function_;
};

/// POSIX syslog tracer. Construction throws ConfigurationError on unsupported systems.
///
/// The configured facility is supplied per record and the identity is included
/// as a message prefix, without changing process-global `openlog` state.
///
/// @see https://pubs.opengroup.org/onlinepubs/9699919799/functions/syslog.html
class NEURALPLUS_API SyslogTracer final : public Tracer {
public:
    /// Creates a POSIX syslog destination with an identity prefix and facility.
    explicit SyslogTracer(std::string identity = "neuralplus",
                          int facility = 0);
    ~SyslogTracer() override;

    /// Sends one metadata-only event to POSIX syslog.
    void record(const TraceEvent& event) override;

private:
    SyslogTracer(const SyslogTracer&) = delete;
    SyslogTracer& operator=(const SyslogTracer&) = delete;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

/// Shared tracer collection accepted by ClientOptions.
using Tracers = std::vector<std::shared_ptr<Tracer>>;

/// Returns the stable SDK spelling of a trace level.
[[nodiscard]] NEURALPLUS_API const char* to_string(TraceLevel level) noexcept;

/// Returns the stable SDK spelling of an event category.
[[nodiscard]] NEURALPLUS_API const char* to_string(TraceEventType type) noexcept;

}  // namespace neuralplus
