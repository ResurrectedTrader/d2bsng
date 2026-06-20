#pragma once

#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace d2bs::thread_utils {
// Snapshot the thread IDs of every thread in the current process via
// CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD). Returns an empty vector if the
// snapshot can't be taken. Order is the snapshot's order - callers that need
// a stable presentation should sort.
std::vector<uint32_t> EnumerateProcessThreads();

std::string GetThreadStacktrace(uint32_t threadId = 0, uint32_t skip = 0);

// Walk the stack starting from a given CONTEXT (e.g. the ContextRecord
// captured by an exception handler). Required when called from a VEH/SEH
// callback - the callback's own stack is unrelated to where the crash
// happened, so a regular GetThreadStacktrace returns the handler's frames,
// not the faulting code's.
std::string GetStacktraceFromContext(const CONTEXT* context, uint32_t skip = 0);

std::string GetThreadDescription(uint32_t threadId = 0);
void SetThreadDescription(const std::string& description, uint32_t threadId = 0);

// Returns true if the calling thread has implicit thread-local storage
// (thread_local / __declspec(thread)) backed for THIS module. Foreign threads
// created by code that bypasses loader thread-init - e.g. a staged or manually-
// mapped DLL spawning its own workers - can have a NULL TEB
// ThreadLocalStoragePointer; touching ANY thread_local on such a thread access-
// violates. Code reachable from our process-global API hooks (speedhack, the
// Sleep hook, the VEH) must gate every thread_local read behind this check.
// Cheap: one TEB-relative load plus an array index, and it never touches
// thread_local itself, so it is safe to call from a thread that has none.
[[nodiscard]] bool HasThreadLocalStorage() noexcept;

// Writes `content` to a timestamped file alongside the host executable
// (Game.exe directory). Returns the path on success or an empty path on
// failure. Uses raw Win32 file APIs so it stays usable when the CRT/iostream/
// spdlog state may be compromised by the same crash.
std::filesystem::path WriteCrashLog(std::string_view content);

// Optional hook: when set, CrashAndExit invokes it before writing the dump
// or shutting down. Used by the framework to pop the console visible
// (so the dump renders live), but a port can register anything - sound an
// alarm, post to a webhook, etc. Left null in tests and the bare utils
// library. Best-effort: invoked once, exceptions are swallowed.
inline std::atomic<void (*)()> onCrashFunction{nullptr};

// Per-thread context string appended to VEH / ExceptionHandler output. Set
// via CrashContextScope around work whose origin isn't visible in the C++
// stack (e.g. the posting site of a queued task).
inline thread_local std::string crashContext;

class CrashContextScope {
    std::string prev_;

   public:
    explicit CrashContextScope(std::string ctx) : prev_(std::move(crashContext)) { crashContext = std::move(ctx); }
    ~CrashContextScope() { crashContext = std::move(prev_); }

    CrashContextScope(const CrashContextScope&) = delete;
    CrashContextScope& operator=(const CrashContextScope&) = delete;
    CrashContextScope(CrashContextScope&&) = delete;
    CrashContextScope& operator=(CrashContextScope&&) = delete;
};

// Final-stage crash exit: writes `dump` via WriteCrashLog, flushes spdlog,
// and terminates the process with `exitCode`. Used by all unhandled-exception
// handlers and V8/D2 fatal-error callbacks so the crash path is uniform.
[[noreturn]] void CrashAndExit(std::string_view dump, uint32_t exitCode);

LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS exceptionInfo);

// First-chance exception handler. Backstop for cases where our UEF was
// overridden (V8/Crashpad, Detours) so we still get a record before the
// active filter does anything. Returns EXCEPTION_CONTINUE_SEARCH so it
// doesn't intervene - purely diagnostic.
LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS exceptionInfo);
}  // namespace d2bs::thread_utils
