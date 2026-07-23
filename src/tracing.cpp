// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/tracing.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#endif

using namespace neuralplus;

namespace {

std::string format_timestamp(
    const std::chrono::system_clock::time_point& timestamp) {
    const std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch()) %
        1000;

    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setfill('0') << std::setw(3) << milliseconds.count() << 'Z';
    return output.str();
}

JsonValue event_json(const TraceEvent& event, bool include_payload) {
    JsonValue value{
        {"timestamp", format_timestamp(event.timestamp)},
        {"level", to_string(event.level)},
        {"type", to_string(event.type)},
        {"session_id", event.session_id},
        {"run_id", event.run_id},
        {"operation_id", event.operation_id},
        {"name", event.name},
        {"attributes", event.attributes},
    };
    if (include_payload && !event.payload.empty()) {
        value["payload"] = event.payload;
    }
    return value;
}

std::string human_line(const TraceEvent& event) {
    std::ostringstream output;
    output << format_timestamp(event.timestamp) << " [" << to_string(event.level)
           << "] " << to_string(event.type);
    if (!event.name.empty()) {
        output << " name=" << JsonValue(event.name).dump();
    }
    if (!event.session_id.empty()) {
        output << " session=" << JsonValue(event.session_id).dump();
    }
    if (!event.run_id.empty()) {
        output << " run=" << JsonValue(event.run_id).dump();
    }
    if (!event.operation_id.empty()) {
        output << " operation=" << JsonValue(event.operation_id).dump();
    }
    if (!event.attributes.empty()) {
        output << " attributes=" << event.attributes.dump();
    }
    return output.str();
}

#if !defined(_WIN32)
std::mutex& syslog_mutex() {
    static std::mutex mutex;
    return mutex;
}

int syslog_priority(TraceLevel level) {
    switch (level) {
        case TraceLevel::debug:
            return LOG_DEBUG;
        case TraceLevel::info:
            return LOG_INFO;
        case TraceLevel::warning:
            return LOG_WARNING;
        case TraceLevel::error:
            return LOG_ERR;
    }
    return LOG_INFO;
}
#endif

}  // namespace

Tracer::~Tracer() = default;

ConsoleTracer::ConsoleTracer() = default;

ConsoleTracer::~ConsoleTracer() = default;

void ConsoleTracer::record(const TraceEvent& event) {
    const std::string line = human_line(event);
    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << line << '\n';
}

class FileTracer::Impl final {
public:
    Impl(std::string file_path, FileTracerOptions file_options)
        : options(file_options) {
        if (file_path.empty()) {
            throw ConfigurationError("trace file path must not be empty");
        }

#if !defined(_WIN32)
        int flags = O_WRONLY | O_CREAT;
        flags |= options.append ? O_APPEND : O_TRUNC;
#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        const int descriptor =
            ::open(file_path.c_str(), flags, S_IRUSR | S_IWUSR);
        if (descriptor == -1) {
            throw ConfigurationError("trace file could not be opened");
        }

        struct stat metadata {};
        if (::fstat(descriptor, &metadata) != 0) {
            (void)::close(descriptor);
            throw ConfigurationError("trace file metadata could not be read");
        }
        if (options.append && metadata.st_size > 0) {
            const auto existing_size =
                static_cast<unsigned long long>(metadata.st_size);
            if (existing_size >
                static_cast<unsigned long long>(
                    std::numeric_limits<std::size_t>::max())) {
                (void)::close(descriptor);
                throw ConfigurationError("trace file is too large");
            }
            bytes_written = static_cast<std::size_t>(existing_size);
        }

        stream = ::fdopen(descriptor, options.append ? "ab" : "wb");
        if (stream == nullptr) {
            (void)::close(descriptor);
            throw ConfigurationError("trace file stream could not be opened");
        }
#else
        std::ios::openmode mode = std::ios::out | std::ios::binary;
        if (options.append) {
            mode |= std::ios::app;
            std::ifstream existing(
                file_path, std::ios::binary | std::ios::ate);
            if (existing) {
                const auto position = existing.tellg();
                if (position > 0) {
                    bytes_written = static_cast<std::size_t>(position);
                }
            }
        } else {
            mode |= std::ios::trunc;
        }
        stream.open(file_path, mode);
        if (!stream) {
            throw ConfigurationError("trace file could not be opened");
        }
#endif
    }

    ~Impl() {
#if !defined(_WIN32)
        if (stream != nullptr) {
            (void)::fclose(stream);
        }
#endif
    }

    FileTracerOptions options;
    std::mutex mutex;
#if !defined(_WIN32)
    std::FILE* stream{nullptr};
#else
    std::ofstream stream;
#endif
    std::size_t bytes_written{0};
};

FileTracer::FileTracer(std::string path, FileTracerOptions options)
    : impl_(std::make_unique<Impl>(std::move(path), options)) {}

FileTracer::~FileTracer() = default;

void FileTracer::record(const TraceEvent& event) {
    std::string line = event_json(event, impl_->options.include_payloads).dump();
    line.push_back('\n');

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->options.max_bytes != 0 &&
        line.size() > impl_->options.max_bytes - std::min(
                          impl_->bytes_written, impl_->options.max_bytes)) {
        throw Error("trace file size limit reached");
    }

#if !defined(_WIN32)
    const std::size_t written =
        std::fwrite(line.data(), 1U, line.size(), impl_->stream);
    if (written != line.size()) {
        throw Error("trace file write failed");
    }
    if (impl_->options.flush_each_event) {
        if (std::fflush(impl_->stream) != 0) {
            throw Error("trace file flush failed");
        }
    }
#else
    impl_->stream.write(
        line.data(), static_cast<std::streamsize>(line.size()));
    if (impl_->options.flush_each_event) {
        impl_->stream.flush();
    }
    if (!impl_->stream) {
        throw Error("trace file write failed");
    }
#endif
    impl_->bytes_written += line.size();
}

InMemoryTracer::InMemoryTracer() = default;

InMemoryTracer::~InMemoryTracer() = default;

void InMemoryTracer::record(const TraceEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
}

std::vector<TraceEvent> InMemoryTracer::events() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
}

void InMemoryTracer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
}

FunctionTracer::FunctionTracer(Function function)
    : function_(std::move(function)) {
    if (!function_) {
        throw ConfigurationError("trace callback must not be empty");
    }
}

FunctionTracer::~FunctionTracer() = default;

void FunctionTracer::record(const TraceEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    function_(event);
}

class SyslogTracer::Impl final {
public:
    Impl(std::string value, int requested_facility)
        : identity(std::move(value)), facility(requested_facility) {
        if (identity.empty()) {
            throw ConfigurationError("syslog identity must not be empty");
        }
#if defined(_WIN32)
        throw ConfigurationError("SyslogTracer is not supported on Windows");
#else
        if (facility == 0) {
            facility = LOG_USER;
        }
#endif
    }

    std::string identity;
    int facility;
};

SyslogTracer::SyslogTracer(std::string identity, int facility)
    : impl_(std::make_unique<Impl>(std::move(identity), facility)) {}

SyslogTracer::~SyslogTracer() = default;

void SyslogTracer::record(const TraceEvent& event) {
#if defined(_WIN32)
    (void)event;
    throw ConfigurationError("SyslogTracer is not supported on Windows");
#else
    const std::string line = human_line(event);
    const std::string identity = JsonValue(impl_->identity).dump();
    std::lock_guard<std::mutex> lock(syslog_mutex());
    // Pass the facility on each call and include identity in the message. This
    // avoids changing the process-global openlog(3) identity or closing a
    // connection owned by the embedding application.
    syslog(impl_->facility | syslog_priority(event.level),
           "%s: %s",
           identity.c_str(),
           line.c_str());
#endif
}

const char* neuralplus::to_string(TraceLevel level) noexcept {
    switch (level) {
        case TraceLevel::debug:
            return "debug";
        case TraceLevel::info:
            return "info";
        case TraceLevel::warning:
            return "warning";
        case TraceLevel::error:
            return "error";
    }
    return "info";
}

const char* neuralplus::to_string(TraceEventType type) noexcept {
    switch (type) {
        case TraceEventType::generation_start:
            return "generation_start";
        case TraceEventType::generation_end:
            return "generation_end";
        case TraceEventType::provider_start:
            return "provider_start";
        case TraceEventType::provider_end:
            return "provider_end";
        case TraceEventType::tool_start:
            return "tool_start";
        case TraceEventType::tool_end:
            return "tool_end";
        case TraceEventType::error:
            return "error";
    }
    return "error";
}
