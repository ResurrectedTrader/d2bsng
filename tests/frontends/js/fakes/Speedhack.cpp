// Fake for d2bs::speedhack - IniConfigStore::LoadSettings calls
// speedhack::SetSpeed after reading the INI value, but tests don't link
// detours and don't need real time scaling. Stub the public surface and
// route the value straight into AppConfig.speed.
#include "speedhack/Speedhack.h"

#include "config/AppConfig.h"

namespace d2bs::speedhack {

void SetSpeed(float newSpeed) {
    config::GetAppConfig().speed.store(newSpeed, std::memory_order_relaxed);
}

float GetSpeed() {
    return config::GetAppConfig().speed.load(std::memory_order_relaxed);
}

DWORD ScaleTimeout(DWORD ms) {
    return ms;
}

void Install() {}
void Remove() {}
void OptInCurrentThread() {}

SpeedhackDisabledScope::SpeedhackDisabledScope() = default;
SpeedhackDisabledScope::~SpeedhackDisabledScope() {
    (void)prev_;
}

NestedWaitGuard::NestedWaitGuard() = default;
NestedWaitGuard::~NestedWaitGuard() = default;

}  // namespace d2bs::speedhack
