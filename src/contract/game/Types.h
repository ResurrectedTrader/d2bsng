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
// Stats / states / skills
// ============================================================================

// Stat ids: the rows of itemstatcost.txt. Later versions keep every 1.14d id and only append
// or fill slots 1.14d left unused; names follow the latest rows.
enum class Stat : uint32_t {
    Strength = 0,
    Energy = 1,
    Dexterity = 2,
    Vitality = 3,
    StatPoints = 4,
    NewSkills = 5,
    HitPoints = 6,
    MaxHitPoints = 7,
    Mana = 8,
    MaxMana = 9,
    Stamina = 10,
    MaxStamina = 11,
    Level = 12,
    Experience = 13,
    Gold = 14,
    GoldBank = 15,
    ItemArmorPercent = 16,
    ItemMaxDamagePercent = 17,
    ItemMinDamagePercent = 18,
    ToHit = 19,
    ToBlock = 20,
    MinDamage = 21,
    MaxDamage = 22,
    SecondaryMinDamage = 23,
    SecondaryMaxDamage = 24,
    DamagePercent = 25,
    ManaRecovery = 26,
    ManaRecoveryBonus = 27,
    StaminaRecoveryBonus = 28,
    LastExperience = 29,
    NextExperience = 30,
    ArmorClass = 31,
    ArmorClassVsMissile = 32,
    ArmorClassVsHandToHand = 33,
    NormalDamageReduction = 34,
    MagicDamageReduction = 35,
    DamageResist = 36,
    MagicResist = 37,
    MaxMagicResist = 38,
    FireResist = 39,
    MaxFireResist = 40,
    LightningResist = 41,
    MaxLightningResist = 42,
    ColdResist = 43,
    MaxColdResist = 44,
    PoisonResist = 45,
    MaxPoisonResist = 46,
    DamageAura = 47,
    FireMinDamage = 48,
    FireMaxDamage = 49,
    LightningMinDamage = 50,
    LightningMaxDamage = 51,
    MagicMinDamage = 52,
    MagicMaxDamage = 53,
    ColdMinDamage = 54,
    ColdMaxDamage = 55,
    ColdLength = 56,
    PoisonMinDamage = 57,
    PoisonMaxDamage = 58,
    PoisonLength = 59,
    LifeDrainMinDamage = 60,
    LifeDrainMaxDamage = 61,
    ManaDrainMinDamage = 62,
    ManaDrainMaxDamage = 63,
    StaminaDrainMinDamage = 64,
    StaminaDrainMaxDamage = 65,
    StunLength = 66,
    VelocityPercent = 67,
    AttackRate = 68,
    OtherAnimRate = 69,
    Quantity = 70,
    Value = 71,
    Durability = 72,
    MaxDurability = 73,
    HpRegen = 74,
    ItemMaxDurabilityPercent = 75,
    ItemMaxHpPercent = 76,
    ItemMaxManaPercent = 77,
    ItemAttackerTakesDamage = 78,
    ItemGoldBonus = 79,
    ItemMagicBonus = 80,
    ItemKnockback = 81,
    ItemTimeDuration = 82,
    ItemAddClassSkills = 83,
    UnsentParam1 = 84,
    ItemAddExperience = 85,
    ItemHealAfterKill = 86,
    ItemReducedPrices = 87,
    ItemDoubleHerbDuration = 88,
    ItemLightRadius = 89,
    ItemLightColor = 90,
    ItemReqPercent = 91,
    ItemLevelReq = 92,
    ItemFasterAttackRate = 93,
    ItemLevelReqPercent = 94,
    LastBlockFrame = 95,
    ItemFasterMoveVelocity = 96,
    ItemNonClassSkill = 97,
    State = 98,
    ItemFasterGetHitRate = 99,
    MonsterPlayerCount = 100,
    SkillPoisonOverrideLength = 101,
    ItemFasterBlockRate = 102,
    SkillBypassUndead = 103,
    SkillBypassDemons = 104,
    ItemFasterCastRate = 105,
    SkillBypassBeasts = 106,
    ItemSingleSkill = 107,
    ItemRestInPeace = 108,
    CurseResistance = 109,
    ItemPoisonLengthResist = 110,
    ItemNormalDamage = 111,
    ItemHowl = 112,
    ItemStupidity = 113,
    ItemDamageToMana = 114,
    ItemIgnoreTargetAc = 115,
    ItemFractionalTargetAc = 116,
    ItemPreventHeal = 117,
    ItemHalfFreezeDuration = 118,
    ItemToHitPercent = 119,
    ItemDamageTargetAc = 120,
    ItemDemonDamagePercent = 121,
    ItemUndeadDamagePercent = 122,
    ItemDemonToHit = 123,
    ItemUndeadToHit = 124,
    ItemThrowable = 125,
    ItemElemSkill = 126,
    ItemAllSkills = 127,
    ItemAttackerTakesLightDamage = 128,
    IronMaidenLevel = 129,
    LifeTapLevel = 130,
    ThornsPercent = 131,
    BoneArmor = 132,
    BoneArmorMax = 133,
    ItemFreeze = 134,
    ItemOpenWounds = 135,
    ItemCrushingBlow = 136,
    ItemKickDamage = 137,
    ItemManaAfterKill = 138,
    ItemHealAfterDemonKill = 139,
    ItemExtraBlood = 140,
    ItemDeadlyStrike = 141,
    ItemAbsorbFirePercent = 142,
    ItemAbsorbFire = 143,
    ItemAbsorbLightningPercent = 144,
    ItemAbsorbLightning = 145,
    ItemAbsorbMagicPercent = 146,
    ItemAbsorbMagic = 147,
    ItemAbsorbColdPercent = 148,
    ItemAbsorbCold = 149,
    ItemSlow = 150,
    ItemAura = 151,
    ItemIndestructible = 152,
    ItemCannotBeFrozen = 153,
    ItemStaminaDrainPercent = 154,
    ItemReanimate = 155,
    ItemPierce = 156,
    ItemMagicArrow = 157,
    ItemExplosiveArrow = 158,
    ItemThrowMinDamage = 159,
    ItemThrowMaxDamage = 160,
    SkillHandOfAthena = 161,
    SkillStaminaPercent = 162,
    SkillPassiveStaminaPercent = 163,
    SkillConcentration = 164,
    SkillEnchant = 165,
    SkillPierce = 166,
    SkillConviction = 167,
    SkillChillingArmor = 168,
    SkillFrenzy = 169,
    SkillDecrepify = 170,
    SkillArmorPercent = 171,
    Alignment = 172,
    Target0 = 173,
    Target1 = 174,
    GoldLost = 175,
    ConversionLevel = 176,
    ConversionMaxHp = 177,
    UnitDoOverlay = 178,
    AttackVsMonsterType = 179,
    DamageVsMonsterType = 180,
    Fade = 181,
    ArmorOverridePercent = 182,
    LastHitReactFrame = 183,
    CreateSeason = 184,
    BonusMinDamage = 185,
    BonusMaxDamage = 186,
    ItemPierceColdImmunity = 187,
    ItemAddSkillTab = 188,
    ItemPierceFireImmunity = 189,
    ItemPierceLightningImmunity = 190,
    ItemPiercePoisonImmunity = 191,
    ItemPierceDamageImmunity = 192,
    ItemPierceMagicImmunity = 193,
    ItemNumSockets = 194,
    ItemSkillOnAttack = 195,
    ItemSkillOnKill = 196,
    ItemSkillOnDeath = 197,
    ItemSkillOnHit = 198,
    ItemSkillOnLevelUp = 199,
    ItemChargeNoConsume = 200,
    ItemSkillOnGetHit = 201,
    ModifierListCastId = 202,
    PassiveMasteryItemReqPercent = 203,
    ItemChargedSkill = 204,
    ItemNoConsume = 205,
    PassiveMasteryNoConsume = 206,
    PassiveMasteryReplenishOnCrit = 207,
    MissileThornsPercent = 208,
    PassiveMasteryItemLevelReqPercent = 209,
    UberAncientsEscalation = 210,
    UberAncientsDefeated = 211,
    PassiveMasteryGetHitRate = 212,
    PassiveMasteryAttackSpeed = 213,
    ItemArmorPerLevel = 214,
    ItemArmorPercentPerLevel = 215,
    ItemHpPerLevel = 216,
    ItemManaPerLevel = 217,
    ItemMaxDamagePerLevel = 218,
    ItemMaxDamagePercentPerLevel = 219,
    ItemStrengthPerLevel = 220,
    ItemDexterityPerLevel = 221,
    ItemEnergyPerLevel = 222,
    ItemVitalityPerLevel = 223,
    ItemToHitPerLevel = 224,
    ItemToHitPercentPerLevel = 225,
    ItemColdDamageMaxPerLevel = 226,
    ItemFireDamageMaxPerLevel = 227,
    ItemLightningDamageMaxPerLevel = 228,
    ItemPoisonDamageMaxPerLevel = 229,
    ItemResistColdPerLevel = 230,
    ItemResistFirePerLevel = 231,
    ItemResistLightningPerLevel = 232,
    ItemResistPoisonPerLevel = 233,
    ItemAbsorbColdPerLevel = 234,
    ItemAbsorbFirePerLevel = 235,
    ItemAbsorbLightningPerLevel = 236,
    ItemAbsorbPoisonPerLevel = 237,
    ItemThornsPerLevel = 238,
    ItemFindGoldPerLevel = 239,
    ItemFindMagicPerLevel = 240,
    ItemRegenStaminaPerLevel = 241,
    ItemStaminaPerLevel = 242,
    ItemDamageDemonPerLevel = 243,
    ItemDamageUndeadPerLevel = 244,
    ItemToHitDemonPerLevel = 245,
    ItemToHitUndeadPerLevel = 246,
    ItemCrushingBlowPerLevel = 247,
    ItemOpenWoundsPerLevel = 248,
    ItemKickDamagePerLevel = 249,
    ItemDeadlyStrikePerLevel = 250,
    ItemFindGemsPerLevel = 251,
    ItemReplenishDurability = 252,
    ItemReplenishQuantity = 253,
    ItemExtraStack = 254,
    ItemFindItem = 255,
    ItemSlashDamage = 256,
    ItemSlashDamagePercent = 257,
    ItemCrushDamage = 258,
    ItemCrushDamagePercent = 259,
    ItemThrustDamage = 260,
    ItemThrustDamagePercent = 261,
    ItemAbsorbSlash = 262,
    ItemAbsorbCrush = 263,
    ItemAbsorbThrust = 264,
    ItemAbsorbSlashPercent = 265,
    ItemAbsorbCrushPercent = 266,
    ItemAbsorbThrustPercent = 267,
    ItemArmorByTime = 268,
    ItemArmorPercentByTime = 269,
    ItemHpByTime = 270,
    ItemManaByTime = 271,
    ItemMaxDamageByTime = 272,
    ItemMaxDamagePercentByTime = 273,
    ItemStrengthByTime = 274,
    ItemDexterityByTime = 275,
    ItemEnergyByTime = 276,
    ItemVitalityByTime = 277,
    ItemToHitByTime = 278,
    ItemToHitPercentByTime = 279,
    ItemColdDamageMaxByTime = 280,
    ItemFireDamageMaxByTime = 281,
    ItemLightningDamageMaxByTime = 282,
    ItemPoisonDamageMaxByTime = 283,
    ItemResistColdByTime = 284,
    ItemResistFireByTime = 285,
    ItemResistLightningByTime = 286,
    ItemResistPoisonByTime = 287,
    ItemAbsorbColdByTime = 288,
    ItemAbsorbFireByTime = 289,
    ItemAbsorbLightningByTime = 290,
    ItemAbsorbPoisonByTime = 291,
    ItemFindGoldByTime = 292,
    ItemFindMagicByTime = 293,
    ItemRegenStaminaByTime = 294,
    ItemStaminaByTime = 295,
    ItemDamageDemonByTime = 296,
    ItemDamageUndeadByTime = 297,
    ItemToHitDemonByTime = 298,
    ItemToHitUndeadByTime = 299,
    ItemCrushingBlowByTime = 300,
    ItemOpenWoundsByTime = 301,
    ItemKickDamageByTime = 302,
    ItemDeadlyStrikeByTime = 303,
    ItemFindGemsByTime = 304,
    ItemPierceCold = 305,
    ItemPierceFire = 306,
    ItemPierceLightning = 307,
    ItemPiercePoison = 308,
    ItemDamageVsMonster = 309,
    ItemDamagePercentVsMonster = 310,
    ItemToHitVsMonster = 311,
    ItemToHitPercentVsMonster = 312,
    ItemAcVsMonster = 313,
    ItemAcPercentVsMonster = 314,
    FireLength = 315,
    BurningMin = 316,
    BurningMax = 317,
    ProgressiveDamage = 318,
    ProgressiveSteal = 319,
    ProgressiveOther = 320,
    ProgressiveFire = 321,
    ProgressiveCold = 322,
    ProgressiveLightning = 323,
    ItemExtraCharges = 324,
    ProgressiveToHit = 325,
    PoisonCount = 326,
    DamageFrameRate = 327,
    PierceIndex = 328,
    PassiveFireMastery = 329,
    PassiveLightningMastery = 330,
    PassiveColdMastery = 331,
    PassivePoisonMastery = 332,
    PassiveFirePierce = 333,
    PassiveLightningPierce = 334,
    PassiveColdPierce = 335,
    PassivePoisonPierce = 336,
    PassiveCriticalStrike = 337,
    PassiveDodge = 338,
    PassiveAvoid = 339,
    PassiveEvade = 340,
    PassiveWarmth = 341,
    PassiveMasteryMeleeToHit = 342,
    PassiveMasteryMeleeDamage = 343,
    PassiveMasteryMeleeCrit = 344,
    PassiveMasteryThrowToHit = 345,
    PassiveMasteryThrowDamage = 346,
    PassiveMasteryThrowCrit = 347,
    PassiveWeaponBlock = 348,
    PassiveSummonResist = 349,
    ModifierListSkill = 350,
    ModifierListLevel = 351,
    LastSentHitPointPercent = 352,
    SourceUnitType = 353,
    SourceUnitId = 354,
    ShortParam1 = 355,
    QuestItemDifficulty = 356,
    PassiveMagicMastery = 357,
    PassiveMagicPierce = 358,
    // Not in 1.14d.
    SkillCooldown = 359,
    SkillMissileDamageScale = 360,
    PsychicWard = 361,
    PsychicWardMax = 362,
    SkillChannelingTick = 363,
    CustomizationIndex = 364,
    ItemMagicDamageMaxPerLevel = 365,
    PassiveDamagePierce = 366,
    HeraldTier = 367,
};

// D2 stores hp / mana / stamina (HitPoints..MaxStamina, a contiguous run) in 8.8
// fixed point.
constexpr bool IsFixedPointStat(Stat stat) {
    return stat >= Stat::HitPoints && stat <= Stat::MaxStamina;
}

// A stat list's kind bits (D2StatListStrc::dwFlags): what the list holds and how it
// lives - base stats, an item's magic mods, a set tier, a timed buff, the extended layout.
/// @flags
enum class StatListFlags : uint32_t {
    Base = 0x0,
    Basic = 0x1,
    NewLength = 0x2,
    TempOnly = 0x4,
    Buff = 0x8,
    Curse = 0x20,
    Magic = 0x40,
    Overlay = 0x80,
    Unk0x100 = 0x100,
    Toggle = 0x200,
    Convert = 0x800,
    Set = 0x2000,
    ItemEx = 0x200000,
    Permanent = 0x20000000,
    Dynamic = 0x40000000,
    Extended = 0x80000000
};

// State ids: the rows of states.txt, with the same id stability as Stat.
enum class State : uint32_t {
    None = 0,
    Freeze = 1,
    Poison = 2,
    ResistFire = 3,
    ResistCold = 4,
    ResistLight = 5,
    ResistMagic = 6,
    PlayerBody = 7,
    ResistAll = 8,
    AmplifyDamage = 9,
    FrozenArmor = 10,
    Cold = 11,
    Inferno = 12,
    Blaze = 13,
    BoneArmor = 14,
    Concentrate = 15,
    Enchant = 16,
    InnerSight = 17,
    SkillMove = 18,
    Weaken = 19,
    ChillingArmor = 20,
    Stunned = 21,
    Spiderlay = 22,
    DimVision = 23,
    Slowed = 24,
    FetishAura = 25,
    Shout = 26,
    Taunt = 27,
    Conviction = 28,
    Convicted = 29,
    EnergyShield = 30,
    VenomClaws = 31,
    BattleOrders = 32,
    Might = 33,
    Prayer = 34,
    HolyFire = 35,
    Thorns = 36,
    Defiance = 37,
    Thunderstorm = 38,
    LightningBolt = 39,
    BlessedAim = 40,
    Stamina = 41,
    Concentration = 42,
    HolyWind = 43,
    HolyWindCold = 44,
    Cleansing = 45,
    HolyShock = 46,
    Sanctuary = 47,
    Meditation = 48,
    Fanaticism = 49,
    Redemption = 50,
    BattleCommand = 51,
    PreventHeal = 52,
    Conversion = 53,
    Uninterruptable = 54,
    IronMaiden = 55,
    Terror = 56,
    Attract = 57,
    LifeTap = 58,
    Confuse = 59,
    Decrepify = 60,
    LowerResist = 61,
    OpenWounds = 62,
    Dopplezon = 63,
    CriticalStrike = 64,
    Dodge = 65,
    Avoid = 66,
    Penetrate = 67,
    Evade = 68,
    Pierce = 69,
    Warmth = 70,
    FireMastery = 71,
    LightningMastery = 72,
    ColdMastery = 73,
    BladeMastery = 74,
    AxeMastery = 75,
    MaceMastery = 76,
    PolearmMastery = 77,
    ThrowingMastery = 78,
    SpearMastery = 79,
    IncreasedStamina = 80,
    IronSkin = 81,
    IncreasedSpeed = 82,
    NaturalResistance = 83,
    FingerMageCurse = 84,
    NoManaRegen = 85,
    JustHit = 86,
    SlowMissiles = 87,
    ShiverArmor = 88,
    BattleCry = 89,
    Blue = 90,
    Red = 91,
    DeathDelay = 92,
    Valkyrie = 93,
    Frenzy = 94,
    Berserk = 95,
    Revive = 96,
    SkeletalMastery = 97,
    SourceUnit = 98,
    Redeemed = 99,
    HealthPot = 100,
    HolyShield = 101,
    JustPortaled = 102,
    MonFrenzy = 103,
    CorpseNoDraw = 104,
    Alignment = 105,
    ManaPot = 106,
    Shatter = 107,
    SyncWarped = 108,
    ConversionSave = 109,
    Pregnant = 110,
    GolemMastery = 111,
    Rabies = 112,
    DefenseCurse = 113,
    BloodMana = 114,
    Burning = 115,
    DragonFlight = 116,
    Maul = 117,
    CorpseNoSelect = 118,
    ShadowWarrior = 119,
    FeralRage = 120,
    SkillDelay = 121,
    ProgressiveDamage = 122,
    ProgressiveSteal = 123,
    ProgressiveOther = 124,
    ProgressiveFire = 125,
    ProgressiveCold = 126,
    ProgressiveLightning = 127,
    ShrineArmor = 128,
    ShrineCombat = 129,
    ShrineResistLightning = 130,
    ShrineResistFire = 131,
    ShrineResistCold = 132,
    ShrineResistPoison = 133,
    ShrineSkill = 134,
    ShrineManaRegen = 135,
    ShrineStamina = 136,
    ShrineExperience = 137,
    FenrisRage = 138,
    Wolf = 139,
    Bear = 140,
    BloodLust = 141,
    ChangeClass = 142,
    Attached = 143,
    Hurricane = 144,
    Armageddon = 145,
    Invis = 146,
    Barbs = 147,
    Wolverine = 148,
    Oaksage = 149,
    VineBeast = 150,
    CycloneArmor = 151,
    ClawMastery = 152,
    CloakOfShadows = 153,
    Recycled = 154,
    WeaponBlock = 155,
    Cloaked = 156,
    Quickness = 157,
    BladeShield = 158,
    Fade = 159,
    SummonResist = 160,
    OakSageControl = 161,
    WolverineControl = 162,
    BarbsControl = 163,
    DebugControl = 164,
    Itemset1 = 165,
    Itemset2 = 166,
    Itemset3 = 167,
    Itemset4 = 168,
    Itemset5 = 169,
    Itemset6 = 170,
    RuneWord = 171,
    RestInPeace = 172,
    CorpseExp = 173,
    Whirlwind = 174,
    FullSetGeneric = 175,
    MonsterSet = 176,
    Delerium = 177,
    Antidote = 178,
    Thawing = 179,
    StaminaPot = 180,
    PassiveResistFire = 181,
    PassiveResistCold = 182,
    PassiveResistLtng = 183,
    UberMinion = 184,
    // Not in 1.14d.
    Cooldown = 185,
    SharedStash = 186,
    HideDead = 187,
    Impale = 188,
    Desecrated = 189,
    MarkBear = 190,
    MarkWolf = 191,
    SigilLethargy = 192,
    SigilRancor = 193,
    SigilDeath = 194,
    BloodOath = 195,
    DemonicMastery = 196,
    LevitateMastery = 197,
    HexBane = 198,
    HexBaneDebuff = 199,
    HexSiphon = 200,
    HexSiphonDebuff = 201,
    HexPurge = 202,
    MindBarrier = 203,
    ShadowMastery = 204,
    Sigil = 205,
    PsychicWard = 206,
    BindDemon = 207,
    Consume = 208,
    AdvancedStash = 209,
    HealthLink = 210,
    Engorge = 211,
    DeathMark = 212,
    HealthLinkCaster = 213,
    WarlockDemonGround = 214,
    WarlockDemonTorso = 215,
    SkillChanneling = 216,
    HexPurgeDebuff = 217,
    ChronicleBacklight = 218,
    ChronicleFootprints = 219,
    ChroniclePortal = 220,
    EldritchBlastPeriodic = 221,
    LightningEnchant = 222,
    ColdEnchant = 223,
    TalicFirePierce = 224,
    MadawcLightPierce = 225,
    KorlicColdPierce = 226,
    BindDemonUnderling = 227,
    Herald = 228,
    Apocalypse = 229,
};

// Skill ids: the rows of skills.txt, with the same id stability as Stat. 16 bits
// wide, the width the game stores and sends a skill id in.
enum class Skill : uint16_t {
    Attack = 0,
    Kick = 1,
    Throw = 2,
    Unsummon = 3,
    LeftHandThrow = 4,
    LeftHandSwing = 5,
    MagicArrow = 6,
    FireArrow = 7,
    InnerSight = 8,
    CriticalStrike = 9,
    Jab = 10,
    ColdArrow = 11,
    MultipleShot = 12,
    Dodge = 13,
    PowerStrike = 14,
    PoisonJavelin = 15,
    ExplodingArrow = 16,
    SlowMissiles = 17,
    Avoid = 18,
    Impale = 19,
    LightningBolt = 20,
    IceArrow = 21,
    GuidedArrow = 22,
    Penetrate = 23,
    ChargedStrike = 24,
    PlagueJavelin = 25,
    Strafe = 26,
    ImmolationArrow = 27,
    Dopplezon = 28,
    Evade = 29,
    Fend = 30,
    FreezingArrow = 31,
    Valkyrie = 32,
    Pierce = 33,
    LightningStrike = 34,
    LightningFury = 35,
    FireBolt = 36,
    Warmth = 37,
    ChargedBolt = 38,
    IceBolt = 39,
    FrozenArmor = 40,
    Inferno = 41,
    StaticField = 42,
    Telekinesis = 43,
    FrostNova = 44,
    IceBlast = 45,
    Blaze = 46,
    FireBall = 47,
    Nova = 48,
    Lightning = 49,
    ShiverArmor = 50,
    FireWall = 51,
    Enchant = 52,
    ChainLightning = 53,
    Teleport = 54,
    GlacialSpike = 55,
    Meteor = 56,
    ThunderStorm = 57,
    EnergyShield = 58,
    Blizzard = 59,
    ChillingArmor = 60,
    FireMastery = 61,
    Hydra = 62,
    LightningMastery = 63,
    FrozenOrb = 64,
    ColdMastery = 65,
    AmplifyDamage = 66,
    Teeth = 67,
    BoneArmor = 68,
    SkeletonMastery = 69,
    RaiseSkeleton = 70,
    DimVision = 71,
    Weaken = 72,
    PoisonDagger = 73,
    CorpseExplosion = 74,
    ClayGolem = 75,
    IronMaiden = 76,
    Terror = 77,
    BoneWall = 78,
    GolemMastery = 79,
    RaiseSkeletalMage = 80,
    Confuse = 81,
    LifeTap = 82,
    PoisonExplosion = 83,
    BoneSpear = 84,
    BloodGolem = 85,
    Attract = 86,
    Decrepify = 87,
    BonePrison = 88,
    SummonResist = 89,
    IronGolem = 90,
    LowerResist = 91,
    PoisonNova = 92,
    BoneSpirit = 93,
    FireGolem = 94,
    Revive = 95,
    Sacrifice = 96,
    Smite = 97,
    Might = 98,
    Prayer = 99,
    ResistFire = 100,
    HolyBolt = 101,
    HolyFire = 102,
    Thorns = 103,
    Defiance = 104,
    ResistCold = 105,
    Zeal = 106,
    Charge = 107,
    BlessedAim = 108,
    Cleansing = 109,
    ResistLightning = 110,
    Vengeance = 111,
    BlessedHammer = 112,
    Concentration = 113,
    HolyFreeze = 114,
    Vigor = 115,
    Conversion = 116,
    HolyShield = 117,
    HolyShock = 118,
    Sanctuary = 119,
    Meditation = 120,
    FistOfTheHeavens = 121,
    Fanaticism = 122,
    Conviction = 123,
    Redemption = 124,
    Salvation = 125,
    Bash = 126,
    BladeMastery = 127,
    AxeMastery = 128,
    MaceMastery = 129,
    Howl = 130,
    FindPotion = 131,
    Leap = 132,
    DoubleSwing = 133,
    PoleArmMastery = 134,
    ThrowingMastery = 135,
    SpearMastery = 136,
    Taunt = 137,
    Shout = 138,
    Stun = 139,
    DoubleThrow = 140,
    IncreasedStamina = 141,
    FindItem = 142,
    LeapAttack = 143,
    Concentrate = 144,
    IronSkin = 145,
    BattleCry = 146,
    Frenzy = 147,
    IncreasedSpeed = 148,
    BattleOrders = 149,
    GrimWard = 150,
    Whirlwind = 151,
    Berserk = 152,
    NaturalResistance = 153,
    WarCry = 154,
    BattleCommand = 155,
    FireHit = 156,
    UnHolyBolt = 157,
    SkeletonRaise = 158,
    MaggotEgg = 159,
    ShamanFire = 160,
    MagottUp = 161,
    MagottDown = 162,
    MagottLay = 163,
    AndrialSpray = 164,
    Jump = 165,
    SwarmMove = 166,
    Nest = 167,
    QuickStrike = 168,
    VampireFireball = 169,
    VampireFirewall = 170,
    VampireMeteor = 171,
    GargoyleTrap = 172,
    SpiderLay = 173,
    VampireHeal = 174,
    VampireRaise = 175,
    Submerge = 176,
    FetishAura = 177,
    FetishInferno = 178,
    ZakarumHeal = 179,
    Emerge = 180,
    Resurrect = 181,
    Bestow = 182,
    MissileSkill1 = 183,
    MonTeleport = 184,
    PrimeLightning = 185,
    PrimeBolt = 186,
    PrimeBlaze = 187,
    PrimeFirewall = 188,
    PrimeSpike = 189,
    PrimeIceNova = 190,
    PrimePoisonball = 191,
    PrimePoisonNova = 192,
    DiabLight = 193,
    DiabCold = 194,
    DiabFire = 195,
    FingerMageSpider = 196,
    DiabWall = 197,
    DiabRun = 198,
    DiabPrison = 199,
    PoisonBallTrap = 200,
    AndyPoisonBolt = 201,
    HireableMissile = 202,
    DesertTurret = 203,
    ArcaneTower = 204,
    MonBlizzard = 205,
    Mosquito = 206,
    CursedBallTrapRight = 207,
    CursedBallTrapLeft = 208,
    MonFrozenArmor = 209,
    MonBoneArmor = 210,
    MonBoneSpirit = 211,
    MonCurseCast = 212,
    HellMeteor = 213,
    RegurgitatorEat = 214,
    MonFrenzy = 215,
    QueenDeath = 216,
    ScrollOfIdentify = 217,
    BookOfIdentify = 218,
    ScrollOfTownportal = 219,
    BookOfTownportal = 220,
    Raven = 221,
    PlaguePoppy = 222,
    Wearwolf = 223,
    ShapeShifting = 224,
    Firestorm = 225,
    OakSage = 226,
    SummonSpiritWolf = 227,
    Wearbear = 228,
    MoltenBoulder = 229,
    ArcticBlast = 230,
    CycleOfLife = 231,
    FeralRage = 232,
    Maul = 233,
    Eruption = 234,
    CycloneArmor = 235,
    HeartOfWolverine = 236,
    SummonFenris = 237,
    Rabies = 238,
    FireClaws = 239,
    Twister = 240,
    Vines = 241,
    Hunger = 242,
    ShockWave = 243,
    Volcano = 244,
    Tornado = 245,
    SpiritOfBarbs = 246,
    SummonGrizzly = 247,
    Fury = 248,
    Armageddon = 249,
    Hurricane = 250,
    FireTrauma = 251,
    ClawMastery = 252,
    PsychicHammer = 253,
    TigerStrike = 254,
    DragonTalon = 255,
    ShockField = 256,
    BladeSentinel = 257,
    Quickness = 258,
    FistsOfFire = 259,
    DragonClaw = 260,
    ChargedBoltSentry = 261,
    WakeOfFireSentry = 262,
    WeaponBlock = 263,
    CloakOfShadows = 264,
    CobraStrike = 265,
    BladeFury = 266,
    Fade = 267,
    ShadowWarrior = 268,
    ClawsOfThunder = 269,
    DragonTail = 270,
    LightningSentry = 271,
    InfernoSentry = 272,
    MindBlast = 273,
    BladesOfIce = 274,
    DragonFlight = 275,
    DeathSentry = 276,
    BladeShield = 277,
    Venom = 278,
    ShadowMaster = 279,
    RoyalStrike = 280,
    WakeOfDestructionSentry = 281,
    ImpInferno = 282,
    ImpFireball = 283,
    BaalTaunt = 284,
    BaalCorpseExplode = 285,
    BaalMonsterSpawn = 286,
    CatapultChargedBall = 287,
    CatapultSpikeBall = 288,
    SuckBlood = 289,
    CryHelp = 290,
    HealingVortex = 291,
    Teleport2 = 292,
    SelfResurrect = 293,
    VineAttack = 294,
    OverseerWhip = 295,
    BarbsAura = 296,
    WolverineAura = 297,
    OakSageAura = 298,
    ImpFireMissile = 299,
    Impregnate = 300,
    SiegeBeastStomp = 301,
    MinionSpawner = 302,
    CatapultBlizzard = 303,
    CatapultPlague = 304,
    CatapultMeteor = 305,
    BoltSentry = 306,
    CorpseCycler = 307,
    DeathMaul = 308,
    DefenseCurse = 309,
    BloodMana = 310,
    MonInfernoSentry = 311,
    MonDeathSentry = 312,
    SentryLightning = 313,
    FenrisRage = 314,
    BaalTentacle = 315,
    BaalNova = 316,
    BaalInferno = 317,
    BaalColdMissiles = 318,
    MegademonInferno = 319,
    EvilHutSpawner = 320,
    CountessFirewall = 321,
    ImpBolt = 322,
    HorrorArcticBlast = 323,
    DeathSentryLtng = 324,
    VineCycler = 325,
    BearSmite = 326,
    Resurrect2 = 327,
    BloodLordFrenzy = 328,
    BaalTeleport = 329,
    ImpTeleport = 330,
    BaalCloneTeleport = 331,
    ZakarumLightning = 332,
    VampireMissile = 333,
    MephistoMissile = 334,
    DoomKnightMissile = 335,
    RogueMissile = 336,
    HydraMissile = 337,
    NecromageMissile = 338,
    MonBow = 339,
    MonFireArrow = 340,
    MonColdArrow = 341,
    MonExplodingArrow = 342,
    MonFreezingArrow = 343,
    MonPowerStrike = 344,
    SuccubusBolt = 345,
    MephFrostNova = 346,
    MonIceSpear = 347,
    ShamanIce = 348,
    Diablogeddon = 349,
    DeleriumChange = 350,
    NihlathakCorpseExplosion = 351,
    SerpentCharge = 352,
    TrapNova = 353,
    UnHolyBoltEx = 354,
    ShamanFireEx = 355,
    ImpFireMissileEx = 356,
    // Not in 1.14d.
    Interact = 357,
    Loot = 358,
    TownPortal = 359,
    EmoteWheel = 360,
    SwapWeapons = 361,
    Map = 362,
    ShowItems = 363,
    RunToggle = 364,
    MonHolyFreeze = 365,
    MonLeap = 366,
    MonLeapAttack = 367,
    MonHolyFire = 368,
    MonHolyShock = 369,
    CubeLoot = 370,
    MarkOfTheBear = 371,
    MarkOfTheWolf = 372,
    SummonGoatman = 373,
    DemonicMastery = 374,
    DeathMark = 375,
    SummonTainted = 376,
    SummonDefiler = 377,
    BloodOath = 378,
    Engorge = 379,
    BloodBoil = 380,
    Consume = 381,
    BindDemon = 382,
    Levitate = 383,
    EldritchBlast = 384,
    HexBane = 385,
    HexSiphon = 386,
    PsychicWard = 387,
    EchoingStrike = 388,
    HexPurge = 389,
    BladeWarp = 390,
    Cleave = 391,
    MirroredBlades = 392,
    SigilLethargy = 393,
    RingOfFire = 394,
    MiasmaBolt = 395,
    SigilRancor = 396,
    EnhancedEntropy = 397,
    FlameWave = 398,
    MiasmaChains = 399,
    SigilDeath = 400,
    Apocalypse = 401,
    Abyss = 402,
    SigilDeathExplosion = 403,
    HexPurgeExplosion = 404,
    HealthLink = 405,
    ColdFissure = 406,
    KorlicsLeapAttack = 407,
    ColdEnchant = 408,
    LightningEnchant = 409,
    TalicsWhirlwind = 410,
    TownportalOSkill = 411,
    FireTwisters = 412,
    ColossalVolcano = 413,
    ColossalThunderStorm = 414,
    UberAncientsHeal = 415,
    GoatmanStun = 416,
    GoatmanFrenzy = 417,
    GoatmanBerserk = 418,
    GoatmanCleave = 419,
    TaintedResistFire = 420,
    TaintedFireBolt = 421,
    TaintedFireBall = 422,
    TalicsFirePierce = 423,
    MadawcsLightningPierce = 424,
    KorlicsColdPierce = 425,
    ChargedBoltDisk = 426,
    KorlicsBash = 427,
    HeraldThorns = 428,
    // The game's -1: no skill (an empty hotkey slot, an unset txt link).
    Invalid = UINT16_MAX,
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
// item support.
enum class StashTabType : uint8_t {
    Normal = 0,
    AdvancedStash = 1,
    /// @internal A Chronicle panel, which tracks found set / unique / runeword
    /// items as a list on the player and holds no items; no tab ever reports it.
    Chronicle = 2,
};

// Where StashTab::Withdraw puts what it takes off an advanced stash tab.
enum class StashWithdrawTarget : uint8_t {
    Cursor = 0,
    Inventory = 1,
    Cube = 2,
    Belt = 3,
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

// Area (level) number: the row of levels.txt, as the game and scripts number it.
// 137-141 are not in 1.14d's levels.txt.
enum class LevelId : uint32_t {
    None = 0,
    RogueEncampment = 1,
    BloodMoor = 2,
    ColdPlains = 3,
    StonyField = 4,
    DarkWood = 5,
    BlackMarsh = 6,
    TamoeHighland = 7,
    DenOfEvil = 8,
    CaveLevel1 = 9,
    UndergroundPassageLevel1 = 10,
    HoleLevel1 = 11,
    PitLevel1 = 12,
    CaveLevel2 = 13,
    UndergroundPassageLevel2 = 14,
    HoleLevel2 = 15,
    PitLevel2 = 16,
    BurialGrounds = 17,
    Crypt = 18,
    Mausoleum = 19,
    ForgottenTower = 20,
    TowerCellarLevel1 = 21,
    TowerCellarLevel2 = 22,
    TowerCellarLevel3 = 23,
    TowerCellarLevel4 = 24,
    TowerCellarLevel5 = 25,
    MonasteryGate = 26,
    OuterCloister = 27,
    Barracks = 28,
    JailLevel1 = 29,
    JailLevel2 = 30,
    JailLevel3 = 31,
    InnerCloister = 32,
    Cathedral = 33,
    CatacombsLevel1 = 34,
    CatacombsLevel2 = 35,
    CatacombsLevel3 = 36,
    CatacombsLevel4 = 37,
    Tristram = 38,
    MooMooFarm = 39,
    LutGholein = 40,
    RockyWaste = 41,
    DryHills = 42,
    FarOasis = 43,
    LostCity = 44,
    ValleyOfSnakes = 45,
    CanyonOfTheMagi = 46,
    SewersLevel1Act2 = 47,
    SewersLevel2Act2 = 48,
    SewersLevel3Act2 = 49,
    HaremLevel1 = 50,
    HaremLevel2 = 51,
    PalaceCellarLevel1 = 52,
    PalaceCellarLevel2 = 53,
    PalaceCellarLevel3 = 54,
    StonyTombLevel1 = 55,
    HallsOfTheDeadLevel1 = 56,
    HallsOfTheDeadLevel2 = 57,
    ClawViperTempleLevel1 = 58,
    StonyTombLevel2 = 59,
    HallsOfTheDeadLevel3 = 60,
    ClawViperTempleLevel2 = 61,
    MaggotLairLevel1 = 62,
    MaggotLairLevel2 = 63,
    MaggotLairLevel3 = 64,
    AncientTunnels = 65,
    TalRashasTomb1 = 66,
    TalRashasTomb2 = 67,
    TalRashasTomb3 = 68,
    TalRashasTomb4 = 69,
    TalRashasTomb5 = 70,
    TalRashasTomb6 = 71,
    TalRashasTomb7 = 72,
    DurielsLair = 73,
    ArcaneSanctuary = 74,
    KurastDocks = 75,
    SpiderForest = 76,
    GreatMarsh = 77,
    FlayerJungle = 78,
    LowerKurast = 79,
    KurastBazaar = 80,
    UpperKurast = 81,
    KurastCauseway = 82,
    Travincal = 83,
    SpiderCave = 84,
    SpiderCavern = 85,
    SwampyPitLevel1 = 86,
    SwampyPitLevel2 = 87,
    FlayerDungeonLevel1 = 88,
    FlayerDungeonLevel2 = 89,
    SwampyPitLevel3 = 90,
    FlayerDungeonLevel3 = 91,
    SewersLevel1Act3 = 92,
    SewersLevel2Act3 = 93,
    RuinedTemple = 94,
    DisusedFane = 95,
    ForgottenReliquary = 96,
    ForgottenTemple = 97,
    RuinedFane = 98,
    DisusedReliquary = 99,
    DuranceOfHateLevel1 = 100,
    DuranceOfHateLevel2 = 101,
    DuranceOfHateLevel3 = 102,
    ThePandemoniumFortress = 103,
    OuterSteppes = 104,
    PlainsOfDespair = 105,
    CityOfTheDamned = 106,
    RiverOfFlame = 107,
    ChaosSanctuary = 108,
    Harrogath = 109,
    BloodyFoothills = 110,
    FrigidHighlands = 111,
    ArreatPlateau = 112,
    CrystallinePassage = 113,
    FrozenRiver = 114,
    GlacialTrail = 115,
    DrifterCavern = 116,
    FrozenTundra = 117,
    TheAncientsWay = 118,
    IcyCellar = 119,
    ArreatSummit = 120,
    NihlathaksTemple = 121,
    HallsOfAnguish = 122,
    HallsOfPain = 123,
    HallsOfVaught = 124,
    Abaddon = 125,
    PitOfAcheron = 126,
    InfernalPit = 127,
    TheWorldStoneKeepLevel1 = 128,
    TheWorldStoneKeepLevel2 = 129,
    TheWorldStoneKeepLevel3 = 130,
    ThroneOfDestruction = 131,
    TheWorldstoneChamber = 132,
    MatronsDen = 133,
    ForgottenSands = 134,
    FurnaceOfPain = 135,
    UberTristram = 136,
    MapsAncientTemple = 137,
    MapsDesecratedTemple = 138,
    MapsFrigidPlateau = 139,
    MapsInfernalTrial = 140,
    MapsRuinedCitadel = 141,
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
// later versions keep them and add AnimEndOfCycle.
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
    // Not in 1.14d: the current animation reached the end of its frame cycle, cached
    // by the frame advance (1.14d computes UNITS_IsAtEndOfFrameCycle on demand)
    // and cleared on every mode change.
    AnimEndOfCycle = 0x08000000,
};

// A quest's index in the quest record (getQuest's quest). Values follow D2MOO's
// D2QuestStateFlagIds; names are the quest log's (the
// qstsa<act>q<n> strings), and the game's flag names where the log has none.
// 32 bits wide because the games read the whole register: the record lookup
// computes quest * 16 + flag.
enum class Quest : uint32_t {
    Act1Prologue = 0,
    DenOfEvil = 1,
    SistersBurialGrounds = 2,
    ToolsOfTheTrade = 3,
    TheSearchForCain = 4,
    TheForgottenTower = 5,
    SistersToTheSlaughter = 6,
    Act1Completed = 7,
    Act2Prologue = 8,
    RadamentsLair = 9,
    TheHoradricStaff = 10,
    TaintedSun = 11,
    ArcaneSanctuary = 12,
    TheSummoner = 13,
    TheSevenTombs = 14,
    Act2Completed = 15,
    Act3Prologue = 16,
    LamEsensTome = 17,
    KhalimsWill = 18,
    BladeOfTheOldReligion = 19,
    TheGoldenBird = 20,
    TheBlackenedTemple = 21,
    TheGuardian = 22,
    Act3Completed = 23,
    Act4Prologue = 24,
    TheFallenAngel = 25,
    TerrorsEnd = 26,
    HellsForge = 27,
    Act4Completed = 28,
    Act1Navi = 29,          // QUESTSTATEFLAG_A1Q7, no quest-log entry
    Act2GuardGossip1 = 30,  // QUESTSTATEFLAG_A2Q7, unused by the game
    Act2GuardGossip2 = 31,  // QUESTSTATEFLAG_A2Q8, unused by the game
    DarkWanderer = 32,      // QUESTSTATEFLAG_A3Q7, no quest-log entry
    Malachai = 33,          // QUESTSTATEFLAG_A4Q4, no quest-log entry
    SiegeOnHarrogath = 35,
    RescueOnMountArreat = 36,
    PrisonOfIce = 37,
    BetrayalOfHarrogath = 38,
    RiteOfPassage = 39,
    EveOfDestruction = 40,
    // The game's shared act-intro slot (QUESTSTATEFLAG_A*INTRO); scripts read
    // Akara's free skill / stat reset from it.
    Respec = 41,
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
    RotW = 2,  // Reign of the Warlock
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

// The game client's text language (D2MOO's D2C_Language).
enum class Language : uint32_t {
    English = 0,
    Spanish = 1,
    German = 2,
    French = 3,
    Portuguese = 4,
    Italian = 5,
    Japanese = 6,
    Korean = 7,
    ChineseSimplified = 8,
    ChineseTraditional = 9,
    Polish = 10,
    Russian = 11,
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
    Stat statId;
    uint16_t subIndex;
    int32_t value;
};

// One leaf stat array off a unit's statlist chains (Unit::GetStatLists).
// `flags` and `stateNo` are the game's own provenance fields,
// copied verbatim: they say whether the array is the unit's base stats, an item mod,
// a set tier or a runeword, and whether it currently contributes. GetAllStats /
// GetDetailedStats merge that away.
struct StatListEntry {
    StatListFlags flags = StatListFlags::Base;
    State stateNo = State::None;
    std::vector<StatEntry> stats;
};

// Result row for Room::GetPresetUnits (static placements within a room).
struct PresetUnitInfo {
    UnitType type = {};
    Position roomPos = {};
    Position posInRoom = {};  // game coordinates - see docs/coords.md
    uint32_t id = 0;
    LevelId level = LevelId::None;
    // For UnitType::Tile presets, the destination level reached by
    // walking the tile transition - looked up via Room2::pRoomTiles in
    // game-impl. None for non-tile presets or tile presets that don't appear
    // in pRoomTiles (e.g. cosmetic tiles).
    LevelId tileTargetLevelId = LevelId::None;
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
