// Fakes for d2bs::thread_utils - Commands.cpp's `stacks` console command pulls
// in GetThreadStacktrace, but the production implementation lives in utils.lib
// and depends on stackwalker / DbgHelp. Tests don't need real stack walking,
// so we provide trivial stubs that satisfy the linker.
#include "utils/threadutils.h"

namespace d2bs::thread_utils {

std::vector<uint32_t> EnumerateProcessThreads() {
    return {};
}

std::string GetThreadStacktrace(uint32_t /*threadId*/, uint32_t /*skip*/) {
    return "<stacktrace unavailable in tests>";
}

std::string GetStacktraceFromContext(const CONTEXT* /*context*/, uint32_t /*skip*/) {
    return "<stacktrace unavailable in tests>";
}

std::string GetThreadDescription(uint32_t /*threadId*/) {
    return {};
}

void SetThreadDescription(const std::string& /*description*/, uint32_t /*threadId*/) {}

std::filesystem::path WriteCrashLog(std::string_view /*content*/) {
    return {};
}

[[noreturn]] void CrashAndExit(std::string_view /*dump*/, uint32_t exitCode) {
    ExitProcess(exitCode);
}

LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS /*exceptionInfo*/) {
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS /*exceptionInfo*/) {
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace d2bs::thread_utils
