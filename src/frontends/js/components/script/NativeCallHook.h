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
// Property registrations stash both the getter and setter in a heap-
// allocated PropertyAccessors struct (since SetNativeDataProperty exposes
// only one `data` slot). The struct's lifetime is the isolate's - leaked
// at template build time on purpose.

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

// Paired getter/setter for one property. Heap-allocated by V8Class
// helpers and stashed in v8::External; we never free it (per-isolate
// template setup, bounded in count).
struct PropertyAccessors {
    v8::AccessorNameGetterCallback getter = nullptr;
    v8::AccessorNameSetterCallback setter = nullptr;
};

void PropertyGetterTrampoline(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info);
void PropertySetterTrampoline(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                              const v8::PropertyCallbackInfo<void>& info);

}  // namespace d2bs::js::script
