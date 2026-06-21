#pragma once

#include <v8.h>

#include <string>
#include <string_view>
#include <vector>

#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "components/config/CompatibilityFlags.h"

namespace d2bs::api::classes {

// Native payload for the Compatibility class. The class is a pure static
// namespace (never instantiated); this carries no state and exists only to
// satisfy the V8ClassBase NativeType parameter.
struct CompatibilityData {};

// `Compatibility`: a non-constructable namespace object for inspecting and
// toggling the engine's backwards-compatibility flags (SpiderMonkey/kolbot-era
// behaviors). All flags default to enabled. The flag set is shared across every
// script; a change affects scripts compiled / started afterwards. Used from
// scripts as `Compatibility.set("objectToSource", false)`,
// `Compatibility.enabled()`, etc. The available flag names are in
// the API docs (the CompatibilityFlag set); the store lives in
// d2bs::config::CompatibilityFlags.
class JSCompatibility : public V8ClassBase<JSCompatibility, CompatibilityData> {
   public:
    static constexpr std::string_view ClassName = "Compatibility";
    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        /// @description The names of every currently-enabled flag.
        /// @signature Compatibility.enabled()
        /// @returns {Array<string>} - the enabled flag names, in registration order.
        StaticMethod(
            isolate, tpl, "enabled", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                std::vector<std::string> names;
                for (const auto& flag : d2bs::config::CompatibilityFlags::Instance().All()) {
                    if (flag.enabled) {
                        names.push_back(flag.name);
                    }
                }
                auto arr = v8::Array::New(isolate, static_cast<int32_t>(names.size()));
                uint32_t i = 0;
                for (const auto& name : names) {
                    arr->Set(context, i++, v8_convert::ToV8(isolate, name)).Check();
                }
                args.GetReturnValue().Set(arr);
            });

        /// @description Enable or disable one or more flags. Two forms: a single (flag, enabled) pair, or an object of
        /// flag-name to boolean for setting several at once. The object form is all-or-nothing - if any key is not a
        /// known flag it throws and changes nothing. Most flags take effect for scripts compiled / started after the
        /// change.
        /// @signature Compatibility.set(flag, enabled)
        /// @param flag {CompatibilityFlag} - a compatibility flag name.
        /// @param enabled {boolean} - true to enable, false to disable.
        /// @signature Compatibility.set(flags)
        /// @param flags {object} - a `{ [flag: string]: boolean }` map; each named flag is set accordingly.
        /// @throws {TypeError} - if a flag name is not a known compatibility flag, or the arguments match neither form.
        StaticMethod(
            isolate, tpl, "set", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                auto& registry = d2bs::config::CompatibilityFlags::Instance();

                // Object form: set({flag: bool, ...}). Validate every key before
                // applying so a typo can't leave a half-applied change.
                if (args.Length() == 1 && args[0]->IsObject()) {
                    auto obj = args[0].As<v8::Object>();
                    v8::Local<v8::Array> keys;
                    if (!obj->GetOwnPropertyNames(context).ToLocal(&keys)) {
                        return;
                    }
                    std::vector<std::pair<std::string, bool>> updates;
                    updates.reserve(keys->Length());
                    for (uint32_t i = 0; i < keys->Length(); ++i) {
                        v8::Local<v8::Value> key;
                        if (!keys->Get(context, i).ToLocal(&key)) {
                            return;
                        }
                        auto name = v8_convert::ToString(isolate, key);
                        if (!registry.Has(name)) {
                            v8_error::ThrowTypeError(isolate, "Unknown compatibility flag: " + name);
                            return;
                        }
                        v8::Local<v8::Value> value;
                        if (!obj->Get(context, key).ToLocal(&value)) {
                            return;
                        }
                        updates.emplace_back(name, value->BooleanValue(isolate));
                    }
                    for (const auto& [name, enabled] : updates) {
                        registry.SetEnabled(name, enabled);
                    }
                    return;
                }

                // Pair form: set(flag, enabled).
                if (args.Length() >= 2 && args[0]->IsString()) {
                    auto name = v8_convert::ToString(isolate, args[0]);
                    if (!registry.SetEnabled(name, args[1]->BooleanValue(isolate))) {
                        v8_error::ThrowTypeError(isolate, "Unknown compatibility flag: " + name);
                    }
                    return;
                }

                v8_error::ThrowTypeError(isolate,
                                         "Compatibility.set requires (flag, enabled) or ({flag: enabled, ...})");
            });

        /// @description Restore every flag to its default state (all enabled).
        /// @signature Compatibility.reset()
        StaticMethod(
            isolate, tpl, "reset",
            +[](const v8::FunctionCallbackInfo<v8::Value>&) { d2bs::config::CompatibilityFlags::Instance().Reset(); });
    }
};

}  // namespace d2bs::api::classes
