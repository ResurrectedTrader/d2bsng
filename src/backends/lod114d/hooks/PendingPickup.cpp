#include "hooks/PendingPickup.h"

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

#include "game/GameLock.h"
#include "game/Types.h"
#include "hooks/HookManager.h"

#include <D2PacketDef.h>  // D2GSPacketClt16, D2GSPacketSrv0A

namespace d2bs::lod114d::hooks::pickup {

namespace {

using Clock = std::chrono::steady_clock;

// Long enough for a server round trip; a pickup the server ignores (full
// inventory, item already taken) never gets an answer.
constexpr std::chrono::milliseconds PICKUP_TIMEOUT{500};

constexpr uint8_t CLT_PICKUP_ITEM = 0x16;
constexpr uint8_t SRV_REMOVE_OBJECT = 0x0A;
constexpr uint8_t SRV_ITEM_ACTION = 0x9C;
constexpr uint8_t SRV_ITEM_ACTION_OWNED = 0x9D;
constexpr size_t ITEM_ACTION_GUID_OFFSET = 4;

struct Pending {
    uint32_t itemId = 0;
    Clock::time_point sentAt;
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - written by the send / receive hooks
std::mutex mutex;
// Only the latest pickup is kept: the server handles a client's packets in order,
// so its answer for the latest one comes after the answers for any earlier ones.
std::optional<Pending> pending;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

uint32_t ReadU32(std::span<const uint8_t> packet, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, packet.subspan(offset, sizeof(value)).data(), sizeof(value));
    return value;
}

void Answered(uint32_t itemId) {
    const std::lock_guard lock(mutex);
    if (pending.has_value() && pending->itemId == itemId) {
        pending.reset();
    }
}

std::optional<Clock::time_point> PendingSince() {
    const std::lock_guard lock(mutex);
    if (pending.has_value() && Clock::now() - pending->sentAt >= PICKUP_TIMEOUT) {
        pending.reset();
    }
    return pending.transform([](const Pending& p) { return p.sentAt; });
}

}  // namespace

void OnPacketSent(std::span<const uint8_t> packet) {
    if (packet.size() < sizeof(D2GSPacketClt16) || packet[0] != CLT_PICKUP_ITEM) {
        return;
    }
    const uint32_t itemId = ReadU32(packet, offsetof(D2GSPacketClt16, dwUnitGUID));
    const std::lock_guard lock(mutex);
    pending = Pending{.itemId = itemId, .sentAt = Clock::now()};
}

void OnPacketReceived(std::span<const uint8_t> packet) {
    if (packet.empty()) {
        return;
    }
    switch (packet[0]) {
        case SRV_REMOVE_OBJECT:
            if (packet.size() >= sizeof(D2GSPacketSrv0A) &&
                packet[offsetof(D2GSPacketSrv0A, nUnitType)] == std::to_underlying(game::UnitType::Item)) {
                Answered(ReadU32(packet, offsetof(D2GSPacketSrv0A, dwUnitGUID)));
            }
            break;
        case SRV_ITEM_ACTION:
        case SRV_ITEM_ACTION_OWNED:
            if (packet.size() >= ITEM_ACTION_GUID_OFFSET + sizeof(uint32_t)) {
                Answered(ReadU32(packet, ITEM_ACTION_GUID_OFFSET));
            }
            break;
        default:
            break;
    }
}

void WaitForPendingPickup() {
    if (GetGameThreadId() == GetCurrentThreadId()) {
        return;
    }
    const auto sentAt = PendingSince();
    if (!sentAt.has_value()) {
        return;
    }
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(*sentAt + PICKUP_TIMEOUT - Clock::now());
    constexpr std::chrono::milliseconds POLL_INTERVAL{5};
    game::PollUntil(remaining, POLL_INTERVAL, [] { return !PendingSince().has_value(); });
}

}  // namespace d2bs::lod114d::hooks::pickup
