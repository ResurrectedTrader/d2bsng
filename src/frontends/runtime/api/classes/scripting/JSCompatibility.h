#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "config/CompatibilityFlags.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Native payload for the Compatibility class. The class is a pure static
// namespace (never instantiated); this carries no state and exists only to
// satisfy the ClassBase NativeType parameter.
struct CompatibilityData {};

// `Compatibility`: a non-constructable namespace object for inspecting and
// toggling the engine's backwards-compatibility flags (SpiderMonkey/kolbot-era
// behaviors). All flags default to enabled. The flag set is shared across every
// script; a change affects scripts compiled / started afterwards. Used from
// scripts as `Compatibility.set("objectToSource", false)`,
// `Compatibility.enabled()`, etc. The available flag names are in
// the API docs (the CompatibilityFlag set); the store lives in
// d2bs::config::CompatibilityFlags.
class JSCompatibility : public ClassBase<JSCompatibility, CompatibilityData> {
   public:
    static constexpr std::string_view ClassName = "Compatibility";

    static void Configure(const ub::Class<CompatibilityData>& cls) {
        /// @description The names of every currently-enabled flag.
        /// @signature Compatibility.enabled()
        /// @returns {Array<string>} - the enabled flag names, in registration order.
        StaticMethod(
            cls, "enabled", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                const auto& context = args.GetContext();
                std::vector<std::string> names;
                for (const auto& flag : config::CompatibilityFlags::Instance().All()) {
                    if (flag.enabled) {
                        names.push_back(flag.name);
                    }
                }
                auto arr = ub::Array::New(context, static_cast<uint32_t>(names.size()));
                if (!arr) {
                    return;
                }
                uint32_t i = 0;
                for (const auto& name : names) {
                    if (!arr->Set(context, i++, convert::ToJS(isolate, name)).value_or(false)) {
                        return;
                    }
                }
                args.GetReturnValue().Set(*arr);
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
            cls, "set", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                const auto& context = args.GetContext();
                auto& registry = config::CompatibilityFlags::Instance();

                // Object form: set({flag: bool, ...}). Validate every key before
                // applying so a typo can't leave a half-applied change.
                if (args.Length() == 1 && args[0].IsObject()) {
                    auto obj = args[0].To<ub::Object>();
                    if (!obj) {
                        return;
                    }
                    auto keys = obj->GetOwnPropertyNames(context);
                    if (!keys) {
                        return;
                    }
                    std::vector<std::pair<std::string, bool>> updates;
                    updates.reserve(keys->Length());
                    for (uint32_t i = 0; i < keys->Length(); ++i) {
                        auto key = keys->Get(context, i);
                        if (!key) {
                            return;
                        }
                        auto name = convert::ToString(context, *key);
                        if (!registry.Has(name)) {
                            error::ThrowTypeError(isolate, "Unknown compatibility flag: " + name);
                            return;
                        }
                        auto keyName = key->To<ub::Name>();
                        if (!keyName) {
                            return;
                        }
                        auto value = obj->Get(context, *keyName);
                        if (!value) {
                            return;
                        }
                        updates.emplace_back(name, convert::ToBool(context, *value));
                    }
                    for (const auto& [name, enabled] : updates) {
                        registry.SetEnabled(name, enabled);
                    }
                    return;
                }

                // Pair form: set(flag, enabled).
                if (args.Length() >= 2 && args[0].IsString()) {
                    auto name = convert::ToString(context, args[0]);
                    if (!registry.SetEnabled(name, convert::ToBool(context, args[1]))) {
                        error::ThrowTypeError(isolate, "Unknown compatibility flag: " + name);
                    }
                    return;
                }

                error::ThrowTypeError(isolate, "Compatibility.set requires (flag, enabled) or ({flag: enabled, ...})");
            });

        /// @description Restore every flag to its default state (all enabled).
        /// @signature Compatibility.reset()
        StaticMethod(cls, "reset", +[](const ub::CallbackInfo&) { config::CompatibilityFlags::Instance().Reset(); });
    }
};

}  // namespace d2bs::api::classes
