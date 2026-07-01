#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/HandleCache.h"
#include "game/Types.h"

namespace d2bs::game {

class Room;

// Identity-based handle for D2DrlgLevelStrc*.
// Stores the level number as identity and resolves to a game pointer on demand
// with per-frame caching via HandleCache.
//
// Coordinate convention: Bounds() returns game coordinates.
// The game stores levels as subtiles (1 subtile = 5 game-coords); the conversion
// happens in the game-layer implementation. See docs/coords.md.
class Level {
    uint32_t id_ = 0;
    HandleCache cache_;

    void* ResolvePtr() const;

   public:
    explicit Level(uint32_t id = 0) : id_(id) {}
    explicit operator bool() const;

    // Properties
    uint32_t Id() const { return id_; }
    std::string Name() const;
    Rect Bounds() const;

    // Traversal
    Room GetFirstRoom() const;

    // Exits
    std::vector<ExitInfo> GetExits() const;

    // Factory - resolves a Level from the current act's misc data.
    // Returns std::nullopt if the level does not exist.
    static std::optional<Level> Get(uint32_t levelNo);

    // === Framework-impl (defined inline in Finders.h) ===
    // Find the room at `pos` (game coordinates) within this level.
    // Returns nullopt if no room contains the point.
    std::optional<Room> FindRoomAt(Position pos) const;
    // Collect preset units across every room in this level, applying optional
    // type/classId filters (matches each room's GetPresetUnits contract).
    std::vector<PresetUnitInfo> GetPresetUnits(std::optional<uint32_t> type = std::nullopt,
                                               std::optional<uint32_t> classId = std::nullopt) const;
};

}  // namespace d2bs::game
