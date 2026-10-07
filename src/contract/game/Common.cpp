#include "game/Common.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "game/Constants.h"
#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "game/Unit.h"
#include "utils/Strings.h"

namespace d2bs::game {

namespace {

std::array<std::optional<std::string>, Unit::MAX_AFFIX_SLOTS> AffixNames(
    const Unit& item, const std::array<uint16_t, Unit::MAX_AFFIX_SLOTS>& codes) {
    std::array<std::optional<std::string>, Unit::MAX_AFFIX_SLOTS> out;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
    for (size_t i = 0; i < Unit::MAX_AFFIX_SLOTS; ++i) {
        out[i] = item.MagicAffixName(codes[i]);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    return out;
}

struct SkillEntry {
    std::string_view name;
    Skill id;
};

// The 1.14d skill names, from reference/d2bs/D2Skills.h: the localized names
// scripts were written against, which skills.txt does not always carry
// ("Phoenix Strike" is skills.txt's "Royal strike").
constexpr auto SKILLS_1_14D = std::to_array<SkillEntry>({
    {.name = "Attack", .id = Skill::Attack},
    {.name = "Kick", .id = Skill::Kick},
    {.name = "Throw", .id = Skill::Throw},
    {.name = "Unsummon", .id = Skill::Unsummon},
    {.name = "Left Hand Throw", .id = Skill::LeftHandThrow},
    {.name = "Left Hand Swing", .id = Skill::LeftHandSwing},
    {.name = "Magic Arrow", .id = Skill::MagicArrow},
    {.name = "Fire Arrow", .id = Skill::FireArrow},
    {.name = "Inner Sight", .id = Skill::InnerSight},
    {.name = "Critical Strike", .id = Skill::CriticalStrike},
    {.name = "Jab", .id = Skill::Jab},
    {.name = "Cold Arrow", .id = Skill::ColdArrow},
    {.name = "Multiple Shot", .id = Skill::MultipleShot},
    {.name = "Dodge", .id = Skill::Dodge},
    {.name = "Power Strike", .id = Skill::PowerStrike},
    {.name = "Poison Javelin", .id = Skill::PoisonJavelin},
    {.name = "Exploding Arrow", .id = Skill::ExplodingArrow},
    {.name = "Slow Missiles", .id = Skill::SlowMissiles},
    {.name = "Avoid", .id = Skill::Avoid},
    {.name = "Impale", .id = Skill::Impale},
    {.name = "Lightning Bolt", .id = Skill::LightningBolt},
    {.name = "Ice Arrow", .id = Skill::IceArrow},
    {.name = "Guided Arrow", .id = Skill::GuidedArrow},
    {.name = "Penetrate", .id = Skill::Penetrate},
    {.name = "Charged Strike", .id = Skill::ChargedStrike},
    {.name = "Plague Javelin", .id = Skill::PlagueJavelin},
    {.name = "Strafe", .id = Skill::Strafe},
    {.name = "Immolation Arrow", .id = Skill::ImmolationArrow},
    {.name = "Decoy", .id = Skill::Dopplezon},
    {.name = "Evade", .id = Skill::Evade},
    {.name = "Fend", .id = Skill::Fend},
    {.name = "Freezing Arrow", .id = Skill::FreezingArrow},
    {.name = "Valkyrie", .id = Skill::Valkyrie},
    {.name = "Pierce", .id = Skill::Pierce},
    {.name = "Lightning Strike", .id = Skill::LightningStrike},
    {.name = "Lightning Fury", .id = Skill::LightningFury},
    {.name = "Fire Bolt", .id = Skill::FireBolt},
    {.name = "Warmth", .id = Skill::Warmth},
    {.name = "Charged Bolt", .id = Skill::ChargedBolt},
    {.name = "Ice Bolt", .id = Skill::IceBolt},
    {.name = "Frozen Armor", .id = Skill::FrozenArmor},
    {.name = "Inferno", .id = Skill::Inferno},
    {.name = "Static Field", .id = Skill::StaticField},
    {.name = "Telekinesis", .id = Skill::Telekinesis},
    {.name = "Frost Nova", .id = Skill::FrostNova},
    {.name = "Ice Blast", .id = Skill::IceBlast},
    {.name = "Blaze", .id = Skill::Blaze},
    {.name = "Fire Ball", .id = Skill::FireBall},
    {.name = "Nova", .id = Skill::Nova},
    {.name = "Lightning", .id = Skill::Lightning},
    {.name = "Shiver Armor", .id = Skill::ShiverArmor},
    {.name = "Fire Wall", .id = Skill::FireWall},
    {.name = "Enchant", .id = Skill::Enchant},
    {.name = "Chain Lightning", .id = Skill::ChainLightning},
    {.name = "Teleport", .id = Skill::Teleport},
    {.name = "Glacial Spike", .id = Skill::GlacialSpike},
    {.name = "Meteor", .id = Skill::Meteor},
    {.name = "Thunder Storm", .id = Skill::ThunderStorm},
    {.name = "Energy Shield", .id = Skill::EnergyShield},
    {.name = "Blizzard", .id = Skill::Blizzard},
    {.name = "Chilling Armor", .id = Skill::ChillingArmor},
    {.name = "Fire Mastery", .id = Skill::FireMastery},
    {.name = "Hydra", .id = Skill::Hydra},
    {.name = "Lightning Mastery", .id = Skill::LightningMastery},
    {.name = "Frozen Orb", .id = Skill::FrozenOrb},
    {.name = "Cold Mastery", .id = Skill::ColdMastery},
    {.name = "Amplify Damage", .id = Skill::AmplifyDamage},
    {.name = "Teeth", .id = Skill::Teeth},
    {.name = "Bone Armor", .id = Skill::BoneArmor},
    {.name = "Skeleton Mastery", .id = Skill::SkeletonMastery},
    {.name = "Raise Skeleton", .id = Skill::RaiseSkeleton},
    {.name = "Dim Vision", .id = Skill::DimVision},
    {.name = "Weaken", .id = Skill::Weaken},
    {.name = "Poison Dagger", .id = Skill::PoisonDagger},
    {.name = "Corpse Explosion", .id = Skill::CorpseExplosion},
    {.name = "Clay Golem", .id = Skill::ClayGolem},
    {.name = "Iron Maiden", .id = Skill::IronMaiden},
    {.name = "Terror", .id = Skill::Terror},
    {.name = "Bone Wall", .id = Skill::BoneWall},
    {.name = "Golem Mastery", .id = Skill::GolemMastery},
    {.name = "Raise Skeletal Mage", .id = Skill::RaiseSkeletalMage},
    {.name = "Confuse", .id = Skill::Confuse},
    {.name = "Life Tap", .id = Skill::LifeTap},
    {.name = "Poison Explosion", .id = Skill::PoisonExplosion},
    {.name = "Bone Spear", .id = Skill::BoneSpear},
    {.name = "Blood Golem", .id = Skill::BloodGolem},
    {.name = "Attract", .id = Skill::Attract},
    {.name = "Decrepify", .id = Skill::Decrepify},
    {.name = "Bone Prison", .id = Skill::BonePrison},
    {.name = "Summon Resist", .id = Skill::SummonResist},
    {.name = "Iron Golem", .id = Skill::IronGolem},
    {.name = "Lower Resist", .id = Skill::LowerResist},
    {.name = "Poison Nova", .id = Skill::PoisonNova},
    {.name = "Bone Spirit", .id = Skill::BoneSpirit},
    {.name = "Fire Golem", .id = Skill::FireGolem},
    {.name = "Revive", .id = Skill::Revive},
    {.name = "Sacrifice", .id = Skill::Sacrifice},
    {.name = "Smite", .id = Skill::Smite},
    {.name = "Might", .id = Skill::Might},
    {.name = "Prayer", .id = Skill::Prayer},
    {.name = "Resist Fire", .id = Skill::ResistFire},
    {.name = "Holy Bolt", .id = Skill::HolyBolt},
    {.name = "Holy Fire", .id = Skill::HolyFire},
    {.name = "Thorns", .id = Skill::Thorns},
    {.name = "Defiance", .id = Skill::Defiance},
    {.name = "Resist Cold", .id = Skill::ResistCold},
    {.name = "Zeal", .id = Skill::Zeal},
    {.name = "Charge", .id = Skill::Charge},
    {.name = "Blessed Aim", .id = Skill::BlessedAim},
    {.name = "Cleansing", .id = Skill::Cleansing},
    {.name = "Resist Lightning", .id = Skill::ResistLightning},
    {.name = "Vengeance", .id = Skill::Vengeance},
    {.name = "Blessed Hammer", .id = Skill::BlessedHammer},
    {.name = "Concentration", .id = Skill::Concentration},
    {.name = "Holy Freeze", .id = Skill::HolyFreeze},
    {.name = "Vigor", .id = Skill::Vigor},
    {.name = "Conversion", .id = Skill::Conversion},
    {.name = "Holy Shield", .id = Skill::HolyShield},
    {.name = "Holy Shock", .id = Skill::HolyShock},
    {.name = "Sanctuary", .id = Skill::Sanctuary},
    {.name = "Meditation", .id = Skill::Meditation},
    {.name = "Fist of the Heavens", .id = Skill::FistOfTheHeavens},
    {.name = "Fanaticism", .id = Skill::Fanaticism},
    {.name = "Conviction", .id = Skill::Conviction},
    {.name = "Redemption", .id = Skill::Redemption},
    {.name = "Salvation", .id = Skill::Salvation},
    {.name = "Bash", .id = Skill::Bash},
    {.name = "Sword Mastery", .id = Skill::BladeMastery},
    {.name = "Axe Mastery", .id = Skill::AxeMastery},
    {.name = "Mace Mastery", .id = Skill::MaceMastery},
    {.name = "Howl", .id = Skill::Howl},
    {.name = "Find Potion", .id = Skill::FindPotion},
    {.name = "Leap", .id = Skill::Leap},
    {.name = "Double Swing", .id = Skill::DoubleSwing},
    {.name = "Pole Arm Mastery", .id = Skill::PoleArmMastery},
    {.name = "Throwing Mastery", .id = Skill::ThrowingMastery},
    {.name = "Spear Mastery", .id = Skill::SpearMastery},
    {.name = "Taunt", .id = Skill::Taunt},
    {.name = "Shout", .id = Skill::Shout},
    {.name = "Stun", .id = Skill::Stun},
    {.name = "Double Throw", .id = Skill::DoubleThrow},
    {.name = "Increased Stamina", .id = Skill::IncreasedStamina},
    {.name = "Find Item", .id = Skill::FindItem},
    {.name = "Leap Attack", .id = Skill::LeapAttack},
    {.name = "Concentrate", .id = Skill::Concentrate},
    {.name = "Iron Skin", .id = Skill::IronSkin},
    {.name = "Battle Cry", .id = Skill::BattleCry},
    {.name = "Frenzy", .id = Skill::Frenzy},
    {.name = "Increased Speed", .id = Skill::IncreasedSpeed},
    {.name = "Battle Orders", .id = Skill::BattleOrders},
    {.name = "Grim Ward", .id = Skill::GrimWard},
    {.name = "Whirlwind", .id = Skill::Whirlwind},
    {.name = "Berserk", .id = Skill::Berserk},
    {.name = "Natural Resistance", .id = Skill::NaturalResistance},
    {.name = "War Cry", .id = Skill::WarCry},
    {.name = "Battle Command", .id = Skill::BattleCommand},
    {.name = "Scroll of Townportal", .id = Skill::ScrollOfTownportal},
    {.name = "Book of Townportal", .id = Skill::BookOfTownportal},
    {.name = "Raven", .id = Skill::Raven},
    {.name = "Poison Creeper", .id = Skill::PlaguePoppy},
    {.name = "Werewolf", .id = Skill::Wearwolf},
    {.name = "Shape Shifting", .id = Skill::ShapeShifting},
    {.name = "Firestorm", .id = Skill::Firestorm},
    {.name = "Oak Sage", .id = Skill::OakSage},
    {.name = "Summon Spirit Wolf", .id = Skill::SummonSpiritWolf},
    {.name = "Werebear", .id = Skill::Wearbear},
    {.name = "Molten Boulder", .id = Skill::MoltenBoulder},
    {.name = "Arctic Blast", .id = Skill::ArcticBlast},
    {.name = "Carrion Vine", .id = Skill::CycleOfLife},
    {.name = "Feral Rage", .id = Skill::FeralRage},
    {.name = "Maul", .id = Skill::Maul},
    {.name = "Fissure", .id = Skill::Eruption},
    {.name = "Cyclone Armor", .id = Skill::CycloneArmor},
    {.name = "Heart of Wolverine", .id = Skill::HeartOfWolverine},
    {.name = "Summon Dire Wolf", .id = Skill::SummonFenris},
    {.name = "Rabies", .id = Skill::Rabies},
    {.name = "Fire Claws", .id = Skill::FireClaws},
    {.name = "Twister", .id = Skill::Twister},
    {.name = "Solar Creeper", .id = Skill::Vines},
    {.name = "Hunger", .id = Skill::Hunger},
    {.name = "Shock Wave", .id = Skill::ShockWave},
    {.name = "Volcano", .id = Skill::Volcano},
    {.name = "Tornado", .id = Skill::Tornado},
    {.name = "Spirit of barbs", .id = Skill::SpiritOfBarbs},
    {.name = "Summon Grizzly", .id = Skill::SummonGrizzly},
    {.name = "Fury", .id = Skill::Fury},
    {.name = "Armageddon", .id = Skill::Armageddon},
    {.name = "Hurricane", .id = Skill::Hurricane},
    {.name = "Fire Blast", .id = Skill::FireTrauma},
    {.name = "Claw Mastery", .id = Skill::ClawMastery},
    {.name = "Psychic Hammer", .id = Skill::PsychicHammer},
    {.name = "Tiger Strike", .id = Skill::TigerStrike},
    {.name = "Dragon Talon", .id = Skill::DragonTalon},
    {.name = "Shock Web", .id = Skill::ShockField},
    {.name = "Blade Sentinel", .id = Skill::BladeSentinel},
    {.name = "Burst of Speed", .id = Skill::Quickness},
    {.name = "Fists of Fire", .id = Skill::FistsOfFire},
    {.name = "Dragon Claw", .id = Skill::DragonClaw},
    {.name = "Charged Bolt Sentry", .id = Skill::ChargedBoltSentry},
    {.name = "Wake of Fire", .id = Skill::WakeOfFireSentry},
    {.name = "Weapon Block", .id = Skill::WeaponBlock},
    {.name = "Cloak of Shadows", .id = Skill::CloakOfShadows},
    {.name = "Cobra Strike", .id = Skill::CobraStrike},
    {.name = "Blade Fury", .id = Skill::BladeFury},
    {.name = "Fade", .id = Skill::Fade},
    {.name = "Shadow Warrior", .id = Skill::ShadowWarrior},
    {.name = "Claws of Thunder", .id = Skill::ClawsOfThunder},
    {.name = "Dragon Tail", .id = Skill::DragonTail},
    {.name = "Lightning Sentry", .id = Skill::LightningSentry},
    {.name = "Wake of Inferno", .id = Skill::InfernoSentry},
    {.name = "Mind Blast", .id = Skill::MindBlast},
    {.name = "Blades of Ice", .id = Skill::BladesOfIce},
    {.name = "Dragon Flight", .id = Skill::DragonFlight},
    {.name = "Death Sentry", .id = Skill::DeathSentry},
    {.name = "Blade Shield", .id = Skill::BladeShield},
    {.name = "Venom", .id = Skill::Venom},
    {.name = "Shadow Master", .id = Skill::ShadowMaster},
    {.name = "Phoenix Strike", .id = Skill::RoyalStrike},
});

}  // namespace

uint32_t PricingNpcClassId(uint32_t npcClassId) {
    const auto inventory = GetTxtValue("monstats", npcClassId, "inventory");
    const auto* row = std::get_if<int64_t>(&inventory);
    return row == nullptr || *row == 0 ? NPC_CHARSI_CLASS_ID : npcClassId;
}

bool IsWaypointLevel(LevelId levelId) {
    constexpr int64_t NO_WAYPOINT = 255;
    const auto waypoint = GetTxtValue("levels", std::to_underlying(levelId), "Waypoint");
    const auto* index = std::get_if<int64_t>(&waypoint);
    return index == nullptr || *index != NO_WAYPOINT;
}

std::optional<uint16_t> SkillNameStringId(Skill skill) {
    const auto descRow = GetTxtValue("skills", std::to_underlying(skill), "skilldesc");
    const auto* desc = std::get_if<int64_t>(&descRow);
    if (desc == nullptr) {
        return std::nullopt;
    }
    const auto strName = GetTxtValue("skilldesc", static_cast<uint32_t>(*desc), "str name");
    const auto* str = std::get_if<int64_t>(&strName);
    if (str == nullptr) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(*str);
}

std::optional<Skill> LegacySkillByName(std::string_view name) {
    const auto entry = std::ranges::find_if(SKILLS_1_14D, [name](const SkillEntry& candidate) {
        return utils::EqualsCaseInsensitive(candidate.name, name);
    });
    if (entry == SKILLS_1_14D.end()) {
        return std::nullopt;
    }
    return entry->id;
}

std::string Unit::Prefix() const {
    return MagicAffixName(PrefixNum()).value_or(std::string{});
}

std::string Unit::Suffix() const {
    return MagicAffixName(SuffixNum()).value_or(std::string{});
}

std::array<std::optional<std::string>, Unit::MAX_AFFIX_SLOTS> Unit::Prefixes() const {
    GameReadLock guard;
    return AffixNames(*this, PrefixNums());
}

std::array<std::optional<std::string>, Unit::MAX_AFFIX_SLOTS> Unit::Suffixes() const {
    GameReadLock guard;
    return AffixNames(*this, SuffixNums());
}

}  // namespace d2bs::game
