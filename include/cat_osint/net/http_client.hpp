#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <string>

namespace cat_osint {

struct HttpRequest {
    std::string method{"GET"};
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    std::chrono::milliseconds timeout{15000};
    std::size_t max_response_bytes{8 * 1024 * 1024};
    bool allow_http_loopback{};
    bool loopback_only{};
};

struct HttpResponse {
    unsigned int status{};
    std::map<std::string, std::string> headers;
    std::string body;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse perform(const HttpRequest& request) = 0;
};

class WinHttpClient final : public HttpClient {
public:
    HttpResponse perform(const HttpRequest& request) override;
};

} // namespace cat_osint
