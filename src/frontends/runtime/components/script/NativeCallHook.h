#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

#include "unibind/unibind.h"
#include "utils/Profiling.h"

// Trampolines between the engine and the native callbacks registered through ClassBase / the
// global-function helpers. Each one runs OnNativeCall first (console stack capture), then times the
// callback for the Profiling panel. The callback's data carries the interned NativeBinding /
// PropertyAccessors entry, which holds the callback(s), the name, and the cumulative stats.

namespace d2bs::runtime::script {

// Process-wide count of scripts in StackCaptureMode::OnEveryCall. While zero, OnNativeCall
// short-circuits on this one relaxed load. Maintained by Script::SetStackCaptureMode.
inline std::atomic onEveryCallCaptureCount{0};

// Refreshes the owning Script's last-known stack trace when it has per-call capture enabled.
void OnNativeCall(ub::Isolate& isolate);

struct NativeBinding {
    NativeBinding(std::string name, ub::FunctionCallback callback) : name(std::move(name)), callback(callback) {}

    std::string name;
    ub::FunctionCallback callback;
    profiling::NativeStats stats;
};

struct PropertyAccessors {
    PropertyAccessors(std::string name, ub::AccessorGetterCallback getter, ub::AccessorSetterCallback setter)
        : name(std::move(name)), getter(getter), setter(setter) {}

    std::string name;
    ub::AccessorGetterCallback getter;
    ub::AccessorSetterCallback setter;
    profiling::NativeStats getStats;
    profiling::NativeStats setStats;
};

// Interned per (name, callbacks): class setup re-runs for every isolate, and the stats have to
// survive a script restart. Entries are never destroyed - every isolate's callback data points
// into them - and the returned pointer is stable. Keyed on the name too, so two properties sharing
// one generic accessor pair keep separate stats.
NativeBinding* InternFunction(const std::string& name, ub::FunctionCallback callback);
PropertyAccessors* InternAccessors(const std::string& name, ub::AccessorGetterCallback getter,
                                   ub::AccessorSetterCallback setter);

void MethodTrampoline(const ub::CallbackInfo& info);
void PropertyGetterTrampoline(const ub::Local<ub::Name>& property, const ub::PropertyCallbackInfo& info);
void PropertySetterTrampoline(const ub::Local<ub::Name>& property, const ub::Local<ub::Value>& value,
                              const ub::PropertyCallbackInfo& info);

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

}  // namespace d2bs::runtime::script
