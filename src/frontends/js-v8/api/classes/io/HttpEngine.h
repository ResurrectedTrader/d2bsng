#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace d2bs::api::classes {

inline constexpr uint32_t DEFAULT_HTTP_TIMEOUT_MS = 30000;         // per network operation (resolve/connect/send/recv)
inline constexpr uint32_t DEFAULT_HTTP_TOTAL_TIMEOUT_MS = 120000;  // overall wall-clock cap on the request; 0 disables
inline constexpr size_t DEFAULT_HTTP_MAX_RESPONSE_BYTES =
    64ULL * 1024 * 1024;  // 64 MiB - guards a 32-bit address space

// A fully-buffered HTTP response produced by the engine.
struct HttpResponse {
    int32_t status = 0;
    std::string statusText;
    std::string url;                             // final URL after any redirects
    std::map<std::string, std::string> headers;  // names lowercased; duplicates joined with ", "
    std::vector<uint8_t> body;
};

// A request to perform, free of any V8 / JS dependency.
struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<uint8_t> body;
    uint32_t timeoutMs = DEFAULT_HTTP_TIMEOUT_MS;
    uint32_t totalTimeoutMs = DEFAULT_HTTP_TOTAL_TIMEOUT_MS;
    bool followRedirects = true;
    bool insecure = false;  // when true, skip TLS certificate/hostname validation (caller's explicit risk)
    size_t maxResponseBytes = DEFAULT_HTTP_MAX_RESPONSE_BYTES;
};

// Perform `request` synchronously, blocking the calling thread. On success fills
// `out` and returns an empty string; on failure returns a human-readable error
// message (and leaves `out` unspecified). A non-2xx status is a normal success,
// not an error.
//
// Backed by WinHTTP: HTTPS validates against the Windows certificate store
// (unless request.insecure), WPAD proxy auto-detection is used, gzip/deflate is
// transparently decoded, and the outbound connection bypasses the game's SOCKS5
// proxy detour.
std::string PerformHttpRequest(const HttpRequest& request, HttpResponse& out);

}  // namespace d2bs::api::classes
