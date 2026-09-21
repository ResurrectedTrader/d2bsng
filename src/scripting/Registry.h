#pragma once

#include <string_view>

#include "Args.h"
#include "Class.h"

namespace d2bs::script {

// The registration pass. The runtime declares its whole API surface into this,
// and what declaring one means is the frontend's business.
//
// The shape follows V8's, because that is the model this tree already thinks in
// and an engine-neutral reinvention would be a worse abstraction in both
// directions. Where V8 is followed it is followed exactly; the departures are
// only where V8's API carries strictly less information than another engine
// needs, because V8 has an out-of-band mechanism to fall back on and others do
// not:
//
//   - A binding returns bool. V8's own callback returns void because it signals
//     termination through TerminateExecution(); an engine with no such channel
//     says it by returning false with nothing pending. A void contract would
//     discard that answer and swallow a stop().
//
//   - Ownership is declared, never a supplied finalizer. V8 lets a finalizer
//     touch the isolate; an engine whose finalizers may not call it at all
//     cannot implement a contract that accepts one.
//
//   - A native a wrapper owns holds no engine handle. V8 permits a persistent
//     handle inside a native struct; an engine with thread-affine roots and
//     restricted finalizers does not.
class Registry {
   public:
    explicit Registry(void* state) : state_(state) {}

    void Global(std::string_view name, Native fn);
    void Constant(std::string_view name, double value);
    void Constant(std::string_view name, std::string_view value);

    // Declare a bound class. `destroy` frees its native and is the only thing
    // that runs when a wrapper is collected, so it must do nothing else.
    [[nodiscard]] ClassDecl Class(std::string_view name, Destroy destroy);

    // Declare a named object with properties but no class - what `me` is.
    [[nodiscard]] ObjectDecl Object(std::string_view name);

    // What the frontend is registering into, handed back. V8 records into
    // process-wide declarations and never reads this; an engine that registers
    // into something of its own has it here.
    [[nodiscard]] void* State() const { return state_; }

   private:
    void* state_;
};

}  // namespace d2bs::script
