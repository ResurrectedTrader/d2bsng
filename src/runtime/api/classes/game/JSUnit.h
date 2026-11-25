#pragma once

#include <v8.h>

#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "game/Unit.h"

namespace d2bs::runtime::api::classes {

// V8 binding for game::Unit - covers all unit types (players, monsters, objects, missiles, items, tiles). Obtained via
// getUnit(); not directly constructable.

class JSUnit : public ClassBase<JSUnit, game::Unit> {
   public:
    static constexpr std::string_view ClassName = "Unit";

    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);

    // getMercHP's value for `owner`'s mercenary, under the caller's game read lock: its HP percent, 0 while it lies
    // dead in view, nullopt when `owner` is no player or has no mercenary in view.
    static std::optional<uint32_t> MercHpPercent(const game::Unit& owner);
};

}  // namespace d2bs::runtime::api::classes
