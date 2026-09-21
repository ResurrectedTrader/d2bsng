#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "CallArgs.h"
#include "Ref.h"
#include "Types.h"

namespace d2bs::script {

class Registry;

// Everything one script needs from a scripting engine, and nothing else.
//
// The split this draws is the one that matters. A script is mostly bookkeeping
// - its name, its mode, the files it has included, the handlers it registered,
// the drawables it owns, how often it should capture a stack - and none of that
// comes from an engine. Only the operations below do. So the script itself
// lives in the runtime, written once, and holds one of these.
//
// Deliberately virtual, unlike Args. An Engine is created once per script and
// its members are called at script lifetime or once per event; the per-binding
// hot path goes through Args, which is link-resolved and inlined back by LTO.
// Paying for a vtable at this granularity buys a far simpler contract and costs
// nothing measurable. Getting that granularity backwards is what makes an
// engine abstraction either slow or enormous.
class Engine {
   public:
    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;
    virtual ~Engine() = default;

    // --- the runtime's own object ----------------------------------------

    // The runtime attaches its per-script object here, and every binding
    // running in this engine gets it back through Args::Internal(). The engine
    // never looks at it. This is the whole of what the contract knows about a
    // script: that the runtime has one, and that it is a pointer.
    virtual void SetInternal(void* internal) = 0;

    // --- running source ---------------------------------------------------

    // What a failed Run does with the exception it caught.
    enum class OnError : uint8_t {
        Report,    // report it and swallow it: the script stops here
        Propagate  // report it and re-raise, so an include fails its includer
    };

    // Compile and run source. `originName` is what appears in stack traces and
    // error messages. Reports false with the error already reported, since how
    // an engine formats an exception is its own business.
    virtual bool Run(std::string_view source, std::string_view originName, OnError onError = OnError::Report) = 0;

    // Run a snippet for the console REPL and describe the result. Nothing when
    // it threw; the engine reports the error itself, as above.
    virtual std::optional<std::string> Evaluate(std::string_view source) = 0;

    // --- calling back into the script -------------------------------------

    // Call one function the script gave us. Arguments are described as values
    // rather than handed over as engine objects, so the caller never holds
    // anything collectable. Reports whether the call voted to block, which is
    // what the event layer asks of a handler.
    virtual bool Call(const Ref& function, const CallArgs& args) = 0;

    // Call a global function by name, if the script defined one. Nothing when
    // it did not - which is how the runtime tells "no entry point" from "the
    // entry point voted false". The runtime owns the convention that the name
    // is `main`; the engine only knows how to look a global up.
    virtual std::optional<bool> CallGlobal(std::string_view name, const CallArgs& args) = 0;

    // --- the engine's own queued work -------------------------------------

    // Run whatever the engine has pending of its own: resolved promises,
    // platform tasks, debugger traffic. The runtime's event loop calls this
    // each turn, because only the engine knows what it has outstanding.
    virtual void Pump() = 0;

    // --- what the console reports -----------------------------------------

    // Optional because an engine need not expose heap numbers; the panels
    // already render "-" when they are absent.
    [[nodiscard]] virtual std::optional<HeapStats> GetHeapStats() = 0;
    virtual void RequestGarbageCollection() = 0;

    // Walk the script's current call stack. The runtime decides *when* to do
    // this - that policy is a mode flag and a cached snapshot, neither of which
    // needs an engine - and only the walk lands here.
    [[nodiscard]] virtual StackTraceSnapshot CaptureStack(int32_t maxFrames) = 0;

    // --- lifecycle ---------------------------------------------------------

    // Interrupt whatever the script is running. Must be safe to call from
    // another thread, which is the whole point of it.
    virtual void Terminate() = 0;
};

// Declare the API surface every engine this frontend creates should carry.
// Called once during bring-up: the runtime supplies the bindings, the frontend
// decides what registering one means.
void DeclareApi(void (*declare)(Registry&));

// Make an engine for one script. `name` is what the script is called, which an
// engine with a debugger shows as the target's title. Null when it could not
// start, which the runtime reports as the script failing to start.
[[nodiscard]] std::unique_ptr<Engine> CreateEngine(std::string_view name);

// Start the engine's debugger listener on `port`, so a remote debugger can
// attach to any script. Reports whether it is listening - false when the port
// could not be bound, and always false for an engine whose EngineInfo says it
// has no debugger. Idempotent: starting on the port already bound is a no-op.
bool StartDebugger(uint16_t port);
void StopDebugger();

// What the engine is and what it supports, so the console asks rather than
// assuming which one it is talking to.
[[nodiscard]] EngineInfo GetEngineInfo();

}  // namespace d2bs::script
