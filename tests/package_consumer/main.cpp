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

    OpenAIConfig config = models::openai::gpt_5_6_terra();
    config.api_key = "non-secret-test-key";
    auto client = make_client(std::move(config), options);

    Session session;
    return session.messages().empty() &&
                   client->model().provider == Provider::openai &&
                   client->model().id == "gpt-5.6-terra"
               ? 0
               : 1;
}
