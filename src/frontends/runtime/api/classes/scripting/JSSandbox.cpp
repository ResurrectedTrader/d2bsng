#include "JSSandbox.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "components/script/CompileSource.h"
#include "config/AppConfig.h"
#include "utils/utils.h"

namespace d2bs::api::classes {

// The sandbox realm behind a Sandbox instance, or an empty context. A copy rather than a
// reference: script run in the sandbox may call clearScope and replace the one on the native.
static ub::Context GetSandboxContext(const ub::Local<ub::Object>& self) {
    auto* data = JSSandbox::Unwrap(self);
    if (!data) {
        return {};
    }
    return data->context;
}

/// @description Create an isolated JS execution environment whose global object is the sandbox scope.
/// @signature Sandbox()
/// @returns {Sandbox} - a new sandbox with an empty scope and no included files.
std::unique_ptr<SandboxData> JSSandbox::New(const ub::CallbackInfo& args) {
    auto data = std::make_unique<SandboxData>();

    // Create a new context - its global object IS the sandbox scope
    // (matching d2bs reference where innerObj = JS_NewObject(box->context, &global_obj))
    data->context = ub::Context::New(args.GetIsolate()).value_or(ub::Context());
    return data;
}

void JSSandbox::Configure(const ub::Class<SandboxData>& cls) {
    cls.SetHandler(ub::NamedPropertyHandler{.getter = &NamedPropertyGetter,
                                            .setter = &NamedPropertySetter,
                                            .query = &NamedPropertyQuery,
                                            .deleter = &NamedPropertyDeleter,
                                            .enumerator = &NamedPropertyEnumerator});

    /// @description Compile and run JS source in the sandbox scope, returning its completion value.
    /// @signature evaluate(code: string)
    /// @param code {string} - JS source to compile and execute in the sandbox context.
    /// @returns {any} - the completion value; undefined if compile/run threw.
    Method(
        cls, "evaluate", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            if (!error::CheckArgCount(args, 1, "evaluate")) {
                return;
            }
            if (!args[0].IsString()) {
                error::ThrowTypeError(isolate, "evaluate() requires a string argument");
                return;
            }

            const auto sandboxContext = GetSandboxContext(args.This());
            if (sandboxContext.IsEmpty()) {
                error::ThrowError(isolate, "Invalid sandbox object");
                return;
            }

            std::string source = convert::ToString(args.GetContext(), args[0]);

            // Execute in the sandbox context - its global is the scope
            const ub::ContextScope contextScope(sandboxContext);
            auto script = runtime::script::CompileSource(sandboxContext, std::move(source), "sandbox");
            if (!script) {
                return;
            }
            if (auto result = script->Run(sandboxContext)) {
                args.GetReturnValue().Set(*result);
            }
        });

    /// @description Read, compile, and run a libs/ script file in the sandbox scope, deduped per sandbox.
    /// @signature include(file: string)
    /// @param file {string} - filename resolved under the script base "libs/" directory (lowercased for dedup).
    /// @returns {any|boolean} - the file's completion value on success; false if already included, not found,
    /// unopenable, or compile/run failed.
    Method(
        cls, "include", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            if (!error::CheckArgCount(args, 1, "include")) {
                return;
            }
            if (!args[0].IsString()) {
                error::ThrowTypeError(isolate, "include() requires a string argument");
                return;
            }

            auto* data = Unwrap(args.This());
            if (!data || data->context.IsEmpty()) {
                error::ThrowError(isolate, "Invalid sandbox object");
                return;
            }

            // Normalize for case-insensitive dedup on Windows (matches Script::Include).
            std::string filename = utils::ToLower(convert::ToString(args.GetContext(), args[0]));

            if (data->includedFiles.contains(filename)) {
                args.GetReturnValue().SetFalse();
                return;
            }

            auto resolved = config::GetPathRelScript("libs/" + filename);
            if (resolved.empty() || !std::filesystem::exists(resolved)) {
                args.GetReturnValue().SetFalse();
                return;
            }

            std::ifstream file(resolved, std::ios::binary);
            if (!file.is_open()) {
                args.GetReturnValue().SetFalse();
                return;
            }
            std::string source((std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());
            file.close();

            // Compile and execute in sandbox context
            const auto sandboxContext = data->context;
            const ub::ContextScope contextScope(sandboxContext);
            auto script = runtime::script::CompileSource(sandboxContext, std::move(source), filename);
            if (!script) {
                args.GetReturnValue().SetFalse();
                return;
            }

            auto result = script->Run(sandboxContext);
            if (!result) {
                args.GetReturnValue().SetFalse();
                return;
            }
            data->includedFiles.insert(filename);
            args.GetReturnValue().Set(*result);
        });

    /// @description Test whether a file has already been included into this sandbox.
    /// @signature isIncluded(file: string)
    /// @param file {string} - filename to check (lowercased before lookup, matching include()'s key).
    /// @returns {boolean} - true if previously included; false otherwise.
    Method(
        cls, "isIncluded", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            if (!error::CheckArgCount(args, 1, "isIncluded")) {
                return;
            }
            if (!args[0].IsString()) {
                error::ThrowTypeError(isolate, "isIncluded() requires a string argument");
                return;
            }

            auto* data = Unwrap(args.This());
            if (!data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            const std::string filename = utils::ToLower(convert::ToString(args.GetContext(), args[0]));
            args.GetReturnValue().Set(data->includedFiles.contains(filename));
        });

    /// @description Reset the sandbox to a fresh empty scope, discarding all state and included files.
    /// @signature clearScope()
    /// @returns {undefined} - nothing.
    Method(
        cls, "clearScope", +[](const ub::CallbackInfo& args) {
            auto* data = Unwrap(args.This());
            if (!data) {
                return;
            }

            // Recreate the context to get a fresh global scope
            data->context = ub::Context::New(args.GetIsolate()).value_or(ub::Context());
            data->includedFiles.clear();
        });
}

// Each hook enters the sandbox realm before touching its global object: a realm's global is
// access-checked against the current realm, and the hook runs with the caller's realm current.

ub::Intercepted JSSandbox::NamedPropertyGetter(const ub::Local<ub::Name>& property,
                                               const ub::PropertyCallbackInfo& info) {
    const auto inner = GetSandboxContext(info.This());
    if (inner.IsEmpty()) {
        return ub::Intercepted::No;
    }

    const ub::ContextScope contextScope(inner);
    auto result = inner.GlobalObject().Get(inner, property);
    if (result && !result->IsUndefined()) {
        info.GetReturnValue().Set(*result);
        return ub::Intercepted::Yes;
    }
    return ub::Intercepted::No;
}

ub::Intercepted JSSandbox::NamedPropertySetter(const ub::Local<ub::Name>& property, const ub::Local<ub::Value>& value,
                                               const ub::PropertyCallbackInfo& info) {
    const auto inner = GetSandboxContext(info.This());
    if (inner.IsEmpty()) {
        return ub::Intercepted::No;
    }

    const ub::ContextScope contextScope(inner);
    static_cast<void>(inner.GlobalObject().Set(inner, property, value));
    return ub::Intercepted::Yes;
}

std::optional<ub::PropertyAttribute> JSSandbox::NamedPropertyQuery(const ub::Local<ub::Name>& property,
                                                                   const ub::PropertyCallbackInfo& info) {
    const auto inner = GetSandboxContext(info.This());
    if (inner.IsEmpty()) {
        return std::nullopt;
    }

    const ub::ContextScope contextScope(inner);
    if (inner.GlobalObject().Has(inner, property).value_or(false)) {
        return ub::PropertyAttribute::None;
    }
    return std::nullopt;
}

std::optional<bool> JSSandbox::NamedPropertyDeleter(const ub::Local<ub::Name>& property,
                                                    const ub::PropertyCallbackInfo& info) {
    const auto inner = GetSandboxContext(info.This());
    if (inner.IsEmpty()) {
        return false;
    }

    const ub::ContextScope contextScope(inner);
    return inner.GlobalObject().Delete(inner, property).value_or(false);
}

std::optional<ub::Local<ub::Array>> JSSandbox::NamedPropertyEnumerator(const ub::PropertyCallbackInfo& info) {
    const auto inner = GetSandboxContext(info.This());
    if (inner.IsEmpty()) {
        return ub::Array::New(info.GetContext(), 0);
    }

    // Own enumerable string keys of the sandbox global. The reference walk also took enumerable
    // inherited keys, of which a global object's prototype chain has none.
    const ub::ContextScope contextScope(inner);
    if (auto props = inner.GlobalObject().GetOwnPropertyNames(inner)) {
        return props;
    }
    return ub::Array::New(info.GetContext(), 0);
}

}  // namespace d2bs::api::classes
