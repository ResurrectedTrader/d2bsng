#include "game/Level.h"

#include "game/Room.h"

// All Level methods return defaults - the test suite never invokes the
// game-handle path. Tests construct LevelGrid (and CollisionLookup::secondary)
// directly via MapFixture, bypassing BuildLevelGrid / Level::FirstRoom / etc.

namespace d2bs::game {

void* Level::ResolvePtr() const {
    return nullptr;
}

Level::operator bool() const {
    return false;
}

std::string Level::Name() const {
    return "";
}

Rect Level::Bounds() const {
    return Rect::Zero;
}

Room Level::GetFirstRoom() const {
    return Room();
}

std::vector<ExitInfo> Level::GetExits() const {
    return {};
}

std::optional<Level> Level::Get(uint32_t /*levelNo*/) {
    return std::nullopt;
}

}  // namespace d2bs::game
