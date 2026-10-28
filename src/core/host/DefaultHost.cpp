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

ScriptThread StartScriptThread(std::string_view /*name*/, void (*body)(void* context, const std::stop_token& token),
                               void* context) {
    return ScriptThread([body, context](const std::stop_token& token) { body(context, token); });
}

void JoinScriptThread(ScriptThread& thread) {
    thread.join();
}

void OnScriptWake(const ScriptThread& /*thread*/, uint32_t /*delayMs*/) {}

void ScriptWait(std::chrono::milliseconds remaining, std::chrono::milliseconds slice,
                bool (* /*mustWakeEachSlice*/)(void* context), void* /*context*/) {
    const core::speedhack::SpeedhackDisabledScope realWaits;
    std::this_thread::sleep_for(std::clamp(remaining, std::chrono::milliseconds(1), slice));
}

void OnNativeCall(std::string_view /*name*/) {}

bool RunFrame() {
    return false;
}

void WaitForTask(const std::atomic<bool>& done) {
    done.wait(false, std::memory_order_acquire);
}

}  // namespace d2bs::host
