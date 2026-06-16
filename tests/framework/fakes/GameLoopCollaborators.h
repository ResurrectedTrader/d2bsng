#pragma once

// Test harness state for GameLoop unit tests. Collects every side effect GameLoop::OnSleep
// performs against its collaborators (event dispatch, drawable flush,
// ExitGame, IsTownByLevelNo, GetGameState, Unit::Player() read-through) so
// individual test cases can assert on them without the real ScriptEngine /
// EventDispatch / Drawable plumbing.

#include <cstdint>
#include <optional>
#include <vector>

#include "game/Types.h"

namespace d2bs::test {

struct GameLoopState {
    // Inputs read by GameLoop::OnSleep / TakeSnapshot. Optional fields mirror
    // Snapshot: unset means "Unit::Player() returned nullopt" for this tick.
    d2bs::game::GameState clientState = d2bs::game::GameState::Menu;
    std::optional<uint32_t> playerId;
    std::optional<uint32_t> hp;
    std::optional<uint32_t> mp;
    uint32_t areaId = 0;
    bool isTown = false;

    // Side effects captured from GameLoop::OnSleep.
    std::vector<uint32_t> lifeEvents;
    std::vector<uint32_t> manaEvents;
    std::vector<uint32_t> playerAssignEvents;
    int32_t exitGameCount = 0;
    int32_t drawAllCount = 0;
    std::optional<d2bs::game::GameState> lastDrawState;
};

// Accessor - state is a process-singleton mirroring the collaborators it
// stands in for. Tests call Reset() at the top of each TEST_CASE.
GameLoopState& State();
void Reset();

}  // namespace d2bs::test
