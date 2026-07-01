#pragma once

// SOCKS5 proxy for the game's outbound Battle.net connections.
//
// With `-proxy socks5://[user:password@]host:port` on the command line, Install()
// Detours the WS2_32 `connect` export so outbound TCP connections are tunnelled
// through the SOCKS5 proxy - the game's BNCS (6112), the realm game server / D2GS
// (4000), and the BnFTP file download. It is the same export the game imports, so
// hooking it is version-independent (no build-specific address). DNS stays native:
// the game resolves gateway / realm hostnames itself and reaches `connect` with an
// already-resolved IPv4, which we forward verbatim as a SOCKS5 IPv4 CONNECT.
//
// Without `-proxy`, Install() is a no-op and connections go out directly.
//
// The detour is process-wide, so a script's own TCP sockets (JSSocket) are routed
// too; that matches the intent of -proxy (don't touch the network directly). Our
// proxy dial uses the Detours trampoline, so it never re-enters the hook, and the
// game's UDP local-IP probe is passed through (SOCKS5 CMD CONNECT is TCP-only). The
// handshake is synchronous but select-driven, so it works whether the socket was
// left blocking (D2GS / BnFTP) or non-blocking (BNCS).

namespace d2bs::hooks::socks5 {

// Parse `-proxy` and, when present and valid, Detour `connect`. No-op when `-proxy`
// is absent or malformed (the latter is logged). Called once from HookManager
// during install.
void Install();

// Remove the `connect` detour. Idempotent.
void Remove();

}  // namespace d2bs::hooks::socks5
