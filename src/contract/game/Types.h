#pragma once

// Shared POD/value types crossing the framework/game boundary. Stdlib-only (plus the generated enum names) so
// components, pathfinding, tests, and game implementations can all share them without heavier includes.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

// ReSharper disable once CppUnusedIncludeDirective - EnumName / format_as for the enumerations below
#include "ContractEnumNames.h"

namespace d2bs::game {

// ============================================================================
// Click / UI
// ============================================================================

enum class ClickButton : uint8_t {
    Left = 0,
    Right = 1,
    ShiftLeft = 2,
    ShiftRight = 3,
    Mercenary = 4,
};

// Which weapon slot a skill is bound to.
enum class Hand : uint8_t {
    Right = 0,
    Left = 1,
};

// Transition direction for a key event.
enum class KeyState : uint8_t {
    Down = 0,
    Up = 1,
};

// Owner of the inventory we're interacting with.
enum class InventoryOwner : uint8_t {
    Player = 0,
    Mercenary = 1,
};

enum class ClickResult : uint8_t {
    Dispatched,             // Underlying game action was invoked.
    TransactionInProgress,  // Blocked by an open TransactionDialog* flag.
    InvalidTarget,          // Slot out of range, no click fn, bad belt cell, etc.
    NotAnItem,              // Passed unit is not a UNIT_ITEM (ClickItem only).
    StashTabUnavailable,    // Unknown stash tab, or the backend could not make it active in time.
};

// Control type values from reference/d2bs/Constants.h.
enum class ControlType : uint32_t {
    Unknown = 0,
    EditBox = 1,
    Image = 2,
    Unused = 3,
    TextBox = 4,
    ScrollBar = 5,
    Button = 6,
    List = 7,
};

// Password field marker for D2WinControlStrc::dwIsCloaked.
constexpr uint32_t CONTROL_CLOAKED_PASSWORD = 33;

// A control's raw state word (1.14d's D2WinControlStrc::dwState; Control.disabled).
// The JS `state` property is this minus Normal, so scripts see 0-3 there.
enum class ControlState : uint32_t {
    Hidden = 0,  // e.g. character-create OK before a class is picked
    Normal = 2,
    Disabled = 4,              // a greyed-out button, an unavailable Battle.net difficulty
    Active = 5,                // an edit box with focus, a ticked checkbox, a clickable button
    DifficultyEnabled = 0x0D,  // a selectable single-player difficulty button
};

// 1.14d's UI panel-state ids (getUIFlag). Values follow D2MOO's D2C_UIvars; D2R
// renumbered its own panel table.
enum class UiFlag : uint32_t {
    Game = 0x00,
    Inventory = 0x01,
    StatScreen = 0x02,
    MiniSkill = 0x03,  // skill selection
    SkillTree = 0x04,
    ChatBox = 0x05,
    NewStats = 0x06,   // red new-stats button
    NewSkills = 0x07,  // red new-skills button
    NpcMenu = 0x08,
    EscMenu = 0x09,
    Automap = 0x0A,
    Config = 0x0B,  // key configuration
    NpcShop = 0x0C,
    HoldAlt = 0x0D,  // item-label highlight
    Anvil = 0x0E,    // imbue / socket / personalize / orifice
    QuestScreen = 0x0F,
    IniScroll = 0x10,  // Inifuss tree scroll
    QuestLog = 0x11,   // red quest-log button
    Unknown18 = 0x12,
    HirIcons = 0x13,
    Waypoint = 0x14,
    MiniPanel = 0x15,
    PartyScreen = 0x16,
    MpTrade = 0x17,
    MsgLog = 0x18,
    Stash = 0x19,
    Cube = 0x1A,
    SteegStone = 0x1B,
    GuildVault = 0x1C,
    Unknown29 = 0x1D,
    Unknown30 = 0x1E,
    BeltRows = 0x1F,
    Unknown32 = 0x20,
    HelpScreen = 0x21,
    HelpButton = 0x22,
    HireIcons = 0x23,
    MercInventory = 0x24,
    RecipeScroll = 0x25,
};

// ============================================================================
// Party / NPC interaction
// ============================================================================

enum class PartyMode : uint32_t {
    AllowLoot = 0,
    Unhostile = 1,
    Invite = 2,
    Leave = 3,
    Hostile = 4,
    HostileAlt = 5,
};

// A roster member's party button state (Party.partyflag); a single value, not a
// bit set. Values follow D2MOO's D2C_RosterControlFlags, which D2R keeps.
enum class PartyState : uint32_t {
    Invite = 0,
    InParty = 1,
    Accept = 2,
    Leave = 3,
    Cancel = 4,
};

// Per-pair relationship bits between two players (getPlayerFlag). Values follow
// D2MOO's D2C_RosterInfoFlags, which D2R keeps.
/// @flags
enum class RosterFlag : uint32_t {
    Loot = 0x01,
    Ignore = 0x02,
    Squelch = 0x04,
    Hostile = 0x08,
};

enum class CancelMode : int32_t {
    CloseInteract = 0,
    ClearCursor = 1,
    CloseNPC = 2,
    ClearScreen = 3,
};

// ============================================================================
// Inventory / trade / gold
// ============================================================================

// Gold dialog action codes, as the game's gold dialog and the reference `gold()`
// define them (reference commandRef: 1 drop, 2 inventory to trade, 3 inventory to
// stash, 4 stash to inventory). Deposit / Withdraw need the stash panel open.
enum class GoldActionMode : int32_t {
    Drop = 1,
    Trade = 2,
    Deposit = 3,
    Withdraw = 4,
};

enum class TradeInfoMode : uint32_t {
    RecentTradeId = 0,
    RecentTradeName = 1,
    RecentTradeId2 = 2,
};

// Query modes for the acceptTrade() JS function's optional mode parameter.
// When mode is provided, the function returns trade state info instead of accepting.
enum class AcceptTradeQueryMode : uint32_t {
    IsAccepted = 1,
    RecentTradeId = 2,
    IsBlocked = 3,
};

// What activated a desecrated (terror) zone.
enum class DesecratedZoneSource : uint8_t {
    Rotation = 1,         // the seeded timed rotation
    WorldstoneShard = 2,  // a player used a Worldstone Shard
};

enum class ShopMode : int32_t {
    Sell = 1,
    Buy = 2,
    BuyFill = 6,
};

enum class ItemCostMode : int32_t {
    Buy = 0,
    Sell = 1,
    Repair = 2,
};

// ============================================================================
// Character / game state
// ============================================================================

enum class CharacterClass : uint32_t {
    Amazon = 0,
    Sorceress = 1,
    Necromancer = 2,
    Paladin = 3,
    Barbarian = 4,
    Druid = 5,
    Assassin = 6,
    Warlock = 7,  // D2R 3.0 (Reign of the Warlock) only
};

enum class Difficulty : uint32_t {
    Normal = 0,
    Nightmare = 1,
    Hell = 2,
    HighestAvailable = 3,
};

enum class GameState : uint32_t {
    Null = 0,
    Menu = 1,
    InGame = 2,
    Busy = 3,
};

// ============================================================================
// IPC (WM_COPYDATA protocol)
// ============================================================================

// Well-known IPC mode IDs matching reference/d2bs/D2Handlers.cpp:184-196
// (GameEventHandler -> WM_COPYDATA). External tools and other d2bs instances
// send these as the COPYDATASTRUCT::dwData value; the framework intercepts
// the two reserved modes and dispatches anything else to JS via the
// CopyDataEvent listener.
enum class IpcMode : uint32_t {
    Evaluate = 0x1337,        // payload is JS source; runs via ScriptEngine::Evaluate
    SwitchProfile = 0x31337,  // payload is profile name; runs via runtime::profile::Switch
    // Any value other than the two reserved ones passes through to scripts.
};

// ============================================================================
// Login / OOG flow
// ============================================================================

// Result status of game::Login(). Mirrors reference Profile::login() return
// codes: 0=ok, 1=timeout, 2=error. See reference/d2bs/Profile.cpp:97-296.
enum class LoginStatus : uint8_t {
    Success,  // reached a terminal success location (Lobby/Chat/Channel/etc.) or GetGameState() == InGame
    Timeout,  // exceeded ProfileData::maxLoginTime
    Error,    // terminal error location or control-manipulation failure
};

// Outcome of game::Login(). Invariant: status != Success => !errorMessage.empty().
// The invariant lets callers pass errorMessage directly to error::ThrowError
// (which takes string_view) without a null/empty guard.
struct LoginResult {
    LoginStatus status;
    std::string errorMessage;
};

// ============================================================================
// Unit / item
// ============================================================================

enum class UnitType : uint32_t {
    Player = 0,
    Monster = 1,
    Object = 2,
    Missile = 3,
    Item = 4,
    Tile = 5,
};

// Unit kind values match the reference bitmask relationship:
// PRIVATE_ITEM (0x03) & PRIVATE_UNIT (0x01) == PRIVATE_UNIT
// This means InventoryItem is also a Unit (for type checking via bitwise AND).
enum class UnitKind : uint32_t {
    Player = 0,         // The 'me' object - always resolves to current player
    Regular = 1,        // Normal unit from getUnit()
    InventoryItem = 3,  // Inventory item from getItem() - has owner info
};

enum class NodePage : uint8_t {
    Storage = 1,
    Belt = 2,
    Equipped = 3,
};

enum class ItemLocation : uint8_t {
    Ground = 0,
    Equip = 1,
    Belt = 2,
    Inventory = 3,
    Store = 4,
    Trade = 5,
    Cube = 6,
    Stash = 7,
    Null = 255,
};

// Offset for encoding ItemLocation in unit mode filter parameter.
// Scripts use mode = 100 + ItemLocation to filter items by location.
constexpr uint32_t ITEM_LOCATION_MODE_OFFSET = 100;

// Which stash a tab belongs to. Vanilla LoD has a single personal tab; paged
// stashes (mods, D2R) add more, including account-wide shared tabs.
enum class StashTabKind : uint8_t {
    Personal = 0,
    Shared = 1,
};

// What a stash tab holds. LoD tabs are all Normal; D2R adds tabs with stackable
// item support and the Chronicle tab that tracks found set / unique / runeword
// items instead of holding items.
enum class StashTabType : uint8_t {
    Normal = 0,
    AdvancedStash = 1,
    Chronicle = 2,
};

enum class ItemQuality : uint32_t {
    Inferior = 1,
    Normal = 2,
    Superior = 3,
    Magic = 4,
    Set = 5,
    Rare = 6,
    Unique = 7,
    Crafted = 8,
    Tempered = 9,
};

enum class BodyLocation : uint8_t {
    None = 0,
    Head = 1,
    Amulet = 2,
    Body = 3,
    RightPrimary = 4,
    LeftPrimary = 5,
    RightRing = 6,
    LeftRing = 7,
    Belt = 8,
    Feet = 9,
    Gloves = 10,
    RightSecondary = 11,
    LeftSecondary = 12,
};

// Monster special-type bitflags (Unit.spectype).
/// @flags
enum class MonsterSpecType : uint32_t {
    SuperUnique = 0x01,
    Champion = 0x02,
    Unique = 0x04,  // unique / boss
    Minion = 0x08,
};

// Collision-grid cell bits (Room collision, getCollision, checkCollision masks).
// Values and names follow D2MOO's D2C_CollisionMaskFlags; D2R keeps the same bits.
/// @flags
enum class CollisionFlag : uint16_t {
    None = 0x0000,
    Wall = 0x0001,            // 'black space' in arcane sanctuary, cliff walls etc; blocks players
    Visible = 0x0002,         // tile obstacles you can't shoot over
    MissileBarrier = 0x0004,  // used inconsistently; guards against missiles / flying units
    NoPlayer = 0x0008,
    Preset = 0x0010,  // set on some floors, not others
    Blank = 0x0020,   // returned for an invalid subtile
    Missile = 0x0040,
    Player = 0x0080,
    Monster = 0x0100,
    Item = 0x0200,
    Object = 0x0400,
    Door = 0x0800,
    NoPath = 0x1000,  // set for units sometimes, but not always
    Pet = 0x2000,     // tied to whether an attackable monster is present
    Corpse = 0x8000,  // also used by portals
    All = 0xFFFF,

    Water = Missile | Player,
    MaskInvalid = Blank | MissileBarrier | Visible | Wall,
    MaskPlayerPath = Wall | NoPlayer | Object | Door | NoPath,
    MaskPlayerFlying = Door | MissileBarrier,
    MaskPlayerWhirlwind = Wall | Object | Door,
    MaskRadialBarrier = Door | MissileBarrier | Wall,
    MaskFlyingUnit = MissileBarrier | Door | NoPath,
    MaskMonsterThatCanOpenDoors = Wall | Object | NoPath | Pet,
    MaskMonsterMissile = Monster | Wall,
    MaskMonsterPath = MaskMonsterThatCanOpenDoors | Door,
    MaskDoorBlockVisibility = Door | MissileBarrier | Visible,
    MaskBlocksDoor = Player | Monster | Corpse,
    MaskSpawn = Wall | Item | Object | Door | NoPath | Pet,
    MaskPlacement = MaskSpawn | Preset | Monster,
};

// Item unit flag bits (Unit.getFlags / getFlag). Values follow D2MOO's D2C_ItemFlags;
// D2R keeps them and adds the Chronicle bits.
/// @flags
enum class ItemFlag : uint32_t {
    NewItem = 0x00000001,
    Target = 0x00000002,
    Targeting = 0x00000004,
    Deleted = 0x00000008,
    Identified = 0x00000010,
    Quantity = 0x00000020,
    SwitchIn = 0x00000040,
    SwitchOut = 0x00000080,
    Broken = 0x00000100,
    Repaired = 0x00000200,
    Socketed = 0x00000800,
    NoSell = 0x00001000,
    InStore = 0x00002000,
    NoEquip = 0x00004000,
    Named = 0x00008000,
    IsEar = 0x00010000,
    StartItem = 0x00020000,
    Init = 0x00080000,
    CompactSave = 0x00200000,
    Ethereal = 0x00400000,
    JustSaved = 0x00800000,
    Personalized = 0x01000000,
    // Set only while a gamble item is serialized; its bitstream then ends at the
    // item code (D2MOO names it IFLAG_LOWQUALITY but uses it this way).
    Gamble = 0x02000000,
    Runeword = 0x04000000,
    Item = 0x08000000,
    // D2R only: the item carries a Chronicle drop record after its stat lists,
    // the full form (drop time and up to 8 account ids) or the short one (one id).
    ChronicleRecord = 0x10000000,
    ChronicleRecordShort = 0x20000000,
};

// The game's 0-based act index (the JS `act` property is 1-based).
enum class Act : uint8_t {
    I = 0,
    II = 1,
    III = 2,
    IV = 3,
    V = 4,
};

// Unit.mode, by unit type: player (PlayerMode), monster (MonsterMode), object
// (ObjectMode) and item (ItemMode) animation modes. Values follow D2MOO's
// D2C_PlayerModes / D2C_MonModes / D2C_ObjModes / D2C_ItemModes; D2R keeps them.
enum class PlayerMode : uint32_t {
    Death = 0,
    Neutral = 1,
    Walk = 2,
    Run = 3,
    GetHit = 4,
    TownNeutral = 5,
    TownWalk = 6,
    Attack1 = 7,
    Attack2 = 8,
    Block = 9,
    Cast = 10,
    Throw = 11,
    Kick = 12,
    Skill1 = 13,
    Skill2 = 14,
    Skill3 = 15,
    Skill4 = 16,
    Dead = 17,
    Sequence = 18,
    Knockback = 19,
};

enum class MonsterMode : uint32_t {
    Death = 0,
    Neutral = 1,
    Walk = 2,
    GetHit = 3,
    Attack1 = 4,
    Attack2 = 5,
    Block = 6,
    Cast = 7,
    Skill1 = 8,
    Skill2 = 9,
    Skill3 = 10,
    Skill4 = 11,
    Dead = 12,
    Knockback = 13,
    Sequence = 14,
    Run = 15,
};

enum class ObjectMode : uint32_t {
    Neutral = 0,
    Operating = 1,
    Opened = 2,
    Special1 = 3,
    Special2 = 4,
    Special3 = 5,
    Special4 = 6,
    Special5 = 7,
};

enum class ItemMode : uint32_t {
    Stored = 0,  // inventory, cube or stash
    Equip = 1,
    InBelt = 2,
    OnGround = 3,
    OnCursor = 4,
    Dropping = 5,
    Socketed = 6,
};

// Extended unit flag bits (Unit.flagsex). Values follow D2MOO's D2C_UnitFlagsEx;
// D2R keeps them and adds SharedStash.
/// @flags
enum class UnitFlagEx : uint32_t {
    HasInventory = 0x00000001,
    UpdateInventory = 0x00000002,
    IsVendorItem = 0x00000004,
    IsShapeshifted = 0x00000008,
    ItemInit = 0x00000010,
    IsInLineOfSight = 0x00000080,
    HasBeenDeleted = 0x00000100,
    StoreOwnerInfo = 0x00000400,
    IsCorpse = 0x00001000,
    PathRelated = 0x00002000,
    Teleported = 0x00010000,  // needs a resync
    StoreLastAttacker = 0x00020000,
    NoDraw = 0x00040000,
    IsExpansion = 0x02000000,
    ServerUnit = 0x04000000,
    SharedStash = 0x08000000,  // D2R only: a shared stash tab's owner unit
};

// A quest record's per-quest bit index (getQuest's flag). Values follow D2MOO's
// D2C_OriginalQuestFlags; D2R keeps them.
enum class QuestFlag : uint32_t {
    RewardGranted = 0,
    RewardPending = 1,
    Started = 2,
    LeaveTown = 3,
    EnterArea = 4,
    Custom1 = 5,
    Custom2 = 6,
    Custom3 = 7,
    Custom4 = 8,
    Custom5 = 9,
    Custom6 = 10,
    Custom7 = 11,
    UpdateQuestLog = 12,
    PrimaryGoalDone = 13,
    CompletedNow = 14,
    CompletedBefore = 15,
};

// Value-space enums for JS API number properties. The game getters return the
// raw underlying integer; these document the meaning of each value and back the
// option-set tables in the API docs.
enum class GameType : uint32_t {
    Classic = 0,
    Expansion = 1,
};

enum class ScreenSize : uint32_t {
    Res640x480 = 0,
    Res800x600 = 1,
};

enum class WeaponSet : uint32_t {
    Primary = 0,    // slot I
    Secondary = 1,  // slot II / swap
};

enum class MoveMode : uint32_t {
    Walk = 0,
    Run = 1,
};

// ============================================================================
// Geometric primitives
// ============================================================================

struct Position;

// Signed 2D point. Used for map coordinates, pathfinding, and anywhere values
// may legitimately be negative. Also aliased by the pathfinding namespace.
struct Point {
    int32_t x = 0;
    int32_t y = 0;
    bool operator==(const Point&) const = default;
    Point operator+(Point b) const { return {.x = x + b.x, .y = y + b.y}; }
    Point operator-(Point b) const { return {.x = x - b.x, .y = y - b.y}; }
    static const Point Zero;

    // Converts to an unsigned Position. Caller must ensure x/y are non-negative
    // (D2 world coordinates are always non-negative at the boundaries where
    // this is invoked - typically converting pathfinder output back to game state).
    [[nodiscard]] Position ToPosition() const;
};
inline constexpr Point Point::Zero{};

// Unsigned 2D position. Used for grid coordinates and anything that can never
// be negative in the game domain (e.g., container slot positions).
struct Position {
    uint32_t x = 0;
    uint32_t y = 0;
    bool operator==(const Position&) const = default;
    Position operator+(Position b) const { return {.x = x + b.x, .y = y + b.y}; }
    Position operator-(Position b) const { return {.x = x - b.x, .y = y - b.y}; }
    static const Position Zero;

    // D2 coordinates are always within int32 range.
    [[nodiscard]] Point ToPoint() const { return {.x = static_cast<int32_t>(x), .y = static_cast<int32_t>(y)}; }
};
inline constexpr Position Position::Zero{};

inline Position Point::ToPosition() const {
    return {.x = static_cast<uint32_t>(x), .y = static_cast<uint32_t>(y)};
}

// Unsigned 2D size. Used for widths/heights of grids, text, regions.
struct Size {
    uint32_t width = 0;
    uint32_t height = 0;
    bool operator==(const Size&) const = default;
    [[nodiscard]] size_t Area() const { return static_cast<size_t>(width) * height; }
    static const Size Zero;
};
inline constexpr Size Size::Zero{};

// Rectangle: unsigned origin + size. Game-coord by convention (same as Position).
// Types whose identity IS a rectangle (Room, Level, Control) expose a single
// Bounds() accessor returning Rect, instead of separate Pos() / Size() pairs.
struct Rect {
    Position origin = Position::Zero;
    Size size = Size::Zero;
    bool operator==(const Rect&) const = default;

    [[nodiscard]] Position Center() const {
        return {.x = origin.x + (size.width / 2), .y = origin.y + (size.height / 2)};
    }

    // Inclusive/exclusive bounds: p.x in [origin.x, origin.x + size.width)
    [[nodiscard]] bool Contains(Position p) const {
        return p.x >= origin.x && p.x < origin.x + size.width && p.y >= origin.y && p.y < origin.y + size.height;
    }

    // Signed-point variant: pathfinder A* produces negative-coord neighbors near
    // grid edges. Guards against signed->unsigned wrap before the rectangle test.
    [[nodiscard]] bool Contains(Point p) const {
        if (p.x < 0 || p.y < 0)
            return false;
        return Contains(p.ToPosition());
    }

    static const Rect Zero;
};
inline constexpr Rect Rect::Zero{};

static_assert(std::is_trivially_copyable_v<Rect>);
static_assert(sizeof(Rect) == 16);  // 2 x 8-byte pairs

// ============================================================================
// Query result aggregates
// ============================================================================

// Iteration state for unit queries. Carries the original search domain, match
// criteria to re-apply when resuming, and (for InventoryItem cursors) the identity
// of the owner whose inventory is being walked. Fields left `nullopt` mean
// "no constraint" for criteria, or "not applicable" for anchors.
//
// Game-layer impls never read or write this struct; it is framework-owned
// state that rides on the Unit handle.
struct UnitCursorState {
    std::optional<UnitType> type;

    std::optional<std::string> name;
    std::optional<uint32_t> classId;
    std::optional<uint32_t> mode;
    std::optional<uint32_t> unitId;

    // InventoryItem cursor anchor - identity of the owner whose inventory is being
    // walked. Set at construction by FindFirstInventoryItem; read by FindNextInventoryItem to
    // validate the item is still in the original inventory and to stamp the next handle.
    std::optional<uint32_t> ownerId;
    std::optional<UnitType> ownerType;
};

// Result row for stat iteration (Unit::GetAllStats / GetDetailedStats).
// `subIndex` disambiguates skill-specific / item-specific stat variants.
struct StatEntry {
    uint32_t statId;
    uint32_t subIndex;
    int32_t value;
};

// One leaf stat array off a unit's statlist chains (Unit::GetStatLists).
// `flags` (STATLIST_* bitmask) and `stateNo` are the game's own provenance fields,
// copied verbatim: they say whether the array is the unit's base stats, an item mod,
// a set tier or a runeword, and whether it currently contributes. GetAllStats /
// GetDetailedStats merge that away.
struct StatListEntry {
    uint32_t flags = 0;
    uint32_t stateNo = 0;
    std::vector<StatEntry> stats;
};

// Result row for Room::GetPresetUnits (static placements within a room).
struct PresetUnitInfo {
    UnitType type = {};
    Position roomPos = {};
    Position posInRoom = {};  // game coordinates - see docs/coords.md
    uint32_t id = 0;
    uint32_t level = 0;
    // For UnitType::Tile presets, the destination level reached by
    // walking the tile transition - looked up via Room2::pRoomTiles in
    // game-impl. 0 for non-tile presets or tile presets that don't appear
    // in pRoomTiles (e.g. cosmetic tiles).
    uint32_t tileTargetLevelId = 0;
};

// Single dialog line returned from GetDialogLines.
struct DialogLine {
    std::string text;
    bool isSelectable;
};

}  // namespace d2bs::game

template <>
struct std::hash<d2bs::game::Point> {
    size_t operator()(const d2bs::game::Point& p) const noexcept {
        return std::hash<uint64_t>{}((static_cast<uint64_t>(static_cast<uint32_t>(p.x)) << 32) |
                                     static_cast<uint64_t>(static_cast<uint32_t>(p.y)));
    }
};
