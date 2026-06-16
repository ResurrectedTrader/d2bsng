#include "hooks/Socks5Proxy.h"

// winsock2 must precede Windows.h so the legacy winsock.h (pulled in by
// Windows.h) does not win and shadow the v2 declarations.
#include <winsock2.h>
#include <ws2tcpip.h>
//
#include <Windows.h>
#include <detours/detours.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "components/proxy/ProxyBypass.h"
#include "game/LaunchOptions.h"
#include "utils/utils.h"

#pragma comment(lib, "Ws2_32.lib")

namespace d2bs::hooks::socks5 {

namespace {

using ConnectFn = int(WSAAPI*)(SOCKET, const sockaddr*, int);

// Bound every blocking step of the handshake. The game's own BNCS socket carries a
// 3s SO_RCVTIMEO, so this is only the outer ceiling for the proxy itself.
constexpr int HANDSHAKE_TIMEOUT_MS = 10000;

// SOCKS5 (RFC 1928) + username/password auth (RFC 1929) wire constants.
constexpr uint8_t SOCKS5_VERSION = 0x05;
constexpr uint8_t SOCKS5_CMD_CONNECT = 0x01;
constexpr uint8_t SOCKS5_RSV = 0x00;
constexpr uint8_t SOCKS5_ATYP_IPV4 = 0x01;
constexpr uint8_t SOCKS5_ATYP_DOMAIN = 0x03;
constexpr uint8_t SOCKS5_ATYP_IPV6 = 0x04;
constexpr uint8_t SOCKS5_AUTH_NONE = 0x00;
constexpr uint8_t SOCKS5_AUTH_USERPASS = 0x02;
constexpr uint8_t SOCKS5_AUTH_VERSION = 0x01;
constexpr uint8_t SOCKS5_AUTH_OK = 0x00;
constexpr uint8_t SOCKS5_REP_SUCCESS = 0x00;
constexpr size_t SOCKS5_MAX_FIELD = 255;  // RFC 1929 ULEN/PLEN ceiling; also caps the BND.ADDR drain

struct ProxyConfig {
    std::string host;
    std::string port;
    std::string username;  // empty == no authentication
    std::string password;
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - hook owns module-level state by definition
ConnectFn realConnect = nullptr;  // set in Install(); Detours rewrites it to the trampoline (original connect)
bool installed = false;
std::shared_ptr<spdlog::logger> logger;  // dedicated "socks5" logger; created in Install() once the sinks are wired

std::optional<ProxyConfig> config;         // parsed once in Install(), before the hook goes live
std::mutex resolveMutex;                   // guards resolvedProxy
std::optional<sockaddr_in> resolvedProxy;  // proxy endpoint, resolved lazily and cached
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Parse socks5://[user[:password]@]host:port. Returns nullopt on anything that
// is not a well-formed socks5 URL with both a host and a port.
std::optional<ProxyConfig> ParseProxyUrl(std::string_view url) {
    constexpr std::string_view SCHEME = "socks5://";
    if (!url.starts_with(SCHEME)) {
        return std::nullopt;
    }
    url.remove_prefix(SCHEME.size());

    ProxyConfig cfg;

    // Split optional credentials at the last '@' so the host part never contains one.
    if (const auto at = url.rfind('@'); at != std::string_view::npos) {
        const std::string_view creds = url.substr(0, at);
        url.remove_prefix(at + 1);
        if (const auto colon = creds.find(':'); colon != std::string_view::npos) {
            cfg.username = std::string{creds.substr(0, colon)};
            cfg.password = std::string{creds.substr(colon + 1)};
        } else {
            cfg.username = std::string{creds};
        }
    }

    // host:port - split at the last ':' (IPv4 / hostname hosts carry no ':').
    const auto colon = url.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= url.size()) {
        return std::nullopt;
    }
    cfg.host = std::string{url.substr(0, colon)};
    cfg.port = std::string{url.substr(colon + 1)};
    return cfg;
}

// Resolve the proxy endpoint once (native DNS) and cache it. Retries on each call
// until it succeeds, so a transient resolver failure at startup is not permanent.
std::optional<sockaddr_in> ResolveProxy() {
    const std::scoped_lock lock(resolveMutex);
    if (resolvedProxy) {
        return resolvedProxy;
    }
    if (!config) {
        return std::nullopt;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    if (getaddrinfo(config->host.c_str(), config->port.c_str(), &hints, &result) != 0 || result == nullptr) {
        return std::nullopt;
    }
    sockaddr_in addr{};
    std::memcpy(&addr, result->ai_addr, sizeof(addr));
    freeaddrinfo(result);

    resolvedProxy = addr;
    return resolvedProxy;
}

// Wait until `s` is readable (or writable) or the timeout elapses. Drives the
// handshake uniformly whether the game left the socket blocking (D2GS / BnFTP) or
// non-blocking (BNCS, switched via FIONBIO before connect).
bool WaitReady(SOCKET s, bool forWrite, int timeoutMs) {
    fd_set ready;
    fd_set except;
    FD_ZERO(&ready);
    FD_ZERO(&except);
    FD_SET(s, &ready);
    FD_SET(s, &except);

    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    const int rc = select(0, forWrite ? nullptr : &ready, forWrite ? &ready : nullptr, &except, &tv);
    if (rc <= 0) {
        return false;  // timeout (0) or error
    }
    if (FD_ISSET(s, &except)) {
        return false;
    }
    return FD_ISSET(s, &ready) != 0;
}

bool SendAll(SOCKET s, std::span<const uint8_t> buf) {
    size_t sent = 0;
    while (sent < buf.size()) {
        if (!WaitReady(s, /*forWrite=*/true, HANDSHAKE_TIMEOUT_MS)) {
            return false;
        }
        const int n = send(s, reinterpret_cast<const char*>(buf.data() + sent), static_cast<int>(buf.size() - sent), 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
        } else if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool RecvExact(SOCKET s, std::span<uint8_t> buf) {
    size_t got = 0;
    while (got < buf.size()) {
        if (!WaitReady(s, /*forWrite=*/false, HANDSHAKE_TIMEOUT_MS)) {
            return false;
        }
        const int n = recv(s, reinterpret_cast<char*>(buf.data() + got), static_cast<int>(buf.size() - got), 0);
        if (n > 0) {
            got += static_cast<size_t>(n);
        } else if (n == 0) {
            return false;  // peer closed
        } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

// Connect `s` to the proxy, tolerating a non-blocking socket (WSAEWOULDBLOCK ->
// wait for writability -> check SO_ERROR). Uses the Detours trampoline so dialing
// the proxy does not re-enter our own hook.
bool ProxyConnect(SOCKET s, const sockaddr_in& proxy) {
    const int rc = realConnect(s, reinterpret_cast<const sockaddr*>(&proxy), static_cast<int>(sizeof(proxy)));
    if (rc == 0) {
        return true;
    }
    const int err = WSAGetLastError();
    if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEALREADY) {
        return false;
    }
    if (!WaitReady(s, /*forWrite=*/true, HANDSHAKE_TIMEOUT_MS)) {
        return false;
    }
    int soErr = 0;
    int len = static_cast<int>(sizeof(soErr));
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len) != 0) {
        return false;
    }
    return soErr == 0;
}

// Perform the SOCKS5 greeting / optional auth / CONNECT on the already-proxy-
// connected socket `s`, asking the proxy to reach `dest` (an already-resolved
// IPv4 endpoint - the game does its own DNS).
bool Handshake(SOCKET s, const ProxyConfig& cfg, const sockaddr_in& dest) {
    const bool useAuth = !cfg.username.empty();

    // Greeting: VER, NMETHODS, METHODS... (offer no-auth, plus user/pass when we have credentials).
    std::vector<uint8_t> greeting{SOCKS5_VERSION, 1, SOCKS5_AUTH_NONE};
    if (useAuth) {
        greeting[1] = 2;  // NMETHODS
        greeting.push_back(SOCKS5_AUTH_USERPASS);
    }
    if (!SendAll(s, greeting)) {
        return false;
    }

    // Method selection: VER, METHOD.
    std::array<uint8_t, 2> selection{};
    if (!RecvExact(s, selection)) {
        return false;
    }
    if (selection[0] != SOCKS5_VERSION) {
        return false;
    }
    const uint8_t method = selection[1];

    if (method == SOCKS5_AUTH_USERPASS) {
        if (cfg.username.size() > SOCKS5_MAX_FIELD || cfg.password.size() > SOCKS5_MAX_FIELD) {
            return false;
        }
        std::vector<uint8_t> auth;
        auth.push_back(SOCKS5_AUTH_VERSION);
        auth.push_back(static_cast<uint8_t>(cfg.username.size()));
        auth.insert(auth.end(), cfg.username.begin(), cfg.username.end());
        auth.push_back(static_cast<uint8_t>(cfg.password.size()));
        auth.insert(auth.end(), cfg.password.begin(), cfg.password.end());
        if (!SendAll(s, auth)) {
            return false;
        }
        std::array<uint8_t, 2> authReply{};
        if (!RecvExact(s, authReply)) {
            return false;
        }
        if (authReply[0] != SOCKS5_AUTH_VERSION || authReply[1] != SOCKS5_AUTH_OK) {
            return false;
        }
    } else if (method != SOCKS5_AUTH_NONE) {
        return false;  // 0xFF (no acceptable methods) or anything unexpected
    }

    // CONNECT request: VER, CMD, RSV, ATYP=IPv4, DST.ADDR(4), DST.PORT(2). Both
    // address and port are already in network byte order inside sockaddr_in.
    std::array<uint8_t, 10> request{};
    request[0] = SOCKS5_VERSION;
    request[1] = SOCKS5_CMD_CONNECT;
    request[2] = SOCKS5_RSV;
    request[3] = SOCKS5_ATYP_IPV4;
    std::memcpy(&request[4], &dest.sin_addr, 4);
    std::memcpy(&request[8], &dest.sin_port, 2);
    if (!SendAll(s, request)) {
        return false;
    }

    // Reply header: VER, REP, RSV, ATYP.
    std::array<uint8_t, 4> reply{};
    if (!RecvExact(s, reply)) {
        return false;
    }
    if (reply[0] != SOCKS5_VERSION || reply[1] != SOCKS5_REP_SUCCESS) {
        return false;
    }

    // Drain the bound address + port that follow, sized by ATYP.
    int bndLen = 0;
    switch (reply[3]) {
        case SOCKS5_ATYP_IPV4:
            bndLen = 4;
            break;
        case SOCKS5_ATYP_IPV6:
            bndLen = 16;
            break;
        case SOCKS5_ATYP_DOMAIN: {
            std::array<uint8_t, 1> domainLen{};
            if (!RecvExact(s, domainLen)) {
                return false;
            }
            bndLen = domainLen[0];
            break;
        }
        default:
            return false;
    }
    std::vector<uint8_t> bound(static_cast<size_t>(bndLen) + 2);
    return RecvExact(s, bound);
}

int WSAAPI HookedConnect(SOCKET s, const sockaddr* name, int namelen) {
    // Pass through anything outside our remit: no proxy configured, a thread that
    // opted out (the framework's script sockets - see components/proxy/ProxyBypass.h),
    // a malformed / non-IPv4 address, or a non-TCP socket. The last guard matters:
    // the game's local-IP discovery connects a UDP socket to an echo host, and
    // SOCKS5 CMD CONNECT is TCP-only.
    if (!config || d2bs::proxy::IsThreadBypassed() || name == nullptr ||
        namelen < static_cast<int>(sizeof(sockaddr_in)) || name->sa_family != AF_INET) {
        return realConnect(s, name, namelen);
    }
    int sockType = 0;
    int optLen = static_cast<int>(sizeof(sockType));
    if (getsockopt(s, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&sockType), &optLen) != 0 ||
        sockType != SOCK_STREAM) {
        return realConnect(s, name, namelen);
    }

    const auto proxy = ResolveProxy();
    if (!proxy) {
        // Configured but unresolvable: fail the connect rather than fall back to a
        // direct one. A direct connection would defeat the entire point of -proxy.
        logger->error("cannot resolve proxy {}:{}; failing connect", config->host, config->port);
        WSASetLastError(WSAECONNREFUSED);
        return SOCKET_ERROR;
    }

    sockaddr_in dest{};
    std::memcpy(&dest, name, sizeof(dest));

    if (!ProxyConnect(s, *proxy) || !Handshake(s, *config, dest)) {
        logger->warn("tunnel via {}:{} failed", config->host, config->port);
        WSASetLastError(WSAECONNREFUSED);
        return SOCKET_ERROR;
    }
    return 0;  // tunnel established; the socket is now wired through to dest
}

}  // namespace

void Install() {
    const auto& opts = game::GetLaunchOptions();
    if (!opts.proxy) {
        return;  // no -proxy: leave connect unhooked, connections go out directly
    }

    // Dedicated "socks5" logger over the framework's sinks. GetLogger copies the
    // default logger's sinks on first use; calling it here (after Framework's
    // SetupLogging, well before any connect) is late enough to catch them.
    logger = d2bs::utils::GetLogger("socks5");

    auto parsed = ParseProxyUrl(*opts.proxy);
    if (!parsed) {
        logger->error("invalid -proxy '{}' (expected socks5://[user:password@]host:port); proxy disabled", *opts.proxy);
        return;
    }
    config = std::move(parsed);

    // The game has already called WSAStartup, but make our own ref-counted call so
    // getaddrinfo / select are guaranteed usable regardless of game-thread timing.
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // Detour the WS2_32 `connect` export. It is the same export the game imports, so
    // hooking it catches the game's connects without depending on any build-specific
    // address. realConnect becomes the trampoline (the original connect).
    realConnect = connect;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&realConnect), reinterpret_cast<PVOID>(&HookedConnect));
    const LONG err = DetourTransactionCommit();
    if (err != NO_ERROR) {
        logger->error("Detours attach on connect failed ({}); proxy disabled", err);
        config.reset();
        WSACleanup();
        return;
    }

    installed = true;
    logger->info("routing connections via socks5://{}:{}", config->host, config->port);
}

void Remove() {
    if (!installed) {
        return;
    }
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID*>(&realConnect), reinterpret_cast<PVOID>(&HookedConnect));
    const LONG err = DetourTransactionCommit();
    if (err != NO_ERROR) {
        logger->error("Detours detach on connect failed ({})", err);
    }

    installed = false;
    {
        const std::scoped_lock lock(resolveMutex);
        resolvedProxy.reset();
    }
    config.reset();
    WSACleanup();
}

}  // namespace d2bs::hooks::socks5
