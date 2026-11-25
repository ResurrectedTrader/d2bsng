#pragma once

// Packet-only enumerations: values that appear in a packet field and nowhere in the contract.

#include <cstdint>

// ReSharper disable once CppUnusedIncludeDirective - EnumName / format_as for the enumerations below
#include "Lod114dEnumNames.h"

namespace d2bs::lod114d::packets {

// What a server item packet (0x9C / 0x9D) does with its item, as D2MOO's D2C_SRV2CLT9C_9D_PARAMS.
enum class ItemAction : uint8_t {
    AddToGround,
    GroundToCursor,
    DropToGround,
    OnGround,
    PutInContainer,
    RemoveFromContainer,
    Equip,
    IndirectlySwapBodyItem,
    Unequip,
    SwapBodyItem,
    AddQuantity,
    AddToShop,
    RemoveFromShop,
    SwapInContainer,
    PutInBelt,
    RemoveFromBelt,
    SwapInBelt,
    AutoUnequip,
    ToCursor,
    ItemInSocket,
    Unknown0x14,
    UpdateStats,
    Unknown0x16,
    WeaponSwitch,
};

// The type of a server 0x5A event message, as D2MOO's D2C_SRV2CLT5A_TYPES. 0x13 and up carry other layouts (skill
// and damage notices), which the packet prints as bytes.
enum class EventMessageType : uint8_t {
    Disconnect,
    DropError,
    PlayerJoin,
    PlayerLeft,
    NotInGame,
    NotLoggedIn,
    SlainBy,
    PvpStatus,
    PlayerIsBusy,
    PleaseWait,
    HasItemsInBox,
    Unused1,
    Unused2,
    NotListeningToYou,
    CantDoThat,
    RealmGoingDown,
    PleaseWaitHostile,
    JordanStones,
    DiabloClone,
};

// What a server 0x77 asks the client's UI to do, as D2MOO's D2C_SRV2CLT77_PARAMS.
enum class UiAction : uint8_t {
    AskToTrade = 0,
    AskedToTrade = 1,
    AcceptTrade = 5,
    OpenTrade = 6,
    SellerNoRoom = 9,
    BuyerNoRoom = 10,
    CloseTrade = 12,
    TradeDone = 13,
    CheckBox = 14,
    NormalCheckBox = 15,
    OpenStash = 16,
    CloseStash = 17,
    OpenCube = 21,
};

}  // namespace d2bs::lod114d::packets
