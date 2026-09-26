#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
#include "ImportTypes.h"
#include "extras/BNGatewayAccess.h"

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::lod114d::imports::bnclient {

// ---- Variables -------------------------------------------------------------
// The client's parsed gateway list singleton (BNGatewayAccess). Read to
// enumerate realms from the live in-memory list; see the realms hook (hooks/Realms).
inline GameVar<extras::BNGatewayAccessState> gBNGatewayAccess{0x481838};
inline GameVar<char*> gpszClassicCdKey{0x482744};
inline GameVar<char*> gpszExpansionCdKey{0x48274C};
inline GameVar<char*> gpszKeyOwner{0x482750};

// ---- Interior addresses (intercept tail-targets) ---------------------------
// Tail-jump targets for the raw-CDKey injection intercepts. DClass lands in
// the 5-byte alternative path the original code's short JMP at 0x123671
// would otherwise skip (both branches converge at 0x123678). DLod is the
// sequential continuation immediately after the patched 5-byte MOV.
inline GameAsmFunc DClass{0x123673};
inline GameAsmFunc DLod{0x12395D};

// Tail-jump target for the SID_REQUIREDWORK (0x4C) suppression intercept
// (RequiredWorkThunk @ site 0x11C7AF): the original
// NET_SID_CLIENT_IncomingPacketHandler the patched `call` would have invoked.
// The thunk zeroes the message id when it is 0x4C, then jumps here so every
// other Battle.net packet dispatches normally.
inline GameAsmFunc SID_CLIENT_IncomingPacketHandler{0x121B00};

}  // namespace d2bs::lod114d::imports::bnclient
// NOLINTEND(readability-identifier-naming)
