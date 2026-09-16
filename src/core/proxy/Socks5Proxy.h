#pragma once

#include <string_view>

// SOCKS5 proxy for the game's outbound Battle.net connections.
//
// Given a `socks5://[user:password@]host:port` spec (the backend's `-proxy`
// launch option), Install() Detours four WS2_32 exports so outbound TCP
// connections are tunnelled through the SOCKS5 proxy - the game's BNCS (6112),
// the realm game server / D2GS (4000), and the BnFTP file download. They are the
// same exports the game imports, so hooking them is version-independent (no
// build-specific address).
//
// DNS is resolved by the proxy, not locally. The game resolves gateway / realm
// hostnames itself and reaches `connect` with an IPv4 already in hand, which
// would deny the proxy any say in which server it lands on; `gethostbyname` is
// detoured to remember every IP->hostname mapping so the CONNECT can carry
// ATYP=DOMAIN and let the proxy re-resolve. `getpeername` reports the
// destination the game asked for rather than the proxy, so the game's follow-up
// connections re-tunnel, and `closesocket` retires the mapping.
//
// With an empty spec, Install() is a no-op and connections go out directly.
//
// The detour is process-wide, so a script's own TCP sockets (JSSocket) are routed
// too; that matches the intent of -proxy (don't touch the network directly). Our
// proxy dial uses the Detours trampoline, so it never re-enters the hook, and the
// game's UDP local-IP probe is passed through (SOCKS5 CMD CONNECT is TCP-only). The
// handshake is synchronous but select-driven, so it works whether the socket was
// left blocking (D2GS / BnFTP) or non-blocking (BNCS).

namespace d2bs::proxy::socks5 {

// Detour WS2_32 connect through the SOCKS5 proxy `proxySpec` names
// (socks5://[user:password@]host:port). An empty spec is a no-op; a
// malformed one is logged and ignored. Idempotent.
void Install(std::string_view proxySpec);

// Remove the connect detour. Idempotent.
void Remove();

}  // namespace d2bs::proxy::socks5
