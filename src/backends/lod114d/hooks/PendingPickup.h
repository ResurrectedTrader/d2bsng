#pragma once

#include <cstdint>
#include <span>

// Holds a belt drink back while a ground pickup is in flight.
//
// When a ground pickup (client 0x16) and a belt use (client 0x26) reach the
// server in the same server frame, D2Game puts the picked-up potion into the
// column's first free belt slot, then the drink compacts the column, and the
// belt updates go out in queue order (the new potion first). The client then
// places one item on another and later asserts in CITEMS_ItemAction_UpdateStats
// (Game.exe 0x4C4C70, FOG_DisplayHalt line 3166, on an SCMD 0x9D). Native D2Game
// does this, and the native client's drink path (CPANEL_UseBeltItem) checks only
// the drunk item's pending state, not outstanding pickups; a player rarely lands
// both in one server frame, but a bot's concurrent pickit and potion threads do.
//
// The send hook records the last pickup's item id, the receive hook clears it
// once the server answers for that item, and a scripted belt use waits for that
// answer (bounded by a timeout) before it goes out, so the two never share a
// server frame. A belt use the game thread sends (the player's own hotkey) is
// not held back.
namespace d2bs::lod114d::hooks::pickup {

// Feed every outbound game packet the client actually sends.
void OnPacketSent(std::span<const uint8_t> packet);

// Feed every inbound game packet.
void OnPacketReceived(std::span<const uint8_t> packet);

// Blocks until no ground pickup is awaiting the server's answer, or the pickup
// times out. Releases this thread's game read locks while waiting. Returns at
// once on the game thread, which must not block (its frames deliver the answer).
void WaitForPendingPickup();

}  // namespace d2bs::lod114d::hooks::pickup
