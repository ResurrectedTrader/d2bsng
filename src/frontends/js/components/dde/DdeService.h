#pragma once

#include <Windows.h>
#include <ddeml.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "utils/utils.h"

namespace d2bs::dde {

// DDE transaction kind. Values match the numeric `mode` argument the JS
// `sendDDE(mode, ...)` global uses (reference/d2bs JSCore.cpp my_sendDDE).
enum class Transaction : uint32_t {
    Request = 0,   // XTYP_REQUEST - outbound only (inbound is rejected by CBF_FAIL_REQUESTS)
    Poke = 1,      // XTYP_POKE    - push data
    Evaluate = 2,  // XTYP_EXECUTE - run the payload as JS
};

// Handler for inbound transactions. Runs on the DDE pump thread - implementations
// must be thread-safe with the rest of the framework.
//
// The inbound path only ever delivers Poke or Execute (Request is blocked by
// CBF_FAIL_REQUESTS at the DDE layer, matching reference/d2bs/dde.cpp). The
// DDE-level response is hardcoded to match reference (TRUE for XTYP_CONNECT,
// 0 for POKE/EXECUTE), so the handler is fire-and-forget - no return needed.
using Handler =
    std::function<void(Transaction txn, std::string_view topic, std::string_view item, std::string_view data)>;

// Self-contained DDE service singleton.
//
// Owns its own Windows message pump thread, so it works regardless of how the
// host DLL is loaded - in particular, CreateRemoteThread-style injection
// (where the DllMain thread exits before any message can be pumped) does not
// break DDE delivery here.
//
// Also provides a static Send() client method using an ephemeral
// APPCMD_CLIENTONLY instance per call (matches reference/d2bs/dde.cpp SendDDE).
class DdeService {
   public:
    static DdeService& Instance();

    // Register the DDE server under the canonical name "d2bs-<PID>" and start
    // the internal pump thread. Idempotent; a second call while running is a
    // no-op returning true. Returns false if DdeInitialize / DdeCreateStringHandle
    // / DdeNameService fails (details logged via spdlog).
    bool Start(Handler handler);

    // Unregister the service and stop the pump thread. Idempotent.
    void Stop();

    // Synchronous client-side DDE send. Uses its own ephemeral APPCMD_CLIENTONLY
    // instance per call (matches reference), independent of the inbound server.
    // Returns nullopt on transport failure. For Request, the response is the
    // returned string. For Poke/Execute on success, returns an empty string.
    std::optional<std::string> Send(Transaction txn, std::string_view server, std::string_view topic,
                                    std::string_view item, std::string_view data);

    DdeService(const DdeService&) = delete;
    DdeService& operator=(const DdeService&) = delete;

   private:
    DdeService() = default;
    ~DdeService();

    // Actual per-instance callback, invoked via StaticCallback trampoline.
    HDDEDATA HandleCallback(UINT uType, UINT uFmt, HCONV hconv, HSZ hsz1, HSZ hsz2, HDDEDATA hdata, ULONG_PTR dw1,
                            ULONG_PTR dw2);

    // C-style trampoline required by DdeInitialize (no user-data slot).
    static HDDEDATA CALLBACK StaticCallback(UINT uType, UINT uFmt, HCONV hconv, HSZ hsz1, HSZ hsz2, HDDEDATA hdata,
                                            ULONG_PTR dw1, ULONG_PTR dw2);

    void RunMessagePump();

    Handler handler_;
    DWORD idInst_ = 0;
    HSZ serviceName_ = nullptr;

    std::atomic<bool> running_{false};
    DWORD pumpThreadId_ = 0;
    std::jthread pumpThread_;

    // Dedicated DDE logger. spdlog's registry caches by name so repeated GetLogger
    // calls return the same sink-set; this NSDMI initialises once at singleton
    // construction.
    std::shared_ptr<spdlog::logger> logger_ = utils::GetLogger("dde");
};

}  // namespace d2bs::dde
