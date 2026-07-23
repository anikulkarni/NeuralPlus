// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/neuralplus.hpp"

#include <memory>

using namespace neuralplus;

int main() {
    auto transport = std::make_shared<MockHttpTransport>(
        [](const HttpRequest&) { return HttpResponse{}; });
    ClientOptions options;
    options.transport = std::move(transport);

    OpenAIConfig config{"package-consumer-model"};
    config.api_key = "non-secret-test-key";
    auto client = make_client(std::move(config), options);

    Session session;
    return session.messages().empty() &&
                   client->model().provider == Provider::openai
               ? 0
               : 1;
}
