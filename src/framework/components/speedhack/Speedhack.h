#pragma once

#include <Windows.h>

#include <cstdint>

// Global time-scaling ("speedhack"). Hooks the Win32 time/wait API surface so
// every read of monotonic / wall-clock time is multiplied by a single global
// multiplier, and every timed wait has its duration divided by that same
// multiplier. With speed = 2 the game's frame timer advances twice as fast and
// Sleep / WaitForSingleObject return after half the real-world wall time.
//
// Coverage matrix:
//   Reads (scaled up)       Waits (duration scaled down)
//   --------------------    -----------------------------
//   GetTickCount            Sleep                (hooked in HookManager,
//   GetTickCount64                                calls ScaleTimeout here)
//   QueryPerformanceCounter SleepEx
//   timeGetTime             WaitForSingleObject(Ex)
//   GetSystemTime           WaitForMultipleObjects(Ex)
//   GetLocalTime            MsgWaitForMultipleObjects(Ex)
//   GetSystemTimeAsFileTime SleepConditionVariableCS / SRW
//   GetSystemTimePreciseAsFileTime
//                           WaitOnAddress
//
// Not hooked: QueryPerformanceFrequency (constant; only the count is scaled),
// and the ntdll `Nt*` equivalents (the kernel32 wrappers above call into them,
// so hooking both would double-scale).
//
// Thread-safety: SetSpeed runs under a single mutex and re-anchors per-function
// base/offset pairs so virtual time stays continuous across speed changes.
// Read-path is lock-free (atomic loads only); the brief window where a reader
// can observe a torn (speed, base, offset) tuple manifests as a one-shot
// discontinuity of at most a few microseconds and is acceptable for our use.

namespace d2bs::speedhack {

constexpr float MIN_SPEED = 0.01F;
constexpr float MAX_SPEED = 40.0F;

// Set the global speed multiplier. Clamped to [MIN_SPEED, MAX_SPEED]. Calling
// with the current speed is a no-op.
void SetSpeed(float newSpeed);

// Current speed multiplier (last value passed to SetSpeed, post-clamp).
float GetSpeed();

// Scale a Sleep / wait timeout in milliseconds. INFINITE and 0 pass through
// unchanged. Other values are divided by speed (toward zero) - at high speeds
// a small ms can scale to 0 and become a yield, which is the correct
// speedhack contract for "sleep N virt-ms" but spins inside tight wall-time
// loops. Wrap such loops in SpeedhackDisabledScope.
DWORD ScaleTimeout(DWORD ms);

// Install / remove all speedhack hooks. Caller owns lifecycle (HookManager).
// Each call runs its own Detours transaction. Idempotent.
void Install();
void Remove();

// Opt the calling thread INTO speedhack scaling for its lifetime. Threads
// that haven't opted in see real time from the read hooks and unscaled
// timeouts from the wait hooks - the safe default, because we can't
// enumerate every background thread V8 (and friends) might spawn. Hook
// bodies running on threads that *did* opt in return virtual time / scaled
// timeouts.
//
// Opt in:
//   - the game thread (D2's main thread)
//   - each script worker thread (one per active JS isolate)
//
// Do NOT opt in: V8 internal workers, the console render thread, DDE
// pump, framework init, etc. Scaling Wait timeouts on V8 workers makes
// them spin and was the cause of the 2x-speed -> 10x-CPU pathology.
//
// Idempotent; one-way (thread_local, lives until thread exit).
void OptInCurrentThread();

// RAII full opt-out of the speedhack on the current thread: while in scope, both
// time reads and timed waits observe real (unscaled) time. Reentrant (saves and
// restores the prior opt-in state).
class SpeedhackDisabledScope {
   public:
    SpeedhackDisabledScope();
    ~SpeedhackDisabledScope();
    SpeedhackDisabledScope(const SpeedhackDisabledScope&) = delete;
    SpeedhackDisabledScope& operator=(const SpeedhackDisabledScope&) = delete;
    SpeedhackDisabledScope(SpeedhackDisabledScope&&) = delete;
    SpeedhackDisabledScope& operator=(SpeedhackDisabledScope&&) = delete;

   private:
    bool prev_{false};
};

// RAII nesting-depth guard for hooked wait calls. Tracks how deep we are in
// the wait-hook chain on this thread so any nested call (Sleep -> SleepEx,
// WaitForSingle -> WaitForMultiple, etc.) sees the chain is already scaling
// and passes the timeout through unchanged. Construct at the top of every
// hook body; ScaleTimeout reads the depth and skips scaling for any call
// past the outermost. Works regardless of which Windows internal calls which.
class NestedWaitGuard {
   public:
    NestedWaitGuard();
    // NOLINTNEXTLINE(performance-trivially-destructible) - real impl decrements waitChainDepth_; test fake defaults
    ~NestedWaitGuard();
    NestedWaitGuard(const NestedWaitGuard&) = delete;
    NestedWaitGuard& operator=(const NestedWaitGuard&) = delete;
    NestedWaitGuard(NestedWaitGuard&&) = delete;
    NestedWaitGuard& operator=(NestedWaitGuard&&) = delete;
};

}  // namespace d2bs::speedhack
