#include "components/speedhack/Speedhack.h"

#include <Windows.h>

#include <synchapi.h>
#include <timeapi.h>

#include <detours/detours.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "components/config/AppConfig.h"
#include "utils/threadutils.h"

#pragma comment(lib, "winmm.lib")

namespace d2bs::speedhack {

namespace {

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
std::atomic<bool> isInstalled{false};

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

GetTickCountFn realGetTickCount = ::GetTickCount;
GetTickCount64Fn realGetTickCount64 = ::GetTickCount64;
QueryPerformanceCounterFn realQueryPerformanceCounter = ::QueryPerformanceCounter;
TimeGetTimeFn realTimeGetTime = ::timeGetTime;
GetSystemTimeFn realGetSystemTime = ::GetSystemTime;
GetLocalTimeFn realGetLocalTime = ::GetLocalTime;
GetSystemTimeAsFileTimeFn realGetSystemTimeAsFileTime = ::GetSystemTimeAsFileTime;
GetSystemTimePreciseAsFileTimeFn realGetSystemTimePreciseAsFileTime = ::GetSystemTimePreciseAsFileTime;
SleepExFn realSleepEx = ::SleepEx;
WaitForSingleObjectFn realWaitForSingleObject = ::WaitForSingleObject;
WaitForSingleObjectExFn realWaitForSingleObjectEx = ::WaitForSingleObjectEx;
WaitForMultipleObjectsFn realWaitForMultipleObjects = ::WaitForMultipleObjects;
WaitForMultipleObjectsExFn realWaitForMultipleObjectsEx = ::WaitForMultipleObjectsEx;
MsgWaitForMultipleObjectsFn realMsgWaitForMultipleObjects = ::MsgWaitForMultipleObjects;
MsgWaitForMultipleObjectsExFn realMsgWaitForMultipleObjectsEx = ::MsgWaitForMultipleObjectsEx;
SleepConditionVariableCSFn realSleepConditionVariableCS = ::SleepConditionVariableCS;
SleepConditionVariableSRWFn realSleepConditionVariableSRW = ::SleepConditionVariableSRW;
WaitOnAddressFn realWaitOnAddress = ::WaitOnAddress;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

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
    return d2bs::thread_utils::HasThreadLocalStorage() && threadOptIn;
}

// Read-API hooks ------------------------------------------------------------

DWORD WINAPI HookedGetTickCount() {
    const DWORD real = realGetTickCount();
    if (!ThreadOptedIn()) {
        return real;
    }
    const auto snap = LoadSnapshot(dwMsState);
    // 32-bit unsigned subtraction wraps correctly across the DWORD rollover
    const DWORD delta = real - static_cast<DWORD>(snap.realBase);
    return static_cast<DWORD>(snap.virtualOffset) + static_cast<DWORD>(static_cast<double>(delta) * snap.speed);
}

ULONGLONG WINAPI HookedGetTickCount64() {
    const ULONGLONG real = realGetTickCount64();
    if (!ThreadOptedIn()) {
        return real;
    }
    const auto snap = LoadSnapshot(u64MsState);
    return static_cast<ULONGLONG>(ToVirtual(snap, static_cast<int64_t>(real)));
}

BOOL WINAPI HookedQueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
    LARGE_INTEGER realQpc;
    const BOOL ok = realQueryPerformanceCounter(&realQpc);
    if (!ok || lpPerformanceCount == nullptr) {
        return ok;
    }
    if (!ThreadOptedIn()) {
        *lpPerformanceCount = realQpc;
        return TRUE;
    }
    const auto snap = LoadSnapshot(qpcState);
    lpPerformanceCount->QuadPart = ToVirtual(snap, realQpc.QuadPart);
    return TRUE;
}

DWORD WINAPI HookedTimeGetTime() {
    const DWORD real = realTimeGetTime();
    if (!ThreadOptedIn()) {
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
    realGetSystemTimeAsFileTime(&realFt);
    if (!ThreadOptedIn()) {
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
    realGetSystemTimePreciseAsFileTime(&realFt);
    if (!ThreadOptedIn()) {
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
    if (!ThreadOptedIn()) {
        realGetSystemTime(lpSystemTime);
        return;
    }
    FILETIME realFt;
    realGetSystemTimeAsFileTime(&realFt);
    const auto snap = LoadSnapshot(fileTimeState);
    const FILETIME virtFt = Int64ToFileTime(ToVirtual(snap, FileTimeToInt64(realFt)));
    FileTimeToSystemTime(&virtFt, lpSystemTime);
}

VOID WINAPI HookedGetLocalTime(LPSYSTEMTIME lpSystemTime) {
    if (lpSystemTime == nullptr) {
        return;
    }
    if (!ThreadOptedIn()) {
        realGetLocalTime(lpSystemTime);
        return;
    }
    // Compute virtual UTC FILETIME first, then convert UTC -> local via the
    // OS - avoids cracking the timezone offset ourselves.
    FILETIME realFt;
    realGetSystemTimeAsFileTime(&realFt);
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
    return realSleepEx(ScaleTimeout(dwMilliseconds), bAlertable);
}

DWORD WINAPI HookedWaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    NestedWaitGuard guard;
    return realWaitForSingleObject(hHandle, ScaleTimeout(dwMilliseconds));
}

DWORD WINAPI HookedWaitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable) {
    NestedWaitGuard guard;
    return realWaitForSingleObjectEx(hHandle, ScaleTimeout(dwMilliseconds), bAlertable);
}

DWORD WINAPI HookedWaitForMultipleObjects(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds) {
    NestedWaitGuard guard;
    return realWaitForMultipleObjects(nCount, lpHandles, bWaitAll, ScaleTimeout(dwMilliseconds));
}

DWORD WINAPI HookedWaitForMultipleObjectsEx(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds,
                                            BOOL bAlertable) {
    NestedWaitGuard guard;
    return realWaitForMultipleObjectsEx(nCount, lpHandles, bWaitAll, ScaleTimeout(dwMilliseconds), bAlertable);
}

DWORD WINAPI HookedMsgWaitForMultipleObjects(DWORD nCount, const HANDLE* pHandles, BOOL fWaitAll, DWORD dwMilliseconds,
                                             DWORD dwWakeMask) {
    NestedWaitGuard guard;
    return realMsgWaitForMultipleObjects(nCount, pHandles, fWaitAll, ScaleTimeout(dwMilliseconds), dwWakeMask);
}

DWORD WINAPI HookedMsgWaitForMultipleObjectsEx(DWORD nCount, const HANDLE* pHandles, DWORD dwMilliseconds,
                                               DWORD dwWakeMask, DWORD dwFlags) {
    NestedWaitGuard guard;
    return realMsgWaitForMultipleObjectsEx(nCount, pHandles, ScaleTimeout(dwMilliseconds), dwWakeMask, dwFlags);
}

BOOL WINAPI HookedSleepConditionVariableCS(PCONDITION_VARIABLE conditionVariable, PCRITICAL_SECTION criticalSection,
                                           DWORD dwMilliseconds) {
    NestedWaitGuard guard;
    return realSleepConditionVariableCS(conditionVariable, criticalSection, ScaleTimeout(dwMilliseconds));
}

BOOL WINAPI HookedSleepConditionVariableSRW(PCONDITION_VARIABLE conditionVariable, PSRWLOCK srwLock,
                                            DWORD dwMilliseconds, ULONG flags) {
    NestedWaitGuard guard;
    return realSleepConditionVariableSRW(conditionVariable, srwLock, ScaleTimeout(dwMilliseconds), flags);
}

BOOL WINAPI HookedWaitOnAddress(volatile VOID* address, PVOID compareAddress, SIZE_T addressSize,
                                DWORD dwMilliseconds) {
    NestedWaitGuard guard;
    return realWaitOnAddress(address, compareAddress, addressSize, ScaleTimeout(dwMilliseconds));
}

// SetSpeed re-anchor: freeze virtual time at the current instant using the
// outgoing speed, then start a new linear curve from (realNow, virtualNow)
// with the incoming speed. Keeps virtual time continuous across changes.
void ReanchorLocked(float oldSpeed, float newSpeed) {
    LARGE_INTEGER realQpc;
    realQueryPerformanceCounter(&realQpc);
    FILETIME realFt;
    realGetSystemTimeAsFileTime(&realFt);
    const int64_t realDwMs = static_cast<int64_t>(realGetTickCount());
    const int64_t realU64Ms = static_cast<int64_t>(realGetTickCount64());

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
    if (!ThreadOptedIn()) {
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
    if (d2bs::thread_utils::HasThreadLocalStorage()) {
        ++waitChainDepth;
    }
}

NestedWaitGuard::~NestedWaitGuard() {
    if (d2bs::thread_utils::HasThreadLocalStorage()) {
        --waitChainDepth;
    }
}

void Install() {
    bool expected = false;
    if (!isInstalled.compare_exchange_strong(expected, true)) {
        return;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&realGetTickCount), reinterpret_cast<PVOID>(&HookedGetTickCount));
    DetourAttach(reinterpret_cast<PVOID*>(&realGetTickCount64), reinterpret_cast<PVOID>(&HookedGetTickCount64));
    DetourAttach(reinterpret_cast<PVOID*>(&realQueryPerformanceCounter),
                 reinterpret_cast<PVOID>(&HookedQueryPerformanceCounter));
    DetourAttach(reinterpret_cast<PVOID*>(&realTimeGetTime), reinterpret_cast<PVOID>(&HookedTimeGetTime));
    DetourAttach(reinterpret_cast<PVOID*>(&realGetSystemTime), reinterpret_cast<PVOID>(&HookedGetSystemTime));
    DetourAttach(reinterpret_cast<PVOID*>(&realGetLocalTime), reinterpret_cast<PVOID>(&HookedGetLocalTime));
    DetourAttach(reinterpret_cast<PVOID*>(&realGetSystemTimeAsFileTime),
                 reinterpret_cast<PVOID>(&HookedGetSystemTimeAsFileTime));
    DetourAttach(reinterpret_cast<PVOID*>(&realGetSystemTimePreciseAsFileTime),
                 reinterpret_cast<PVOID>(&HookedGetSystemTimePreciseAsFileTime));
    DetourAttach(reinterpret_cast<PVOID*>(&realSleepEx), reinterpret_cast<PVOID>(&HookedSleepEx));
    DetourAttach(reinterpret_cast<PVOID*>(&realWaitForSingleObject),
                 reinterpret_cast<PVOID>(&HookedWaitForSingleObject));
    DetourAttach(reinterpret_cast<PVOID*>(&realWaitForSingleObjectEx),
                 reinterpret_cast<PVOID>(&HookedWaitForSingleObjectEx));
    DetourAttach(reinterpret_cast<PVOID*>(&realWaitForMultipleObjects),
                 reinterpret_cast<PVOID>(&HookedWaitForMultipleObjects));
    DetourAttach(reinterpret_cast<PVOID*>(&realWaitForMultipleObjectsEx),
                 reinterpret_cast<PVOID>(&HookedWaitForMultipleObjectsEx));
    DetourAttach(reinterpret_cast<PVOID*>(&realMsgWaitForMultipleObjects),
                 reinterpret_cast<PVOID>(&HookedMsgWaitForMultipleObjects));
    DetourAttach(reinterpret_cast<PVOID*>(&realMsgWaitForMultipleObjectsEx),
                 reinterpret_cast<PVOID>(&HookedMsgWaitForMultipleObjectsEx));
    DetourAttach(reinterpret_cast<PVOID*>(&realSleepConditionVariableCS),
                 reinterpret_cast<PVOID>(&HookedSleepConditionVariableCS));
    DetourAttach(reinterpret_cast<PVOID*>(&realSleepConditionVariableSRW),
                 reinterpret_cast<PVOID>(&HookedSleepConditionVariableSRW));
    DetourAttach(reinterpret_cast<PVOID*>(&realWaitOnAddress), reinterpret_cast<PVOID>(&HookedWaitOnAddress));
    const LONG err = DetourTransactionCommit();
    if (err != NO_ERROR) {
        spdlog::error("speedhack: Detours install failed: {}", err);
    }
}

void Remove() {
    bool expected = true;
    if (!isInstalled.compare_exchange_strong(expected, false)) {
        return;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID*>(&realGetTickCount), reinterpret_cast<PVOID>(&HookedGetTickCount));
    DetourDetach(reinterpret_cast<PVOID*>(&realGetTickCount64), reinterpret_cast<PVOID>(&HookedGetTickCount64));
    DetourDetach(reinterpret_cast<PVOID*>(&realQueryPerformanceCounter),
                 reinterpret_cast<PVOID>(&HookedQueryPerformanceCounter));
    DetourDetach(reinterpret_cast<PVOID*>(&realTimeGetTime), reinterpret_cast<PVOID>(&HookedTimeGetTime));
    DetourDetach(reinterpret_cast<PVOID*>(&realGetSystemTime), reinterpret_cast<PVOID>(&HookedGetSystemTime));
    DetourDetach(reinterpret_cast<PVOID*>(&realGetLocalTime), reinterpret_cast<PVOID>(&HookedGetLocalTime));
    DetourDetach(reinterpret_cast<PVOID*>(&realGetSystemTimeAsFileTime),
                 reinterpret_cast<PVOID>(&HookedGetSystemTimeAsFileTime));
    DetourDetach(reinterpret_cast<PVOID*>(&realGetSystemTimePreciseAsFileTime),
                 reinterpret_cast<PVOID>(&HookedGetSystemTimePreciseAsFileTime));
    DetourDetach(reinterpret_cast<PVOID*>(&realSleepEx), reinterpret_cast<PVOID>(&HookedSleepEx));
    DetourDetach(reinterpret_cast<PVOID*>(&realWaitForSingleObject),
                 reinterpret_cast<PVOID>(&HookedWaitForSingleObject));
    DetourDetach(reinterpret_cast<PVOID*>(&realWaitForSingleObjectEx),
                 reinterpret_cast<PVOID>(&HookedWaitForSingleObjectEx));
    DetourDetach(reinterpret_cast<PVOID*>(&realWaitForMultipleObjects),
                 reinterpret_cast<PVOID>(&HookedWaitForMultipleObjects));
    DetourDetach(reinterpret_cast<PVOID*>(&realWaitForMultipleObjectsEx),
                 reinterpret_cast<PVOID>(&HookedWaitForMultipleObjectsEx));
    DetourDetach(reinterpret_cast<PVOID*>(&realMsgWaitForMultipleObjects),
                 reinterpret_cast<PVOID>(&HookedMsgWaitForMultipleObjects));
    DetourDetach(reinterpret_cast<PVOID*>(&realMsgWaitForMultipleObjectsEx),
                 reinterpret_cast<PVOID>(&HookedMsgWaitForMultipleObjectsEx));
    DetourDetach(reinterpret_cast<PVOID*>(&realSleepConditionVariableCS),
                 reinterpret_cast<PVOID>(&HookedSleepConditionVariableCS));
    DetourDetach(reinterpret_cast<PVOID*>(&realSleepConditionVariableSRW),
                 reinterpret_cast<PVOID>(&HookedSleepConditionVariableSRW));
    DetourDetach(reinterpret_cast<PVOID*>(&realWaitOnAddress), reinterpret_cast<PVOID>(&HookedWaitOnAddress));
    const LONG err = DetourTransactionCommit();
    if (err != NO_ERROR) {
        spdlog::error("speedhack: Detours remove failed: {}", err);
    }
}

}  // namespace d2bs::speedhack
