#include "components/dde/DdeService.h"

#include <array>
#include <future>
#include <utility>

#include "utils/threadutils.h"

namespace d2bs::dde {

namespace {

// Matches reference/d2bs/dde.cpp DdeCallback flags exactly.
constexpr UINT SERVER_FLAGS = APPCLASS_STANDARD | APPCMD_FILTERINITS | CBF_FAIL_ADVISES | CBF_FAIL_REQUESTS |
                              CBF_SKIP_CONNECT_CONFIRMS | CBF_SKIP_REGISTRATIONS | CBF_SKIP_UNREGISTRATIONS;

constexpr DWORD CLIENT_TIMEOUT_MS = 5000;

// Read a DDE string handle into a narrow std::string.
std::string QueryString(DWORD idInst, HSZ hsz) {
    if (hsz == nullptr) {
        return {};
    }
    DWORD len = DdeQueryStringA(idInst, hsz, nullptr, 0, CP_WINANSI);
    if (len == 0) {
        return {};
    }
    // DdeQueryStringA writes `len` chars plus a trailing NUL; std::string(len, '\0')
    // has `len + 1` writable bytes (the last being the internal NUL sentinel), so
    // passing `len + 1` as the buffer size is safe.
    std::string s(len, '\0');
    DdeQueryStringA(idInst, hsz, s.data(), len + 1, CP_WINANSI);
    return s;
}

// Read an HDDEDATA payload into a narrow std::string. Trailing NUL (if present,
// standard DDE CF_TEXT convention) is stripped.
std::string QueryData(HDDEDATA hdata) {
    if (hdata == nullptr) {
        return {};
    }
    DWORD size = DdeGetData(hdata, nullptr, 0, 0);
    if (size == 0) {
        return {};
    }
    std::string buf(size, '\0');
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - DDE API takes LPBYTE
    DdeGetData(hdata, reinterpret_cast<LPBYTE>(buf.data()), size, 0);
    if (!buf.empty() && buf.back() == '\0') {
        buf.pop_back();
    }
    return buf;
}

// Tiny helper to cast integer DDE return codes (DDE_FACK / DDE_FNOTPROCESSED)
// to the HDDEDATA opaque pointer type the callback must return.
HDDEDATA AsHddeData(uintptr_t value) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr) - DDE ABI
    return reinterpret_cast<HDDEDATA>(value);
}

}  // namespace

DdeService& DdeService::Instance() {
    static DdeService inst;
    return inst;
}

DdeService::~DdeService() {
    Stop();
}

bool DdeService::Start(Handler handler) {
    if (running_.load(std::memory_order_acquire)) {
        return true;
    }
    handler_ = std::move(handler);

    std::promise<bool> startupPromise;
    auto startupFuture = startupPromise.get_future();

    pumpThread_ = std::jthread([this, promise = std::move(startupPromise)]() mutable {
        thread_utils::SetThreadDescription("d2bs DDE pump");

        UINT err = DdeInitializeA(&idInst_, &DdeService::StaticCallback, SERVER_FLAGS, 0);
        if (err != DMLERR_NO_ERROR) {
            logger_->error("DdeInitialize failed: 0x{:X}", err);
            promise.set_value(false);
            return;
        }

        std::array<char, 32> name{};
        std::snprintf(name.data(), name.size(), "d2bs-%lu", GetCurrentProcessId());
        serviceName_ = DdeCreateStringHandleA(idInst_, name.data(), CP_WINANSI);
        if (serviceName_ == nullptr) {
            logger_->error("DdeCreateStringHandle failed: 0x{:X}", DdeGetLastError(idInst_));
            DdeUninitialize(idInst_);
            idInst_ = 0;
            promise.set_value(false);
            return;
        }

        if (DdeNameService(idInst_, serviceName_, nullptr, DNS_REGISTER | DNS_FILTERON) == nullptr) {
            logger_->error("DdeNameService failed: 0x{:X}", DdeGetLastError(idInst_));
            DdeFreeStringHandle(idInst_, serviceName_);
            DdeUninitialize(idInst_);
            serviceName_ = nullptr;
            idInst_ = 0;
            promise.set_value(false);
            return;
        }

        pumpThreadId_ = GetCurrentThreadId();
        running_.store(true, std::memory_order_release);
        logger_->info("registered service '{}' on thread {}", name.data(), pumpThreadId_);
        promise.set_value(true);

        RunMessagePump();

        // Teardown on the pump thread.
        DdeNameService(idInst_, nullptr, nullptr, DNS_UNREGISTER);
        DdeFreeStringHandle(idInst_, serviceName_);
        DdeUninitialize(idInst_);
        serviceName_ = nullptr;
        idInst_ = 0;
        running_.store(false, std::memory_order_release);
    });

    return startupFuture.get();
}

void DdeService::Stop() {
    if (!running_.load(std::memory_order_acquire)) {
        // Either never started or already stopped. If the pump thread started
        // but startup failed, the jthread is joinable with a completed lambda -
        // clean that up before returning.
        if (pumpThread_.joinable()) {
            pumpThread_.join();
        }
        return;
    }
    DWORD tid = pumpThreadId_;
    if (tid != 0) {
        PostThreadMessageW(tid, WM_QUIT, 0, 0);
    }
    if (pumpThread_.joinable()) {
        pumpThread_.join();
    }
    pumpThreadId_ = 0;
    handler_ = {};
}

void DdeService::RunMessagePump() {
    MSG msg;
    while (true) {
        BOOL rc = GetMessageW(&msg, nullptr, 0, 0);
        if (rc <= 0) {
            // 0 = WM_QUIT received, -1 = error.
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

HDDEDATA CALLBACK DdeService::StaticCallback(UINT uType, UINT uFmt, HCONV hconv, HSZ hsz1, HSZ hsz2, HDDEDATA hdata,
                                             ULONG_PTR dw1, ULONG_PTR dw2) {
    return Instance().HandleCallback(uType, uFmt, hconv, hsz1, hsz2, hdata, dw1, dw2);
}

HDDEDATA DdeService::HandleCallback(UINT uType, UINT /*uFmt*/, HCONV /*hconv*/, HSZ hsz1, HSZ hsz2, HDDEDATA hdata,
                                    ULONG_PTR /*dw1*/, ULONG_PTR /*dw2*/) {
    // Matches reference/d2bs/dde.cpp: accept all connects, fire-and-forget
    // Poke/Execute, always return 0 (XTYP_REQUEST is rejected by CBF_FAIL_REQUESTS).
    switch (uType) {
        case XTYP_CONNECT:
            return AsHddeData(TRUE);

        case XTYP_POKE:
        case XTYP_EXECUTE:
            if (handler_) {
                Transaction txn = (uType == XTYP_POKE) ? Transaction::Poke : Transaction::Evaluate;
                handler_(txn, QueryString(idInst_, hsz1), QueryString(idInst_, hsz2), QueryData(hdata));
            }
            return nullptr;

        default:
            return nullptr;
    }
}

std::optional<std::string> DdeService::Send(Transaction txn, std::string_view server, std::string_view topic,
                                            std::string_view item, std::string_view data) {
    DWORD idInst = 0;
    UINT err = DdeInitializeA(&idInst, &DdeService::StaticCallback, APPCMD_CLIENTONLY, 0);
    if (err != DMLERR_NO_ERROR) {
        logger_->error("Send: DdeInitialize failed: 0x{:X}", err);
        return std::nullopt;
    }

    // Empty strings are not valid DDE names; reference substitutes literal `""` quotes.
    std::string serverStr(server.empty() ? std::string_view{"\"\""} : server);
    std::string topicStr(topic.empty() ? std::string_view{"\"\""} : topic);
    std::string itemStr(item.empty() ? std::string_view{"\"\""} : item);

    HSZ hszServer = DdeCreateStringHandleA(idInst, serverStr.c_str(), CP_WINANSI);
    HSZ hszTopic = DdeCreateStringHandleA(idInst, topicStr.c_str(), CP_WINANSI);
    HSZ hszItem = DdeCreateStringHandleA(idInst, itemStr.c_str(), CP_WINANSI);

    auto cleanup = [&]() {
        if (hszServer != nullptr) {
            DdeFreeStringHandle(idInst, hszServer);
        }
        if (hszTopic != nullptr) {
            DdeFreeStringHandle(idInst, hszTopic);
        }
        if (hszItem != nullptr) {
            DdeFreeStringHandle(idInst, hszItem);
        }
        DdeUninitialize(idInst);
    };

    if (hszServer == nullptr || hszTopic == nullptr || hszItem == nullptr) {
        logger_->error("Send: DdeCreateStringHandle failed: 0x{:X}", DdeGetLastError(idInst));
        cleanup();
        return std::nullopt;
    }

    HCONV hConv = DdeConnect(idInst, hszServer, hszTopic, nullptr);
    if (hConv == nullptr) {
        logger_->error("Send: DdeConnect failed for server='{}' topic='{}': 0x{:X}", serverStr, topicStr,
                       DdeGetLastError(idInst));
        cleanup();
        return std::nullopt;
    }

    std::optional<std::string> result;
    switch (txn) {
        case Transaction::Request: {
            HDDEDATA hdata =
                DdeClientTransaction(nullptr, 0, hConv, hszItem, CF_TEXT, XTYP_REQUEST, CLIENT_TIMEOUT_MS, nullptr);
            if (hdata != nullptr) {
                result = QueryData(hdata);
                DdeFreeDataHandle(hdata);
            }
            break;
        }
        case Transaction::Poke: {
            std::string buf(data);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - DDE API takes LPBYTE
            HDDEDATA rc = DdeClientTransaction(reinterpret_cast<LPBYTE>(buf.data()), static_cast<DWORD>(buf.size()) + 1,
                                               hConv, hszItem, CF_TEXT, XTYP_POKE, CLIENT_TIMEOUT_MS, nullptr);
            if (rc != nullptr) {
                result = std::string{};
            }
            break;
        }
        case Transaction::Evaluate: {
            std::string buf(data);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - DDE API takes LPBYTE
            HDDEDATA rc = DdeClientTransaction(reinterpret_cast<LPBYTE>(buf.data()), static_cast<DWORD>(buf.size()) + 1,
                                               hConv, nullptr, 0, XTYP_EXECUTE, CLIENT_TIMEOUT_MS, nullptr);
            if (rc != nullptr) {
                result = std::string{};
            }
            break;
        }
    }

    DdeDisconnect(hConv);
    cleanup();
    return result;
}

}  // namespace d2bs::dde
