#pragma once

// Packet id -> packet instance. Every caller gets its own instance, so nothing in the packet layer is shared mutable
// state between the threads that decode.

#include <cstdint>
#include <memory>

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PacketRegistry {
   public:
    // Null for an id no class maps.
    static std::unique_ptr<Packet> MakeClientPacket(uint8_t id);
    static std::unique_ptr<Packet> MakeServerPacket(uint8_t id);
};

}  // namespace d2bs::lod114d::packets
