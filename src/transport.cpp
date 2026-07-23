// Copyright 2026 Aniket Kulkarni
// SPDX-License-Identifier: Apache-2.0

#include "neuralplus/transport.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace neuralplus;

#if defined(_WIN32) && LIBCURL_VERSION_NUM < 0x075400
#error "NeuralPlus requires libcurl 7.84.0 or newer on Windows"
#endif

namespace {

unsigned char ascii_lower(unsigned char character) noexcept {
    if (character >= static_cast<unsigned char>('A') &&
        character <= static_cast<unsigned char>('Z')) {
        return static_cast<unsigned char>(
            character - static_cast<unsigned char>('A') +
            static_cast<unsigned char>('a'));
    }
    return character;
}

class CurlGlobal final {
public:
    CurlGlobal() noexcept
        : result(curl_global_init(CURL_GLOBAL_DEFAULT)) {}

    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;

    CURLcode result;
};

#if !defined(_WIN32)
// A namespace-scope object initializes libcurl before main(), including on
// distributions whose pre-7.84 libcurl global API is not thread-safe. This is
// the pattern recommended for a C++ module:
// https://curl.se/libcurl/c/libcurl.html#global-constants
CurlGlobal eager_curl_global;
#endif

CurlGlobal& curl_global() {
#if !defined(_WIN32)
    return eager_curl_global;
#else
    // Windows forbids doing this work under the DLL loader lock. Supported
    // Windows builds therefore require libcurl 7.84 or newer, whose global
    // initialization is thread-safe.
    static CurlGlobal global;
    return global;
#endif
}

struct CurlHandle final {
    CurlHandle() = default;

    CURL* value{curl_easy_init()};

    ~CurlHandle() {
        if (value != nullptr) {
            curl_easy_cleanup(value);
        }
    }

    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
};

struct CurlHeaders final {
    CurlHeaders() = default;

    curl_slist* value{nullptr};

    ~CurlHeaders() {
        if (value != nullptr) {
            curl_slist_free_all(value);
        }
    }

    CurlHeaders(const CurlHeaders&) = delete;
    CurlHeaders& operator=(const CurlHeaders&) = delete;
};

bool ascii_iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto left_character = static_cast<unsigned char>(left[index]);
        const auto right_character = static_cast<unsigned char>(right[index]);
        if (ascii_lower(left_character) != ascii_lower(right_character)) {
            return false;
        }
    }
    return true;
}

std::string trim_ascii(std::string value) {
    const auto is_space = [](unsigned char character) {
        return character == ' ' || character == '\t' ||
               character == '\r' || character == '\n' ||
               character == '\f' || character == '\v';
    };
    const auto begin = std::find_if_not(value.begin(), value.end(), is_space);
    const auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

bool valid_header_name(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    return std::all_of(
        name.begin(), name.end(), [](unsigned char character) {
            const bool ascii_alphanumeric =
                (character >= 'A' && character <= 'Z') ||
                (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9');
            return ascii_alphanumeric ||
                   character == '!' || character == '#' ||
                   character == '$' || character == '%' ||
                   character == '&' || character == '\'' ||
                   character == '*' || character == '+' ||
                   character == '-' || character == '.' ||
                   character == '^' || character == '_' ||
                   character == '`' || character == '|' ||
                   character == '~';
        });
}

bool valid_header_value(std::string_view value) {
    return std::all_of(
        value.begin(), value.end(), [](unsigned char character) {
            return character == '\t' ||
                   (character >= 0x20U && character != 0x7FU);
        });
}

long milliseconds_as_long(std::chrono::milliseconds value,
                          const char* option_name) {
    if (value.count() <= 0) {
        throw ConfigurationError(
            std::string(option_name) + " must be greater than zero");
    }
    if (value.count() > (std::numeric_limits<long>::max)()) {
        throw ConfigurationError(std::string(option_name) + " is too large");
    }
    return static_cast<long>(value.count());
}

void set_option(CURL* handle, CURLoption option, long value) {
    if (curl_easy_setopt(handle, option, value) != CURLE_OK) {
        throw TransportError("libcurl request configuration failed");
    }
}

void set_option(CURL* handle, CURLoption option, const char* value) {
    if (curl_easy_setopt(handle, option, value) != CURLE_OK) {
        throw TransportError("libcurl request configuration failed");
    }
}

void set_option(CURL* handle, CURLoption option, void* value) {
    if (curl_easy_setopt(handle, option, value) != CURLE_OK) {
        throw TransportError("libcurl request configuration failed");
    }
}

struct BodyWriteContext {
    std::string* body;
    std::size_t limit;
    bool limit_exceeded{false};
    bool write_failed{false};
};

struct HeaderWriteContext {
    std::vector<HttpHeader>* headers;
    std::size_t limit;
    std::size_t received{0};
    bool limit_exceeded{false};
    bool write_failed{false};
};

std::optional<std::size_t> callback_bytes(std::size_t item_size,
                                          std::size_t item_count) {
    if (item_size != 0 &&
        item_count >
            (std::numeric_limits<std::size_t>::max)() / item_size) {
        return std::nullopt;
    }
    return item_size * item_count;
}

std::size_t write_body(char* data,
                       std::size_t item_size,
                       std::size_t item_count,
                       void* destination) noexcept {
    auto* context = static_cast<BodyWriteContext*>(destination);
    const auto bytes = callback_bytes(item_size, item_count);
    if (!bytes.has_value() ||
        *bytes > context->limit -
                     (std::min)(context->body->size(), context->limit)) {
        context->limit_exceeded = true;
        return 0;
    }
    try {
        context->body->append(data, *bytes);
    } catch (...) {
        context->write_failed = true;
        return 0;
    }
    return *bytes;
}

std::size_t write_header(char* data,
                         std::size_t item_size,
                         std::size_t item_count,
                         void* destination) noexcept {
    auto* context = static_cast<HeaderWriteContext*>(destination);
    const auto bytes = callback_bytes(item_size, item_count);
    if (!bytes.has_value() ||
        *bytes > context->limit -
                     (std::min)(context->received, context->limit)) {
        context->limit_exceeded = true;
        return 0;
    }
    context->received += *bytes;

    try {
        // Example: "X-Request-Id: r1\r\n" becomes
        // HttpHeader{"X-Request-Id", "r1"}; "HTTP/1.1 200 OK\r\n" has no
        // colon and is intentionally skipped.
        // https://www.rfc-editor.org/rfc/rfc9110.html#section-5.2
        std::string line(data, *bytes);
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            return *bytes;
        }

        context->headers->push_back(
            HttpHeader{trim_ascii(line.substr(0, separator)),
                       trim_ascii(line.substr(separator + 1))});
    } catch (...) {
        context->write_failed = true;
        return 0;
    }
    return *bytes;
}

const char* method_name(HttpMethod method) {
    switch (method) {
        case HttpMethod::get:
            return "GET";
        case HttpMethod::post:
            return "POST";
        case HttpMethod::put:
            return "PUT";
        case HttpMethod::patch:
            return "PATCH";
        case HttpMethod::delete_:
            return "DELETE";
    }
    return "POST";
}

}  // namespace

std::optional<std::string> HttpResponse::header(std::string_view name) const {
    for (const auto& item : headers) {
        if (ascii_iequals(item.name, name)) {
            return item.value;
        }
    }
    return std::nullopt;
}

HttpTransport::~HttpTransport() = default;

class CurlHttpTransport::Impl final {
public:
    explicit Impl(HttpTransportOptions value) : options(std::move(value)) {
        if (options.connect_timeout.count() <= 0) {
            throw ConfigurationError(
                "connect timeout must be greater than zero");
        }
        if (options.max_response_body_bytes == 0 ||
            options.max_response_header_bytes == 0) {
            throw ConfigurationError(
                "HTTP response size limits must be greater than zero");
        }
        if (curl_global().result != CURLE_OK) {
            throw TransportError("libcurl global initialization failed");
        }
    }

    HttpTransportOptions options;
};

CurlHttpTransport::CurlHttpTransport(HttpTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

CurlHttpTransport::~CurlHttpTransport() = default;

HttpResponse CurlHttpTransport::send(const HttpRequest& request) {
    if (request.url.empty()) {
        throw ConfigurationError("HTTP request URL must not be empty");
    }

    CurlHandle handle;
    if (handle.value == nullptr) {
        throw TransportError("libcurl could not allocate a request handle");
    }

    HttpResponse response;
    BodyWriteContext body_context{
        &response.body, impl_->options.max_response_body_bytes};
    HeaderWriteContext header_context{
        &response.headers, impl_->options.max_response_header_bytes};
    set_option(handle.value, CURLOPT_URL, request.url.c_str());
    set_option(handle.value, CURLOPT_NOSIGNAL, 1L);
    set_option(handle.value, CURLOPT_FOLLOWLOCATION, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
    set_option(handle.value, CURLOPT_PROTOCOLS_STR, "http,https");
    set_option(handle.value, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    set_option(handle.value, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    set_option(handle.value, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    set_option(handle.value, CURLOPT_CONNECTTIMEOUT_MS,
               milliseconds_as_long(impl_->options.connect_timeout, "connect timeout"));
    set_option(handle.value, CURLOPT_TIMEOUT_MS,
               milliseconds_as_long(request.timeout, "request timeout"));
    set_option(handle.value, CURLOPT_SSL_VERIFYPEER,
               impl_->options.verify_tls ? 1L : 0L);
    set_option(handle.value, CURLOPT_SSL_VERIFYHOST,
               impl_->options.verify_tls ? 2L : 0L);
    if (curl_easy_setopt(handle.value, CURLOPT_WRITEFUNCTION, &write_body) != CURLE_OK) {
        throw TransportError("libcurl request configuration failed");
    }
    set_option(handle.value, CURLOPT_WRITEDATA, &body_context);
    if (curl_easy_setopt(handle.value, CURLOPT_HEADERFUNCTION, &write_header) != CURLE_OK) {
        throw TransportError("libcurl request configuration failed");
    }
    set_option(handle.value, CURLOPT_HEADERDATA, &header_context);

    if (!impl_->options.user_agent.empty()) {
        set_option(handle.value, CURLOPT_USERAGENT,
                   impl_->options.user_agent.c_str());
    }
    if (!impl_->options.proxy.empty()) {
        set_option(handle.value, CURLOPT_PROXY, impl_->options.proxy.c_str());
    }

    const char* method = method_name(request.method);
    if (request.method == HttpMethod::get) {
        set_option(handle.value, CURLOPT_HTTPGET, 1L);
    } else if (request.method == HttpMethod::post) {
        set_option(handle.value, CURLOPT_POST, 1L);
    } else {
        set_option(handle.value, CURLOPT_CUSTOMREQUEST, method);
    }

    if (request.method == HttpMethod::get && !request.body.empty()) {
        throw ConfigurationError(
            "HTTP GET requests must not contain a body");
    }
    if (request.method != HttpMethod::get) {
        if (request.body.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<curl_off_t>::max)())) {
            throw ConfigurationError("HTTP request body is too large");
        }
        set_option(handle.value, CURLOPT_POSTFIELDS,
                   request.body.data());
        if (curl_easy_setopt(handle.value, CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(request.body.size())) != CURLE_OK) {
            throw TransportError("libcurl request configuration failed");
        }
    }

    CurlHeaders curl_headers;
    for (const auto& header : request.headers) {
        if (!valid_header_name(header.name) ||
            !valid_header_value(header.value)) {
            throw ConfigurationError("HTTP header contains invalid characters");
        }
        const std::string serialized = header.name + ": " + header.value;
        curl_slist* next = curl_slist_append(curl_headers.value, serialized.c_str());
        if (next == nullptr) {
            throw TransportError("libcurl could not allocate request headers");
        }
        curl_headers.value = next;
    }
    if (curl_headers.value != nullptr &&
        curl_easy_setopt(handle.value, CURLOPT_HTTPHEADER, curl_headers.value) != CURLE_OK) {
        throw TransportError("libcurl request configuration failed");
    }

    const CURLcode result = curl_easy_perform(handle.value);
    if (result != CURLE_OK) {
        if (body_context.limit_exceeded ||
            header_context.limit_exceeded) {
            throw TransportError(
                "HTTP response exceeded the configured size limit");
        }
        if (body_context.write_failed ||
            header_context.write_failed) {
            throw TransportError(
                "HTTP response could not be stored");
        }
        // Do not include the URL, headers, body, or provider diagnostic buffer:
        // any of those can contain credentials or user content.
        throw TransportError(std::string("HTTP request failed: ") +
                             curl_easy_strerror(result));
    }

    long response_status = 0;
    if (curl_easy_getinfo(handle.value, CURLINFO_RESPONSE_CODE,
                          &response_status) != CURLE_OK) {
        throw TransportError("libcurl could not read the HTTP response status");
    }
    response.status = static_cast<int>(response_status);
    return response;
}

std::shared_ptr<HttpTransport>
neuralplus::make_default_http_transport(HttpTransportOptions options) {
    return std::make_shared<CurlHttpTransport>(std::move(options));
}

MockHttpTransport::MockHttpTransport(Handler handler)
    : handler_(std::move(handler)) {
    if (!handler_) {
        throw ConfigurationError("mock HTTP transport handler must not be empty");
    }
}

MockHttpTransport::~MockHttpTransport() = default;

HttpResponse MockHttpTransport::send(const HttpRequest& request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(request);
    }
    return handler_(request);
}

std::vector<HttpRequest> MockHttpTransport::requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
}

void MockHttpTransport::clear_requests() {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.clear();
}
