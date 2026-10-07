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
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace d2bs::game {

using lod114d::imports::extras::D2ActiveRoomStrc;
using lod114d::imports::extras::D2DrlgRoomStrc;

static_assert(std::to_underlying(CollisionFlag::Wall) == COLLIDE_WALL);
static_assert(std::to_underlying(CollisionFlag::Visible) == COLLIDE_VISIBLE);
static_assert(std::to_underlying(CollisionFlag::NoPlayer) == COLLIDE_NOPLAYER);
static_assert(std::to_underlying(CollisionFlag::Corpse) == COLLIDE_CORPSE);
static_assert(std::to_underlying(CollisionFlag::Water) == COLLIDE_WATER);
static_assert(std::to_underlying(CollisionFlag::MaskInvalid) == COLLIDE_MASK_INVALID);
static_assert(std::to_underlying(CollisionFlag::MaskPlayerPath) == COLLIDE_MASK_PLAYER_PATH);
static_assert(std::to_underlying(CollisionFlag::MaskPlayerFlying) == COLLIDE_MASK_PLAYER_FLYING);
static_assert(std::to_underlying(CollisionFlag::MaskPlayerWhirlwind) == COLLIDE_MASK_PLAYER_WW);
static_assert(std::to_underlying(CollisionFlag::MaskRadialBarrier) == COLLIDE_MASK_RADIAL_BARRIER);
static_assert(std::to_underlying(CollisionFlag::MaskFlyingUnit) == COLLIDE_MASK_FLYING_UNIT);
static_assert(std::to_underlying(CollisionFlag::MaskMonsterThatCanOpenDoors) ==
              COLLIDE_MASK_MONSTER_THAT_CAN_OPEN_DOORS);
static_assert(std::to_underlying(CollisionFlag::MaskMonsterMissile) == COLLIDE_MASK_MONSTER_MISSILE);
static_assert(std::to_underlying(CollisionFlag::MaskMonsterPath) == COLLIDE_MASK_MONSTER_PATH);
static_assert(std::to_underlying(CollisionFlag::MaskDoorBlockVisibility) == COLLIDE_MASK_DOOR_BLOCK_VIS);
static_assert(std::to_underlying(CollisionFlag::MaskBlocksDoor) == COLLIDE_MASK_BLOCKS_DOOR);
static_assert(std::to_underlying(CollisionFlag::MaskPlacement) == COLLIDE_MASK_PLACEMENT);
static_assert(sizeof(CollisionFlag) == sizeof(uint16_t));

namespace {

// D2MOO types the grid as raw words; converted on the copy rather than read through
// an enum pointer.
std::vector<CollisionFlag> CopyCells(const uint16_t* cells, size_t count) {
    return std::span(cells, count) |
           std::views::transform([](uint16_t cell) { return static_cast<CollisionFlag>(cell); }) |
           std::ranges::to<std::vector>();
}

inline D2DrlgRoomStrc* AsDrlgRoom(void* p) noexcept {
    return static_cast<D2DrlgRoomStrc*>(p);
}

}  // namespace

void* Room::ResolvePtr() const {
    assert(IsGameLockHeld() && "resolve under the game lock - use Resolve<T>()");
    if (level_ == game::LevelId::None)
        return nullptr;
    if (auto* cached = cache_.Get())
        return cached;
    auto* lvl = FindLevelInChain(level_);
    void* resolved = FindRoomInLevelByPos(lvl, pos_);
    cache_.Set(resolved);
    return resolved;
}

Room::operator bool() const {
    GameReadLock guard;
    return ResolvePtr() != nullptr;
}

Room Room::FromPtr(void* p) {
    if (p == nullptr)
        return Room();
    auto* drlgRoom = AsDrlgRoom(p);
    if (drlgRoom->pLevel == nullptr)
        return Room();
    Room handle(static_cast<game::LevelId>(drlgRoom->pLevel->nLevelId),
                {.x = static_cast<uint32_t>(drlgRoom->nTileXPos), .y = static_cast<uint32_t>(drlgRoom->nTileYPos)});
    handle.cache_.Set(p);
    return handle;
}

int32_t Room::Number() const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom)
        return 0;
    if (drlgRoom->nType != DRLGTYPE_PRESET || drlgRoom->pMaze == nullptr)
        return -1;
    // Byte 0x00 of the preset struct = reference's `dwRoomNumber` =
    // D2MOO's `nLevelPrest` (same byte, different name).
    return drlgRoom->pMaze->nLevelPrest;
}

int32_t Room::SubNumber() const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom)
        return 0;
    if (drlgRoom->nType != DRLGTYPE_PRESET || drlgRoom->pMaze == nullptr || drlgRoom->pMaze->pMap == nullptr)
        return -1;
    // Reference reads `*pType2Info->pdwSubNumber` (a DWORD* at byte 0x08
    // dereferenced) - that's the first DWORD of D2MOO's pMap target,
    // which is `D2DrlgMapStrc::nLevelPrest`.
    return drlgRoom->pMaze->pMap->nLevelPrest;
}

Rect Room::Bounds() const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom)
        return Rect::Zero;
    return {
        .origin = {.x = static_cast<uint32_t>(drlgRoom->nTileXPos * SUBTILE_SCALE),
                   .y = static_cast<uint32_t>(drlgRoom->nTileYPos * SUBTILE_SCALE)},
        .size = {.width = static_cast<uint32_t>(drlgRoom->nTileWidth * SUBTILE_SCALE),
                 .height = static_cast<uint32_t>(drlgRoom->nTileHeight * SUBTILE_SCALE)},
    };
}

LevelId Room::CorrectTomb() const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom || drlgRoom->pLevel == nullptr || drlgRoom->pLevel->pDrlg == nullptr)
        return game::LevelId::None;
    return static_cast<game::LevelId>(drlgRoom->pLevel->pDrlg->nStaffTombLevel);
}

std::vector<std::vector<CollisionFlag>> Room::GetCollision() const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom) {
        return {};
    }
    RoomDataGuard roomData(drlgRoom);
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
    std::vector<std::vector<CollisionFlag>> out;
    out.reserve(height);
    const uint16_t* p = grid->pCollisionMask;
    for (uint32_t j = 0; j < height; ++j) {
        out.push_back(CopyCells(p, width));
        p += width;
    }
    return out;
}

std::vector<CollisionFlag> Room::GetCollisionFlat() const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom) {
        return {};
    }
    RoomDataGuard roomData(drlgRoom);
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
    return CopyCells(grid->pCollisionMask, count);
}

CollisionFlag Room::CollisionAt(Position pos) const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom) {
        return CollisionFlag::None;
    }
    RoomDataGuard roomData(drlgRoom);
    auto* activeRoom = drlgRoom->pRoom;
    if (activeRoom == nullptr || activeRoom->pCollisionGrid == nullptr) {
        return CollisionFlag::None;
    }
    auto* grid = activeRoom->pCollisionGrid;
    if (grid->pCollisionMask == nullptr) {
        return CollisionFlag::None;
    }
    // Offsets against the grid's own origin, which bounds the position before the unsigned
    // subtraction. Matches D2's own indexing (D2Collision.cpp) and the reference's.
    const auto originX = static_cast<uint32_t>(grid->pRoomCoords.nSubtileX);
    const auto originY = static_cast<uint32_t>(grid->pRoomCoords.nSubtileY);
    const auto width = static_cast<uint32_t>(grid->pRoomCoords.nSubtileWidth);
    const auto height = static_cast<uint32_t>(grid->pRoomCoords.nSubtileHeight);
    if (pos.x < originX || pos.y < originY || pos.x >= originX + width || pos.y >= originY + height) {
        return CollisionFlag::None;
    }
    const uint32_t index = ((pos.y - originY) * width) + (pos.x - originX);
    return static_cast<CollisionFlag>(grid->pCollisionMask[index]);
}

Room Room::GetNext() const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom)
        return Room();
    return FromPtr(drlgRoom->pDrlgRoomNext);
}

Room Room::GetFirst() const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom || drlgRoom->pLevel == nullptr)
        return Room();
    return FromPtr(drlgRoom->pLevel->pFirstRoomEx);
}

std::vector<Room> Room::GetNearby() const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom || drlgRoom->ppRoomsNear == nullptr)
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
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom || drlgRoom->pLevel == nullptr)
        return Level();
    return Level(static_cast<game::LevelId>(drlgRoom->pLevel->nLevelId));
}

std::optional<Unit> Room::GetFirstUnit() const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom || drlgRoom->pRoom == nullptr || drlgRoom->pRoom->pUnitFirst == nullptr)
        return std::nullopt;
    return Unit::FromPtr(drlgRoom->pRoom->pUnitFirst);
}

std::vector<PresetUnitInfo> Room::GetPresetUnits(std::optional<UnitType> type, std::optional<uint32_t> classId) const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom)
        return {};
    RoomDataGuard roomData(drlgRoom);
    const Position roomPos{.x = static_cast<uint32_t>(drlgRoom->nTileXPos),
                           .y = static_cast<uint32_t>(drlgRoom->nTileYPos)};

    // For UNIT_TILE presets the destination level lives on DrlgRoom::pRoomTiles
    // - each warp entry has a pointer-to-DWORD tile id that, when matching
    // a preset's nIndex, names the target DrlgRoom (and via it, the target
    // level). Walked here so framework callers see the destination as a
    // populated field on PresetUnitInfo without needing a second boundary
    // call. Reference: D2Helpers.cpp::GetTileLevelNo.
    auto resolveTileTarget = [&drlgRoom](uint32_t presetTileId) -> game::LevelId {
        for (auto* warp = drlgRoom->pRoomTiles; warp != nullptr; warp = warp->pNext) {
            if (warp->pPresetTileId == nullptr || warp->pDrlgRoom == nullptr || warp->pDrlgRoom->pLevel == nullptr) {
                continue;
            }
            if (*warp->pPresetTileId == presetTileId) {
                return static_cast<game::LevelId>(warp->pDrlgRoom->pLevel->nLevelId);
            }
        }
        return game::LevelId::None;
    };

    std::vector<PresetUnitInfo> out;
    for (auto* preset = drlgRoom->pPresetUnits; preset != nullptr; preset = preset->pNext) {
        const auto presetType = preset->nUnitType;
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
            .level = game::LevelId::None,
            .tileTargetLevelId = presetType == UnitType::Tile ? resolveTileTarget(presetIndex) : game::LevelId::None,
        });
    }
    return out;
}

uint32_t Room::GetStat(uint32_t statIndex) const {
    const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
    if (!drlgRoom)
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
    RoomDataGuard roomData(drlgRoom);
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
    {
        const auto drlgRoom = Resolve<D2DrlgRoomStrc>();
        if (!drlgRoom || drlgRoom->pLevel == nullptr || lod114d::imports::d2client::UNITS_GetPlayerUnit() == nullptr)
            return false;
    }

    // Reveal mutates layer state (`*AutomapLayer`, `RevealAutomapRoom`,
    // `AddAutomapCell`), all of which the engine touches each frame on
    // the game thread. Marshal the entire body through GameThread::Execute
    // so the writes interleave atomically with the engine's frame work.
    // Reference (Room.cpp:14) wraps the same code in an AutoCriticalRoom.
    // The room is re-resolved there: a pointer resolved here goes stale while Execute waits.
    return GameThread::Execute([self = *this, drawPresets]() -> bool {
        const auto drlgRoom = self.Resolve<D2DrlgRoomStrc>();
        if (!drlgRoom || drlgRoom->pLevel == nullptr)
            return false;
        RoomDataGuard roomData(drlgRoom);
        auto* activeRoom = drlgRoom->pRoom;
        if (activeRoom == nullptr)
            return false;
        auto* player = lod114d::imports::d2client::UNITS_GetPlayerUnit();
        if (player == nullptr)
            return false;
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
            *lod114d::imports::d2client::gpAutomapLayer = lod114d::asm_thunks::InitAutomapLayerForLevel(targetLevel);
            switched = true;
        }
        lod114d::imports::d2client::AUTOMAP_RevealRoom(activeRoom, /*dwClipFlag=*/1U,
                                                       *lod114d::imports::d2client::gpAutomapLayer);
        if (drawPresets) {
            DrawPresetsForRoom(drlgRoom);
        }
        if (switched) {
            lod114d::asm_thunks::InitAutomapLayerForLevel(playerLevelNo);
        }
        return true;
    });
}

bool Room::operator==(const Room& other) const {
    const auto lhs = Resolve<D2DrlgRoomStrc>();
    const auto rhs = other.Resolve<D2DrlgRoomStrc>();
    return lhs && lhs == rhs;
}

}  // namespace d2bs::game
