#pragma once

#include "neuralplus/core/types.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace neuralplus {

class Model {
public:
    virtual ~Model() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    virtual ModelResponse generate(const ModelRequest& request) = 0;
};

/// Base class for composable model decorators such as retries, caching, routing, and tracing.
class ModelDecorator : public Model {
public:
    explicit ModelDecorator(std::shared_ptr<Model> next) : next_(std::move(next)) {
        if (!next_) {
            throw std::invalid_argument("next model must not be null");
        }
    }

protected:
    [[nodiscard]] Model& next() const noexcept {
        return *next_;
    }

    [[nodiscard]] const std::shared_ptr<Model>& next_ptr() const noexcept {
        return next_;
    }

private:
    std::shared_ptr<Model> next_;
};

}  // namespace neuralplus
