#pragma once

// Shared POD/value types crossing the framework/game boundary. Stdlib-only so components, pathfinding, tests, and game
// implementations can all share them without heavier includes.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>

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

enum class CancelMode : int32_t {
    CloseInteract = 0,
    ClearCursor = 1,
    CloseNPC = 2,
    ClearScreen = 3,
};

// ============================================================================
// Inventory / trade / gold
// ============================================================================

enum class GoldActionMode : int32_t {
    Drop = 0,
    Stash = 1,
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
    SwitchProfile = 0x31337,  // payload is profile name; runs via profile::Switch
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
// The invariant lets callers pass errorMessage directly to v8_error::ThrowError
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

// Result row for Level::GetExits (level transitions).
//
// `type` is either ExitType::Linkage (room-to-room edge between two
// different levels) or ExitType::Tile (UNIT_TILE preset with a non-zero
// destination via the source room's pRoomTiles warp table). The JS API
// surface treats both identically - the type tag is just metadata.
enum class ExitType : uint32_t {
    Linkage = 1,
    Tile = 2,
};

struct ExitInfo {
    Position pos;  // game coordinates - see docs/coords.md
    uint32_t target;
    ExitType type;
    uint32_t tileId;
    uint32_t level;
};

// Result row for Room::GetPresetUnits (static placements within a room).
struct PresetUnitInfo {
    uint32_t type = 0;
    Position roomPos;
    Position posInRoom;  // game coordinates - see docs/coords.md
    uint32_t id = 0;
    uint32_t level = 0;
    // For UNIT_TILE presets (type == 5), the destination level reached by
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
