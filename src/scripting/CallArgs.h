#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "Instance.h"

namespace d2bs::script {

// Raw bytes a script receives as a byte array.
struct Bytes {
    std::span<const uint8_t> data;
};

// A value one script serialised for another. Both ends are the same frontend,
// so the blob never crosses engines: it travels opaquely and only the frontend
// that wrote it reads it back.
//
// An empty blob is undefined, which is what a sender that could not clone a
// value stores rather than dropping it and shifting every argument after it.
struct Serialized {
    std::span<const uint8_t> data;
};

// One argument a call hands to a script, named rather than built. Numbers keep
// the width they were written with, because that is what the engine turns into
// an integer or a double.
//
// A Slot is the exception: an object or an array the binding built in its own
// call frame, for the callbacks that are handed a structure rather than a
// scalar. Only a binding can name one, and only for the length of its own call,
// which is exactly the reach of Value::Call - an event is assembled outside any
// call frame and has nothing that could make one.
using Argument = std::variant<bool, int32_t, uint32_t, double, std::string_view, Bytes, Serialized, Slot>;

// The arguments one call into a script will receive. The strings and spans are
// views into storage the event owns; the list is filled and converted inside a
// single call and outlives nothing.
class CallArgs {
   public:
    // The arguments to pass, in order.
    void Set(std::vector<Argument> arguments) { arguments_ = std::move(arguments); }

    [[nodiscard]] std::span<const Argument> Arguments() const { return arguments_; }

   private:
    std::vector<Argument> arguments_;
};

}  // namespace d2bs::script
