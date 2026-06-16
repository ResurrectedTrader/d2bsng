#pragma once

// Test shim for src/framework/components/events/EventDispatch.h
//
// Test-only shim for components/events/EventDispatch.h. The real header pulls
// in <v8.h> via ScriptBroadcastEventDispatch, which the test binary does not
// link. GameLoop.cpp only touches the three dispatchers below, so that's all we
// declare here; the capturing implementations live in
// tests/framework/fakes/GameLoopCollaborators.cpp.

#include <cstdint>

namespace d2bs {

void LifeEventDispatch(uint32_t life);
void ManaEventDispatch(uint32_t mana);
void PlayerAssignEventDispatch(uint32_t unitId);

}  // namespace d2bs
