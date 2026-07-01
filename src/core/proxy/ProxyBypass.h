#pragma once

// Per-thread opt-out for the SOCKS5 `connect` detour (installed by the port - see
// src/backends/lod114d/hooks/Socks5Proxy.cpp). With `-proxy`, that detour routes every
// outbound TCP connection through the proxy by default; code that must reach the
// network directly wraps its connect() in a BypassScope so the detour passes it
// through.
//
// The framework's own JSSocket (script sockets, not the game's Battle.net traffic)
// is the one caller that opts out. Default-proxy / explicit-bypass is deliberate:
// it keeps the safe failure mode - a game connection can never accidentally skip
// the proxy, only an explicitly-scoped connect does. (This is why we do not key
// the bypass off speedhack's thread opt-in, which also covers the game thread.)
//
// Header-only so both the framework (JSSocket) and the port hook share one
// thread_local instance once linked into the DLL.

namespace d2bs::proxy {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - per-thread bypass state
inline thread_local bool threadBypass = false;

// True while the calling thread is inside a BypassScope.
inline bool IsThreadBypassed() {
    return threadBypass;
}

// RAII: route any connect() on this thread directly, skipping the SOCKS5 proxy,
// for the scope's lifetime. Reentrant (saves and restores the prior state).
class BypassScope {
   public:
    BypassScope() : prev_(threadBypass) { threadBypass = true; }
    ~BypassScope() { threadBypass = prev_; }
    BypassScope(const BypassScope&) = delete;
    BypassScope& operator=(const BypassScope&) = delete;
    BypassScope(BypassScope&&) = delete;
    BypassScope& operator=(BypassScope&&) = delete;

   private:
    bool prev_;
};

}  // namespace d2bs::proxy
