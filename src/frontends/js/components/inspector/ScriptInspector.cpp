#include "components/inspector/ScriptInspector.h"

#include <chrono>
#include <utility>

#include "components/inspector/InspectorServer.h"
#include "components/script/Script.h"
#include "config/AppConfig.h"
#include "game/GameLock.h"
#include "speedhack/Speedhack.h"
#include "utils/utils.h"

namespace d2bs::js::inspector {

namespace {

// V8 hands the embedder protocol text as a StringView that may be 8-bit
// (Latin-1 / UTF-8) or 16-bit (UTF-16). Chrome DevTools speaks UTF-8 JSON, so
// normalize to a UTF-8 std::string.
std::string StringViewToUtf8(const v8_inspector::StringView& view) {
    if (view.length() == 0) {
        return {};
    }
    if (view.is8Bit()) {
        return {reinterpret_cast<const char*>(view.characters8()), view.length()};
    }
    // 16-bit (UTF-16) -> UTF-8 via the shared utility (utils::ToStr defaults to CP_UTF8).
    return utils::ToStr(std::wstring(reinterpret_cast<const wchar_t*>(view.characters16()), view.length()));
}

v8_inspector::StringView Utf8ToStringView(const std::string& str) {
    return {reinterpret_cast<const uint8_t*>(str.data()), str.size()};
}

}  // namespace

// CDP messages V8 emits for this session, forwarded to the attached DevTools
// client via the server. Runs on the isolate thread (inside dispatch / pause).
class InspectorChannel : public v8_inspector::V8Inspector::Channel {
   public:
    explicit InspectorChannel(ScriptInspector* owner) : owner_(owner) {}
    void sendResponse(int /*callId*/, std::unique_ptr<v8_inspector::StringBuffer> message) override {
        owner_->SendToClient(StringViewToUtf8(message->string()));
    }
    void sendNotification(std::unique_ptr<v8_inspector::StringBuffer> message) override {
        owner_->SendToClient(StringViewToUtf8(message->string()));
    }
    void flushProtocolNotifications() override {}

   private:
    ScriptInspector* owner_;
};

// Pause-loop and default-context plumbing for V8's debugger. Runs on the
// isolate thread.
class InspectorClient : public v8_inspector::V8InspectorClient {
   public:
    explicit InspectorClient(ScriptInspector* owner) : owner_(owner) {}
    void runMessageLoopOnPause(int /*contextGroupId*/) override { owner_->RunPauseLoop(); }
    void quitMessageLoopOnPause() override { owner_->QuitPauseLoop(); }
    void runIfWaitingForDebugger(int /*contextGroupId*/) override {}
    v8::Local<v8::Context> ensureDefaultContextInGroup(int /*contextGroupId*/) override { return owner_->Context(); }
    double currentTimeMS() override {
        // DevTools timestamps want real wall-clock time. This runs on the script
        // thread, which opts into the speedhack (system_clock::now() reads the
        // hooked GetSystemTimePreciseAsFileTime), so bypass scaling here -
        // otherwise timestamps would race ahead at speed > 1.
        speedhack::SpeedhackDisabledScope realTime;
        return std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    // Map a Windows script path (e.g. C:\d2bs\libs\Town.js) to a file:// URL,
    // relative to the script base (file:///libs/Town.js), so DevTools' Sources
    // panel binds gutter-set breakpoints reliably, the install path stays out
    // of DevTools, and the URLs match the target url AttachInspector advertises.
    // The compile origin stays the raw path (kolbot's require.js regex depends
    // on it); only the URL DevTools sees changes. Non-path origins (e.g.
    // "Console") pass through unchanged (nullptr = use the resource name
    // verbatim).
    std::unique_ptr<v8_inspector::StringBuffer> resourceNameToUrl(
        const v8_inspector::StringView& resourceName) override {
        std::string name = StringViewToUtf8(resourceName);
        if (name.size() < 3 || name[1] != ':') {
            return nullptr;
        }
        std::string url = config::GetAppConfig().GetScriptPaths().FileUrl(name);
        return v8_inspector::StringBuffer::create(
            v8_inspector::StringView(reinterpret_cast<const uint8_t*>(url.data()), url.size()));
    }

   private:
    ScriptInspector* owner_;
};

ScriptInspector::ScriptInspector(Script* script, std::string title, std::string url) : script_(script) {
    // Constructed inside AttachInspector's Isolate::Scope + HandleScope, after the
    // script's isolate is set, so GetIsolate() and the context Local are valid.
    // isolate_ holds a strong ref (see header): keeps the isolate alive for our
    // whole lifetime, including ~ScriptInspector's contextDestroyed.
    isolate_ = script_->GetIsolate();
    v8::Local<v8::Context> context = script_->GetContext();

    channel_ = std::make_unique<InspectorChannel>(this);
    client_ = std::make_unique<InspectorClient>(this);
    inspector_ = v8_inspector::V8Inspector::create(isolate_.get(), client_.get());

    v8_inspector::V8ContextInfo info(context, CONTEXT_GROUP_ID, Utf8ToStringView(title));
    inspector_->contextCreated(info);

    // Routing key for the ws path: the script's OS thread id (unique among live
    // scripts). The human-readable label is `title` (shown in chrome://inspect),
    // so the id itself needn't be meaningful. Read back via target_->Id().
    target_ = std::make_shared<InspectorTarget>(std::to_string(script_->GetNativeThreadId()), std::move(title),
                                                std::move(url), std::weak_ptr(isolate_));
    InspectorServer::Instance().AddTarget(target_);

    // Let the busy-script interrupt (InspectorTarget::Push) resolve us by isolate.
    isolate_->SetData(ISOLATE_DATA_SLOT, this);
}

ScriptInspector::~ScriptInspector() {
    // Stop the busy-script interrupt from resolving a half-destroyed inspector.
    isolate_->SetData(ISOLATE_DATA_SLOT, nullptr);

    // Stop the server from delivering to a target whose consumer is going away.
    InspectorServer::Instance().RemoveTarget(target_->Id());

    // The isolate is still alive and current: TeardownIsolate destroys us inside
    // its Isolate::Scope + HandleScope, before the script's context is reset.
    TeardownSession();
    if (inspector_) {
        v8::HandleScope scope(isolate_.get());
        if (auto context = script_->GetContext(); !context.IsEmpty()) {
            inspector_->contextDestroyed(context);
        }
    }
    inspector_.reset();
}

void ScriptInspector::DrainIncoming() {
    for (const auto& event : target_->DrainAll()) {
        ProcessEvent(event);
    }
    // A disconnect that arrived during a pause defers its session teardown to
    // here, where we're guaranteed to be outside the (nested) run loop.
    if (sessionTeardownPending_ && !inPauseLoop_) {
        TeardownSession();
    }
}

void ScriptInspector::ProcessEvent(const InspectorTarget::Event& event) {
    v8::HandleScope scope(isolate_.get());
    switch (event.kind) {
        case InspectorTarget::EventKind::Connected:
            // A fresh DevTools client. Tear down any lingering session first -
            // including one whose teardown was deferred from a pause-time
            // disconnect - so a quick reconnect never reuses the old session.
            TeardownSession();
            session_ = inspector_->connect(CONTEXT_GROUP_ID, channel_.get(), v8_inspector::StringView(),
                                           v8_inspector::V8Inspector::kFullyTrusted,
                                           v8_inspector::V8Inspector::kNotWaitingForDebugger);
            break;
        case InspectorTarget::EventKind::Message:
            if (session_) {
                // Mark inspector-driven execution: a console.log run by this
                // dispatch (a DevTools REPL evaluate) routes to V8's console (the
                // DevTools panel), not the script's polyfill. See SetupIsolate.
                ReplEvalScope evalScope;
                session_->dispatchProtocolMessage(Utf8ToStringView(event.payload));
            }
            break;
        case InspectorTarget::EventKind::Disconnected:
            if (inPauseLoop_) {
                // V8 forbids destroying the session inside the (nested) pause
                // loop. Flag it: RunPauseLoop unwinds every pause level on this
                // flag (resuming V8 at each), then DrainIncoming tears it down.
                sessionTeardownPending_ = true;
            } else {
                TeardownSession();
            }
            break;
    }
}

void ScriptInspector::RunPauseLoop() {
    // A breakpoint must not freeze the game or deadlock the game locks: release
    // any game lock this thread holds for the duration of the pause and reacquire
    // on the way out. Both releasers no-op when no lock is held; a thread never
    // holds both at once, but releasing both keeps this correct regardless of
    // which lock the script was paused under.
    game::GameWriteLockReleaser writeReleaser;
    game::GameReadLockReleaser readReleaser;

    // V8 re-enters this loop when a debugger evaluation (Runtime.evaluate /
    // evaluateOnCallFrame) itself hits a breakpoint or `debugger`. Save/restore
    // the pause flags so an inner resume doesn't abandon the outer pause.
    const bool outerInPause = inPauseLoop_;
    const bool outerQuit = quitPause_;
    inPauseLoop_ = true;
    quitPause_ = false;

    const std::stop_token stop = script_->GetStopToken();
    // sessionTeardownPending_ is set by a disconnect during pause; it exits every
    // nested level (each resumes V8 via the trailing resume() below).
    while (!quitPause_ && !sessionTeardownPending_ && !stop.stop_requested()) {
        for (const auto& event : target_->DrainAll()) {
            ProcessEvent(event);
            if (quitPause_ || sessionTeardownPending_) {
                break;
            }
        }
        if (quitPause_ || sessionTeardownPending_ || stop.stop_requested()) {
            break;
        }
        target_->WaitForEvents(stop, std::chrono::milliseconds(50));
    }

    // Leaving other than via a clean per-level resume (quitMessageLoopOnPause set
    // quitPause_): tell V8 to leave this pause level so it unwinds. A disconnect
    // pops every level this way; Stop()'s TerminateExecution then aborts.
    if (!quitPause_ && session_) {
        session_->resume();
    }
    inPauseLoop_ = outerInPause;
    quitPause_ = outerQuit;
}

void ScriptInspector::QuitPauseLoop() {
    // V8 calls this synchronously on the isolate thread from inside
    // dispatchProtocolMessage (the resume/step that ends the pause), so RunPauseLoop
    // sees quitPause_ the moment ProcessEvent returns - no queue wakeup needed.
    quitPause_ = true;
}

void ScriptInspector::TeardownSession() {
    if (session_) {
        session_->stop();
        session_.reset();
    }
    sessionTeardownPending_ = false;
}

void ScriptInspector::SendToClient(const std::string& message) {
    InspectorServer::Instance().Send(target_->Id(), message);
}

v8::Local<v8::Context> ScriptInspector::Context() const {
    return script_->GetContext();
}

}  // namespace d2bs::js::inspector
