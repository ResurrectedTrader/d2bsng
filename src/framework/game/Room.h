#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "game/HandleCache.h"
#include "game/Types.h"

namespace d2bs::game {

class Level;
class Unit;

// Identity-based handle for the room-tree side of a game room (D2MOO's
// D2DrlgRoomStrc). Stores (level, pos) as identity and resolves to a game
// pointer on demand with per-frame caching via HandleCache.
//
// Coordinate convention: Bounds() returns game coordinates.
// The game stores rooms as subtiles (1 subtile = 5 game-coords); the conversion
// happens in the game-layer implementation. See docs/coords.md.
class Room {
    uint32_t level_ = 0;
    Position pos_;
    HandleCache cache_;

    void* ResolvePtr() const;

   public:
    explicit Room(uint32_t level = 0, Position pos = {}) : level_(level), pos_(pos) {}
    explicit operator bool() const;
    bool operator==(const Room& other) const;

    // Extracts identity (level, pos) from a raw DrlgRoom pointer.
    // Used during transition from pointer-based to identity-based code.
    static Room FromPtr(void* p);

    // Properties
    int32_t Number() const;
    int32_t SubNumber() const;
    Rect Bounds() const;
    uint32_t Flags() const;
    uint32_t LevelId() const { return level_; }
    uint32_t CorrectTomb() const;

    // Collision (via the room's ActiveRoom side, with AddRoomData /
    // RemoveRoomData if needed).
    std::vector<std::vector<uint16_t>> GetCollision() const;
    std::vector<uint16_t> GetCollisionFlat() const;

    // Traversal
    Room GetNext() const;
    Room GetFirst() const;
    std::vector<Room> GetNearby() const;
    Level GetLevel() const;
    // First unit in this room's ActiveRoom unit-chain
    // (DrlgRoom::pRoom->pUnitFirst). nullopt when the room has no units.
    std::optional<Unit> GetFirstUnit() const;

    // Preset units
    std::vector<PresetUnitInfo> GetPresetUnits(std::optional<uint32_t> type = std::nullopt,
                                               std::optional<uint32_t> classId = std::nullopt) const;

    // Stats (stat index for various room properties).
    // RAW stat-access API: returns values in mixed units per index (subtiles for
    // ActiveRoom/DrlgRoom pos+size, game-coords for Coll game fields). See
    // docs/coords.md for the per-index unit table. Does not apply the
    // Pos()/Size() scaling.
    uint32_t GetStat(uint32_t statIndex) const;

    // ActiveRoom-based queries
    bool Reveal(bool drawPresets = false) const;

    // Factory - find a room by level and position
    static std::optional<Room> Find(uint32_t level, Position pos);
};

}  // namespace d2bs::game
