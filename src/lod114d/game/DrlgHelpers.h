#pragma once

#include <cstdint>

#include "RoomData.h"
#include "game/GameThread.h"
#include "game/Types.h"
#include "imports/D2Client.h"
#include "imports/D2Common.h"
#include "imports/extras/D2DrlgActStrc.h"
#include "imports/extras/D2DrlgLevelStrc.h"
#include "imports/extras/D2DrlgRoomStrc.h"
#include "imports/extras/D2PresetUnitStrc.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <DataTbls/ObjectsTbls.h>  // D2ObjectsTxt
#include <Units/Units.h>           // D2UnitStrc
#pragma clang diagnostic pop

namespace d2bs::game {

// Shared drlg-chain walkers. Inline so each TU copies after LTO; no link-time conflicts.

// Walk the player's drlg level chain looking for the requested id, lazily
// initialising any matched level whose room list hasn't been populated yet.
// D2COMMON_GetLevel(pDrlg, levelNo) is intentionally NOT used - the entry
// point at 0x242AE0 has been observed to crash 1.14d in this exact path.
//
// `InitLevel` mutates the level's room chain - must run on the game thread
// to avoid TOCTOU races with concurrent script readers and the engine's
// frame work.
//
// `D2UnitStrc::pDrlgAct` is typed as D2MOO's `::D2DrlgActStrc*`, but the bytes
// at that address follow the 1.14d allocation modeled by
// `extras::D2DrlgActStrc`. Reinterpret at the unit boundary.
inline d2bs::imports::extras::D2DrlgLevelStrc* FindLevelInChain(uint32_t levelId) {
    auto* player = imports::d2client::UNITS_GetPlayerUnit();
    if (player == nullptr) {
        return nullptr;
    }
    auto* act = reinterpret_cast<d2bs::imports::extras::D2DrlgActStrc*>(player->pDrlgAct);
    if (act == nullptr || act->pDrlg == nullptr) {
        return nullptr;
    }
    for (auto* level = act->pDrlg->pLevel; level != nullptr; level = level->pNextLevel) {
        if (static_cast<uint32_t>(level->nLevelId) != levelId) {
            continue;
        }
        if (level->pFirstRoomEx == nullptr) {
            GameThread::Execute([level] {
                if (level->pFirstRoomEx == nullptr) {
                    imports::d2common::DRLG_InitLevel(level);
                }
            });
        }
        return level;
    }
    return nullptr;
}

// Walk a level's room chain matching the (subtile) position stored on the
// Room handle. Both sides compare nTileXPos/nTileYPos directly - these are
// the same units that Room::FromPtr extracts.
inline d2bs::imports::extras::D2DrlgRoomStrc* FindRoomInLevelByPos(d2bs::imports::extras::D2DrlgLevelStrc* lvl,
                                                                   Position pos) {
    if (lvl == nullptr) {
        return nullptr;
    }
    for (auto* r = lvl->pFirstRoomEx; r != nullptr; r = r->pDrlgRoomNext) {
        if (static_cast<uint32_t>(r->nTileXPos) == pos.x && static_cast<uint32_t>(r->nTileYPos) == pos.y) {
            return r;
        }
    }
    return nullptr;
}

// Reference: Room.cpp:54-110 DrawPresets. Walks pPresetUnits and overlays
// special automap cells per (type, txtFileNo, level) - special NPC icons,
// the Lower Kurast uberchest hint, the Anya/Frozen River markers, etc.
// Returns the chosen cell number for `preset`, or -1 to skip.
inline int32_t PickPresetCellNo(const d2bs::imports::extras::D2PresetUnitStrc* preset, uint32_t levelNo) {
    int32_t cell = -1;
    if (preset->nUnitType == 1) {  // Special NPCs
        if (preset->nIndex == 256) {
            cell = 300;  // Izzy
        } else if (preset->nIndex == 745) {
            cell = 745;  // Hephasto
        }
    } else if (preset->nUnitType == 2) {  // Objects
        if (preset->nIndex == 580 && levelNo == 79) {
            cell = 318;  // Lower Kurast uberchest hint
        } else if (preset->nIndex == 371) {
            cell = 301;  // Countess Chest
        } else if (preset->nIndex == 152) {
            cell = 300;  // A2 Orifice
        } else if (preset->nIndex == 460) {
            cell = 1468;  // Frozen Anya
        } else if (preset->nIndex == 402 && levelNo == 46) {
            cell = 0;  // Canyon / Arcane waypoint
        } else if (preset->nIndex == 267 && levelNo != 75 && levelNo != 103) {
            cell = 0;
        } else if (preset->nIndex == 376 && levelNo == 107) {
            cell = 376;
        } else if (preset->nIndex > 574) {
            cell = 0;
        }

        if (cell == -1) {
            auto* obj = imports::d2common::DATATBLS_GetObjectsTxtRecord(preset->nIndex);
            if (obj != nullptr) {
                cell = static_cast<int32_t>(obj->dwAutomap);
            }
        }
    }
    return cell;
}

// Overlay preset markers for `drlgRoom` onto the currently-active automap
// layer. Caller is responsible for ensuring the layer is the right one for
// `drlgRoom->pLevel` and that this runs on the game thread (the engine
// touches AutomapLayer / NewAutomapCell / AddAutomapCell each frame).
inline void DrawPresetsForRoom(d2bs::imports::extras::D2DrlgRoomStrc* drlgRoom) {
    if (drlgRoom == nullptr || drlgRoom->pLevel == nullptr) {
        return;
    }
    auto* layer = *imports::d2client::gpAutomapLayer;
    if (layer == nullptr) {
        return;
    }
    const uint32_t levelNo = static_cast<uint32_t>(drlgRoom->pLevel->nLevelId);
    const int32_t roomPosX = drlgRoom->nTileXPos;
    const int32_t roomPosY = drlgRoom->nTileYPos;
    for (auto* preset = drlgRoom->pPresetUnits; preset != nullptr; preset = preset->pNext) {
        const int32_t cell = PickPresetCellNo(preset, levelNo);
        if (cell <= 0 || cell >= 1258) {
            continue;
        }
        auto* automapCell = imports::d2client::AUTOMAP_NewCell();
        if (automapCell == nullptr) {
            continue;
        }
        automapCell->nCellNo = static_cast<uint16_t>(cell);
        const int32_t pX = preset->nXpos + (roomPosX * 5);
        const int32_t pY = preset->nYpos + (roomPosY * 5);
        automapCell->xPixel = static_cast<uint16_t>((((pX - pY) * 16) / 10) + 1);
        automapCell->yPixel = static_cast<uint16_t>((((pY + pX) * 8) / 10) - 3);
        imports::d2client::AUTOMAP_AddCell(automapCell, &layer->pObjects);
    }
}

}  // namespace d2bs::game
