#include "game/Unit.h"

#include "fakes/GameLoopCollaborators.h"

// Minimal Unit fake for GameLoop tests. Only the methods GameLoop::TakeSnapshot
// actually calls (Player, ResolvePtr - drives operator bool, Id, Hp, Mp, Area)
// are implemented - other Unit members exist only as declarations and are not
// linked because no test exercises them.

namespace d2bs::game {

// ResolvePtr returns a non-null sentinel when the test harness has a player
// seeded, matching the real Unit contract ("resolvable handle == truthy").
// The pointer is never dereferenced - the fake accessors read from test State.
void* Unit::ResolvePtr() const {
    if (!d2bs::test::State().playerId.has_value()) {
        return nullptr;
    }
    return reinterpret_cast<void*>(static_cast<uintptr_t>(0x1));
}

Unit Unit::Player() {
    return Unit{};
}

uint32_t Unit::Id() const {
    return d2bs::test::State().playerId.value_or(0);
}

uint32_t Unit::Hp() const {
    return d2bs::test::State().hp.value_or(0);
}

uint32_t Unit::Mp() const {
    return d2bs::test::State().mp.value_or(0);
}

uint32_t Unit::Area() const {
    return d2bs::test::State().areaId;
}

}  // namespace d2bs::game
