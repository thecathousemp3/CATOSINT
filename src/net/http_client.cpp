#include "cat_osint/net/http_client.hpp"

#include <algorithm>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace cat_osint {

#ifdef _WIN32
namespace {

class InternetHandle {
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~InternetHandle() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    HINTERNET get() const { return handle_; }

private:
    HINTERNET handle_;
};

std::wstring to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        throw std::runtime_error("HTTP request contains invalid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::string to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

std::string lower_ascii(std::string value) {
    for (auto& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

} // namespace
#endif

HttpResponse WinHttpClient::perform(const HttpRequest& request) {
#ifdef _WIN32
    if (request.url.empty() || request.timeout.count() <= 0 ||
        request.max_response_bytes == 0 || request.method.empty() ||
        !std::all_of(request.method.begin(), request.method.end(), [](unsigned char ch) {
            return ch >= 'A' && ch <= 'Z';
        })) {
        throw std::invalid_argument("HTTP method, URL, timeout, and response limit must be valid");
    }

    const auto url = to_wide(request.url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
        throw std::runtime_error("Invalid HTTP URL");
    }

    const std::wstring scheme(parts.lpszScheme, parts.dwSchemeLength);
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    const std::wstring extra = parts.dwExtraInfoLength == 0
        ? std::wstring{}
        : std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const bool https = scheme == L"https";
    const bool http = scheme == L"http";
    const auto host_lower = lower_ascii(to_utf8(host));
    const bool loopback = host_lower == "localhost" ||
                          host_lower == "127.0.0.1" ||
                          host_lower == "::1" ||
                          host_lower == "[::1]";
    if (request.loopback_only && !loopback) {
        throw std::invalid_argument("Local model endpoint must use a loopback host");
    }
    if (!https && !(http && request.allow_http_loopback && loopback)) {
        throw std::invalid_argument("Only HTTPS is allowed, except HTTP to a local model endpoint");
    }
    if (parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0) {
        throw std::invalid_argument("Credentials must not be embedded in the URL");
    }

    const DWORD proxy_mode = request.loopback_only
        ? WINHTTP_ACCESS_TYPE_NO_PROXY
        : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
    InternetHandle session(WinHttpOpen(
        L"CAT-OSINT/0.1",
        proxy_mode,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session.get()) {
        throw std::runtime_error("Could not initialize Windows HTTP client");
    }
    const auto timeout = static_cast<int>(request.timeout.count());
    if (!WinHttpSetTimeouts(session.get(), timeout, timeout, timeout, timeout)) {
        throw std::runtime_error("Could not configure HTTP timeouts");
    }

    InternetHandle connection(WinHttpConnect(
        session.get(), host.c_str(), parts.nPort, 0));
    if (!connection.get()) {
        throw std::runtime_error("Could not connect to HTTP host");
    }
    const auto resource = path + extra;
    const auto method = to_wide(request.method);
    InternetHandle http_request(WinHttpOpenRequest(
        connection.get(), method.c_str(), resource.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        https ? WINHTTP_FLAG_SECURE : 0));
    if (!http_request.get()) {
        throw std::runtime_error("Could not create HTTP request");
    }

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(
        http_request.get(), WINHTTP_OPTION_REDIRECT_POLICY,
        &redirect_policy, sizeof(redirect_policy))) {
        throw std::runtime_error("Could not disable automatic HTTP redirects");
    }

    std::wstring header_block;
    for (const auto& [key, value] : request.headers) {
        if (key.find_first_of("\r\n") != std::string::npos ||
            value.find_first_of("\r\n") != std::string::npos) {
            throw std::invalid_argument("HTTP headers must not contain line breaks");
        }
        header_block += to_wide(key + ": " + value + "\r\n");
    }
    const auto* headers = header_block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_block.c_str();
    const auto header_length = header_block.empty()
        ? 0
        : static_cast<DWORD>(header_block.size());
    if (request.body.size() > static_cast<std::size_t>(MAXDWORD)) {
        throw std::invalid_argument("HTTP request body is too large");
    }
    const auto body_size = static_cast<DWORD>(request.body.size());

    if (!WinHttpSendRequest(
            http_request.get(), headers, header_length,
            request.body.empty() ? WINHTTP_NO_REQUEST_DATA
                                 : const_cast<char*>(request.body.data()),
            body_size, body_size, 0) ||
        !WinHttpReceiveResponse(http_request.get(), nullptr)) {
        throw std::runtime_error("HTTP request failed or timed out");
    }

    HttpResponse response;
    DWORD status{};
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(
            http_request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
            WINHTTP_NO_HEADER_INDEX)) {
        throw std::runtime_error("Could not read HTTP response status");
    }
    response.status = status;

    DWORD available{};
    while (true) {
        if (!WinHttpQueryDataAvailable(http_request.get(), &available)) {
            throw std::runtime_error("Could not check HTTP response size");
        }
        if (available == 0) {
            break;
        }
        if (response.body.size() + available > request.max_response_bytes) {
            throw std::runtime_error("HTTP response exceeded configured size limit");
        }
        const auto offset = response.body.size();
        response.body.resize(offset + available);
        DWORD read{};
        if (!WinHttpReadData(
                http_request.get(), response.body.data() + offset, available, &read)) {
            throw std::runtime_error("Could not read HTTP response body");
        }
        response.body.resize(offset + read);
    }

    DWORD header_size{};
    WinHttpQueryHeaders(
        http_request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &header_size,
        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && header_size > sizeof(wchar_t)) {
        std::wstring raw_headers(header_size / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(
                http_request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                WINHTTP_HEADER_NAME_BY_INDEX, raw_headers.data(), &header_size,
                WINHTTP_NO_HEADER_INDEX)) {
            std::size_t position{};
            while ((position = raw_headers.find(L"\r\n", position)) != std::wstring::npos) {
                const auto end = raw_headers.find(L"\r\n", position + 2);
                if (end == std::wstring::npos) {
                    break;
                }
                const auto line = raw_headers.substr(position + 2, end - position - 2);
                const auto colon = line.find(L':');
                if (colon != std::wstring::npos) {
                    auto key = lower_ascii(to_utf8(line.substr(0, colon)));
                    auto value = to_utf8(line.substr(colon + 1));
                    const auto first = value.find_first_not_of(" \t");
                    if (first != std::string::npos) {
                        value.erase(0, first);
                    }
                    response.headers[key] = std::move(value);
                }
                position = end;
            }
        }
    }
    return response;
#else
    (void)request;
    throw std::runtime_error("The initial HTTP transport is currently implemented for Windows");
#endif
}

} // namespace cat_osint
