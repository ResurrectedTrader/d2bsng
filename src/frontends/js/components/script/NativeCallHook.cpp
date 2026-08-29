#include "components/script/NativeCallHook.h"

#include <deque>
#include <mutex>

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

PropertyAccessors* InternAccessors(v8::AccessorNameGetterCallback getter, v8::AccessorNameSetterCallback setter) {
    // deque, not vector: callers keep the returned pointer for the isolate's lifetime, and
    // a vector would invalidate it on growth. Linear scan is fine - this runs only during
    // per-isolate template setup, over a table the size of the binding surface.
    // Intentionally never destroyed: the v8::Externals handed to V8 point into these, and
    // a static destructor would run while those are still reachable.
    static auto& table = *new std::deque<PropertyAccessors>;
    static auto& mutex = *new std::mutex;

    std::scoped_lock lock(mutex);
    for (auto& entry : table) {
        if (entry.getter == getter && entry.setter == setter) {
            return &entry;
        }
    }
    return &table.emplace_back(PropertyAccessors{.getter = getter, .setter = setter});
}

void PropertyGetterTrampoline(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
    OnNativeCall(info.GetIsolate());
    const auto* accessors = static_cast<const PropertyAccessors*>(info.Data().As<v8::External>()->Value());
    if (accessors == nullptr || accessors->getter == nullptr) {
        return;
    }
    accessors->getter(property, info);
}

void PropertySetterTrampoline(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                              const v8::PropertyCallbackInfo<void>& info) {
    OnNativeCall(info.GetIsolate());
    const auto* accessors = static_cast<const PropertyAccessors*>(info.Data().As<v8::External>()->Value());
    if (accessors == nullptr || accessors->setter == nullptr) {
        return;
    }
    accessors->setter(property, value, info);
}

}  // namespace d2bs::js::script
