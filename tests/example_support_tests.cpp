// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "../examples/example_support.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace neuralplus_examples;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_rejected(const std::string& url) {
    bool rejected = false;
    try {
        require_secure_base_url(url);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "unsafe base URL was accepted: " + url);
}

void test_secure_base_urls() {
    require_secure_base_url("https://api.example.com/v1");
    require_secure_base_url("HTTPS://API.EXAMPLE.COM/v1");
    require_secure_base_url("http://localhost:8000/v1");
    require_secure_base_url("http://LOCALHOST/v1");
    require_secure_base_url("http://127.0.0.1:8000/v1");
    require_secure_base_url("http://[::1]:8000/v1");

    require_rejected("http://api.example.com/v1");
    require_rejected("http://localhost.example.com/v1");
    require_rejected("http://localhost@example.com/v1");
    require_rejected("http://[::1]example.com/v1");
    require_rejected("ftp://localhost/resource");
    require_rejected("not-a-url");
}

}  // namespace

int main() {
    try {
        test_secure_base_urls();
        std::cout << "example support tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "example support test failure: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
