#include "game/Room.h"

#include "game/Level.h"
#include "game/Unit.h"

// All Room methods return defaults - the test suite never invokes the
// game-handle path. Tests construct LevelGrid (and CollisionLookup::secondary)
// directly via MapFixture, bypassing BuildLevelGrid / Room::FirstRoom / etc.

namespace d2bs::game {

void* Room::ResolvePtr() const {
    return nullptr;
}

Room::operator bool() const {
    return false;
}

Room Room::FromPtr(void* /*p*/) {
    return Room();
}

int32_t Room::Number() const {
    return 0;
}

int32_t Room::SubNumber() const {
    return 0;
}

Rect Room::Bounds() const {
    return Rect::Zero;
}

uint32_t Room::Flags() const {
    return 0;
}

uint32_t Room::CorrectTomb() const {
    return 0;
}

std::vector<std::vector<uint16_t>> Room::GetCollision() const {
    return {};
}

std::vector<uint16_t> Room::GetCollisionFlat() const {
    return {};
}

Room Room::GetNext() const {
    return Room();
}

Room Room::GetFirst() const {
    return Room();
}

std::vector<Room> Room::GetNearby() const {
    return {};
}

Level Room::GetLevel() const {
    return Level();
}

std::optional<Unit> Room::GetFirstUnit() const {
    return std::nullopt;
}

std::vector<PresetUnitInfo> Room::GetPresetUnits(std::optional<uint32_t> /*type*/,
                                                 std::optional<uint32_t> /*classId*/) const {
    return {};
}

uint32_t Room::GetStat(uint32_t /*statIndex*/) const {
    return 0;
}

bool Room::Reveal(bool /*drawPresets*/) const {
    return false;
}

std::optional<Room> Room::Find(uint32_t /*level*/, Position /*pos*/) {
    return std::nullopt;
}

}  // namespace d2bs::game
