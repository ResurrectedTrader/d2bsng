#pragma once

#include <v8.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include "CallArgs.h"
#include "ScriptTypes.h"

namespace d2bs {
class BaseEvent;
class Script;
}  // namespace d2bs

namespace d2bs::js::drawing {
struct Drawable;
}  // namespace d2bs::js::drawing

namespace d2bs::js::script {

// Report a reference released off its owning thread. Out of line because the
// logger reaches spdlog, and a header the event types include must not.
//
// Logged rather than asserted, and it does not stop the release: shipping
// builds define NDEBUG, so an assert here is only ever seen by a build nobody
// runs. V8 tolerates the release, so continuing is what already happens; the
// report exists because a stricter engine would corrupt its root list here, and
// this is the only warning of that anyone will get.
void ReportOffThreadRelease();

// A JS function held by native code on behalf of the script that owns it.
//
// The rules are the stricter of the engines this tree may host rather than V8's
// own: the reference is created, reset and destroyed on its owning script's
// thread and nowhere else, and it is move-only, because a copy would be a
// second GC root. It records the isolate and thread it came from, so a
// reference used against the wrong script is refused rather than read against
// another heap.
class Ref {
   public:
    Ref() = default;
    Ref(v8::Isolate* isolate, v8::Local<v8::Function> function)
        : isolate_(isolate), owner_(std::this_thread::get_id()), function_(isolate, function) {}

    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    Ref(Ref&&) noexcept = default;
    Ref& operator=(Ref&&) noexcept = default;

    ~Ref() {
        if (!OnOwningThread()) {
            ReportOffThreadRelease();
        }
    }

    [[nodiscard]] bool IsEmpty() const { return function_.IsEmpty(); }

    // Drop the reference. Owning thread only, as for the destructor.
    void Reset() {
        if (!OnOwningThread()) {
            ReportOffThreadRelease();
        }
        function_.Reset();
        isolate_ = nullptr;
    }

   private:
    friend class Invocation;

    // A reference holding nothing roots nothing, so it is free to go anywhere -
    // which is what a moved-from one is.
    [[nodiscard]] bool OnOwningThread() const { return function_.IsEmpty() || owner_ == std::this_thread::get_id(); }

    v8::Isolate* isolate_ = nullptr;
    std::thread::id owner_;
    v8::Global<v8::Function> function_;
};

// Capture a call's arguments as structured-clone blobs, for an event carrying
// values from one script to another. Runs in the caller's frame, so a value
// whose serialisation throws throws out of the call that sent it; one that
// cannot be serialised at all becomes undefined and keeps its position.
std::vector<std::vector<uint8_t>> SerializeArgs(const v8::FunctionCallbackInfo<v8::Value>& args);

// One event's turn on one script's thread: the engine to call into and the
// handlers that script registered for the event. Events say what to call
// through this, which is why they need not name the engine themselves.
class Invocation {
   public:
    Invocation(Script& script, v8::Isolate* isolate, std::span<const v8::Local<v8::Function>> handlers)
        : script_(&script), isolate_(isolate), handlers_(handlers) {}

    // Call every handler the script registered, in order, and report whether any
    // of them returned a truthy value. A handler that throws is logged under the
    // event's name and reports false; the handlers after it still run.
    bool Run(const BaseEvent& event);

    // Call one function the event carries instead of the registered handlers. A
    // reference owned by another script is refused and reports false.
    bool Run(const BaseEvent& event, const Ref& function);

    // Call one of a drawable's input handlers. The event names the drawable and
    // the handler it wants rather than carrying the function, because the game
    // thread picks the target and may not touch a script value there; the
    // handler is resolved here, on the owning thread, where one may be made.
    // Reports false if the script has cleared that handler in the meantime.
    bool Run(const BaseEvent& event, const drawing::Drawable& drawable, DrawableHandler which);

    // Compile and run a snippet in this script's context, reporting the result
    // or the error to the console. An event that carries code rather than
    // arguments asks for an engine operation, which no value can describe.
    void Evaluate(std::string_view code);

   private:
    bool Call(const BaseEvent& event, v8::Local<v8::Function> function);

    Script* script_;
    v8::Isolate* isolate_;
    std::span<const v8::Local<v8::Function>> handlers_;
};

}  // namespace d2bs::js::script
