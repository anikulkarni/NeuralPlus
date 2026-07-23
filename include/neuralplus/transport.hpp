// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

/// @file
/// Provider-independent HTTP boundary and built-in transport implementations.

#pragma once

#include "neuralplus/export.hpp"
#include "neuralplus/types.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neuralplus {

/// HTTP method used by provider adapters.
enum class HttpMethod {
    get,
    post,
    put,
    patch,
    delete_,
};

/// One HTTP header.
struct HttpHeader {
    /// Header field name.
    std::string name;

    /// Header field value.
    std::string value;

    /// Treats this value as a credential for exact response/error redaction.
    bool sensitive{false};
};

/// Provider-independent HTTP request.
struct HttpRequest {
    /// Request method.
    HttpMethod method{HttpMethod::post};

    /// Absolute request URL.
    std::string url;

    /// Request headers, including provider authentication.
    std::vector<HttpHeader> headers;

    /// Serialized request body.
    std::string body;

    /// Complete request timeout.
    std::chrono::milliseconds timeout{60000};
};

/// Provider-independent HTTP response.
struct NEURALPLUS_API HttpResponse {
    /// HTTP response status.
    int status{0};

    /// Response headers.
    std::vector<HttpHeader> headers;

    /// Raw response body.
    std::string body;

    /// Finds a header using case-insensitive ASCII comparison.
    [[nodiscard]] std::optional<std::string> header(std::string_view name) const;
};

/// Mockable HTTP boundary used by all built-in provider clients.
///
/// A transport may be shared by concurrent clients, so `send` implementations
/// must be thread-safe.
class NEURALPLUS_API HttpTransport {
public:
    virtual ~HttpTransport();

    /// Sends one complete request and returns its HTTP response.
    virtual HttpResponse send(const HttpRequest& request) = 0;

protected:
    HttpTransport() = default;

private:
    HttpTransport(const HttpTransport&) = delete;
    HttpTransport& operator=(const HttpTransport&) = delete;
};

/// Settings for the built-in libcurl transport.
struct HttpTransportOptions {
    /// TCP/TLS connection timeout.
    std::chrono::milliseconds connect_timeout{10000};

    /// Enables peer and host certificate verification.
    bool verify_tls{true};

    /// Optional libcurl proxy URL.
    std::string proxy;

    /// HTTP User-Agent sent by the built-in transport.
    std::string user_agent{"NeuralPlus"};

    /// Maximum response-body bytes accepted from a provider.
    std::size_t max_response_body_bytes{16U * 1024U * 1024U};

    /// Maximum aggregate response-header bytes accepted from a provider.
    std::size_t max_response_header_bytes{256U * 1024U};
};

/// Production HTTP transport backed by libcurl.
///
/// NeuralPlus initializes libcurl once for the process lifetime and
/// intentionally does not call `curl_global_cleanup`. On Unix with libcurl
/// older than 7.84, the library must be loaded during normal process startup,
/// before application threads begin. Do not repeatedly load and unload a
/// NeuralPlus shared library as a plugin. Applications that require
/// `curl_global_init_mem` or a dynamically unloadable HTTP module should
/// inject a custom HttpTransport instead.
///
/// @see https://curl.se/libcurl/c/libcurl-easy.html
/// @see https://curl.se/libcurl/c/curl_global_init.html
/// @see https://curl.se/libcurl/c/curl_global_cleanup.html
class NEURALPLUS_API CurlHttpTransport final : public HttpTransport {
public:
    /// Creates a libcurl transport with the supplied connection settings.
    explicit CurlHttpTransport(HttpTransportOptions options = {});
    ~CurlHttpTransport() override;

    /// Sends one request synchronously through libcurl.
    HttpResponse send(const HttpRequest& request) override;

private:
    CurlHttpTransport(const CurlHttpTransport&) = delete;
    CurlHttpTransport& operator=(const CurlHttpTransport&) = delete;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

/// Returns the built-in production HTTP transport.
[[nodiscard]] NEURALPLUS_API std::shared_ptr<HttpTransport>
make_default_http_transport(HttpTransportOptions options = {});

/// Deterministic callback transport that also records requests for tests.
class NEURALPLUS_API MockHttpTransport final : public HttpTransport {
public:
    /// Callback signature used to produce a deterministic HTTP response.
    ///
    /// The callback may be invoked concurrently and must be thread-safe.
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    /// Creates a recording transport backed by `handler`.
    explicit MockHttpTransport(Handler handler);
    ~MockHttpTransport() override;

    /// Records the request and delegates response creation to the handler.
    HttpResponse send(const HttpRequest& request) override;

    /// Returns a thread-safe snapshot of recorded requests.
    [[nodiscard]] std::vector<HttpRequest> requests() const;

    /// Removes all recorded requests.
    void clear_requests();

private:
    MockHttpTransport(const MockHttpTransport&) = delete;
    MockHttpTransport& operator=(const MockHttpTransport&) = delete;

    Handler handler_;
    mutable std::mutex mutex_;
    std::vector<HttpRequest> requests_;
};

}  // namespace neuralplus
