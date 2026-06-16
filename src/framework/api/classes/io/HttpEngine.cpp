#include "HttpEngine.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

#include "components/proxy/ProxyBypass.h"
#include "utils/utils.h"

namespace d2bs::api::classes {

namespace {

constexpr DWORD HOST_BUFFER_CHARS = 256;
constexpr DWORD URL_BUFFER_CHARS = 8192;

// Format the current GetLastError() value via the system message table, suffixed
// with the numeric code. WinHTTP-range codes (12xxx) have no system message and
// surface as just the code, which is documented and greppable.
std::string FormatLastError() {
    DWORD code = GetLastError();
    LPWSTR buffer = nullptr;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - FormatMessage's ALLOCATE_BUFFER idiom
    DWORD chars =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string message = (chars != 0 && buffer != nullptr) ? d2bs::utils::ToStr(std::wstring(buffer, chars)) : "";
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
        message.pop_back();
    }
    return message.empty() ? "error " + std::to_string(code) : message + " (" + std::to_string(code) + ")";
}

// RAII wrapper around an HINTERNET; closes the handle on scope exit.
class WinHttpHandle {
   public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    WinHttpHandle(WinHttpHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~WinHttpHandle() { Reset(); }

    void Reset() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
            handle_ = nullptr;
        }
    }
    HINTERNET Get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

   private:
    HINTERNET handle_ = nullptr;
};

// Query a string-valued response header / status field into UTF-8.
std::string QueryStringHeader(HINTERNET request, DWORD infoLevel) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, infoLevel, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &bytes,
                        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
        return {};
    }
    std::wstring buffer((bytes / sizeof(wchar_t)) + 1, L'\0');
    if (WinHttpQueryHeaders(request, infoLevel, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &bytes,
                            WINHTTP_NO_HEADER_INDEX) == FALSE) {
        return {};
    }
    size_t nul = buffer.find(L'\0');
    if (nul != std::wstring::npos) {
        buffer.resize(nul);
    }
    return d2bs::utils::ToStr(buffer);
}

// Query a string-valued request option (e.g. WINHTTP_OPTION_URL) into UTF-8.
std::string QueryStringOption(HINTERNET request, DWORD option) {
    DWORD bytes = 0;
    WinHttpQueryOption(request, option, nullptr, &bytes);
    if (bytes == 0) {
        return {};
    }
    std::wstring buffer((bytes / sizeof(wchar_t)) + 1, L'\0');
    if (WinHttpQueryOption(request, option, buffer.data(), &bytes) == FALSE) {
        return {};
    }
    size_t nul = buffer.find(L'\0');
    if (nul != std::wstring::npos) {
        buffer.resize(nul);
    }
    return d2bs::utils::ToStr(buffer);
}

// Parse a WINHTTP_QUERY_RAW_HEADERS_CRLF block into a lowercased-name map. The
// leading status line ("HTTP/1.1 200 OK") has no colon, so it is skipped.
void ParseHeaders(const std::string& raw, std::map<std::string, std::string>& out) {
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find("\r\n", pos);
        size_t end = (eol == std::string::npos) ? raw.size() : eol;
        std::string line = raw.substr(pos, end - pos);
        pos = (eol == std::string::npos) ? raw.size() : eol + 2;

        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string name = d2bs::utils::ToLower(line.substr(0, colon));
        std::string value(d2bs::utils::Trim(std::string_view(line).substr(colon + 1)));

        auto existing = out.find(name);
        if (existing == out.end()) {
            out.emplace(std::move(name), std::move(value));
        } else {
            existing->second += ", " + value;
        }
    }
}

}  // namespace

std::string PerformHttpRequest(const HttpRequest& request, HttpResponse& out) {
    // Keep script HTTP off the game's SOCKS5 connect detour (see ProxyBypass.h).
    // Synchronous WinHTTP issues its connect() on this thread, so the thread-local
    // bypass applies.
    d2bs::proxy::BypassScope noProxy;

    std::wstring wideUrl = d2bs::utils::ToWStr(request.url);

    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    std::array<wchar_t, HOST_BUFFER_CHARS> hostBuffer{};
    std::array<wchar_t, URL_BUFFER_CHARS> pathBuffer{};
    std::array<wchar_t, URL_BUFFER_CHARS> extraBuffer{};
    components.lpszHostName = hostBuffer.data();
    components.dwHostNameLength = static_cast<DWORD>(hostBuffer.size());
    components.lpszUrlPath = pathBuffer.data();
    components.dwUrlPathLength = static_cast<DWORD>(pathBuffer.size());
    components.lpszExtraInfo = extraBuffer.data();
    components.dwExtraInfoLength = static_cast<DWORD>(extraBuffer.size());

    if (WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components) == FALSE) {
        return "invalid URL: " + request.url;
    }

    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    const bool isHttps = components.nScheme == INTERNET_SCHEME_HTTPS;
    const INTERNET_PORT port = components.nPort;

    WinHttpHandle session(
        WinHttpOpen(L"d2bsng", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        session = WinHttpHandle(WinHttpOpen(L"d2bsng", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                            WINHTTP_NO_PROXY_BYPASS, 0));
    }
    if (!session) {
        return "WinHttpOpen failed: " + FormatLastError();
    }

    // Overall wall-clock deadline, enforced at the I/O checkpoints below. A non-zero total also
    // tightens the per-operation timeout so a slow connect/send/receive phase cannot by itself
    // outlast the total budget. The synchronous WinHTTP calls cannot be interrupted mid-call, so
    // the effective granularity is one per-operation timeout / one body-read chunk.
    const auto start = std::chrono::steady_clock::now();
    const auto totalExceeded = [&request, start]() {
        if (request.totalTimeoutMs == 0) {
            return false;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        return elapsed.count() >= static_cast<int64_t>(request.totalTimeoutMs);
    };

    uint32_t perOpTimeout = request.timeoutMs;
    if (request.totalTimeoutMs != 0 && request.totalTimeoutMs < perOpTimeout) {
        perOpTimeout = request.totalTimeoutMs;
    }
    const int timeout = static_cast<int>(perOpTimeout);
    WinHttpSetTimeouts(session.Get(), timeout, timeout, timeout, timeout);

    // Advertise and transparently decode gzip/deflate where supported (Win8.1+);
    // harmlessly ignored on older systems.
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(session.Get(), WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));

    WinHttpHandle connection(WinHttpConnect(session.Get(), host.c_str(), port, 0));
    if (!connection) {
        return "WinHttpConnect failed: " + FormatLastError();
    }

    const DWORD requestFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    std::wstring method = d2bs::utils::ToWStr(request.method);
    const wchar_t* objectName = path.empty() ? nullptr : path.c_str();
    WinHttpHandle handle(WinHttpOpenRequest(connection.Get(), method.c_str(), objectName, nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags));
    if (!handle) {
        return "WinHttpOpenRequest failed: " + FormatLastError();
    }

    if (!request.followRedirects) {
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(handle.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    }

    // Opt-in only: disable TLS certificate / hostname validation. Dangerous (MITM-able);
    // intended for self-signed or development endpoints the caller explicitly trusts.
    if (isHttps && request.insecure) {
        DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                              SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(handle.Get(), WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
    }

    std::wstring headerBlock;
    for (const auto& [name, value] : request.headers) {
        headerBlock += d2bs::utils::ToWStr(name);
        headerBlock += L": ";
        headerBlock += d2bs::utils::ToWStr(value);
        headerBlock += L"\r\n";
    }
    if (!headerBlock.empty()) {
        WinHttpAddRequestHeaders(handle.Get(), headerBlock.c_str(), static_cast<DWORD>(headerBlock.size()),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    const DWORD bodyLength = static_cast<DWORD>(request.body.size());
    // WinHttpSendRequest takes a non-const body pointer although it does not modify it.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - WinHTTP API takes LPVOID
    LPVOID bodyPointer = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<uint8_t*>(request.body.data());
    if (WinHttpSendRequest(handle.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, bodyPointer, bodyLength, bodyLength, 0) ==
        FALSE) {
        return "WinHttpSendRequest failed: " + FormatLastError();
    }

    if (WinHttpReceiveResponse(handle.Get(), nullptr) == FALSE) {
        return "WinHttpReceiveResponse failed: " + FormatLastError();
    }
    if (totalExceeded()) {
        return "request exceeded total timeout of " + std::to_string(request.totalTimeoutMs) + " ms";
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(handle.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    out.status = static_cast<int32_t>(statusCode);
    out.statusText = QueryStringHeader(handle.Get(), WINHTTP_QUERY_STATUS_TEXT);
    out.url = QueryStringOption(handle.Get(), WINHTTP_OPTION_URL);
    if (out.url.empty()) {
        out.url = request.url;
    }
    ParseHeaders(QueryStringHeader(handle.Get(), WINHTTP_QUERY_RAW_HEADERS_CRLF), out.headers);

    for (;;) {
        if (totalExceeded()) {
            return "request exceeded total timeout of " + std::to_string(request.totalTimeoutMs) + " ms";
        }
        DWORD available = 0;
        if (WinHttpQueryDataAvailable(handle.Get(), &available) == FALSE) {
            return "WinHttpQueryDataAvailable failed: " + FormatLastError();
        }
        if (available == 0) {
            break;
        }
        const size_t offset = out.body.size();
        if (offset + available > request.maxResponseBytes) {
            return "response body exceeds maximum size of " + std::to_string(request.maxResponseBytes) + " bytes";
        }
        out.body.resize(offset + available);
        DWORD read = 0;
        if (WinHttpReadData(handle.Get(), out.body.data() + offset, available, &read) == FALSE) {
            return "WinHttpReadData failed: " + FormatLastError();
        }
        out.body.resize(offset + read);
        if (read == 0) {
            break;
        }
    }

    return {};
}

}  // namespace d2bs::api::classes
