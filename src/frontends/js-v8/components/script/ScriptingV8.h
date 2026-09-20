#pragma once

#include <v8.h>

#include <array>
#include <cstddef>

#include "scripting/Args.h"
#include "scripting/Registry.h"

namespace d2bs::js::script {

struct CallState;

// One in-flight object or array being built for a return value. Carries its
// owning call so a nested builder can allocate another slot without the
// contract ever seeing either.
struct BuilderSlot {
    CallState* owner = nullptr;
    v8::Isolate* isolate = nullptr;
    v8::Local<v8::Object> object;  // an Array is an Object
};

// What a d2bs::script::Args points at for the duration of one call. Lives on the
// trampoline's stack: the arguments and the return slot belong to V8's frame,
// and the builder slots hold Locals that die with the surrounding HandleScope.
// Nothing here outlives the call, which is the property that lets the same
// contract run on an engine with exact rooting.
struct CallState {
    // Deep enough for the shapes this API returns (arrays of flat objects); an
    // overflow drops writes rather than corrupting anything.
    static constexpr size_t MAX_BUILDERS = 16;

    explicit CallState(const v8::FunctionCallbackInfo<v8::Value>& info) : info(&info) {}

    const v8::FunctionCallbackInfo<v8::Value>* info;
    std::array<BuilderSlot, MAX_BUILDERS> builders;
    size_t used = 0;

    BuilderSlot* Allocate(v8::Isolate* isolate, v8::Local<v8::Object> object);
};

// Registration target for one isolate's global template.
struct RegistryState {
    v8::Isolate* isolate;
    v8::Local<v8::ObjectTemplate> global;
};

}  // namespace d2bs::js::script
