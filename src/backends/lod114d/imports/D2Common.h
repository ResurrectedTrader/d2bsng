#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include "extras/D2ActiveRoomStrc.h"
#include "extras/D2DrlgActStrc.h"
#include "extras/D2DrlgLevelStrc.h"
#include "extras/D2DrlgStrc.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <D2BitManip.h>            // D2BitBufferStrc
#include <D2Chat.h>                // D2HoverTextStrc
#include <D2Constants.h>           // D2C_Difficulties, D2C_TransactionTypes
#include <D2Inventory.h>           // D2InventoryStrc
#include <D2Skills.h>              // D2SkillStrc
#include <D2StatList.h>            // D2StatListExStrc, D2StatStrc
#include <D2Waypoints.h>           // D2WaypointDataStrc
#include <DataTbls/ItemsTbls.h>    // D2ItemsTxt
#include <DataTbls/LevelsTbls.h>   // D2LevelsTxt
#include <DataTbls/ObjectsTbls.h>  // D2ObjectsTxt
#include <Units/Units.h>           // D2UnitStrc
#pragma clang diagnostic pop

#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h. Names mirror the D2MOO ordinal
// exports listed in `dependencies/D2MOO/source/D2Common/definitions/`.

// D2GameStrc is forward-declared by D2MOO and only appears as an opaque
// pointer parameter in ITEMS_GetAllRepairCosts; we don't need its definition
// to type the import.
struct D2GameStrc;

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::d2common {

// 1.14d-correct shadow types live in `imports::extras`. Aliasing them inside
// this namespace lets call-sites use `d2common::DUNGEON_GetLevelIdFromRoom`
// etc. with the correct 1.14d layouts without further qualification.
using extras::D2ActiveRoomStrc;
using extras::D2DrlgActStrc;
using extras::D2DrlgLevelStrc;
using extras::D2DrlgStrc;

// ---- Functions -------------------------------------------------------------
inline StdcallFunc<void(D2DrlgLevelStrc* /*pLevel*/)> DRLG_InitLevel{0x2424A0};
inline StdcallFunc<D2ObjectsTxt*(uint32_t /*nObjectId*/)> DATATBLS_GetObjectsTxtRecord{0x240E90};

inline StdcallFunc<D2LevelsTxt*(uint32_t /*nLevelId*/)> DATATBLS_GetLevelsTxtRecord{0x21DB70};
inline StdcallFunc<D2ItemsTxt*(uint32_t /*nItemId*/)> DATATBLS_GetItemsTxtRecord{0x2335F0};

// 1.14d-only entry: returns the per-level "misc/layer" structure indexed by
// nLevelNo. No matching D2MOO ordinal; named under the DRLG_ domain to match
// the surrounding dungeon-structure helpers.
inline FastcallFunc<void*(uint32_t /*nLevelId*/)> DRLG_GetLayer{0x21E470};
inline FastcallFunc<D2DrlgLevelStrc*(D2DrlgStrc* /*pDrlg*/, int32_t /*nLevelId*/)> DRLG_GetLevel{0x242AE0};

// MOO: STATLIST_GetStatListFromUnitStateAndFlag(D2UnitStrc* pUnit, int nState, int nFlag).
inline StdcallFunc<D2StatListExStrc*(D2UnitStrc* /*pUnit*/, int32_t /*nState*/, int32_t /*nFlag*/)>
    STATLIST_GetStatListFromUnitStateAndFlag{0x2257D0};
inline StdcallFunc<int32_t(D2StatListExStrc* /*pStatList*/, D2StatStrc* /*pStatArray*/, size_t /*nMaxEntries*/)>
    STATLIST_CopyStats{0x225C90};
// MOO: STATLIST_UnitGetStatValue(const D2UnitStrc* pUnit, int nStatId, uint16_t dwLayer).
// On 1.14d the body reads `dwLayer` but doesn't act on it; the stdcall slot is
// still required to match the ABI.
inline StdcallFunc<int32_t(D2UnitStrc* /*pUnit*/, uint32_t /*dwStat*/, uint32_t /*dwLayer*/)> STATLIST_UnitGetStatValue{
    0x225480};
inline StdcallFunc<int32_t(D2UnitStrc* /*pUnit*/, uint32_t /*dwStateNo*/)> STATES_CheckState{0x239DF0};

inline StdcallFunc<int32_t(D2UnitStrc* /*pUnit1*/, D2UnitStrc* /*pUnit2*/, uint32_t /*nCollisionMask*/)>
    UNITS_TestCollisionWithUnit{0x222AA0};
inline StdcallFunc<D2ActiveRoomStrc*(D2UnitStrc* /*pUnit*/)> UNITS_GetRoom{0x220BB0};

// Reference d2bs declared these as D2CLIENT FUNCPTRs (the JS API calls them from
// the client) but they are D2COMMON exports per MOO: in 1.14d's monolithic
// Game.exe they live at these client-relative offsets, but the underlying
// implementation is the same UNITS_* helper that MOO ships in D2Common. The
// reference d2bs signatures are kept (__fastcall, uint32_t return where MOO
// has int) so existing callers don't need re-typing.
inline FastcallFunc<uint32_t(uint32_t /*dwUnitId*/)> UNITS_GetCurrentLifePercentage{0x79080};
inline FastcallFunc<uint32_t(D2UnitStrc* /*pUnit*/)> UNITS_GetClientCoordX{0x5ADF0};
inline FastcallFunc<uint32_t(D2UnitStrc* /*pUnit*/)> UNITS_GetClientCoordY{0x5AE20};

// Reference d2bs already tagged this as D2COMMON; MOO exposes MISSILE_SetOwner
// (@11129) and MISSILE_CheckUnitIfOwner (@11130) but no matching getter, so
// the entry takes the MISSILE_ domain prefix without a direct MOO ordinal.
inline FastcallFunc<D2UnitStrc*(D2UnitStrc* /*pMissile*/)> MISSILE_GetOwnerUnit{0x639D0};

inline StdcallFunc<uint32_t(D2UnitStrc* /*pUnit*/, D2SkillStrc* /*pSkill*/, bool /*bBonus*/)> SKILLS_GetSkillLevel{
    0x2442A0};

inline StdcallFunc<int32_t(D2UnitStrc* /*pItem*/, D2UnitStrc* /*pUnit*/)> ITEMS_GetLevelRequirement{0x22BA60};

inline StdcallFunc<int32_t(D2UnitStrc* /*pPlayer*/, D2UnitStrc* /*pItem*/, D2C_Difficulties /*nDifficulty*/,
                           D2BitBufferStrc* /*pQuestFlags*/, uint32_t /*nVendorId*/,
                           D2C_TransactionTypes /*nTransactionType*/)>
    ITEMS_GetTransactionCost{0x22FDC0};
inline StdcallFunc<int32_t(D2GameStrc* /*pGame*/, D2UnitStrc* /*pUnit*/, uint32_t /*nNpcId*/,
                           D2C_Difficulties /*nDifficulty*/, D2BitBufferStrc* /*pQuestFlags*/,
                           void(__fastcall* /*pfCallback*/)(D2GameStrc*, D2UnitStrc*, D2UnitStrc*))>
    ITEMS_GetAllRepairCosts{0x22FE60};

// 1.14d-only entry: returns the localised name string for a magic-affix table
// index (wMagicPrefix[i] / wMagicSuffix[i]). No matching D2MOO ordinal: D2MOO
// exposes the underlying D2MagicAffixTxt records via DATATBLS_GetMagicAffixTxtRecord
// but not this name-lookup helper.
inline StdcallFunc<char*(uint16_t /*wAffixIndex*/)> ITEMS_GetMagicalMods{0x233EE0};
inline StdcallFunc<D2UnitStrc*(D2InventoryStrc* /*pInventory*/)> INVENTORY_GetFirstItem{0x23B2C0};
inline StdcallFunc<D2UnitStrc*(D2UnitStrc* /*pItem*/)> INVENTORY_GetNextItem{0x23DFA0};

// Reference d2bs declared this as a D2CLIENT FUNCPTR; per MOO it is a D2COMMON
// export. The 1.14d entry is a thin no-arg wrapper that fetches the active
// inventory implicitly (MOO's signature takes a D2InventoryStrc*); reference's
// __fastcall void shape is preserved here.
inline FastcallFunc<D2UnitStrc*()> INVENTORY_GetCursorItem{0x680A0};

inline StdcallFunc<D2HoverTextStrc*(void* /*pMemPool*/, const char* /*szText*/, uint32_t /*nTimeout*/)>
    CHAT_AllocHoverMsg{0x261110};

// Mark/unmark client-visibility tracking for a room. d2bs's walkability-sweep
// calls these to force the game to populate a room's collision data without
// actually drawing it. Reference d2bs names them AddRoomData / RemoveRoomData
// after that use; the MOO names describe the underlying behavior.
inline StdcallFunc<void(D2DrlgActStrc* /*pAct*/, int32_t /*nLevelId*/, int32_t /*nX*/, int32_t /*nY*/,
                        D2ActiveRoomStrc* /*pRoom*/)>
    DUNGEON_SetClientIsInSight{0x21A070};
inline StdcallFunc<void(D2DrlgActStrc* /*pAct*/, int32_t /*nLevelId*/, int32_t /*nX*/, int32_t /*nY*/,
                        D2ActiveRoomStrc* /*pRoom*/)>
    DUNGEON_UnsetClientIsInSight{0x21A0C0};

// Semantically matches MOO's QUESTRECORD_GetQuestState shape (BitBuffer + two
// ints). 1.14d encodes (quest, flag) into the (nQuest, nState) pair the
// QUESTRECORD_ family uses elsewhere, but the offset is distinct from
// QUESTRECORD_GetQuestState in 1.10f-MOO, so this entry takes its own
// QUESTRECORD_ name rather than aliasing the MOO ordinal.
inline StdcallFunc<int32_t(D2BitBufferStrc* /*pQuestRecord*/, uint32_t /*dwQuest*/, uint32_t /*dwFlag*/)>
    QUESTRECORD_GetQuestFlag{0x25C310};

// Coordinate transforms: the in/out args are two contiguous int32_t cells (X
// then Y) on the caller's stack frame. Reference passes the address of each as
// a separate stdcall arg; passing a `Point*` would change the ABI (stdcall is
// position-based, not struct-coalescing), so the two-pointer shape stays.
inline StdcallFunc<void(int32_t* /*pX*/, int32_t* /*pY*/)> AUTOMAP_MapToAbsScreen{0x243260};
inline StdcallFunc<void(int32_t* /*pX*/, int32_t* /*pY*/)> AUTOMAP_AbsScreenToMap{0x243510};

inline StdcallFunc<int32_t(D2WaypointDataStrc* /*pData*/, uint16_t /*wField*/)> WAYPOINTS_IsActivated{0x260E50};

inline StdcallFunc<bool(uint32_t /*nLevelId*/)> DUNGEON_IsTownLevelId{0x21AAF0};
inline StdcallFunc<int32_t(D2ActiveRoomStrc* /*pRoom*/)> DUNGEON_GetLevelIdFromRoom{0x21A1B0};

// ---- Variables -------------------------------------------------------------
// MOO export name `sgptDataTables` (#10042); base pointer for the global data
// table the game decodes the .txt files into.
inline GameVar<uint8_t*> sgptDataTables{0x344304};

}  // namespace d2bs::imports::d2common
// NOLINTEND(readability-identifier-naming)
