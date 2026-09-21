#pragma once

#include <span>
#include <utility>
#include <vector>

#include "Value.h"

namespace d2bs::script {

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
