#include "components/script/NativeCallHook.h"

#include <bit>
#include <deque>
#include <format>
#include <mutex>
#include <string>

#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"
#include "game/GameThread.h"
#include "host/Host.h"

namespace d2bs::runtime::script {

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

// Numbers, booleans and strings by value, an object only by kind (its identity changes every call). Reads the return
// value only when asked, so a host that ignores results costs no handle.
class V8NativeResult final : public host::NativeResult {
   public:
    V8NativeResult(v8::Isolate* isolate, v8::ReturnValue<v8::Value> returnValue)
        : isolate_(isolate), returnValue_(returnValue) {}

    [[nodiscard]] uint64_t Hash() const override {
        const auto value = returnValue_.Get();
        if (value.IsEmpty() || value->IsUndefined()) {
            return 2;
        }
        if (value->IsNull()) {
            return 3;
        }
        if (value->IsNumber()) {
            return std::bit_cast<uint64_t>(value.As<v8::Number>()->Value());
        }
        if (value->IsBoolean()) {
            return value->BooleanValue(isolate_) ? 5 : 4;
        }
        if (value->IsString()) {
            return static_cast<uint64_t>(value.As<v8::String>()->GetIdentityHash()) | (uint64_t{1} << 40);
        }
        return 1;
    }

    [[nodiscard]] std::string Text() const override {
        constexpr size_t MAX_TEXT = 60;
        const auto value = returnValue_.Get();
        if (value.IsEmpty() || value->IsUndefined()) {
            return "undefined";
        }
        if (value->IsNull()) {
            return "null";
        }
        if (value->IsNumber()) {
            return std::format("{}", value.As<v8::Number>()->Value());
        }
        if (value->IsBoolean()) {
            return value->BooleanValue(isolate_) ? "true" : "false";
        }
        if (value->IsString()) {
            const v8::String::Utf8Value utf8(isolate_, value);
            return std::string{*utf8 != nullptr ? *utf8 : ""}.substr(0, MAX_TEXT);
        }
        return "object";
    }

   private:
    v8::Isolate* isolate_;
    v8::ReturnValue<v8::Value> returnValue_;
};

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
                                   v8::AccessorNameSetterCallbackV2 setter) {
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

    const auto take = [&samples](std::string_view name, utils::profiling::NativeCall kind,
                                 const utils::profiling::NativeStats& stats) {
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
        take(entry.name, utils::profiling::NativeCall::Function, entry.stats);
    }
    for (const auto& entry : tables.accessors) {
        take(entry.name, utils::profiling::NativeCall::Getter, entry.getStats);
        take(entry.name, utils::profiling::NativeCall::Setter, entry.setStats);
    }
    return samples;
}

void ResetNativeBindings() {
    auto& tables = GetTables();
    const std::scoped_lock lock(tables.mutex);
    const auto clear = [](utils::profiling::NativeStats& stats) {
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
    auto* binding =
        static_cast<NativeBinding*>(args.DataV2().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (binding == nullptr || binding->callback == nullptr) {
        return;
    }
    host::OnNativeCall(binding->name);
    const utils::profiling::ScopedNativeCall timing(utils::profiling::NativeCall::Function, binding->stats);
    try {
        binding->callback(args);
        host::ObserveNativeResult(V8NativeResult{args.GetIsolate(), args.GetReturnValue()});
    } catch (const game::GameThread::Cancelled&) {
        args.GetIsolate()->TerminateExecution();
    }
}

void PropertyGetterTrampoline(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
    OnNativeCall(info.GetIsolate());
    auto* accessors =
        static_cast<PropertyAccessors*>(info.DataV2().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (accessors == nullptr || accessors->getter == nullptr) {
        return;
    }
    host::OnNativeCall(accessors->name);
    const utils::profiling::ScopedNativeCall timing(utils::profiling::NativeCall::Getter, accessors->getStats);
    try {
        accessors->getter(property, info);
        host::ObserveNativeResult(V8NativeResult{info.GetIsolate(), info.GetReturnValue()});
    } catch (const game::GameThread::Cancelled&) {
        info.GetIsolate()->TerminateExecution();
    }
}

void PropertySetterTrampoline(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                              const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    OnNativeCall(info.GetIsolate());
    auto* accessors =
        static_cast<PropertyAccessors*>(info.DataV2().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (accessors == nullptr || accessors->setter == nullptr) {
        return;
    }
    host::OnNativeCall(accessors->name);
    const utils::profiling::ScopedNativeCall timing(utils::profiling::NativeCall::Setter, accessors->setStats);
    try {
        accessors->setter(property, value, info);
    } catch (const game::GameThread::Cancelled&) {
        info.GetIsolate()->TerminateExecution();
    }
}

}  // namespace d2bs::runtime::script
