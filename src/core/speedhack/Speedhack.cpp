#include "speedhack/Speedhack.h"

#include <Windows.h>

#include <synchapi.h>
#include <timeapi.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "config/AppConfig.h"
#include "detour/Hook.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

#pragma comment(lib, "winmm.lib")

namespace d2bs::speedhack {

namespace {

spdlog::logger& Log() {
    static const auto LOGGER = utils::GetLogger("core.speedhack");
    return *LOGGER;
}

// State for one time domain (one DomainState per family of hooked reads that
// share a clock). All fields are atomic so hook bodies can load them without
// taking stateMutex; the mutex only serializes SetSpeed re-anchor writes.
struct DomainState {
    std::atomic<int64_t> realBase{0};
    std::atomic<int64_t> virtualOffset{0};
};

struct Snapshot {
    float speed;
    int64_t realBase;
    int64_t virtualOffset;
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex stateMutex;
// The speed multiplier lives in AppConfig.speed (read here, written
// exclusively from SetSpeed under stateMutex). Read hooks always go
// through ToVirtual on opted-in threads - at speed=1.0 the per-domain
// snapshot returns `realBase + delta * 1.0 + virtualOffset`, which is
// monotonic across speed changes (the accumulated offset from any
// earlier non-1.0 phase isn't dropped when speed returns to 1.0). Wait
// hooks (ScaleTimeout) ARE stateless, so they short-circuit at speed=1.0.
std::atomic isInstalled{false};

// Latched true by ReanchorLocked the first time the multiplier actually moves,
// and never cleared - so the virtual offset accumulated during any past non-1.0
// phase keeps applying even after speed returns to 1.0 (see ToVirtual). While it
// is false (the speedhack was never engaged this run - the common case) the read
// hooks below skip the per-domain snapshot + scale and return the real OS value,
// collapsing each hooked clock read back to ~a real read.
std::atomic scalingActive{false};

// Per-thread opt-in. Threads default to "not scaled" so V8's background
// workers (parallel marker, compiler, sweeper, ...) and any other thread
// we don't explicitly know about see real time. The game thread and each
// script worker thread call OptInCurrentThread() to enable scaling for
// themselves. thread_local keeps the read path free of atomics.
thread_local bool threadOptIn = false;

// Recursion depth of hooked wait functions on this thread. Each hook body
// constructs a NestedWaitGuard at entry which increments this; ScaleTimeout
// scales only when depth == 1 (we are the outermost wait in the chain).
// Protects against the Sleep -> SleepEx -> NtDelayExecution chain (and the
// Wait family's analogous chaining) double-scaling the same logical call.
thread_local int waitChainDepth = 0;

DomainState dwMsState;      // GetTickCount / timeGetTime
DomainState u64MsState;     // GetTickCount64
DomainState qpcState;       // QueryPerformanceCounter
DomainState fileTimeState;  // GetSystemTime* / GetLocalTime / *AsFileTime

using GetTickCountFn = DWORD(WINAPI*)();
using GetTickCount64Fn = ULONGLONG(WINAPI*)();
using QueryPerformanceCounterFn = BOOL(WINAPI*)(LARGE_INTEGER*);
using TimeGetTimeFn = DWORD(WINAPI*)();
using GetSystemTimeFn = VOID(WINAPI*)(LPSYSTEMTIME);
using GetLocalTimeFn = VOID(WINAPI*)(LPSYSTEMTIME);
using GetSystemTimeAsFileTimeFn = VOID(WINAPI*)(LPFILETIME);
using GetSystemTimePreciseAsFileTimeFn = VOID(WINAPI*)(LPFILETIME);
using SleepExFn = DWORD(WINAPI*)(DWORD, BOOL);
using WaitForSingleObjectFn = DWORD(WINAPI*)(HANDLE, DWORD);
using WaitForSingleObjectExFn = DWORD(WINAPI*)(HANDLE, DWORD, BOOL);
using WaitForMultipleObjectsFn = DWORD(WINAPI*)(DWORD, const HANDLE*, BOOL, DWORD);
using WaitForMultipleObjectsExFn = DWORD(WINAPI*)(DWORD, const HANDLE*, BOOL, DWORD, BOOL);
using MsgWaitForMultipleObjectsFn = DWORD(WINAPI*)(DWORD, const HANDLE*, BOOL, DWORD, DWORD);
using MsgWaitForMultipleObjectsExFn = DWORD(WINAPI*)(DWORD, const HANDLE*, DWORD, DWORD, DWORD);
using SleepConditionVariableCSFn = BOOL(WINAPI*)(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD);
using SleepConditionVariableSRWFn = BOOL(WINAPI*)(PCONDITION_VARIABLE, PSRWLOCK, DWORD, ULONG);
using WaitOnAddressFn = BOOL(WINAPI*)(volatile VOID*, PVOID, SIZE_T, DWORD);

// The replacements the slots below are paired with; the bodies follow.
DWORD WINAPI HookedGetTickCount();
ULONGLONG WINAPI HookedGetTickCount64();
BOOL WINAPI HookedQueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount);
DWORD WINAPI HookedTimeGetTime();
VOID WINAPI HookedGetSystemTime(LPSYSTEMTIME lpSystemTime);
VOID WINAPI HookedGetLocalTime(LPSYSTEMTIME lpSystemTime);
VOID WINAPI HookedGetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime);
VOID WINAPI HookedGetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime);
DWORD WINAPI HookedSleepEx(DWORD dwMilliseconds, BOOL bAlertable);
DWORD WINAPI HookedWaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
DWORD WINAPI HookedWaitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable);
DWORD WINAPI HookedWaitForMultipleObjects(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds);
DWORD WINAPI HookedWaitForMultipleObjectsEx(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds,
                                            BOOL bAlertable);
DWORD WINAPI HookedMsgWaitForMultipleObjects(DWORD nCount, const HANDLE* pHandles, BOOL fWaitAll, DWORD dwMilliseconds,
                                             DWORD dwWakeMask);
DWORD WINAPI HookedMsgWaitForMultipleObjectsEx(DWORD nCount, const HANDLE* pHandles, DWORD dwMilliseconds,
                                               DWORD dwWakeMask, DWORD dwFlags);
BOOL WINAPI HookedSleepConditionVariableCS(PCONDITION_VARIABLE conditionVariable, PCRITICAL_SECTION criticalSection,
                                           DWORD dwMilliseconds);
BOOL WINAPI HookedSleepConditionVariableSRW(PCONDITION_VARIABLE conditionVariable, PSRWLOCK srwLock,
                                            DWORD dwMilliseconds, ULONG flags);
BOOL WINAPI HookedWaitOnAddress(volatile VOID* address, PVOID compareAddress, SIZE_T addressSize, DWORD dwMilliseconds);

detour::Hook<GetTickCountFn> getTickCountHook{GetTickCount, &HookedGetTickCount};
detour::Hook<GetTickCount64Fn> getTickCount64Hook{GetTickCount64, &HookedGetTickCount64};
detour::Hook<QueryPerformanceCounterFn> queryPerformanceCounterHook{QueryPerformanceCounter,
                                                                    &HookedQueryPerformanceCounter};
detour::Hook<TimeGetTimeFn> timeGetTimeHook{timeGetTime, &HookedTimeGetTime};
detour::Hook<GetSystemTimeFn> getSystemTimeHook{GetSystemTime, &HookedGetSystemTime};
detour::Hook<GetLocalTimeFn> getLocalTimeHook{GetLocalTime, &HookedGetLocalTime};
detour::Hook<GetSystemTimeAsFileTimeFn> getSystemTimeAsFileTimeHook{GetSystemTimeAsFileTime,
                                                                    &HookedGetSystemTimeAsFileTime};
detour::Hook<GetSystemTimePreciseAsFileTimeFn> getSystemTimePreciseAsFileTimeHook{
    GetSystemTimePreciseAsFileTime, &HookedGetSystemTimePreciseAsFileTime};
detour::Hook<SleepExFn> sleepExHook{SleepEx, &HookedSleepEx};
detour::Hook<WaitForSingleObjectFn> waitForSingleObjectHook{WaitForSingleObject, &HookedWaitForSingleObject};
detour::Hook<WaitForSingleObjectExFn> waitForSingleObjectExHook{WaitForSingleObjectEx, &HookedWaitForSingleObjectEx};
detour::Hook<WaitForMultipleObjectsFn> waitForMultipleObjectsHook{WaitForMultipleObjects,
                                                                  &HookedWaitForMultipleObjects};
detour::Hook<WaitForMultipleObjectsExFn> waitForMultipleObjectsExHook{WaitForMultipleObjectsEx,
                                                                      &HookedWaitForMultipleObjectsEx};
detour::Hook<MsgWaitForMultipleObjectsFn> msgWaitForMultipleObjectsHook{MsgWaitForMultipleObjects,
                                                                        &HookedMsgWaitForMultipleObjects};
detour::Hook<MsgWaitForMultipleObjectsExFn> msgWaitForMultipleObjectsExHook{MsgWaitForMultipleObjectsEx,
                                                                            &HookedMsgWaitForMultipleObjectsEx};
detour::Hook<SleepConditionVariableCSFn> sleepConditionVariableCSHook{SleepConditionVariableCS,
                                                                      &HookedSleepConditionVariableCS};
detour::Hook<SleepConditionVariableSRWFn> sleepConditionVariableSRWHook{SleepConditionVariableSRW,
                                                                        &HookedSleepConditionVariableSRW};
detour::Hook<WaitOnAddressFn> waitOnAddressHook{WaitOnAddress, &HookedWaitOnAddress};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Every hook in one transaction: the speedhack is only coherent with all of
// its clock and wait reads scaled together.
const std::array<detour::Slot* const, 18> HOOKS = {&getTickCountHook,
                                                   &getTickCount64Hook,
                                                   &queryPerformanceCounterHook,
                                                   &timeGetTimeHook,
                                                   &getSystemTimeHook,
                                                   &getLocalTimeHook,
                                                   &getSystemTimeAsFileTimeHook,
                                                   &getSystemTimePreciseAsFileTimeHook,
                                                   &sleepExHook,
                                                   &waitForSingleObjectHook,
                                                   &waitForSingleObjectExHook,
                                                   &waitForMultipleObjectsHook,
                                                   &waitForMultipleObjectsExHook,
                                                   &msgWaitForMultipleObjectsHook,
                                                   &msgWaitForMultipleObjectsExHook,
                                                   &sleepConditionVariableCSHook,
                                                   &sleepConditionVariableSRWHook,
                                                   &waitOnAddressHook};

Snapshot LoadSnapshot(const DomainState& state) {
    return {
        .speed = config::GetAppConfig().speed.load(std::memory_order_relaxed),
        .realBase = state.realBase.load(std::memory_order_relaxed),
        .virtualOffset = state.virtualOffset.load(std::memory_order_relaxed),
    };
}

int64_t ToVirtual(const Snapshot& snap, int64_t real) {
    const auto delta = static_cast<double>(real - snap.realBase);
    return snap.virtualOffset + static_cast<int64_t>(delta * static_cast<double>(snap.speed));
}

// FILETIME packing helpers - convert between {dwLowDateTime, dwHighDateTime}
// and the 64-bit count of 100-ns intervals since Jan 1, 1601 it represents.

int64_t FileTimeToInt64(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>(u.QuadPart);
}

FILETIME Int64ToFileTime(int64_t value) {
    ULARGE_INTEGER u;
    u.QuadPart = static_cast<uint64_t>(value);
    return FILETIME{.dwLowDateTime = u.LowPart, .dwHighDateTime = u.HighPart};
}

// True if the calling thread should observe scaled time / waits: it opted in
// AND it has this module's thread-local storage. Foreign threads (e.g. a staged
// loader DLL's workers) can be created without loader TLS init, leaving
// TEB->ThreadLocalStoragePointer NULL; reading the `threadOptIn` thread_local on
// them access-violates. Gating on HasThreadLocalStorage() first keeps every hook
// body a safe pass-through for such threads, and must run before any thread_local
// read in a hook.
bool ThreadOptedIn() {
    return thread_utils::HasThreadLocalStorage() && threadOptIn;
}

// Scaling is observable on this thread only once it has actually been engaged
// (scalingActive) AND this thread opted in. scalingActive is loaded first so the
// common "never engaged" path short-circuits to a single relaxed atomic read,
// before the TLS-presence probe / thread_local read and the snapshot + scale.
bool ScalingEngaged() {
    return scalingActive.load(std::memory_order_relaxed) && ThreadOptedIn();
}

// Read-API hooks ------------------------------------------------------------

DWORD WINAPI HookedGetTickCount() {
    const DWORD real = getTickCountHook();
    if (!ScalingEngaged()) {
        return real;
    }
    const auto snap = LoadSnapshot(dwMsState);
    // 32-bit unsigned subtraction wraps correctly across the DWORD rollover
    const DWORD delta = real - static_cast<DWORD>(snap.realBase);
    return static_cast<DWORD>(snap.virtualOffset) + static_cast<DWORD>(static_cast<double>(delta) * snap.speed);
}

ULONGLONG WINAPI HookedGetTickCount64() {
    const ULONGLONG real = getTickCount64Hook();
    if (!ScalingEngaged()) {
        return real;
    }
    const auto snap = LoadSnapshot(u64MsState);
    return static_cast<ULONGLONG>(ToVirtual(snap, static_cast<int64_t>(real)));
}

BOOL WINAPI HookedQueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
    LARGE_INTEGER realQpc;
    const BOOL ok = queryPerformanceCounterHook(&realQpc);
    if (!ok || lpPerformanceCount == nullptr) {
        return ok;
    }
    if (!ScalingEngaged()) {
        *lpPerformanceCount = realQpc;
        return TRUE;
    }
    const auto snap = LoadSnapshot(qpcState);
    lpPerformanceCount->QuadPart = ToVirtual(snap, realQpc.QuadPart);
    return TRUE;
}

DWORD WINAPI HookedTimeGetTime() {
    const DWORD real = timeGetTimeHook();
    if (!ScalingEngaged()) {
        return real;
    }
    // Shares dwMsState with GetTickCount - both are DWORD ms counters, close
    // enough in phase to use the same base/offset. If divergence ever shows
    // up in practice, give timeGetTime its own DomainState.
    const auto snap = LoadSnapshot(dwMsState);
    const DWORD delta = real - static_cast<DWORD>(snap.realBase);
    return static_cast<DWORD>(snap.virtualOffset) + static_cast<DWORD>(static_cast<double>(delta) * snap.speed);
}

VOID WINAPI HookedGetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
    if (lpSystemTimeAsFileTime == nullptr) {
        return;
    }
    FILETIME realFt;
    getSystemTimeAsFileTimeHook(&realFt);
    if (!ScalingEngaged()) {
        *lpSystemTimeAsFileTime = realFt;
        return;
    }
    const auto snap = LoadSnapshot(fileTimeState);
    *lpSystemTimeAsFileTime = Int64ToFileTime(ToVirtual(snap, FileTimeToInt64(realFt)));
}

VOID WINAPI HookedGetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
    if (lpSystemTimeAsFileTime == nullptr) {
        return;
    }
    FILETIME realFt;
    getSystemTimePreciseAsFileTimeHook(&realFt);
    if (!ScalingEngaged()) {
        *lpSystemTimeAsFileTime = realFt;
        return;
    }
    const auto snap = LoadSnapshot(fileTimeState);
    *lpSystemTimeAsFileTime = Int64ToFileTime(ToVirtual(snap, FileTimeToInt64(realFt)));
}

VOID WINAPI HookedGetSystemTime(LPSYSTEMTIME lpSystemTime) {
    if (lpSystemTime == nullptr) {
        return;
    }
    if (!ScalingEngaged()) {
        getSystemTimeHook(lpSystemTime);
        return;
    }
    FILETIME realFt;
    getSystemTimeAsFileTimeHook(&realFt);
    const auto snap = LoadSnapshot(fileTimeState);
    const FILETIME virtFt = Int64ToFileTime(ToVirtual(snap, FileTimeToInt64(realFt)));
    FileTimeToSystemTime(&virtFt, lpSystemTime);
}

VOID WINAPI HookedGetLocalTime(LPSYSTEMTIME lpSystemTime) {
    if (lpSystemTime == nullptr) {
        return;
    }
    if (!ScalingEngaged()) {
        getLocalTimeHook(lpSystemTime);
        return;
    }
    // Compute virtual UTC FILETIME first, then convert UTC -> local via the
    // OS - avoids cracking the timezone offset ourselves.
    FILETIME realFt;
    getSystemTimeAsFileTimeHook(&realFt);
    const auto snap = LoadSnapshot(fileTimeState);
    const FILETIME virtFtUtc = Int64ToFileTime(ToVirtual(snap, FileTimeToInt64(realFt)));
    SYSTEMTIME utc;
    FileTimeToSystemTime(&virtFtUtc, &utc);
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, lpSystemTime);
}

// Wait-API hooks - scale the timeout argument and forward. INFINITE handled
// by ScaleTimeout. Sleep itself is hooked in HookManager which calls
// ScaleTimeout for the pass-through paths.

DWORD WINAPI HookedSleepEx(DWORD dwMilliseconds, BOOL bAlertable) {
    NestedWaitGuard guard;
    return sleepExHook(ScaleTimeout(dwMilliseconds), bAlertable);
}

DWORD WINAPI HookedWaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    NestedWaitGuard guard;
    return waitForSingleObjectHook(hHandle, ScaleTimeout(dwMilliseconds));
}

DWORD WINAPI HookedWaitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable) {
    NestedWaitGuard guard;
    return waitForSingleObjectExHook(hHandle, ScaleTimeout(dwMilliseconds), bAlertable);
}

DWORD WINAPI HookedWaitForMultipleObjects(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds) {
    NestedWaitGuard guard;
    return waitForMultipleObjectsHook(nCount, lpHandles, bWaitAll, ScaleTimeout(dwMilliseconds));
}

DWORD WINAPI HookedWaitForMultipleObjectsEx(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds,
                                            BOOL bAlertable) {
    NestedWaitGuard guard;
    return waitForMultipleObjectsExHook(nCount, lpHandles, bWaitAll, ScaleTimeout(dwMilliseconds), bAlertable);
}

DWORD WINAPI HookedMsgWaitForMultipleObjects(DWORD nCount, const HANDLE* pHandles, BOOL fWaitAll, DWORD dwMilliseconds,
                                             DWORD dwWakeMask) {
    NestedWaitGuard guard;
    return msgWaitForMultipleObjectsHook(nCount, pHandles, fWaitAll, ScaleTimeout(dwMilliseconds), dwWakeMask);
}

DWORD WINAPI HookedMsgWaitForMultipleObjectsEx(DWORD nCount, const HANDLE* pHandles, DWORD dwMilliseconds,
                                               DWORD dwWakeMask, DWORD dwFlags) {
    NestedWaitGuard guard;
    return msgWaitForMultipleObjectsExHook(nCount, pHandles, ScaleTimeout(dwMilliseconds), dwWakeMask, dwFlags);
}

BOOL WINAPI HookedSleepConditionVariableCS(PCONDITION_VARIABLE conditionVariable, PCRITICAL_SECTION criticalSection,
                                           DWORD dwMilliseconds) {
    NestedWaitGuard guard;
    return sleepConditionVariableCSHook(conditionVariable, criticalSection, ScaleTimeout(dwMilliseconds));
}

BOOL WINAPI HookedSleepConditionVariableSRW(PCONDITION_VARIABLE conditionVariable, PSRWLOCK srwLock,
                                            DWORD dwMilliseconds, ULONG flags) {
    NestedWaitGuard guard;
    return sleepConditionVariableSRWHook(conditionVariable, srwLock, ScaleTimeout(dwMilliseconds), flags);
}

BOOL WINAPI HookedWaitOnAddress(volatile VOID* address, PVOID compareAddress, SIZE_T addressSize,
                                DWORD dwMilliseconds) {
    NestedWaitGuard guard;
    return waitOnAddressHook(address, compareAddress, addressSize, ScaleTimeout(dwMilliseconds));
}

// SetSpeed re-anchor: freeze virtual time at the current instant using the
// outgoing speed, then start a new linear curve from (realNow, virtualNow)
// with the incoming speed. Keeps virtual time continuous across changes.
void ReanchorLocked(float oldSpeed, float newSpeed) {
    LARGE_INTEGER realQpc;
    queryPerformanceCounterHook(&realQpc);
    FILETIME realFt;
    getSystemTimeAsFileTimeHook(&realFt);
    const int64_t realDwMs = getTickCountHook();
    const int64_t realU64Ms = static_cast<int64_t>(getTickCount64Hook());

    auto reanchor = [oldSpeed](DomainState& state, int64_t realNow) {
        const int64_t base = state.realBase.load(std::memory_order_relaxed);
        const int64_t offset = state.virtualOffset.load(std::memory_order_relaxed);
        const auto delta = static_cast<double>(realNow - base);
        const int64_t virtualNow = offset + static_cast<int64_t>(delta * static_cast<double>(oldSpeed));
        state.realBase.store(realNow, std::memory_order_relaxed);
        state.virtualOffset.store(virtualNow, std::memory_order_relaxed);
    };
    reanchor(dwMsState, realDwMs);
    reanchor(u64MsState, realU64Ms);
    reanchor(qpcState, realQpc.QuadPart);
    reanchor(fileTimeState, FileTimeToInt64(realFt));
    config::GetAppConfig().speed.store(newSpeed, std::memory_order_relaxed);
    scalingActive.store(true, std::memory_order_relaxed);
}

}  // namespace

// Public API ----------------------------------------------------------------

void SetSpeed(float newSpeed) {
    newSpeed = std::clamp(newSpeed, MIN_SPEED, MAX_SPEED);
    std::lock_guard lock(stateMutex);
    const float oldSpeed = config::GetAppConfig().speed.load(std::memory_order_relaxed);
    if (oldSpeed == newSpeed) {
        return;
    }
    ReanchorLocked(oldSpeed, newSpeed);
}

float GetSpeed() {
    return config::GetAppConfig().speed.load(std::memory_order_relaxed);
}

DWORD ScaleTimeout(DWORD ms) {
    if (ms == INFINITE) {
        return INFINITE;
    }
    if (!ScalingEngaged()) {
        return ms;
    }
    const float s = config::GetAppConfig().speed.load(std::memory_order_relaxed);
    if (s == 1.0F) {
        return ms;
    }
    // Depth > 1 means an outer hook on this thread already scaled this call
    // and we're a nested chain step (Sleep -> SleepEx, WaitForSingle ->
    // WaitForMultiple, etc.). Pass through to avoid double-scaling.
    if (waitChainDepth > 1) {
        return ms;
    }
    return static_cast<DWORD>(static_cast<float>(ms) / s);
}

void OptInCurrentThread() {
    threadOptIn = true;
}

SpeedhackDisabledScope::SpeedhackDisabledScope() : prev_(threadOptIn) {
    threadOptIn = false;
}

SpeedhackDisabledScope::~SpeedhackDisabledScope() {
    threadOptIn = prev_;
}

NestedWaitGuard::NestedWaitGuard() {
    // Skip the thread_local touch on foreign threads with no module TLS (e.g. a
    // staged loader DLL's workers) - reading waitChainDepth there access-
    // violates. TLS presence is stable for the life of a thread, so the dtor's
    // identical check always matches: no unbalanced decrement.
    if (thread_utils::HasThreadLocalStorage()) {
        ++waitChainDepth;
    }
}

NestedWaitGuard::~NestedWaitGuard() {
    if (thread_utils::HasThreadLocalStorage()) {
        --waitChainDepth;
    }
}

void Install() {
    bool expected = false;
    if (!isInstalled.compare_exchange_strong(expected, true)) {
        return;
    }

    if (const int32_t err = detour::AttachAll(HOOKS); err != 0) {
        Log().error("speedhack: Detours install failed: {}", err);
    }
}

void Remove() {
    bool expected = true;
    if (!isInstalled.compare_exchange_strong(expected, false)) {
        return;
    }

    if (const int32_t err = detour::DetachAll(HOOKS); err != 0) {
        Log().error("speedhack: Detours remove failed: {}", err);
    }
}

}  // namespace d2bs::speedhack
