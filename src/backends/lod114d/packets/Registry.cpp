#include "packets/Registry.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

// ReSharper disable once CppUnusedIncludeDirective - gServerPacketSizeTable114d / gClientPacketSizeTable114d
#include <Packet.h>

#include "packets/Packet.h"
// ReSharper disable once CppUnusedIncludeDirective - the packet headers, through RegistryTable.inc
#include "packets/RegistryTable.inc"

namespace d2bs::lod114d::packets {

namespace {

using Factory = std::unique_ptr<Packet> (*)();

template <typename T>
std::unique_ptr<Packet> Make() {
    return std::make_unique<T>();
}

template <typename T>
concept FixedSizePacket = requires { T::SIZE; };

// The game's own size table (-1: variable size), as D2MOO copies it from Game.exe 0x730AE8 / 0x730DC0, is what the
// receive and send paths frame by, so a class that disagrees with it would decode the wrong bytes.
template <typename T>
constexpr bool MatchesGameSize(std::span<const int32_t> sizes) {
    if (T::ID >= sizes.size()) {
        return false;
    }
    if constexpr (FixedSizePacket<T>) {
        return sizes[T::ID] == static_cast<int32_t>(T::SIZE);
    } else {
        return sizes[T::ID] == -1;
    }
}

#define LOD114D_CLIENT_PACKET(id, name) \
    static_assert(name::ID == (id) && MatchesGameSize<name>(gClientPacketSizeTable114d), #name " size mismatch");
#define LOD114D_SERVER_PACKET(id, name) \
    static_assert(name::ID == (id) && MatchesGameSize<name>(gServerPacketSizeTable114d), #name " size mismatch");
#include "packets/RegistryTable.inc"

constexpr size_t ID_COUNT = 256;

constexpr std::array<Factory, ID_COUNT> CLIENT_TABLE = [] {
    std::array<Factory, ID_COUNT> table{};
#define LOD114D_CLIENT_PACKET(id, name) table[id] = &Make<name>;
#include "packets/RegistryTable.inc"
    return table;
}();

constexpr std::array<Factory, ID_COUNT> SERVER_TABLE = [] {
    std::array<Factory, ID_COUNT> table{};
#define LOD114D_SERVER_PACKET(id, name) table[id] = &Make<name>;
#include "packets/RegistryTable.inc"
    return table;
}();

}  // namespace

std::unique_ptr<Packet> PacketRegistry::MakeClientPacket(uint8_t id) {
    const Factory make = CLIENT_TABLE.at(id);
    return make != nullptr ? make() : nullptr;
}

std::unique_ptr<Packet> PacketRegistry::MakeServerPacket(uint8_t id) {
    const Factory make = SERVER_TABLE.at(id);
    return make != nullptr ? make() : nullptr;
}

}  // namespace d2bs::lod114d::packets
