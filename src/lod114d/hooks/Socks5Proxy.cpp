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
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "components/proxy/ProxyBypass.h"
#include "game/LaunchOptions.h"
#include "utils/utils.h"

#pragma comment(lib, "Ws2_32.lib")

namespace d2bs::hooks::socks5 {

namespace {

using ConnectFn = int(WSAAPI*)(SOCKET, const sockaddr*, int);
using GetHostByNameFn = hostent*(WSAAPI*)(const char*);
using GetPeerNameFn = int(WSAAPI*)(SOCKET, sockaddr*, int*);
using CloseSocketFn = int(WSAAPI*)(SOCKET);

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

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp) - hook owns module-level state
ConnectFn realConnect = nullptr;  // set in Install(); Detours rewrites it to the trampoline (original connect)
GetHostByNameFn realGetHostByName = nullptr;  // trampoline to the original gethostbyname
GetPeerNameFn realGetPeerName = nullptr;      // trampoline to the original getpeername
CloseSocketFn realCloseSocket = nullptr;      // trampoline to the original closesocket
bool installed = false;
std::shared_ptr<spdlog::logger> logger;  // dedicated "socks5" logger; created in Install() once the sinks are wired

std::optional<ProxyConfig> config;         // parsed once in Install(), before the hook goes live
std::mutex resolveMutex;                   // guards resolvedProxy
std::optional<sockaddr_in> resolvedProxy;  // proxy endpoint, resolved lazily and cached

// Remote-DNS support. The game resolves Battle.net hostnames locally, then connects
// by IP - so the SOCKS5 proxy never gets to pick a server in *its* region, and the
// bot can be steered onto a gateway whose version-check/download node is unreachable
// from the proxy. We mirror Proxifier's remote DNS: detour gethostbyname to remember
// every IP->hostname mapping, then send ATYP=DOMAIN (the hostname) in the SOCKS5
// CONNECT so the proxy re-resolves it locally. Numeric "hostnames" are not recorded.
std::mutex hostMapMutex;
std::unordered_map<uint32_t, std::string> ipToHost;  // key: IPv4 in network byte order

// Sockets we tunnelled, mapped to the destination the game *asked* for. Because the
// socket is really connected to the SOCKS5 proxy, an un-hooked getpeername() would
// report the proxy's address - and the game uses that to open follow-up connections
// (BnFTP version download / realm) to <peer-ip>:6112, hitting the proxy's own IP and
// stalling. Reporting the original destination keeps the game's view consistent with
// a transparent proxifier, so those follow-ups re-tunnel correctly.
std::mutex peerMapMutex;
std::unordered_map<SOCKET, sockaddr_in> peerMap;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp)

// True if `s` is a dotted-quad / numeric literal rather than a real hostname; we must
// not "remote-resolve" those (sending a numeric ATYP=DOMAIN defeats the purpose).
bool IsNumericHost(const char* s) {
    if (s == nullptr || *s == '\0') {
        return true;
    }
    in_addr v4{};
    in6_addr v6{};
    return InetPtonA(AF_INET, s, &v4) == 1 || InetPtonA(AF_INET6, s, &v6) == 1;
}

void RememberHost(uint32_t ipNetOrder, const char* host) {
    if (IsNumericHost(host)) {
        return;
    }
    const std::scoped_lock lock(hostMapMutex);
    ipToHost[ipNetOrder] = host;
}

// Reverse-lookup the hostname the game resolved for this IPv4 (empty if none/numeric).
std::string HostForIp(uint32_t ipNetOrder) {
    const std::scoped_lock lock(hostMapMutex);
    if (const auto it = ipToHost.find(ipNetOrder); it != ipToHost.end()) {
        return it->second;
    }
    return {};
}

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
    int len = sizeof(soErr);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len) != 0) {
        return false;
    }
    return soErr == 0;
}

// Perform the SOCKS5 greeting / optional auth / CONNECT on the already-proxy-
// connected socket `s`. Reach `dest` by IPv4, or by hostname (ATYP=DOMAIN) when
// `host` is non-empty, so the proxy resolves it in its own region (remote DNS).
bool Handshake(SOCKET s, const ProxyConfig& cfg, const sockaddr_in& dest, std::string_view host) {
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

    // CONNECT request: VER, CMD, RSV, ATYP, DST.ADDR, DST.PORT (port already in
    // network byte order). When we know the hostname the game resolved for this IP
    // we send ATYP=DOMAIN so the proxy resolves it in its own region (remote DNS,
    // like Proxifier); otherwise we fall back to the resolved IPv4.
    std::vector<uint8_t> request{SOCKS5_VERSION, SOCKS5_CMD_CONNECT, SOCKS5_RSV};
    if (!host.empty() && host.size() <= SOCKS5_MAX_FIELD) {
        request.push_back(SOCKS5_ATYP_DOMAIN);
        request.push_back(static_cast<uint8_t>(host.size()));
        request.insert(request.end(), host.begin(), host.end());
    } else {
        request.push_back(SOCKS5_ATYP_IPV4);
        const auto* addr = reinterpret_cast<const uint8_t*>(&dest.sin_addr);
        request.insert(request.end(), addr, addr + 4);
    }
    const auto* portBytes = reinterpret_cast<const uint8_t*>(&dest.sin_port);
    request.insert(request.end(), portBytes, portBytes + 2);
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
    if (!config || proxy::IsThreadBypassed() || name == nullptr ||
        namelen < static_cast<int>(sizeof(sockaddr_in)) || name->sa_family != AF_INET) {
        return realConnect(s, name, namelen);
    }

    // Loopback / private / link-local destinations are never tunnelled: they
    // belong to this machine or the local network, not the remote SOCKS5 peer.
    // During Battle.net login the game makes a 127.0.0.1 self-connection; routing
    // that to the proxy (whose own localhost has nothing listening) fails closed
    // and hangs the client on "Checking versions". System proxifiers bypass these
    // ranges for the same reason.
    {
        const auto* dst = reinterpret_cast<const sockaddr_in*>(name);
        const uint32_t hostOrder = ntohl(dst->sin_addr.s_addr);
        const auto b1 = static_cast<uint8_t>(hostOrder >> 24);
        const auto b2 = static_cast<uint8_t>((hostOrder >> 16) & 0xFF);
        const bool loopback = b1 == 127;                             // 127.0.0.0/8
        const bool linkLocal = b1 == 169 && b2 == 254;               // 169.254.0.0/16
        const bool privateNet = b1 == 10 ||                          // 10.0.0.0/8
                                (b1 == 172 && (b2 & 0xF0) == 16) ||  // 172.16.0.0/12
                                (b1 == 192 && b2 == 168);            // 192.168.0.0/16
        if (loopback || linkLocal || privateNet) {
            return realConnect(s, name, namelen);
        }
    }

    int sockType = 0;
    int optLen = sizeof(sockType);
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

    std::array<char, INET_ADDRSTRLEN> ipbuf{};
    inet_ntop(AF_INET, &dest.sin_addr, ipbuf.data(), ipbuf.size());
    const uint16_t dport = ntohs(dest.sin_port);

    // Prefer remote DNS: if the game resolved a hostname for this IP, hand the
    // hostname to the proxy (ATYP=DOMAIN) so it picks a server in its own region.
    const std::string host = HostForIp(dest.sin_addr.s_addr);
    logger->debug("connect -> {}:{}{}", host.empty() ? ipbuf.data() : host.c_str(), dport,
                  host.empty() ? "" : " (remote DNS)");

    if (!ProxyConnect(s, *proxy) || !Handshake(s, *config, dest, host)) {
        logger->warn("tunnel to {} ({}:{}) via {}:{} failed", host.empty() ? ipbuf.data() : host.c_str(), ipbuf.data(),
                     dport, config->host, config->port);
        WSASetLastError(WSAECONNREFUSED);
        return SOCKET_ERROR;
    }
    {
        const std::scoped_lock lock(peerMapMutex);
        peerMap[s] = dest;  // so getpeername() reports the real server, not the proxy
    }
    return 0;  // tunnel established; the socket is now wired through to dest
}

// getpeername on a tunnelled socket would return the proxy's address (the socket's
// true peer). Report the destination the game requested instead, so follow-up
// connections the game derives from the peer address target the real server.
int WSAAPI HookedGetPeerName(SOCKET s, sockaddr* name, int* namelen) {
    {
        const std::scoped_lock lock(peerMapMutex);
        if (const auto it = peerMap.find(s); it != peerMap.end()) {
            if (name != nullptr && namelen != nullptr && *namelen >= static_cast<int>(sizeof(sockaddr_in))) {
                std::memcpy(name, &it->second, sizeof(sockaddr_in));
                *namelen = static_cast<int>(sizeof(sockaddr_in));
                return 0;
            }
        }
    }
    return realGetPeerName(s, name, namelen);
}

// Drop the peer mapping when the socket closes so a reused descriptor can't inherit
// a stale tunnelled peer.
int WSAAPI HookedCloseSocket(SOCKET s) {
    {
        const std::scoped_lock lock(peerMapMutex);
        peerMap.erase(s);
    }
    return realCloseSocket(s);
}

// Detoured resolver: run the real lookup, then record every IPv4 -> hostname so
// HookedConnect can later send the hostname to the proxy (ATYP=DOMAIN). Pure
// observation; the game still sees exactly the addresses the OS returned. The game
// resolves Battle.net hosts through classic-Winsock gethostbyname, so that is the
// only resolver we need to watch.
hostent* WSAAPI HookedGetHostByName(const char* name) {
    hostent* he = realGetHostByName(name);
    if (he == nullptr || name == nullptr || he->h_addrtype != AF_INET || he->h_length != 4 ||
        he->h_addr_list == nullptr || IsNumericHost(name)) {
        return he;
    }
    for (int i = 0; he->h_addr_list[i] != nullptr; ++i) {
        uint32_t ip = 0;
        std::memcpy(&ip, he->h_addr_list[i], sizeof(ip));
        RememberHost(ip, name);
    }
    return he;
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
    logger = utils::GetLogger("socks5");

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

    // Detour the WS2_32 `connect` export plus the sockets the remote-DNS / peer
    // rewrite needs: `gethostbyname` feeds the IP->hostname map (so we can send
    // ATYP=DOMAIN), and `getpeername` / `closesocket` back the per-socket peer
    // rewrite. They are the same exports the game imports, so no build-specific
    // addresses are needed. The real* pointers become the trampolines (the originals).
    realConnect = connect;
    // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations) - classic-Winsock API the game uses
    realGetHostByName = gethostbyname;
    realGetPeerName = getpeername;
    realCloseSocket = closesocket;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&realConnect), reinterpret_cast<PVOID>(&HookedConnect));
    DetourAttach(reinterpret_cast<PVOID*>(&realGetHostByName), reinterpret_cast<PVOID>(&HookedGetHostByName));
    DetourAttach(reinterpret_cast<PVOID*>(&realGetPeerName), reinterpret_cast<PVOID>(&HookedGetPeerName));
    DetourAttach(reinterpret_cast<PVOID*>(&realCloseSocket), reinterpret_cast<PVOID>(&HookedCloseSocket));
    const LONG err = DetourTransactionCommit();
    if (err != NO_ERROR) {
        logger->error("Detours attach failed ({}); proxy disabled", err);
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
    DetourDetach(reinterpret_cast<PVOID*>(&realGetHostByName), reinterpret_cast<PVOID>(&HookedGetHostByName));
    DetourDetach(reinterpret_cast<PVOID*>(&realGetPeerName), reinterpret_cast<PVOID>(&HookedGetPeerName));
    DetourDetach(reinterpret_cast<PVOID*>(&realCloseSocket), reinterpret_cast<PVOID>(&HookedCloseSocket));
    const LONG err = DetourTransactionCommit();
    if (err != NO_ERROR) {
        logger->error("Detours detach failed ({})", err);
    }

    installed = false;
    {
        const std::scoped_lock lock(resolveMutex);
        resolvedProxy.reset();
    }
    {
        const std::scoped_lock lock(hostMapMutex);
        ipToHost.clear();
    }
    {
        const std::scoped_lock lock(peerMapMutex);
        peerMap.clear();
    }
    config.reset();
    WSACleanup();
}

}  // namespace d2bs::hooks::socks5
