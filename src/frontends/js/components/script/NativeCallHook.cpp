#include "components/script/NativeCallHook.h"

#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"

namespace d2bs::js::script {

void OnNativeCall(v8::Isolate* isolate) {
    // Fast path: while no script is capturing per-call stacks, skip the script
    // lookup entirely. This runs on every JS->native call, so it stays cheap.
    if (onEveryCallCaptureCount.load(std::memory_order_relaxed) == 0) {
        return;
    }
    if (auto* script = ScriptEngine::Instance().GetScript(isolate);
        script != nullptr && script->GetStackCaptureMode() == StackCaptureMode::OnEveryCall) {
        script->RefreshLastStackTrace();
    }
}

void MethodTrampoline(const v8::FunctionCallbackInfo<v8::Value>& args) {
    OnNativeCall(args.GetIsolate());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - function-pointer round-trip through External void*
    const auto fnPtr = reinterpret_cast<v8::FunctionCallback>(args.Data().As<v8::External>()->Value());
    fnPtr(args);
}

void PropertyGetterTrampoline(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
    OnNativeCall(info.GetIsolate());
    const auto* accessors = static_cast<const PropertyAccessors*>(info.Data().As<v8::External>()->Value());
    if (accessors != nullptr && accessors->getter != nullptr) {
        accessors->getter(property, info);
    }
}

void PropertySetterTrampoline(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                              const v8::PropertyCallbackInfo<void>& info) {
    OnNativeCall(info.GetIsolate());
    const auto* accessors = static_cast<const PropertyAccessors*>(info.Data().As<v8::External>()->Value());
    if (accessors != nullptr && accessors->setter != nullptr) {
        accessors->setter(property, value, info);
    }
}

}  // namespace d2bs::js::script
