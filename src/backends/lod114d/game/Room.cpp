#include "game/Room.h"

#include "DrlgHelpers.h"
#include "RoomData.h"
#include "asm_thunks/asm_thunks.h"
#include "game/GameLock.h"
#include "game/GameThread.h"
#include "game/Level.h"
#include "game/Unit.h"
#include "imports/D2Client.h"
#include "imports/extras/D2ActiveRoomStrc.h"
#include "imports/extras/D2DrlgLevelStrc.h"
#include "imports/extras/D2DrlgRoomStrc.h"
#include "imports/extras/D2DrlgStrc.h"
#include "imports/extras/D2PresetUnitStrc.h"
#include "imports/extras/D2RoomTileStrc.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <D2Collision.h>        // D2RoomCollisionGridStrc
#include <Drlg/D2DrlgPreset.h>  // D2DrlgPresetRoomStrc
#include <Units/Units.h>        // D2UnitStrc
#pragma clang diagnostic pop

#include <cstdint>
#include <vector>

namespace d2bs::game {

using imports::extras::D2ActiveRoomStrc;
using imports::extras::D2DrlgRoomStrc;

namespace {

inline D2DrlgRoomStrc* AsDrlgRoom(void* p) noexcept {
    return static_cast<D2DrlgRoomStrc*>(p);
}

// 1 subtile == 5 game-coords. The conversion lives only here per docs/coords.md.
constexpr int32_t SUBTILE_TO_GAME_COORD = 5;

}  // namespace

void* Room::ResolvePtr() const {
    GameReadLock guard;
    if (level_ == 0)
        return nullptr;
    if (auto* cached = cache_.Get())
        return cached;
    auto* lvl = FindLevelInChain(level_);
    void* resolved = FindRoomInLevelByPos(lvl, pos_);
    cache_.Set(resolved);
    return resolved;
}

Room::operator bool() const {
    return ResolvePtr() != nullptr;
}

Room Room::FromPtr(void* p) {
    if (p == nullptr)
        return Room();
    auto* drlgRoom = AsDrlgRoom(p);
    if (drlgRoom->pLevel == nullptr)
        return Room();
    Room handle(static_cast<uint32_t>(drlgRoom->pLevel->nLevelId),
                {.x = static_cast<uint32_t>(drlgRoom->nTileXPos), .y = static_cast<uint32_t>(drlgRoom->nTileYPos)});
    handle.cache_.Set(p);
    return handle;
}

int32_t Room::Number() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr)
        return 0;
    if (drlgRoom->nType != DRLGTYPE_PRESET || drlgRoom->pMaze == nullptr)
        return -1;
    // Byte 0x00 of the preset struct = reference's `dwRoomNumber` =
    // D2MOO's `nLevelPrest` (same byte, different name).
    return drlgRoom->pMaze->nLevelPrest;
}

int32_t Room::SubNumber() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr)
        return 0;
    if (drlgRoom->nType != DRLGTYPE_PRESET || drlgRoom->pMaze == nullptr || drlgRoom->pMaze->pMap == nullptr)
        return -1;
    // Reference reads `*pType2Info->pdwSubNumber` (a DWORD* at byte 0x08
    // dereferenced) - that's the first DWORD of D2MOO's pMap target,
    // which is `D2DrlgMapStrc::nLevelPrest`.
    return drlgRoom->pMaze->pMap->nLevelPrest;
}

Rect Room::Bounds() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr)
        return Rect::Zero;
    return {
        .origin = {.x = static_cast<uint32_t>(drlgRoom->nTileXPos * SUBTILE_TO_GAME_COORD),
                   .y = static_cast<uint32_t>(drlgRoom->nTileYPos * SUBTILE_TO_GAME_COORD)},
        .size = {.width = static_cast<uint32_t>(drlgRoom->nTileWidth * SUBTILE_TO_GAME_COORD),
                 .height = static_cast<uint32_t>(drlgRoom->nTileHeight * SUBTILE_TO_GAME_COORD)},
    };
}

uint32_t Room::Flags() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr)
        return 0;
    // Reference's `dwPresetType` (renamed `nType` in D2MOO at 0x1C) is what the
    // legacy `flags` accessor exposes - DRLGTYPE_MAZE / PRESET / OUTDOOR.
    return static_cast<uint32_t>(drlgRoom->nType);
}

uint32_t Room::CorrectTomb() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr || drlgRoom->pLevel == nullptr || drlgRoom->pLevel->pDrlg == nullptr)
        return 0;
    return static_cast<uint32_t>(drlgRoom->pLevel->pDrlg->nStaffTombLevel);
}

std::vector<std::vector<uint16_t>> Room::GetCollision() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr) {
        return {};
    }
    RoomDataGuard guard(drlgRoom);
    auto* activeRoom = drlgRoom->pRoom;
    if (activeRoom == nullptr || activeRoom->pCollisionGrid == nullptr) {
        return {};
    }
    auto* grid = activeRoom->pCollisionGrid;
    if (grid->pCollisionMask == nullptr) {
        return {};
    }
    const auto width = static_cast<uint32_t>(grid->pRoomCoords.nSubtileWidth);
    const auto height = static_cast<uint32_t>(grid->pRoomCoords.nSubtileHeight);
    std::vector<std::vector<uint16_t>> out;
    out.reserve(height);
    const uint16_t* p = grid->pCollisionMask;
    for (uint32_t j = 0; j < height; ++j) {
        out.emplace_back(p, p + width);
        p += width;
    }
    return out;
}

std::vector<uint16_t> Room::GetCollisionFlat() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr) {
        return {};
    }
    RoomDataGuard guard(drlgRoom);
    auto* activeRoom = drlgRoom->pRoom;
    if (activeRoom == nullptr || activeRoom->pCollisionGrid == nullptr) {
        return {};
    }
    auto* grid = activeRoom->pCollisionGrid;
    if (grid->pCollisionMask == nullptr) {
        return {};
    }
    const auto width = static_cast<uint32_t>(grid->pRoomCoords.nSubtileWidth);
    const auto height = static_cast<uint32_t>(grid->pRoomCoords.nSubtileHeight);
    const auto count = static_cast<size_t>(width) * height;
    return {grid->pCollisionMask, grid->pCollisionMask + count};
}

Room Room::GetNext() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr)
        return Room();
    return FromPtr(drlgRoom->pDrlgRoomNext);
}

Room Room::GetFirst() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr || drlgRoom->pLevel == nullptr)
        return Room();
    return FromPtr(drlgRoom->pLevel->pFirstRoomEx);
}

std::vector<Room> Room::GetNearby() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr || drlgRoom->ppRoomsNear == nullptr)
        return {};
    const auto count = static_cast<size_t>(drlgRoom->nRoomsNear);
    std::vector<Room> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (auto* neighbour = drlgRoom->ppRoomsNear[i]) {
            out.push_back(FromPtr(neighbour));
        }
    }
    return out;
}

Level Room::GetLevel() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr || drlgRoom->pLevel == nullptr)
        return Level();
    return Level(static_cast<uint32_t>(drlgRoom->pLevel->nLevelId));
}

std::optional<Unit> Room::GetFirstUnit() const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr || drlgRoom->pRoom == nullptr || drlgRoom->pRoom->pUnitFirst == nullptr)
        return std::nullopt;
    return Unit::FromPtr(drlgRoom->pRoom->pUnitFirst);
}

std::vector<PresetUnitInfo> Room::GetPresetUnits(std::optional<uint32_t> type, std::optional<uint32_t> classId) const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr)
        return {};
    RoomDataGuard guard(drlgRoom);
    const Position roomPos{.x = static_cast<uint32_t>(drlgRoom->nTileXPos),
                           .y = static_cast<uint32_t>(drlgRoom->nTileYPos)};

    // For UNIT_TILE presets the destination level lives on DrlgRoom::pRoomTiles
    // - each warp entry has a pointer-to-DWORD tile id that, when matching
    // a preset's nIndex, names the target DrlgRoom (and via it, the target
    // level). Walked here so framework callers see the destination as a
    // populated field on PresetUnitInfo without needing a second boundary
    // call. Reference: D2Helpers.cpp::GetTileLevelNo.
    constexpr uint32_t UNIT_TILE = 5;
    auto resolveTileTarget = [drlgRoom](uint32_t presetTileId) -> uint32_t {
        for (auto* warp = drlgRoom->pRoomTiles; warp != nullptr; warp = warp->pNext) {
            if (warp->pPresetTileId == nullptr || warp->pDrlgRoom == nullptr || warp->pDrlgRoom->pLevel == nullptr) {
                continue;
            }
            if (*warp->pPresetTileId == presetTileId) {
                return static_cast<uint32_t>(warp->pDrlgRoom->pLevel->nLevelId);
            }
        }
        return 0;
    };

    std::vector<PresetUnitInfo> out;
    for (auto* preset = drlgRoom->pPresetUnits; preset != nullptr; preset = preset->pNext) {
        const auto presetType = static_cast<uint32_t>(preset->nUnitType);
        const auto presetIndex = static_cast<uint32_t>(preset->nIndex);
        if (type && presetType != *type)
            continue;
        if (classId && presetIndex != *classId)
            continue;
        out.push_back(PresetUnitInfo{
            .type = presetType,
            .roomPos = roomPos,
            .posInRoom = {.x = static_cast<uint32_t>(preset->nXpos), .y = static_cast<uint32_t>(preset->nYpos)},
            .id = presetIndex,
            .level = 0,
            .tileTargetLevelId = (presetType == UNIT_TILE) ? resolveTileTarget(presetIndex) : 0,
        });
    }
    return out;
}

uint32_t Room::GetStat(uint32_t statIndex) const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr)
        return 0;
    // Indices 4-7 read DrlgRoom (no ActiveRoom needed); indices 0-3, 9-16 require ActiveRoom.
    if (statIndex == 4)
        return static_cast<uint32_t>(drlgRoom->nTileXPos);
    if (statIndex == 5)
        return static_cast<uint32_t>(drlgRoom->nTileYPos);
    if (statIndex == 6)
        return static_cast<uint32_t>(drlgRoom->nTileWidth);
    if (statIndex == 7)
        return static_cast<uint32_t>(drlgRoom->nTileHeight);
    RoomDataGuard guard(drlgRoom);
    auto* activeRoom = drlgRoom->pRoom;
    if (activeRoom == nullptr)
        return 0;
    switch (statIndex) {
        case 0:
            return static_cast<uint32_t>(activeRoom->dwXStart);
        case 1:
            return static_cast<uint32_t>(activeRoom->dwYStart);
        case 2:
            return static_cast<uint32_t>(activeRoom->dwXSize);
        case 3:
            return static_cast<uint32_t>(activeRoom->dwYSize);
        default:
            break;
    }
    auto* coll = activeRoom->pCollisionGrid;
    if (coll == nullptr)
        return 0;
    switch (statIndex) {
        case 9:
            return static_cast<uint32_t>(coll->pRoomCoords.nSubtileX);
        case 10:
            return static_cast<uint32_t>(coll->pRoomCoords.nSubtileY);
        case 11:
            return static_cast<uint32_t>(coll->pRoomCoords.nSubtileWidth);
        case 12:
            return static_cast<uint32_t>(coll->pRoomCoords.nSubtileHeight);
        case 13:
            return static_cast<uint32_t>(coll->pRoomCoords.nTileXPos);
        case 14:
            // index 14 = nTileYPos (the Y twin of index 13's nTileXPos); the reference
            // erroneously returned nSubtileY here, duplicating index 10's game-coord Y.
            return static_cast<uint32_t>(coll->pRoomCoords.nTileYPos);
        case 15:
            return static_cast<uint32_t>(coll->pRoomCoords.nTileWidth);
        case 16:
            return static_cast<uint32_t>(coll->pRoomCoords.nTileHeight);
        default:
            return 0;
    }
}

bool Room::Reveal(bool drawPresets) const {
    auto* drlgRoom = AsDrlgRoom(ResolvePtr());
    if (drlgRoom == nullptr || drlgRoom->pLevel == nullptr)
        return false;
    RoomDataGuard guard(drlgRoom);
    auto* activeRoom = drlgRoom->pRoom;
    if (activeRoom == nullptr)
        return false;
    auto* player = imports::d2client::UNITS_GetPlayerUnit();
    if (player == nullptr)
        return false;

    // Reveal mutates layer state (`*AutomapLayer`, `RevealAutomapRoom`,
    // `AddAutomapCell`), all of which the engine touches each frame on
    // the game thread. Marshal the entire body through GameThread::Execute
    // so the writes interleave atomically with the engine's frame work.
    // Reference (Room.cpp:14) wraps the same code in an AutoCriticalRoom.
    return GameThread::Execute([activeRoom, drlgRoom, player, drawPresets]() -> bool {
        // If the room being revealed is on a different level than the
        // player's current automap layer, switch layers around the reveal
        // call. The dynamic-path room pointer is typed as D2MOO's
        // `::D2ActiveRoomStrc*`, but the bytes follow 1.14d's
        // `extras::D2ActiveRoomStrc` layout, so we reinterpret at the path
        // boundary.
        const uint32_t targetLevel = static_cast<uint32_t>(drlgRoom->pLevel->nLevelId);
        uint32_t playerLevelNo = 0U;
        bool switched = false;
        auto* pathRoom = (player->pDynamicPath != nullptr)
                             ? reinterpret_cast<D2ActiveRoomStrc*>(player->pDynamicPath->pRoom)
                             : nullptr;
        if (pathRoom != nullptr && pathRoom->pDrlgRoom != nullptr && pathRoom->pDrlgRoom->pLevel != nullptr &&
            static_cast<uint32_t>(pathRoom->pDrlgRoom->pLevel->nLevelId) != targetLevel) {
            playerLevelNo = static_cast<uint32_t>(pathRoom->pDrlgRoom->pLevel->nLevelId);
            *imports::d2client::gpAutomapLayer = asm_thunks::InitAutomapLayerForLevel(targetLevel);
            switched = true;
        }
        imports::d2client::AUTOMAP_RevealRoom(activeRoom, /*dwClipFlag=*/1U, *imports::d2client::gpAutomapLayer);
        if (drawPresets) {
            DrawPresetsForRoom(drlgRoom);
        }
        if (switched) {
            asm_thunks::InitAutomapLayerForLevel(playerLevelNo);
        }
        return true;
    });
}

std::optional<Room> Room::Find(uint32_t level, Position pos) {
    if (level == 0)
        return std::nullopt;
    Room candidate(level, pos);
    if (!candidate)
        return std::nullopt;
    return candidate;
}

bool Room::operator==(const Room& other) const {
    auto* lhs = ResolvePtr();
    auto* rhs = other.ResolvePtr();
    return lhs != nullptr && lhs == rhs;
}

}  // namespace d2bs::game
