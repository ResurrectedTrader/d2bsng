#pragma once

#include <string_view>

#include "api/core/Class.h"
#include "game/Unit.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Binding for game::Unit - covers all unit types (players, monsters, objects, missiles, items, tiles). Obtained via
// getUnit(); not directly constructable.

class JSUnit : public ClassBase<JSUnit, game::Unit> {
   public:
    static constexpr std::string_view ClassName = "Unit";

    static void Configure(const ub::Class<Native>& cls);
};

}  // namespace d2bs::api::classes
