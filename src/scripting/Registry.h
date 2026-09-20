#pragma once

#include <string_view>

#include "Args.h"

namespace d2bs::script {

// The registration pass: the API declares its surface into this, and what that
// means for a given engine is the frontend's business.
//
// Globals and constants only, for now. The shape follows V8's, because V8's
// model is the one this tree already thinks in and an engine-neutral
// reinvention would be a worse abstraction in both directions. Where V8 is
// followed, it is followed exactly; the departures are only the places where
// V8's API carries strictly less information than another engine needs,
// because V8 has an out-of-band mechanism to fall back on and others do not.
//
// Two of those departures are not in this header yet, and both apply when
// bound classes arrive:
//
//   - Ownership is DECLARED, never a supplied callback. V8 lets a finalizer
//     touch the isolate; SpiderMonkey finalizers may not call the engine at
//     all, so a contract that accepts a finalizer callback is not merely
//     harder there, it is unimplementable. A class declares how its native is
//     owned and the frontend owns the mechanism. Ownership is per instance,
//     not per class: the same native type is handed out owned by one binding
//     and borrowed by another.
//
//   - A native a binding owns holds NO engine handle. V8 permits a v8::Global
//     inside a native struct; an engine whose roots are thread-affine and
//     whose finalizers cannot call the engine does not. Exactly two natives in
//     this tree break the rule today, and they are exactly the two that cannot
//     be written against this contract - which is the rule earning its place.
class Registry {
   public:
    explicit Registry(void* state) : state_(state) {}

    void Global(std::string_view name, Native fn);
    void Constant(std::string_view name, double value);
    void Constant(std::string_view name, std::string_view value);

   private:
    void* state_;
};

}  // namespace d2bs::script
