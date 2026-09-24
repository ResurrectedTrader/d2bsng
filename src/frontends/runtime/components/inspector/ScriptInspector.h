#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "components/inspector/InspectorTarget.h"
#include "unibind/unibind.h"

namespace d2bs {
class Script;
}  // namespace d2bs

namespace d2bs::runtime::inspector {

// Per-isolate glue between the engine's inspector (ub::Inspector) and the
// InspectorServer. It is
// the inspector's client - outbound CDP goes to the server, pauses run here -
// and owns the ub::InspectorSession that exists while a DevTools client is
// attached.
//
// Threading: every method here runs on the owning script's isolate thread.
// Only the InspectorTarget it registers is touched by the server's WebSocket
// threads. Inbound events are drained and dispatched on the isolate thread via
// DrainIncoming() (from the script event loop and from an inspector dispatch
// request) and, while paused at a breakpoint, from RunMessageLoopOnPause().
class ScriptInspector final : public ub::InspectorClient {
   public:
    // Null when the isolate cannot have an inspector.
    [[nodiscard]] static std::unique_ptr<ScriptInspector> Create(Script& script, ub::Context context, std::string title,
                                                                 std::string url);

    // True while the inspector is executing JS on the isolate thread (inside
    // DispatchProtocolMessage) - i.e. a DevTools console / REPL evaluate. The
    // script's console routing reads this  to
    // send console.log to the engine's built-in console (DevTools panel) for REPL
    // calls, vs the script's own print-routed console for normal script execution.
    [[nodiscard]] static bool IsEvaluating() { return evalDepth_ > 0; }

    // RAII marker for inspector-driven execution; wraps DispatchProtocolMessage.
    class ReplEvalScope {
       public:
        ReplEvalScope() { ++evalDepth_; }
        ~ReplEvalScope() { --evalDepth_; }
        ReplEvalScope(const ReplEvalScope&) = delete;
        ReplEvalScope& operator=(const ReplEvalScope&) = delete;
        ReplEvalScope(ReplEvalScope&&) = delete;
        ReplEvalScope& operator=(ReplEvalScope&&) = delete;
    };

    ~ScriptInspector() override;

    ScriptInspector(const ScriptInspector&) = delete;
    ScriptInspector& operator=(const ScriptInspector&) = delete;
    ScriptInspector(ScriptInspector&&) = delete;
    ScriptInspector& operator=(ScriptInspector&&) = delete;

    // Drain and dispatch all queued inbound CDP events. Isolate thread only.
    void DrainIncoming();

    void SendProtocolMessage(std::string_view message) override;
    void RunMessageLoopOnPause() override;
    void QuitMessageLoopOnPause() override;
    [[nodiscard]] double CurrentTimeMs() override;
    [[nodiscard]] std::optional<std::string> ResourceNameToUrl(std::string_view resourceName) override;

   private:
    ScriptInspector(Script& script, ub::Context context);
    [[nodiscard]] bool Start(std::string title, std::string url);

    void ProcessQueued();
    void ProcessEvent(const InspectorTarget::Event& event);
    void TeardownSession();

    // Holds no reference to the isolate: the script destroys its debugger inside
    // TeardownIsolate, while it still holds the isolate itself, and that teardown
    // waits for every other reference to go first - one held here would never go.
    Script* script_;
    ub::Context context_;

    std::unique_ptr<ub::Inspector> inspector_;
    std::unique_ptr<ub::InspectorSession> session_;
    std::shared_ptr<InspectorTarget> target_;
    // Taken from the target but not yet processed: what an event that ended a
    // pause left behind it.
    std::deque<InspectorTarget::Event> backlog_;

    bool inPauseLoop_ = false;
    bool quitPause_ = false;
    bool sessionTeardownPending_ = false;

    // Inspector-driven-execution depth (see IsEvaluating). Isolate-thread-only,
    // but thread_local so the console routing can read it statically with no
    // instance to hand. One isolate per thread, so this never aliases.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - thread-local by design
    inline static thread_local int32_t evalDepth_ = 0;
};

}  // namespace d2bs::runtime::inspector
