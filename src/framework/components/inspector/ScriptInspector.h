#pragma once

#include <v8-inspector.h>
#include <v8.h>

#include <cstdint>
#include <memory>
#include <string>

#include "components/inspector/InspectorTarget.h"

namespace d2bs {
class Script;
}  // namespace d2bs

namespace d2bs::framework::inspector {

class InspectorChannel;
class InspectorClient;

// Per-isolate glue between V8's inspector and the InspectorServer. Owns the
// V8Inspector, the protocol Channel (outbound CDP -> server) and Client
// (pause-loop control), and the V8InspectorSession that exists while a DevTools
// client is attached.
//
// Threading: every method here runs on the owning script's isolate thread.
// Only the InspectorTarget it registers is touched by the server's WebSocket
// threads. Inbound events are drained and dispatched on the isolate thread via
// DrainIncoming() (from the script event loop and from a V8 interrupt) and,
// while paused at a breakpoint, from RunPauseLoop().
class ScriptInspector {
   public:
    // v8::Isolate embedder-data slot where each ScriptInspector registers itself
    // (slot 0 is the owning Script). The busy-script interrupt resolves the
    // inspector from here to drain the CDP queue; see InspectorTarget::Push.
    static constexpr uint32_t ISOLATE_DATA_SLOT = 1;

    // True while the inspector is executing JS on the isolate thread (inside
    // dispatchProtocolMessage) - i.e. a DevTools console / REPL evaluate. The
    // console shim in Script::SetupIsolate reads this to route console.log to
    // V8's built-in console (DevTools panel) for REPL calls, vs the script's own
    // print-routed polyfill for normal script execution.
    [[nodiscard]] static bool IsEvaluating() { return evalDepth_ > 0; }

    // RAII marker for inspector-driven execution; wraps dispatchProtocolMessage.
    class ReplEvalScope {
       public:
        ReplEvalScope() { ++evalDepth_; }
        ~ReplEvalScope() { --evalDepth_; }
        ReplEvalScope(const ReplEvalScope&) = delete;
        ReplEvalScope& operator=(const ReplEvalScope&) = delete;
        ReplEvalScope(ReplEvalScope&&) = delete;
        ReplEvalScope& operator=(ReplEvalScope&&) = delete;
    };

    ScriptInspector(Script* script, std::string title, std::string url);
    ~ScriptInspector();

    ScriptInspector(const ScriptInspector&) = delete;
    ScriptInspector& operator=(const ScriptInspector&) = delete;

    // Drain and dispatch all queued inbound CDP events. Isolate thread only.
    void DrainIncoming();

   private:
    friend class InspectorChannel;
    friend class InspectorClient;

    void ProcessEvent(const InspectorTarget::Event& event);
    void RunPauseLoop();
    void QuitPauseLoop();
    void TeardownSession();
    void SendToClient(const std::string& message);
    [[nodiscard]] v8::Local<v8::Context> Context() const;

    static constexpr int CONTEXT_GROUP_ID = 1;

    Script* script_;
    // Strong ref: the isolate can't be Disposed while this inspector is alive.
    // TeardownIsolate drops it via inspector_.reset() before the isolate's final
    // ref goes, so isolate_ stays valid through ~ScriptInspector (contextDestroyed).
    std::shared_ptr<v8::Isolate> isolate_;

    std::unique_ptr<v8_inspector::V8Inspector::Channel> channel_;
    std::unique_ptr<v8_inspector::V8InspectorClient> client_;
    std::unique_ptr<v8_inspector::V8Inspector> inspector_;
    std::unique_ptr<v8_inspector::V8InspectorSession> session_;
    std::shared_ptr<InspectorTarget> target_;

    bool inPauseLoop_ = false;
    bool quitPause_ = false;
    bool sessionTeardownPending_ = false;

    // Inspector-driven-execution depth (see IsEvaluating). Isolate-thread-only,
    // but thread_local so the console shim's getter can read it statically with
    // no instance to hand. One isolate per thread, so this never aliases.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - thread-local by design
    inline static thread_local int32_t evalDepth_ = 0;
};

}  // namespace d2bs::framework::inspector
