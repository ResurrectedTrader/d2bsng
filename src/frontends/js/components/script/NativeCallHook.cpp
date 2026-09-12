#include "components/script/NativeCallHook.h"

#include <deque>
#include <mutex>

#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"

namespace d2bs::js::script {

void OnNativeCall(v8::Isolate* isolate) {
    if (onEveryCallCaptureCount.load(std::memory_order_relaxed) == 0) {
        return;
    }
    if (auto* script = ScriptEngine::Instance().GetScript(isolate);
        script != nullptr && script->GetStackCaptureMode() == StackCaptureMode::OnEveryCall) {
        script->RefreshLastStackTrace();
    }
}

namespace {

// deque: callers keep pointers into these for the isolate's lifetime, and the entries hold atomics
// so cannot move. Never destroyed - v8::Externals point into them past static destruction. The
// mutex covers registration and the panel's snapshot only; the trampolines never take it.
struct Tables {
    std::mutex mutex;
    std::deque<NativeBinding> functions;
    std::deque<PropertyAccessors> accessors;
};

Tables& GetTables() {
    static auto& tables = *new Tables;
    return tables;
}

// Linear scan under the lock: this runs during per-isolate template setup only, over a table the
// size of the binding surface.
template <typename T, typename Match, typename... Args>
T* Intern(std::deque<T>& table, Match match, Args&&... args) {
    const std::scoped_lock lock(GetTables().mutex);
    for (auto& entry : table) {
        if (match(entry)) {
            return &entry;
        }
    }
    return &table.emplace_back(std::forward<Args>(args)...);
}

}  // namespace

NativeBinding* InternFunction(const std::string& name, v8::FunctionCallback callback) {
    return Intern(
        GetTables().functions, [&](const NativeBinding& e) { return e.callback == callback && e.name == name; }, name,
        callback);
}

PropertyAccessors* InternAccessors(const std::string& name, v8::AccessorNameGetterCallback getter,
                                   v8::AccessorNameSetterCallback setter) {
    return Intern(
        GetTables().accessors,
        [&](const PropertyAccessors& e) { return e.getter == getter && e.setter == setter && e.name == name; }, name,
        getter, setter);
}

#ifdef D2BS_PROFILING

std::vector<NativeBindingSample> SnapshotNativeBindings() {
    auto& tables = GetTables();
    std::vector<NativeBindingSample> samples;
    const std::scoped_lock lock(tables.mutex);
    samples.reserve(tables.functions.size() + tables.accessors.size());

    const auto take = [&samples](std::string_view name, profiling::NativeCall kind,
                                 const profiling::NativeStats& stats) {
        const uint64_t calls = stats.calls.load(std::memory_order_relaxed);
        if (calls == 0) {
            return;
        }
        samples.push_back({.name = name,
                           .kind = kind,
                           .cycles = stats.cycles.load(std::memory_order_relaxed),
                           .calls = calls,
                           .blockedCycles = stats.blockedCycles.load(std::memory_order_relaxed)});
    };

    for (const auto& entry : tables.functions) {
        take(entry.name, profiling::NativeCall::Function, entry.stats);
    }
    for (const auto& entry : tables.accessors) {
        take(entry.name, profiling::NativeCall::Getter, entry.getStats);
        take(entry.name, profiling::NativeCall::Setter, entry.setStats);
    }
    return samples;
}

void ResetNativeBindings() {
    auto& tables = GetTables();
    const std::scoped_lock lock(tables.mutex);
    const auto clear = [](profiling::NativeStats& stats) {
        stats.cycles.store(0, std::memory_order_relaxed);
        stats.calls.store(0, std::memory_order_relaxed);
        stats.blockedCycles.store(0, std::memory_order_relaxed);
    };
    for (auto& entry : tables.functions) {
        clear(entry.stats);
    }
    for (auto& entry : tables.accessors) {
        clear(entry.getStats);
        clear(entry.setStats);
    }
}

#endif  // D2BS_PROFILING

// Timing is scoped below OnNativeCall in each trampoline: stack capture is expensive when armed
// and is the console's cost, not the binding's.
void MethodTrampoline(const v8::FunctionCallbackInfo<v8::Value>& args) {
    OnNativeCall(args.GetIsolate());
    auto* binding = static_cast<NativeBinding*>(args.Data().As<v8::External>()->Value());
    if (binding == nullptr || binding->callback == nullptr) {
        return;
    }
    const profiling::ScopedNativeCall timing(profiling::NativeCall::Function, binding->stats);
    binding->callback(args);
}

void PropertyGetterTrampoline(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
    OnNativeCall(info.GetIsolate());
    auto* accessors = static_cast<PropertyAccessors*>(info.Data().As<v8::External>()->Value());
    if (accessors == nullptr || accessors->getter == nullptr) {
        return;
    }
    const profiling::ScopedNativeCall timing(profiling::NativeCall::Getter, accessors->getStats);
    accessors->getter(property, info);
}

void PropertySetterTrampoline(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                              const v8::PropertyCallbackInfo<void>& info) {
    OnNativeCall(info.GetIsolate());
    auto* accessors = static_cast<PropertyAccessors*>(info.Data().As<v8::External>()->Value());
    if (accessors == nullptr || accessors->setter == nullptr) {
        return;
    }
    const profiling::ScopedNativeCall timing(profiling::NativeCall::Setter, accessors->setStats);
    accessors->setter(property, value, info);
}

}  // namespace d2bs::js::script
