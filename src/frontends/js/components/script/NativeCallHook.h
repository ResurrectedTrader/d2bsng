#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

#include <v8.h>

#include "utils/Profiling.h"

// Trampolines between V8 and the native callbacks registered through V8Class / V8Function. Each
// one runs OnNativeCall first (console stack capture), then times the callback for the Profiling
// panel. V8's single `data` slot carries a pointer to the interned NativeBinding / PropertyAccessors
// entry, which holds the callback(s), the name, and the cumulative stats.

namespace d2bs::js::script {

// Process-wide count of scripts in StackCaptureMode::OnEveryCall. While zero, OnNativeCall
// short-circuits on this one relaxed load. Maintained by Script::SetStackCaptureMode.
inline std::atomic onEveryCallCaptureCount{0};

// Refreshes the owning Script's last-known stack trace when it has per-call capture enabled.
void OnNativeCall(v8::Isolate* isolate);

struct NativeBinding {
    NativeBinding(std::string name, v8::FunctionCallback callback) : name(std::move(name)), callback(callback) {}

    std::string name;
    v8::FunctionCallback callback;
    profiling::NativeStats stats;
};

struct PropertyAccessors {
    PropertyAccessors(std::string name, v8::AccessorNameGetterCallback getter, v8::AccessorNameSetterCallback setter)
        : name(std::move(name)), getter(getter), setter(setter) {}

    std::string name;
    v8::AccessorNameGetterCallback getter;
    v8::AccessorNameSetterCallback setter;
    profiling::NativeStats getStats;
    profiling::NativeStats setStats;
};

// Interned per (name, callbacks): template setup re-runs for every isolate, and the stats have to
// survive a script restart. Entries are never destroyed - v8::Externals point into them - and the
// returned pointer is stable. Keyed on the name too, so two properties sharing one generic accessor
// pair keep separate stats.
NativeBinding* InternFunction(const std::string& name, v8::FunctionCallback callback);
PropertyAccessors* InternAccessors(const std::string& name, v8::AccessorNameGetterCallback getter,
                                   v8::AccessorNameSetterCallback setter);

void MethodTrampoline(const v8::FunctionCallbackInfo<v8::Value>& args);
void PropertyGetterTrampoline(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info);
void PropertySetterTrampoline(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                              const v8::PropertyCallbackInfo<void>& info);

#ifdef D2BS_PROFILING

struct NativeBindingSample {
    std::string_view name;  // into the immortal table
    profiling::NativeCall kind = profiling::NativeCall::Function;
    uint64_t cycles = 0;  // CPU; blocked time is reported separately
    uint64_t calls = 0;
    uint64_t blockedCycles = 0;
};

// Every interned binding that has been called at least once, unordered.
[[nodiscard]] std::vector<NativeBindingSample> SnapshotNativeBindings();

void ResetNativeBindings();

#endif  // D2BS_PROFILING

}  // namespace d2bs::js::script
