#pragma once

#include <string_view>

#include "Args.h"

namespace d2bs::script {

// The registration pass: the API declares its surface into this, and what that
// means for a given engine is the frontend's business.
//
// Globals and constants only, for now. Bound classes need an ownership model
// declared here rather than a finalizer callback handed back - SpiderMonkey
// finalizers may not call into the engine at all, so a portable binding cannot
// be allowed to supply one - and that is a larger design than this header.
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
