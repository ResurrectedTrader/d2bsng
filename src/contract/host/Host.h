#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stop_token>
#include <string_view>
#include <thread>

// ReSharper disable once CppUnusedIncludeDirective - EnumName / format_as for the enumerations below
#include "ContractEnumNames.h"

// Extension points for the program that hosts the frontend and a backend (docs/host_hooks.md). Every executable that
// links them defines these functions once: d2bs.dll, the other injectable DLLs and js_tests compile
// core/host/DefaultHost.cpp, the in-process host (plain threads, real-time waits), whose definitions inline into their
// callers under LTO; an embedding host - a simulator that runs the game on a virtual clock - compiles its own. Nothing
// on the frontend or backend side defines them, and nothing here may name a host.
//
// Only the points a host cannot reach from outside belong here: the script threads' lifecycle and the hand-offs
// between them and the game thread. Taps on the backend's traffic and settings go through the backend's callback
// table, its launch options, the ini and V8's flags instead.
namespace d2bs::host {

// --- Scripts ---------------------------------------------------------------------------------------------------

// Starts the thread of the script `name` (its file name), running `body(context, token)` on it with the thread's
// stop token. The host joins it (JoinScriptThread); the frontend stops it through its stop token.
[[nodiscard]] std::jthread StartScriptThread(std::string_view name,
                                             void (*body)(void* context, const std::stop_token& token), void* context);
// Waits for the script's thread to end, then joins it. Called with no game lock held.
void JoinScriptThread(std::jthread& thread);
// Something is due for the script on `thread` in `delayMs` milliseconds (0: now) - an event, a timer, a stop request.
void OnScriptWake(const std::jthread& thread, uint32_t delayMs);
// The calling script's thread waits for events, `remaining` (real time) at most. Returns when it should look for
// events again: the in-process host sleeps `remaining` clamped to [1ms, slice]; a host that wakes the script for
// every event (OnScriptWake) may wait longer than `slice`, unless `mustWakeEachSlice(context)` (the script draws each
// frame).
void ScriptWait(std::chrono::milliseconds remaining, std::chrono::milliseconds slice,
                bool (*mustWakeEachSlice)(void* context), void* context);

// --- Native calls ----------------------------------------------------------------------------------------------

// Before every native binding a script calls (functions and accessors), on the script's thread.
void OnNativeCall(std::string_view name);

// --- The game thread -------------------------------------------------------------------------------------------

// The frame's wait (the game's sleep, GameLoop::OnSleep) with the frame's write lock already released: true when the
// host ran it, false to run the usual wait.
[[nodiscard]] bool RunFrame();
// A task GameThread::Execute posted to the game thread: Pending until the game thread starts it (Running) or the
// script cancels it first (Cancelled); a started task always reaches Done.
enum class TaskState : uint8_t { Pending, Running, Done, Cancelled };

// Waits until `state` is Done or Cancelled (acquire). Called with no game lock held.
void WaitForTask(const std::atomic<TaskState>& state);

}  // namespace d2bs::host
