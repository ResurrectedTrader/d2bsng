// The in-process host (host/Host.h): d2bs.dll, the other injectable DLLs and js_tests compile this file into
// themselves. Script threads are plain threads and waits are real-time sleeps. It is not part of core.lib, so a host
// with hooks of its own links without it.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stop_token>
#include <string_view>
#include <thread>

#include "host/Host.h"
#include "speedhack/Speedhack.h"

namespace d2bs::host {

std::jthread StartScriptThread(std::string_view /*name*/, void (*body)(void* context, const std::stop_token& token),
                               void* context) {
    return std::jthread([body, context](const std::stop_token& token) { body(context, token); });
}

void JoinScriptThread(std::jthread& thread) {
    thread.join();
}

void OnScriptWake(const std::jthread& /*thread*/, uint32_t /*delayMs*/) {}

void ScriptWait(std::chrono::milliseconds remaining, std::chrono::milliseconds slice,
                bool (* /*mustWakeEachSlice*/)(void* context), void* /*context*/) {
    const core::speedhack::SpeedhackDisabledScope realWaits;
    std::this_thread::sleep_for(std::clamp(remaining, std::chrono::milliseconds(1), slice));
}

void OnNativeCall(std::string_view /*name*/) {}

void ObserveNativeResult(const NativeResult& /*result*/) {}

bool RunFrame() {
    return false;
}

void WaitForTask(const std::atomic<TaskState>& state) {
    auto current = state.load(std::memory_order_acquire);
    while (current != TaskState::Done && current != TaskState::Cancelled) {
        state.wait(current, std::memory_order_acquire);
        current = state.load(std::memory_order_acquire);
    }
}

bool RetainReadLock() {
    return false;
}

}  // namespace d2bs::host
