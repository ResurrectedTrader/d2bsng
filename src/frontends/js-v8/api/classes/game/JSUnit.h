#pragma once

#include <v8.h>

#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "game/Unit.h"

namespace d2bs::api::classes {

// V8 binding for game::Unit - covers all unit types (players, monsters, objects, missiles, items, tiles). Obtained via
// getUnit(); not directly constructable.

class JSUnit : public V8ClassBase<JSUnit, game::Unit> {
   public:
    static constexpr std::string_view ClassName = "Unit";

    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);
};

}  // namespace d2bs::api::classes
