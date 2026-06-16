#include "JSSandbox.h"

#include "components/config/AppConfig.h"
#include "components/script/CompileSource.h"
#include "components/script/ScriptEngine.h"
#include "utils/utils.h"

namespace d2bs::api::classes {

// Helper: get the sandbox context's global object (the inner scope)
static v8::Local<v8::Object> GetInnerGlobal(v8::Isolate* isolate, SandboxData* data) {
    if (!data || data->context.IsEmpty())
        return {};
    return data->context.Get(isolate)->Global();
}

/// @description Create an isolated JS execution environment whose global object is the sandbox scope.
/// @signature Sandbox()
/// @returns {Sandbox} - a new sandbox with an empty scope and no included files.
void JSSandbox::New(const v8::FunctionCallbackInfo<v8::Value>& args) {
    V8_CLASS_CTOR_PROLOGUE;

    auto data = std::make_unique<SandboxData>();

    // Create a new context - its global object IS the sandbox scope
    // (matching d2bs reference where innerObj = JS_NewObject(box->context, &global_obj))
    auto context = v8::Context::New(isolate);
    data->context.Reset(isolate, context);

    InitInstance(isolate, args.This(), std::move(data));
    args.GetReturnValue().Set(args.This());
}

void JSSandbox::ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
    auto inst = tpl->InstanceTemplate();
    auto proto = tpl->PrototypeTemplate();

    // Set up named property handlers - proxy property access to the sandbox context's global
    inst->SetHandler(v8::NamedPropertyHandlerConfiguration(NamedPropertyGetter, NamedPropertySetter, NamedPropertyQuery,
                                                           NamedPropertyDeleter, NamedPropertyEnumerator));

    /// @description Compile and run JS source in the sandbox scope, returning its completion value.
    /// @signature evaluate(code: string)
    /// @param code {string} - JS source to compile and execute in the sandbox context.
    /// @returns {any} - the completion value; undefined if compile/run threw.
    Method(
        isolate, proto, "evaluate", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!v8_error::CheckArgCount(args, 1, "evaluate"))
                return;
            if (!args[0]->IsString()) {
                v8_error::ThrowTypeError(isolate, "evaluate() requires a string argument");
                return;
            }

            auto data = Unwrap(args.This());
            if (!data || data->context.IsEmpty()) {
                v8_error::ThrowError(isolate, "Invalid sandbox object");
                return;
            }

            // Execute in the sandbox context - its global is the scope
            auto sandboxContext = data->context.Get(isolate);
            v8::Context::Scope contextScope(sandboxContext);

            std::string source = v8_convert::ToString(isolate, args[0]);

            v8::Local<v8::Script> script;
            if (!framework::script::CompileSource(isolate, sandboxContext, std::move(source), "sandbox")
                     .ToLocal(&script))
                return;

            v8::Local<v8::Value> result;
            if (script->Run(sandboxContext).ToLocal(&result)) {
                args.GetReturnValue().Set(result);
            }
        });

    /// @description Read, compile, and run a libs/ script file in the sandbox scope, deduped per sandbox.
    /// @signature include(file: string)
    /// @param file {string} - filename resolved under the script base "libs/" directory (lowercased for dedup).
    /// @returns {any|boolean} - the file's completion value on success; false if already included, not found,
    /// unopenable, or compile/run failed.
    Method(
        isolate, proto, "include", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!v8_error::CheckArgCount(args, 1, "include"))
                return;
            if (!args[0]->IsString()) {
                v8_error::ThrowTypeError(isolate, "include() requires a string argument");
                return;
            }

            auto data = Unwrap(args.This());
            if (!data || data->context.IsEmpty()) {
                v8_error::ThrowError(isolate, "Invalid sandbox object");
                return;
            }

            // Normalize for case-insensitive dedup on Windows (matches Script::Include).
            std::string filename = d2bs::utils::ToLower(v8_convert::ToString(isolate, args[0]));

            // Check if already included
            if (data->includedFiles.contains(filename)) {
                args.GetReturnValue().SetFalse();
                return;
            }

            // Resolve path from libs/
            auto resolved = d2bs::config::GetPathRelScript("libs/" + filename);
            if (resolved.empty() || !std::filesystem::exists(resolved)) {
                args.GetReturnValue().SetFalse();
                return;
            }

            // Read file
            std::ifstream file(resolved, std::ios::binary);
            if (!file.is_open()) {
                args.GetReturnValue().SetFalse();
                return;
            }
            std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            // Compile and execute in sandbox context
            auto sandboxContext = data->context.Get(isolate);
            v8::Context::Scope contextScope(sandboxContext);

            v8::Local<v8::Script> script;
            if (!framework::script::CompileSource(isolate, sandboxContext, std::move(source), filename)
                     .ToLocal(&script)) {
                args.GetReturnValue().SetFalse();
                return;
            }

            v8::Local<v8::Value> result;
            if (script->Run(sandboxContext).ToLocal(&result)) {
                data->includedFiles.insert(filename);
                args.GetReturnValue().Set(result);
            } else {
                args.GetReturnValue().SetFalse();
            }
        });

    /// @description Test whether a file has already been included into this sandbox.
    /// @signature isIncluded(file: string)
    /// @param file {string} - filename to check (lowercased before lookup, matching include()'s key).
    /// @returns {boolean} - true if previously included; false otherwise.
    Method(
        isolate, proto, "isIncluded", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!v8_error::CheckArgCount(args, 1, "isIncluded"))
                return;
            if (!args[0]->IsString()) {
                v8_error::ThrowTypeError(isolate, "isIncluded() requires a string argument");
                return;
            }

            auto data = Unwrap(args.This());
            if (!data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            std::string filename = d2bs::utils::ToLower(v8_convert::ToString(isolate, args[0]));
            args.GetReturnValue().Set(data->includedFiles.contains(filename));
        });

    /// @description Reset the sandbox to a fresh empty scope, discarding all state and included files.
    /// @signature clearScope()
    /// @returns {undefined} - nothing.
    Method(
        isolate, proto, "clearScope", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            auto data = Unwrap(args.This());
            if (!data)
                return;

            // Recreate the context to get a fresh global scope
            auto context = v8::Context::New(isolate);
            data->context.Reset(isolate, context);
            data->includedFiles.clear();
        });
}

v8::Intercepted JSSandbox::NamedPropertyGetter(v8::Local<v8::Name> property,
                                               const v8::PropertyCallbackInfo<v8::Value>& info) {
    auto data = Unwrap(info.This());
    auto inner = GetInnerGlobal(info.GetIsolate(), data);
    if (inner.IsEmpty())
        return v8::Intercepted::kNo;

    auto context = data->context.Get(info.GetIsolate());
    v8::Local<v8::Value> result;
    if (inner->Get(context, property).ToLocal(&result) && !result->IsUndefined()) {
        info.GetReturnValue().Set(result);
        return v8::Intercepted::kYes;
    }
    return v8::Intercepted::kNo;
}

v8::Intercepted JSSandbox::NamedPropertySetter(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                                               const v8::PropertyCallbackInfo<void>& info) {
    auto data = Unwrap(info.This());
    auto inner = GetInnerGlobal(info.GetIsolate(), data);
    if (inner.IsEmpty())
        return v8::Intercepted::kNo;

    auto context = data->context.Get(info.GetIsolate());
    inner->Set(context, property, value).Check();
    return v8::Intercepted::kYes;
}

v8::Intercepted JSSandbox::NamedPropertyQuery(v8::Local<v8::Name> property,
                                              const v8::PropertyCallbackInfo<v8::Integer>& info) {
    auto data = Unwrap(info.This());
    auto inner = GetInnerGlobal(info.GetIsolate(), data);
    if (inner.IsEmpty())
        return v8::Intercepted::kNo;

    auto context = data->context.Get(info.GetIsolate());
    if (inner->Has(context, property).FromMaybe(false)) {
        info.GetReturnValue().Set(static_cast<int32_t>(v8::PropertyAttribute::None));
        return v8::Intercepted::kYes;
    }
    return v8::Intercepted::kNo;
}

v8::Intercepted JSSandbox::NamedPropertyDeleter(v8::Local<v8::Name> property,
                                                const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    auto data = Unwrap(info.This());
    auto inner = GetInnerGlobal(info.GetIsolate(), data);
    if (inner.IsEmpty()) {
        info.GetReturnValue().SetFalse();
        return v8::Intercepted::kYes;
    }

    auto context = data->context.Get(info.GetIsolate());
    info.GetReturnValue().Set(inner->Delete(context, property).FromMaybe(false));
    return v8::Intercepted::kYes;
}

void JSSandbox::NamedPropertyEnumerator(const v8::PropertyCallbackInfo<v8::Array>& info) {
    auto data = Unwrap(info.This());
    auto inner = GetInnerGlobal(info.GetIsolate(), data);
    if (inner.IsEmpty()) {
        info.GetReturnValue().Set(v8::Array::New(info.GetIsolate(), 0));
        return;
    }

    auto context = data->context.Get(info.GetIsolate());
    v8::Local<v8::Array> props;
    if (inner->GetPropertyNames(context).ToLocal(&props)) {
        info.GetReturnValue().Set(props);
    } else {
        info.GetReturnValue().Set(v8::Array::New(info.GetIsolate(), 0));
    }
}

}  // namespace d2bs::api::classes
