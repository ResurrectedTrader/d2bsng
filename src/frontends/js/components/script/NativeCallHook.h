#pragma once

#include <atomic>

#include <v8.h>

// Trampolines that sit between V8 and our user-supplied native callbacks
// (Method / StaticMethod / Property / global function Register). Each
// trampoline calls OnNativeCall first so console stack capture, when
// enabled on the calling Script, gets the JS stack at the entry of every
// native callback. The user's original callback pointer is stashed in V8's
// `data` slot (a v8::External around the function pointer).
//
// Property registrations stash both the getter and setter in a
// PropertyAccessors struct (since SetNativeDataProperty exposes only one
// `data` slot), interned by InternAccessors so the struct is shared by
// every isolate that registers the same pair.

namespace d2bs::js::script {

// Process-wide count of scripts currently in StackCaptureMode::OnEveryCall.
// While zero (the universal case - the console Stacktraces panel isn't pinned to
// a script in per-call mode), OnNativeCall short-circuits on this single relaxed
// load, keeping the JS->native trampolines off the per-call script lookup.
// Maintained by Script::SetStackCaptureMode (and cleared in ~Script).
inline std::atomic onEveryCallCaptureCount{0};

// Called from each trampoline at the start of a V8 callback. Looks up the
// Script owning `isolate`; if that Script has stack capture enabled,
// refreshes its last-known stack trace. Safe to call cross-thread, no-op
// for non-Script isolates.
void OnNativeCall(v8::Isolate* isolate);

void MethodTrampoline(const v8::FunctionCallbackInfo<v8::Value>& args);

// Paired getter/setter for one property. Obtained from InternAccessors by the
// V8Class helpers and stashed in a v8::External.
struct PropertyAccessors {
    v8::AccessorNameGetterCallback getter = nullptr;
    v8::AccessorNameSetterCallback setter = nullptr;
};

// Shared PropertyAccessors for one (getter, setter) pair. The template setup that registers
// these re-runs for every isolate, and scripts get a fresh isolate each restart, so
// allocating per registration would grow for the life of the process. The set of distinct
// pairs is fixed by the binding surface, so interning bounds it for real. Entries are never
// destroyed - v8::Externals point into them and can outlive static destruction - and the
// returned pointer is stable.
PropertyAccessors* InternAccessors(v8::AccessorNameGetterCallback getter, v8::AccessorNameSetterCallback setter);

void PropertyGetterTrampoline(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info);
void PropertySetterTrampoline(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                              const v8::PropertyCallbackInfo<void>& info);

}  // namespace d2bs::js::script
