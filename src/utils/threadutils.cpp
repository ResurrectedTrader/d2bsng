#include "threadutils.h"

// Define D2BSNG_HANG_ON_CRASH to hang on crash (instead of exiting) so a
// debugger can attach and inspect the crashing thread. Off by default;
// uncomment to enable while debugging.
// #define D2BSNG_HANG_ON_CRASH 1

#include <atlconv.h>
#include <intrin.h>
#include <array>
#include <cstdint>
#include <string>

#include <spdlog/spdlog.h>
#include <tlhelp32.h>
#include <filesystem>

#include "DeferGuard.h"
#include "utils.h"

#include "stackwalker/MyStackWalker.h"

// Module TLS slot index, emitted by the CRT for any image that uses implicit
// TLS. Declaring it lets HasThreadLocalStorage() index the per-thread TLS array
// the same way compiler-generated thread_local access does.
// NOLINTNEXTLINE(readability-identifier-naming) - CRT-defined symbol name
extern "C" ULONG _tls_index;

// Linker-defined symbol whose address IS this module's PE base, so
// &__ImageBase equals our own HMODULE. The VEH uses it to tell faults in our
// DLL (which statically links V8) apart from faults in foreign modules.
// NOLINTNEXTLINE(readability-identifier-naming) - linker-defined symbol name
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace d2bs::thread_utils {

bool HasThreadLocalStorage() noexcept {
#if defined(_M_IX86)
    constexpr DWORD TLS_POINTER_TEB_OFFSET = 0x2C;  // TEB.ThreadLocalStoragePointer (x86)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - TEB-relative TLS array
    auto* tlsArray = reinterpret_cast<void* const*>(__readfsdword(TLS_POINTER_TEB_OFFSET));
#elif defined(_M_X64)
    constexpr DWORD TLS_POINTER_TEB_OFFSET = 0x58;  // TEB.ThreadLocalStoragePointer (x64)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - TEB-relative TLS array
    auto* tlsArray = reinterpret_cast<void* const*>(__readgsqword(TLS_POINTER_TEB_OFFSET));
#else
    #error "HasThreadLocalStorage: unsupported architecture"
#endif
    // NULL on threads the loader never ran TLS init for (foreign / loader-
    // spawned). A non-null array is sized for every TLS module present at thread
    // init, so indexing our slot is in-bounds; a null per-module block means our
    // thread_locals aren't backed for this thread either.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - TLS array index
    return tlsArray != nullptr && tlsArray[_tls_index] != nullptr;
}

std::vector<uint32_t> EnumerateProcessThreads() {
    std::vector<uint32_t> result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return result;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    const DWORD ourPid = GetCurrentProcessId();
    if (Thread32First(snap, &te) != FALSE) {
        do {
            // dwSize is returned with the size of the populated struct; older
            // snapshots may not include th32OwnerProcessID. Guard against that
            // before filtering.
            if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(te.th32OwnerProcessID) &&
                te.th32OwnerProcessID == ourPid) {
                result.push_back(te.th32ThreadID);
            }
            te.dwSize = sizeof(te);
        } while (Thread32Next(snap, &te) != FALSE);
    }
    CloseHandle(snap);
    return result;
}

std::string GetThreadDescription(uint32_t threadId) {
    if (threadId == 0)
        threadId = GetCurrentThreadId();
    auto handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, threadId);
    if (handle == nullptr) {
        return std::format("failed to open thread: {:#x}", threadId);
    }

    wchar_t* threadName = nullptr;
    auto result = ::GetThreadDescription(handle, &threadName);
    CloseHandle(handle);
    if (SUCCEEDED(result)) {
        auto str = d2bs::utils::ToStr(threadName);
        LocalFree(threadName);
        return str;
    }
    return {};
}

std::string GetThreadStacktrace(uint32_t threadId, uint32_t skip) {
    // Current-thread path uses the pseudo-handle so StackWalker captures
    // inline via RtlCaptureContext (no self-suspension). Cross-thread path
    // needs a real handle with SUSPEND_RESUME|GET_CONTEXT rights.
    MyStackWalker walker;
    if (threadId == 0 || threadId == GetCurrentThreadId()) {
        return walker.GetStackTrace(GetCurrentThread(), skip);
    }
    auto handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
    if (handle == nullptr)
        return std::format("failed to open thread: {:#x}", threadId);
    auto result = walker.GetStackTrace(handle, skip);
    CloseHandle(handle);
    return result;
}

std::string GetStacktraceFromContext(const CONTEXT* context, uint32_t skip) {
    if (context == nullptr)
        return GetThreadStacktrace(0, skip);
    MyStackWalker walker;
    return walker.GetStackTrace(GetCurrentThread(), skip, context);
}

void SetThreadDescription(const std::string& description, uint32_t threadId) {
    if (threadId == 0)
        threadId = GetCurrentThreadId();
    auto handle = OpenThread(THREAD_SET_LIMITED_INFORMATION, FALSE, threadId);
    if (handle == nullptr)
        return;
    auto desc = utils::ToWStr(description);
    ::SetThreadDescription(handle, desc.c_str());
    CloseHandle(handle);
}

namespace {
// Recursion guard for ExceptionHandler (UEF): if the handler itself crashes
// (e.g., the stacktrace walker hits the same broken state), the second-chance
// attempt skips the dump and just exits rather than recursing forever.
std::atomic<bool> handlerEntered{false};

// Separate recursion guard for CrashAndExit. Distinct from `handlerEntered`
// because the normal happy path is ExceptionHandler -> CrashAndExit (which
// must proceed even though handlerEntered is set). This catches the case
// where CrashAndExit's own dump path (e.g. spdlog) AVs, fires VEH, and VEH
// calls CrashAndExit a second time.
std::atomic<bool> crashAndExitEntered{false};

// Compute <d2bs-dll-dir>/d2bs_crash_YYYYMMDD_HHMMSS_<pid>_<tid>.log.
// Preferred over Game.exe's directory because Diablo II often lives under
// Program Files where non-admin writes silently fail. d2bs.dll lives wherever
// the operator installed the bot, which is virtually always user-writable.
std::filesystem::path ComputeCrashLogPath() {
    std::filesystem::path dir;

    // Resolve d2bs.dll's path by asking for the module that contains this
    // function's own code. Works regardless of how the DLL was loaded.
    HMODULE dllModule = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ComputeCrashLogPath), &dllModule)) {
        std::array<wchar_t, MAX_PATH> dllPath{};
        auto len = GetModuleFileNameW(dllModule, dllPath.data(), static_cast<DWORD>(dllPath.size()));
        if (len > 0 && len < dllPath.size()) {
            dir = std::filesystem::path(dllPath.data()).parent_path();
        }
    }

    // Fallback: Game.exe directory. Last resort if the DLL handle lookup
    // failed (shouldn't happen in practice).
    if (dir.empty()) {
        std::array<wchar_t, MAX_PATH> exePath{};
        auto len = GetModuleFileNameW(nullptr, exePath.data(), static_cast<DWORD>(exePath.size()));
        if (len > 0 && len < exePath.size()) {
            dir = std::filesystem::path(exePath.data()).parent_path();
        }
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    auto filename = std::format("d2bs_crash_{:04}{:02}{:02}_{:02}{:02}{:02}_{:#x}_{:#x}.log", st.wYear, st.wMonth,
                                st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId(), GetCurrentThreadId());
    return dir.empty() ? std::filesystem::path(filename) : dir / filename;
}
}  // namespace

std::filesystem::path WriteCrashLog(std::string_view content) {
    auto path = ComputeCrashLogPath();
    HANDLE h = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        auto err = std::format("*** d2bsng crash log open failed (gle={:#x}): {} ***\n", GetLastError(), path.string());
        OutputDebugStringA(err.c_str());
        return {};
    }
    DWORD written = 0;
    WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    return path;
}

// Helper: emit to spdlog inside a structured-exception barrier so an AV
// from corrupt logger state (we've seen spdlog::default_logger come back
// dereferencing 0x2c after framework teardown) can't bubble back into our
// own VEH and recurse. Must live in its own function - MSVC forbids
// __try/__except in the same function as objects with non-trivial dtors.
static void TryEmitSpdlog(std::string_view dump, const char* writtenNote) noexcept {
    __try {
        spdlog::critical(dump);
        if (writtenNote != nullptr) {
            spdlog::critical(writtenNote);
        }
        spdlog::shutdown();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("*** d2bsng CrashAndExit: spdlog emit raised SEH ***\n");
    }
}

[[noreturn]] void CrashAndExit(std::string_view dump, uint32_t exitCode) {
    // Recursion guard: if we're already mid-crash (e.g. spdlog AV'd inside
    // a previous CrashAndExit, VEH caught the AV, VEH called us again),
    // skip the dump path and just exit/hang. Without this we infinitely
    // recurse via VEH -> CrashAndExit -> spdlog AV -> VEH.
    if (crashAndExitEntered.exchange(true, std::memory_order_acq_rel)) {
        OutputDebugStringA("*** d2bsng CrashAndExit recursed - exiting immediately ***\n");
#ifdef D2BSNG_HANG_ON_CRASH
        while (true) {
            Sleep(INFINITE);
        }
#else
        ExitProcess(exitCode);
#endif
    }

    // Write the crash file first via pure Win32 - most likely to succeed even
    // if spdlog/console state is corrupt from whatever caused the crash. If
    // the spdlog emit below deadlocks or AVs, the file is already on disk.
    auto path = WriteCrashLog(dump);
    std::string note;
    if (!path.empty()) {
        note = std::format("*** d2bsng crash log written: {}\n", path.string());
        OutputDebugStringA(note.c_str());
    }

    // Best-effort console log via SEH-protected helper. AVs inside spdlog
    // (e.g. dead default_logger after framework teardown) get caught and
    // swallowed instead of propagating back through VEH.
    TryEmitSpdlog(dump, path.empty() ? nullptr : note.c_str());

#ifdef D2BSNG_HANG_ON_CRASH
    // Hang instead of exiting so a debugger can attach and inspect the
    // crashing thread.
    //
    // Run the port hook (e.g. pop the console visible) only on the hang
    // path - in the exit path the process dies too fast for a UI to be
    // useful. Best-effort: a null hook means no port is attached; a thrown
    // exception is swallowed.
    if (auto onCrash = onCrashFunction.load(std::memory_order_acquire); onCrash != nullptr) {
        try {
            onCrash();
        } catch (...) {
            OutputDebugStringA("*** d2bsng CrashAndExit: onCrashFunction threw ***\n");
        }
    }
    auto hangNote =
        std::format("*** d2bsng hanging (D2BSNG_HANG_ON_CRASH) - would have exited with {:#x} ***\n", exitCode);
    OutputDebugStringA(hangNote.c_str());
    // Do NOT use Sleep here - Sleep is detoured by HookManager.cpp:229 to
    // drive the game loop's per-tick work (GameLoop::OnSleep -> drain queued
    // game-thread tasks). Calling Sleep from a crashing thread would invoke
    // that drain, which executes user code in an already-corrupt state and
    // recurses through the crash handler. WaitForSingleObject on the current
    // process handle blocks forever (the process never signals while it's
    // running) and is not on the Detours list.
    WaitForSingleObject(GetCurrentProcess(), INFINITE);
    // Unreachable, but keeps [[noreturn]] happy if WaitForSingleObject ever
    // returns due to some unforeseen condition.
    while (true) {}
#else
    ExitProcess(exitCode);
#endif
}

// NOTE: This handler allocates memory (stacktrace, logging, file I/O). If the
// crash was caused by heap corruption, these calls may deadlock or crash
// recursively. This is a deliberate tradeoff - getting diagnostic info is more
// valuable than guaranteed handler completion in the common (non-heap-
// corruption) case.
//
// On completion the handler writes a crash log next to Game.exe and
// terminates the process via CrashAndExit, propagating the exception code as
// the process exit code so launchers can detect the crash type.
LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS exceptionInfo) {
    // Sentinel write - bypasses spdlog/console entirely. Visible in DebugView
    // even if our log pipeline is dead. Confirms the handler ran at all.
    OutputDebugStringA("\n*** d2bsng ExceptionHandler entered ***\n");

    if (handlerEntered.exchange(true, std::memory_order_acq_rel)) {
        // Recursive entry - bail out before we attempt the stacktrace again.
        OutputDebugStringA("*** d2bsng ExceptionHandler recursed - exiting ***\n");
        ExitProcess(exceptionInfo->ExceptionRecord->ExceptionCode);
    }

    auto baseAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    auto expAddr = reinterpret_cast<uintptr_t>(exceptionInfo->ExceptionRecord->ExceptionAddress);

    // Write the bare-minimum crash header BEFORE attempting the stacktrace -
    // if stacktrace walking itself crashes, we still have something useful.
    auto preamble = std::format("\n*** Unhandled Exception!\n"
                                "*** Base address: {:#x}\n"
                                "*** Thread id: {:#x}\n"
                                "*** ExpCode: {:#x}\n"
                                "*** ExpFlags: {:#x}\n"
                                "*** ExpAddress: {:#x}\n",
                                baseAddr, GetCurrentThreadId(), exceptionInfo->ExceptionRecord->ExceptionCode,
                                exceptionInfo->ExceptionRecord->ExceptionFlags, expAddr);
    OutputDebugStringA(preamble.c_str());

    // Now the more expensive bits. Walk from the exception's ContextRecord so
    // we get the faulting code's frames, not the handler's.
    auto stackTrace = GetStacktraceFromContext(exceptionInfo->ContextRecord, 0);
    std::string message = std::format("{}\n", stackTrace);

    auto description = GetThreadDescription(0);
    if (!description.empty()) {
        message += std::format("*** Thread description: {}\n", description);
    }

#if EXCEPTION_HANDLER_DUMP_REGISTERS
    auto* ctx = exceptionInfo->ContextRecord;
    #if defined(_M_X64)
    message += std::format("*** Rax: {:#018x} Rcx: {:#018x} Rdx: {:#018x} Rbx: {:#018x}\n"
                           "*** Rsp: {:#018x} Rbp: {:#018x} Rsi: {:#018x} Rdi: {:#018x}\n"
                           "*** R8:  {:#018x} R9:  {:#018x} R10: {:#018x} R11: {:#018x}\n"
                           "*** R12: {:#018x} R13: {:#018x} R14: {:#018x} R15: {:#018x}\n",
                           ctx->Rax, ctx->Rcx, ctx->Rdx, ctx->Rbx, ctx->Rsp, ctx->Rbp, ctx->Rsi, ctx->Rdi, ctx->R8,
                           ctx->R9, ctx->R10, ctx->R11, ctx->R12, ctx->R13, ctx->R14, ctx->R15);
    #elif defined(_M_IX86)
    message += std::format("*** Eax: {:#010x} Ecx: {:#010x} Edx: {:#010x} Ebx: {:#010x}\n"
                           "*** Esp: {:#010x} Ebp: {:#010x} Esi: {:#010x} Edi: {:#010x}\n",
                           ctx->Eax, ctx->Ecx, ctx->Edx, ctx->Ebx, ctx->Esp, ctx->Ebp, ctx->Esi, ctx->Edi);
    #endif
#endif

    OutputDebugStringA(message.c_str());

    CrashAndExit(preamble + message, exceptionInfo->ExceptionRecord->ExceptionCode);
}

LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS exceptionInfo) {
    // Fast bail-out: if a crash dump is already in progress, propagate
    // without re-running our dump logic. This catches the common case
    // where CrashAndExit's spdlog emit raises a second-chance exception
    // back into VEH - without this, we'd recurse VEH -> CrashAndExit ->
    // spdlog AV -> VEH forever until the stack blows.
    if (crashAndExitEntered.load(std::memory_order_acquire)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Foreign threads without our module TLS (e.g. a staged loader DLL's
    // workers) would re-fault the instant we touch a thread_local below
    // (crashContext) or call into a hooked wait while logging (OutputDebugStringA
    // -> hooked WaitForSingleObject -> ...). We can't diagnose them safely, and
    // an AV here would otherwise recurse back through the VEH. Let whatever
    // default handling exists deal with it.
    if (!HasThreadLocalStorage()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Per-thread reentrancy guard: if our own logging path faults (the stack
    // walk, OutputDebugStringA, corrupt logger state), the nested first-chance
    // invocation bails here instead of recursing until the stack blows. Safe to
    // use a thread_local now that TLS is confirmed present above.
    static thread_local bool inHandler = false;
    if (inHandler) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    inHandler = true;
    DeferGuard guard([&] { inHandler = false; });

    DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;

    // Skip noise: C++ exceptions, OutputDebugString notifications, CLR
    // notifications. EXCEPTION_BREAKPOINT and EXCEPTION_SINGLE_STEP are
    // skipped only when a debugger is present - without a debugger those
    // are V8's V8_IMMEDIATE_CRASH() (CHECK failure / OS::Abort path on
    // clang-cl expands `__builtin_trap()` to `int 3`), which would
    // otherwise terminate the process silently. With a debugger attached,
    // we must propagate so single-stepping works.
    switch (code) {
        case EXCEPTION_BREAKPOINT:
        case EXCEPTION_SINGLE_STEP:
            if (IsDebuggerPresent()) {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            break;
        case 0xE06D7363:  // Microsoft C++ exception
        case 0x4001000A:  // CLR notification
        case 0x40010006:  // OutputDebugString notification
        case 0x406D1388:  // MS_VC_EXCEPTION - legacy SetThreadName signal (e.g. ixwebsocket names its threads this way)
            return EXCEPTION_CONTINUE_SEARCH;
        default:
            break;
    }

    // Resolve the faulting module up front and bail before doing any work for
    // faults that aren't ours. We only diagnose - log, stack-walk, and maybe
    // terminate - faults originating in code we own: our DLL (&__ImageBase,
    // which statically links V8) or the game's main module. A fault inside a
    // foreign module (e.g. an obfuscated third-party loader that deliberately
    // raises and then catches its own illegal-/privileged-instruction
    // exceptions as control flow), or in unknown memory (expModule == nullptr,
    // e.g. a V8 JIT code page), is none of our business: return immediately so
    // we neither spam the console nor burn the rate-limit budget below. A
    // genuinely unhandled foreign fault still reaches our
    // SetUnhandledExceptionFilter backstop on the second chance.
    const auto expAddr = reinterpret_cast<uintptr_t>(exceptionInfo->ExceptionRecord->ExceptionAddress);
    HMODULE expModule = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(expAddr), &expModule);
    const auto ourModule = reinterpret_cast<HMODULE>(&__ImageBase);
    if (expModule == nullptr || (expModule != ourModule && expModule != GetModuleHandleA(nullptr))) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Rate-limit so V8's intentional first-chance AVs (Wasm trap, stack
    // guard, etc.) don't flood the console under normal load. After
    // MAX_LOGS events we silently propagate.
    static std::atomic<size_t> logCount{0};
    constexpr size_t MAX_LOGS = 50;
    auto n = logCount.fetch_add(1, std::memory_order_relaxed);
    if (n >= MAX_LOGS) {
        if (n == MAX_LOGS) {
            OutputDebugStringA("*** d2bsng VEH log limit reached - suppressing further events ***\n");
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Module name + RVA at the faulting IP for the log line. expModule is
    // non-null and is our DLL or Game.exe - guaranteed by the early bail above.
    std::string modInfo = " in <unknown module>";
    std::array<char, MAX_PATH> buf{};
    if (GetModuleFileNameA(expModule, buf.data(), buf.size())) {
        const auto base = reinterpret_cast<uintptr_t>(expModule);
        modInfo = std::format(" in {}+{:#x}", std::filesystem::path(buf.data()).filename().string(), expAddr - base);
    }

    // For AV, ExceptionInformation[0]=op (0=read, 1=write, 8=exec) and
    // ExceptionInformation[1]=faulting address. STATUS_HEAP_CORRUPTION
    // (0xC0000374) and STATUS_STACK_BUFFER_OVERRUN (0xC0000409) typically
    // arrive via __fastfail and bypass VEH entirely; if PageHeap is enabled
    // (gflags) the bad operation reaches us as a regular AV instead.
    std::string detail;
    if (code == EXCEPTION_ACCESS_VIOLATION && exceptionInfo->ExceptionRecord->NumberParameters >= 2) {
        auto opType = exceptionInfo->ExceptionRecord->ExceptionInformation[0];
        auto faultAddr = exceptionInfo->ExceptionRecord->ExceptionInformation[1];
        const char* op = "?";
        switch (opType) {
            case 0:
                op = "read";
                break;
            case 1:
                op = "write";
                break;
            case 8:
                op = "exec";
                break;
            default:
                break;
        }
        detail = std::format(" ({} of {:#x})", op, faultAddr);
    }

    std::string ctxSuffix;
    if (!crashContext.empty()) {
        ctxSuffix = std::format(" [{}]", crashContext);
    }

    auto msg = std::format("*** VEH first-chance {:#010x} at {:#x}{}{}{} tid={:#x} ***\n", code, expAddr, modInfo,
                           detail, ctxSuffix, GetCurrentThreadId());

    // Walk the stack at the exception's ContextRecord (NOT the current
    // thread context, which would just show the handler's frames). We do
    // this for AVs too - V8 swallows real crashes inside its own __try, so
    // by the time the UEF / D2 ErrorReportLaunch hook gets called (if at
    // all) the crashing thread's stack is gone. Capturing here gives us the
    // actual fault site. Cost is bounded by MAX_LOGS rate-limit above.
    msg += GetStacktraceFromContext(exceptionInfo->ContextRecord, 0);
    msg += '\n';

    // Always emit via OutputDebugStringA - DebugView captures it even when
    // the persistent crash-log path is blocked (PDB missing, no write
    // permission, etc.). File write is delegated to CrashAndExit on the
    // terminating paths to avoid duplicate writes.
    OutputDebugStringA(msg.c_str());

    // Terminate on ERROR-severity NTSTATUS codes (top two bits set): illegal
    // instruction, stack overflow, integer divide, privileged instruction, etc.
    // - almost always real bugs. Reaching here means the fault is in our own
    // code (foreign-module faults already bailed out above). Informational
    // (0x4xxxxxxx) / warning (0x8xxxxxxx) codes are benign SEH signals and never
    // terminate. AVs are error-severity but are often legitimate first-chance
    // (V8 Wasm traps, stack guard), so they propagate via the path below rather
    // than terminating here.
    constexpr DWORD NT_ERROR_SEVERITY_MASK = 0xC0000000U;
    const bool isError = (code & NT_ERROR_SEVERITY_MASK) == NT_ERROR_SEVERITY_MASK;
    const bool isAv = (code == EXCEPTION_ACCESS_VIOLATION);
    if (isError && !isAv) {
        CrashAndExit(msg, code);
    }

#ifdef D2BSNG_HANG_ON_CRASH
    // Debug mode: also terminate on AV so V8 / D2 __try blocks never get a turn
    // and bury the fault site. (Foreign-module faults already bailed out above.)
    CrashAndExit(msg, code);
#endif

    // First-chance AV in our own code (propagating path): persist a best-effort
    // record in case a downstream handler (V8 UEF, D2 __try) swallows it and
    // calls ExitProcess before our SEH UEF gets a turn.
    WriteCrashLog(msg);
    spdlog::warn(msg);
    return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace d2bs::thread_utils
