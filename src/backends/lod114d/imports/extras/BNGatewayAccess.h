#pragma once

#include <cstddef>
#include <cstdint>

namespace d2bs::lod114d::imports::extras {

// Partial view of D2's BNGatewayAccess singleton - the client's parsed
// Battle.net gateway list. We only read the raw gateway blob: a NUL-separated
// multistring (version, selected-index, then a host/zone/name triple per
// gateway; see the lod114d backend's realms hook, hooks/Realms). Field offsets recovered
// from BNGatewayAccess::Load / GetGatewayList on 1.14d Game.exe.
struct BNGatewayAccessState {
    uint32_t reserved0;        // +0x00
    uint32_t dirtyFlag;        // +0x04: pending-write flag
    uint32_t gatewayCount;     // +0x08
    uint32_t selectedGateway;  // +0x0C: 1-based index
    char* blob;                // +0x10: NUL-separated gateway multistring (null until loaded)
    uint32_t blobLength;       // +0x14
};

static_assert(offsetof(BNGatewayAccessState, blob) == 0x10, "blob must be at +0x10");
static_assert(offsetof(BNGatewayAccessState, blobLength) == 0x14, "blobLength must be at +0x14");

}  // namespace d2bs::lod114d::imports::extras
