// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/session.hpp"

#include <atomic>
#include <stdexcept>
#include <utility>

using namespace neuralplus;

namespace {

std::string make_session_id() {
    static std::atomic<unsigned long long> next_id{1};
    return "session-" +
           std::to_string(next_id.fetch_add(1, std::memory_order_relaxed));
}

void require_idle(bool active) {
    if (active) {
        throw SessionInUseError("session is already in use");
    }
}

}  // namespace

bool SessionState::contains(std::string_view key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_.find(std::string(key)) != values_.end();
}

void SessionState::erase(std::string_view key) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_.erase(std::string(key));
}

void SessionState::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    values_.clear();
}

Session::Session(SessionOptions options)
    : id_(options.id.empty() ? make_session_id() : std::move(options.id)),
      system_message_(std::move(options.system_message)),
      messages_(std::move(options.messages)) {
    if (system_message_ && system_message_->empty()) {
        throw std::invalid_argument("system message must not be empty");
    }

    for (const Message& message : messages_) {
        if (message.role() == Role::system) {
            throw std::invalid_argument(
                "restored messages must not contain a system message; "
                "use SessionOptions::system_message");
        }
    }
}

Session::~Session() = default;

const std::string& Session::id() const noexcept {
    return id_;
}

void Session::set_system(std::string message) {
    if (message.empty()) {
        throw std::invalid_argument("system message must not be empty");
    }

    std::lock_guard<std::mutex> lock(messages_mutex_);
    require_idle(active_.load(std::memory_order_acquire));
    if (!messages_.empty()) {
        throw std::logic_error(
            "system message cannot change after conversation history begins");
    }
    system_message_ = std::move(message);
}

std::optional<std::string> Session::system() const {
    std::lock_guard<std::mutex> lock(messages_mutex_);
    return system_message_;
}

void Session::append(Message message) {
    if (message.role() == Role::system) {
        throw std::invalid_argument(
            "append does not accept system messages; use set_system");
    }

    std::lock_guard<std::mutex> lock(messages_mutex_);
    require_idle(active_.load(std::memory_order_acquire));
    messages_.push_back(std::move(message));
}

Session::Messages Session::messages() const {
    std::lock_guard<std::mutex> lock(messages_mutex_);
    return messages_;
}

void Session::clear_messages() {
    std::lock_guard<std::mutex> lock(messages_mutex_);
    require_idle(active_.load(std::memory_order_acquire));
    messages_.clear();
}

SessionState& Session::state() noexcept {
    return state_;
}

const SessionState& Session::state() const noexcept {
    return state_;
}

Session::RunLease::RunLease(Session& session) noexcept
    : session_(&session) {}

Session::RunLease::RunLease(RunLease&& other) noexcept
    : session_(other.session_) {
    other.session_ = nullptr;
}

Session::RunLease::~RunLease() {
    if (session_ != nullptr) {
        session_->release();
    }
}

Session::RunLease Session::acquire() {
    // Pairing the semantic lease with the history mutex closes the small race
    // between a public append/set_system operation and the start of generation.
    // The mutex is released immediately; it is never held during provider I/O.
    std::lock_guard<std::mutex> lock(messages_mutex_);
    bool expected = false;
    if (!active_.compare_exchange_strong(expected,
                                         true,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
        throw SessionInUseError("session is already in use");
    }
    return RunLease(*this);
}

void Session::release() noexcept {
    active_.store(false, std::memory_order_release);
}

void Session::append_from_client(Message message) {
    if (message.role() == Role::system) {
        throw std::invalid_argument(
            "client transcript must not contain system messages");
    }
    std::lock_guard<std::mutex> lock(messages_mutex_);
    messages_.push_back(std::move(message));
}
