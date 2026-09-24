#include "components/inspector/ScriptInspector.h"

#include <chrono>
#include <utility>

#include "components/inspector/InspectorServer.h"
#include "components/script/Script.h"
#include "config/AppConfig.h"
#include "game/GameLock.h"
#include "speedhack/Speedhack.h"
#include "utils/Strings.h"

namespace d2bs::runtime::inspector {

namespace {

// Runs at the inspector's next safe point on the isolate thread - inside a busy
// script too - where dispatching (and so running script for a DevTools
// evaluate) is allowed. Requests still waiting when the inspector is destroyed
// are dropped, so `data` is never stale.
void OnDispatch(ub::Isolate& /*isolate*/, ub::CallbackData data) {
    if (auto* inspector = data.As<ScriptInspector>()) {
        inspector->DrainIncoming();
    }
}

}  // namespace

std::unique_ptr<ScriptInspector> ScriptInspector::Create(Script& script, ub::Context context, std::string title,
                                                         std::string url) {
    std::unique_ptr<ScriptInspector> inspector(new ScriptInspector(script, std::move(context)));
    if (!inspector->Start(std::move(title), std::move(url))) {
        return nullptr;
    }
    return inspector;
}

ScriptInspector::ScriptInspector(Script& script, ub::Context context)
    : script_(&script), context_(std::move(context)) {}

bool ScriptInspector::Start(std::string title, std::string url) {
    // Constructed on the script's thread with its isolate entered (Script::SetupIsolate).
    inspector_ = ub::Inspector::New(context_.GetIsolate(), *this);
    if (!inspector_) {
        return false;
    }
    inspector_->ContextCreated(context_, title);

    // Routing key for the ws path: the script's OS thread id (unique among live
    // scripts). The human-readable label is `title` (shown in chrome://inspect),
    // so the id itself needn't be meaningful. Read back via target_->Id().
    target_ = std::make_shared<InspectorTarget>(std::to_string(script_->GetNativeThreadId()), std::move(title),
                                                std::move(url), inspector_->Dispatcher(), &OnDispatch,
                                                ub::CallbackData::For(*this));
    InspectorServer::Instance().AddTarget(target_);
    return true;
}

ScriptInspector::~ScriptInspector() {
    // Stop the server from delivering to a target whose consumer is going away.
    InspectorServer::Instance().RemoveTarget(target_->Id());

    // The isolate is still alive and current: the script destroys its debugger
    // on its own thread, before its context is reset.
    TeardownSession();
    inspector_->ContextDestroyed(context_);
    inspector_.reset();
}

void ScriptInspector::DrainIncoming() {
    ProcessQueued();
    // A disconnect that arrived during a pause defers its session teardown to
    // here, where we're guaranteed to be outside the (nested) run loop.
    if (sessionTeardownPending_ && !inPauseLoop_) {
        TeardownSession();
        ProcessQueued();
    }
}

void ScriptInspector::ProcessQueued() {
    for (auto& event : target_->DrainAll()) {
        backlog_.push_back(std::move(event));
    }
    while (!backlog_.empty()) {
        // An event that ends the pause leaves the rest queued for whoever runs
        // next: the level the pause returns to, or the script once it unwinds.
        // A reconnect waits for the unwind too - a session is not destroyed
        // inside a pause.
        if (inPauseLoop_ &&
            (quitPause_ || sessionTeardownPending_ || backlog_.front().kind == InspectorTarget::EventKind::Connected)) {
            break;
        }
        // Popped before it runs: a dispatch can run script, and script can drain
        // again from a dispatch request.
        const InspectorTarget::Event event = std::move(backlog_.front());
        backlog_.pop_front();
        ProcessEvent(event);
    }
}

void ScriptInspector::ProcessEvent(const InspectorTarget::Event& event) {
    switch (event.kind) {
        case InspectorTarget::EventKind::Connected:
            // A fresh DevTools client. Tear down any lingering session first -
            // including one whose teardown was deferred from a pause-time
            // disconnect - so a quick reconnect never reuses the old session.
            TeardownSession();
            session_ = inspector_->Connect();
            break;
        case InspectorTarget::EventKind::Message:
            if (session_ && !sessionTeardownPending_) {
                // Mark inspector-driven execution: a console.log run by this
                // dispatch (a DevTools REPL evaluate) routes to the engine's
                // console (the DevTools panel), not the script's polyfill.
                ReplEvalScope evalScope;
                session_->DispatchProtocolMessage(event.payload);
            }
            break;
        case InspectorTarget::EventKind::Disconnected:
            if (inPauseLoop_) {
                // A session is not destroyed inside the (nested) pause loop.
                // Flag it: the pause loop unwinds every level on this flag
                // (resuming at each), then DrainIncoming tears it down.
                sessionTeardownPending_ = true;
            } else {
                TeardownSession();
            }
            break;
    }
}

void ScriptInspector::SendProtocolMessage(std::string_view message) {
    InspectorServer::Instance().Send(target_->Id(), std::string(message));
}

void ScriptInspector::RunMessageLoopOnPause() {
    // A breakpoint must not freeze the game or deadlock the game locks: release
    // any game lock this thread holds for the duration of the pause and reacquire
    // on the way out. Both releasers no-op when no lock is held; a thread never
    // holds both at once, but releasing both keeps this correct regardless of
    // which lock the script was paused under.
    game::GameWriteLockReleaser writeReleaser;
    game::GameReadLockReleaser readReleaser;

    // The engine re-enters this loop when a debugger evaluation (Runtime.evaluate /
    // evaluateOnCallFrame) itself hits a breakpoint or `debugger`. Save/restore
    // the pause flags so an inner resume doesn't abandon the outer pause.
    const bool outerInPause = inPauseLoop_;
    const bool outerQuit = quitPause_;
    inPauseLoop_ = true;
    quitPause_ = false;

    const std::stop_token stop = script_->GetStopToken();
    // sessionTeardownPending_ is set by a disconnect during pause; it exits every
    // nested level (each resumes the engine via the trailing Resume() below).
    while (!quitPause_ && !sessionTeardownPending_ && !stop.stop_requested()) {
        ProcessQueued();
        if (quitPause_ || sessionTeardownPending_ || stop.stop_requested()) {
            break;
        }
        target_->WaitForEvents(stop, std::chrono::milliseconds(50));
    }

    // Leaving other than via a clean per-level resume (QuitMessageLoopOnPause set
    // quitPause_): tell the engine to leave this pause level so it unwinds. A
    // disconnect pops every level this way; Stop()'s TerminateExecution then aborts.
    if (!quitPause_ && session_) {
        session_->Resume();
    }
    inPauseLoop_ = outerInPause;
    quitPause_ = outerQuit;

    // What the pause left behind would otherwise wait for the next message from
    // DevTools or the script's next delay().
    if (!inPauseLoop_ && (!backlog_.empty() || sessionTeardownPending_)) {
        inspector_->Dispatcher()->RequestDispatch(&OnDispatch, ub::CallbackData::For(*this));
    }
}

void ScriptInspector::QuitMessageLoopOnPause() {
    // Called synchronously on the isolate thread from inside DispatchProtocolMessage
    // (the resume/step that ends the pause), so RunMessageLoopOnPause sees quitPause_
    // the moment ProcessEvent returns - no queue wakeup needed.
    quitPause_ = true;
}

double ScriptInspector::CurrentTimeMs() {
    // DevTools timestamps want real wall-clock time. This runs on the script
    // thread, which opts into the speedhack (system_clock::now() reads the
    // hooked GetSystemTimePreciseAsFileTime), so bypass scaling here -
    // otherwise timestamps would race ahead at speed > 1.
    speedhack::SpeedhackDisabledScope realTime;
    return ub::InspectorClient::CurrentTimeMs();
}

std::optional<std::string> ScriptInspector::ResourceNameToUrl(std::string_view resourceName) {
    // Map a Windows script path (e.g. C:\d2bs\libs\Town.js) to a file:// URL,
    // relative to the script base (file:///libs/Town.js), so DevTools' Sources
    // panel binds gutter-set breakpoints reliably, the install path stays out
    // of DevTools, and the URLs match the target url the script advertises.
    // The compile origin stays the raw path (kolbot's require.js regex depends
    // on it); only the URL DevTools sees changes. Non-path origins (e.g.
    // "Console") pass through unchanged.
    if (resourceName.size() < 3 || resourceName[1] != ':') {
        return std::nullopt;
    }
    return config::GetAppConfig().GetScriptPaths().FileUrl(utils::ToWStr(std::string(resourceName)));
}

void ScriptInspector::TeardownSession() {
    if (session_) {
        session_->Stop();
        session_.reset();
    }
    sessionTeardownPending_ = false;
}

}  // namespace d2bs::runtime::inspector
