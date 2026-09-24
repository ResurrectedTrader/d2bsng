#pragma once

#include "unibind/unibind.h"

namespace d2bs::api::classes {

// The native behind the receiver of one of `Class`'s members, or null after throwing a TypeError
// when the receiver is not an instance of it - a method (or an accessor's getter) lifted onto a
// foreign object. Read `This()`, the object the member was reached through.
template <class Class>
typename Class::Native* Receiver(const ub::CallbackContextBase& info) {
    auto* data = Class::Unwrap(info.This());
    if (data == nullptr) {
        info.ThrowTypeError("Illegal invocation");
    }
    return data;
}

}  // namespace d2bs::api::classes
