// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

/// @file
/// Thread-safe conversation sessions and session-scoped application state.

#pragma once

#include "neuralplus/export.hpp"
#include "neuralplus/types.hpp"

#include <any>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace neuralplus {

class AIClient;

/// Thread-safe, process-local cache associated with one Session.
///
/// Values must be copy-constructible and are intentionally not persisted or traced.
class NEURALPLUS_API SessionState final {
public:
    /// Creates an empty process-local cache.
    SessionState() = default;

    /// Stores or replaces a typed value.
    template <typename T>
    void set(std::string key, T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        values_[std::move(key)] = std::move(value);
    }

    /// Returns a copy of a typed value, or no value when the key is absent.
    template <typename T>
    [[nodiscard]] std::optional<T> get(std::string_view key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = values_.find(std::string(key));
        if (iterator == values_.end()) {
            return std::nullopt;
        }
        const T* value = std::any_cast<T>(&iterator->second);
        if (value == nullptr) {
            throw std::bad_any_cast{};
        }
        return *value;
    }

    /// Atomically updates one typed value.
    ///
    /// The callback runs while the state lock is held. It must not call another
    /// SessionState method on this same object.
    template <typename T, typename UpdateFunction>
    T update(std::string key, T initial_value, UpdateFunction&& update_function) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto result = values_.try_emplace(std::move(key), std::move(initial_value));
        T* current = std::any_cast<T>(&result.first->second);
        if (current == nullptr) {
            throw std::bad_any_cast{};
        }
        *current = std::invoke(std::forward<UpdateFunction>(update_function), *current);
        return *current;
    }

    /// Returns whether the cache contains a key.
    [[nodiscard]] bool contains(std::string_view key) const;

    /// Removes one cached value.
    void erase(std::string_view key);

    /// Removes all cached values.
    void clear();

private:
    SessionState(const SessionState&) = delete;
    SessionState& operator=(const SessionState&) = delete;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::any> values_;
};

/// Options used when creating or restoring a Session.
struct SessionOptions {
    /// Stable application-provided ID, or empty to generate one.
    std::string id;

    /// Provider-independent system instruction.
    std::optional<std::string> system_message;

    /// Restored non-system transcript.
    std::vector<Message> messages;
};

/// Thread-safe conversation history and tool cache.
///
/// Exactly one AIClient generation may own a Session at a time. Concurrent
/// attempts fail immediately with SessionInUseError.
class NEURALPLUS_API Session final {
public:
    /// Ordered collection used for conversation snapshots.
    using Messages = std::vector<Message>;

    /// Creates or restores a session from `options`.
    explicit Session(SessionOptions options = {});
    ~Session();

    /// Returns the stable session identifier.
    [[nodiscard]] const std::string& id() const noexcept;

    /// Sets the provider-independent system instruction.
    ///
    /// It may be changed while the current conversation history is empty.
    void set_system(std::string message);

    /// Returns a thread-safe copy of the system instruction.
    [[nodiscard]] std::optional<std::string> system() const;

    /// Appends restored or application-managed history outside a generation.
    void append(Message message);

    /// Returns a thread-safe snapshot of conversation messages.
    [[nodiscard]] Messages messages() const;

    /// Clears conversation messages while retaining the system instruction and cache.
    void clear_messages();

    /// Returns the session-scoped generic cache.
    [[nodiscard]] SessionState& state() noexcept;

    /// Returns the read-only session-scoped generic cache.
    [[nodiscard]] const SessionState& state() const noexcept;

private:
    friend class AIClient;

    class RunLease final {
    public:
        RunLease(RunLease&& other) noexcept;
        ~RunLease();

    private:
        friend class Session;
        explicit RunLease(Session& session) noexcept;

        RunLease(const RunLease&) = delete;
        RunLease& operator=(const RunLease&) = delete;
        RunLease& operator=(RunLease&&) = delete;

        Session* session_;
    };

    [[nodiscard]] RunLease acquire();
    void release() noexcept;
    void append_from_client(Message message);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    std::string id_;
    mutable std::mutex messages_mutex_;
    std::optional<std::string> system_message_;
    Messages messages_;
    SessionState state_;
    std::atomic<bool> active_{false};
};

}  // namespace neuralplus
