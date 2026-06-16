#include "GameLoopCollaborators.h"

#include "components/characterstate/CharacterState.h"
#include "components/config/AppConfig.h"
#include "components/drawing/Drawable.h"
#include "components/events/EventDispatch.h"
#include "components/script/ScriptEngine.h"
#include "game/GameHelpers.h"
#include "game/Unit.h"

namespace d2bs::test {

GameLoopState& State() {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - test harness singleton
    static GameLoopState state;
    return state;
}

void Reset() {
    State() = GameLoopState{};
}

}  // namespace d2bs::test

// === Event dispatchers (shim) ===
namespace d2bs {

void LifeEventDispatch(uint32_t life) {
    d2bs::test::State().lifeEvents.push_back(life);
}
void ManaEventDispatch(uint32_t mana) {
    d2bs::test::State().manaEvents.push_back(mana);
}
void PlayerAssignEventDispatch(uint32_t unitId) {
    d2bs::test::State().playerAssignEvents.push_back(unitId);
}

}  // namespace d2bs

// === Drawable (shim) ===
namespace d2bs::framework::drawing {

void Drawable::DrawAll(d2bs::game::GameState state) {
    auto& s = d2bs::test::State();
    ++s.drawAllCount;
    s.lastDrawState = state;
}

}  // namespace d2bs::framework::drawing

// === ScriptEngine (shim) ===
namespace d2bs {

ScriptEngine& ScriptEngine::Instance() {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - matches real singleton shape
    static ScriptEngine instance;
    return instance;
}

std::shared_ptr<Script> ScriptEngine::StartScript(const std::filesystem::path& path, ScriptMode mode) {
    auto script = std::make_shared<Script>(path, mode);
    scripts_.push_back(script);
    started_.push_back(script);
    return script;
}

void ScriptEngine::StopAllScripts() {
    for (auto& script : scripts_) {
        script->Stop();
    }
}

void ScriptEngine::Evaluate(const std::string& /*code*/) {}

void ScriptEngine::RestartConsoleScript() {
    ++restartConsoleCount_;
    // Capture the AppConfig-visible consoleScript at this moment so tests can
    // verify SetScriptPaths ran before RestartConsoleScript was invoked.
    restartedConsoleName_ = d2bs::config::GetAppConfig().GetScriptPaths().consoleScript;
}

void ScriptEngine::Reset() {
    scripts_.clear();
    started_.clear();
    restartConsoleCount_ = 0;
    restartedConsoleName_.clear();
    initialized_ = false;
}

}  // namespace d2bs

// === CharacterState (shim) ===
// The real component pulls in nlohmann-json (not in the test's dependency set).
// GameLoop only needs Instance()/OnTick() to resolve, so the fake is a no-op.
namespace d2bs::framework::characterstate {

CharacterState& CharacterState::Instance() {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - matches real singleton shape
    static CharacterState instance;
    return instance;
}

void CharacterState::OnTick(d2bs::game::GameState /*state*/, bool /*sessionEntered*/) {}

}  // namespace d2bs::framework::characterstate

// GetGameState / IsTownByLevelNo / ExitGame are defined in
// fakes/GameHelpers.cpp and route through d2bs::test::State().
