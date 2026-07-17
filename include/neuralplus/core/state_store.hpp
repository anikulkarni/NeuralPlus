#pragma once

#include <any>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace neuralplus {

/// Thread-safe, session-scoped state shared by tool calls.
class StateStore {
public:
    StateStore() = default;

    template <typename T>
    void set(std::string key, T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        values_[std::move(key)] = std::move(value);
    }

    template <typename T>
    [[nodiscard]] std::optional<T> get_copy(std::string_view key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = values_.find(std::string(key));
        if (it == values_.end()) {
            return std::nullopt;
        }
        const auto* value = std::any_cast<T>(&it->second);
        if (value == nullptr) {
            throw std::bad_any_cast{};
        }
        return *value;
    }

    template <typename T, typename UpdateFn>
    T update(std::string key, T initial_value, UpdateFn&& update_fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto [it, inserted] = values_.try_emplace(std::move(key), std::move(initial_value));
        (void)inserted;
        auto* current = std::any_cast<T>(&it->second);
        if (current == nullptr) {
            throw std::bad_any_cast{};
        }
        *current = std::invoke(std::forward<UpdateFn>(update_fn), *current);
        return *current;
    }

    [[nodiscard]] bool contains(std::string_view key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return values_.find(std::string(key)) != values_.end();
    }

    void erase(std::string_view key) {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.erase(std::string(key));
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::any> values_;
};

}  // namespace neuralplus
