#include "ScriptingV8.h"

#include <string>

#include "NativeCallHook.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"

namespace d2bs::js::script {

BuilderSlot* CallState::Allocate(v8::Isolate* isolate, v8::Local<v8::Object> object) {
    builders.push_back(BuilderSlot{.owner = this, .isolate = isolate, .object = object});
    return &builders.back();
}

}  // namespace d2bs::js::script

// The contract's own members, defined here because this frontend is what
// resolves them. The linker matches these to the declarations in scripting.lib
// the same way it matches the game:: wrappers to a backend.
namespace d2bs::script {

namespace {

using d2bs::js::script::BuilderSlot;
using d2bs::js::script::CallState;

CallState* Call(void* state) {
    return static_cast<CallState*>(state);
}

const v8::FunctionCallbackInfo<v8::Value>& Info(void* state) {
    return *Call(state)->info;
}

v8::Isolate* Iso(void* state) {
    return Info(state).GetIsolate();
}

// An argument that was not passed reads as undefined rather than out of bounds.
v8::Local<v8::Value> At(void* state, size_t i) {
    const auto& info = Info(state);
    if (i >= static_cast<size_t>(info.Length())) {
        return v8::Undefined(info.GetIsolate());
    }
    return info[static_cast<int32_t>(i)];
}

BuilderSlot* Slot(void* state) {
    return static_cast<BuilderSlot*>(state);
}

v8::Local<v8::Name> Key(v8::Isolate* isolate, std::string_view key) {
    return api::v8_convert::ToV8(isolate, key).As<v8::Name>();
}

// Shared prologue for the field readers: fails unless the argument really is an
// object carrying that key.
bool Field(void* state, size_t i, std::string_view key, v8::Local<v8::Value>& out) {
    auto value = At(state, i);
    if (!value->IsObject()) {
        return false;
    }
    auto* isolate = Iso(state);
    return value.As<v8::Object>()->Get(isolate->GetCurrentContext(), Key(isolate, key)).ToLocal(&out) &&
           !out->IsUndefined();
}

}  // namespace

bool Args::HasPendingException() const {
    return Iso(state_)->HasPendingException() || Iso(state_)->IsExecutionTerminating();
}

size_t Args::Count() const {
    return static_cast<size_t>(Info(state_).Length());
}

bool Args::IsNumber(size_t i) const {
    return At(state_, i)->IsNumber();
}
bool Args::IsString(size_t i) const {
    return At(state_, i)->IsString();
}
bool Args::IsBool(size_t i) const {
    return At(state_, i)->IsBoolean();
}
bool Args::IsObject(size_t i) const {
    return At(state_, i)->IsObject();
}
bool Args::IsFunction(size_t i) const {
    return At(state_, i)->IsFunction();
}
bool Args::IsNullOrUndefined(size_t i) const {
    return At(state_, i)->IsNullOrUndefined();
}

std::optional<int32_t> Args::Int32(size_t i) const {
    auto value = At(state_, i);
    int32_t out = 0;
    if (!value->IsNumber() || !value->Int32Value(Iso(state_)->GetCurrentContext()).To(&out)) {
        return std::nullopt;
    }
    return out;
}

std::optional<uint32_t> Args::Uint32(size_t i) const {
    auto value = At(state_, i);
    uint32_t out = 0;
    if (!value->IsNumber() || !value->Uint32Value(Iso(state_)->GetCurrentContext()).To(&out)) {
        return std::nullopt;
    }
    return out;
}

std::optional<double> Args::Number(size_t i) const {
    auto value = At(state_, i);
    double out = 0;
    if (!value->IsNumber() || !value->NumberValue(Iso(state_)->GetCurrentContext()).To(&out)) {
        return std::nullopt;
    }
    return out;
}

std::optional<bool> Args::Bool(size_t i) const {
    auto value = At(state_, i);
    if (value->IsNullOrUndefined()) {
        return std::nullopt;
    }
    return value->BooleanValue(Iso(state_));
}

std::optional<std::string> Args::String(size_t i) const {
    auto value = At(state_, i);
    if (value->IsNullOrUndefined()) {
        return std::nullopt;
    }
    return api::v8_convert::ToString(Iso(state_), value);
}

// The numeric field readers reject a non-number rather than coercing it. That
// is deliberate and load-bearing: a string silently coerced through Int32Value
// corrupts coordinates, which is why the hand-written extractors check
// IsNumber() first.
std::optional<int32_t> Args::FieldInt32(size_t i, std::string_view key) const {
    v8::Local<v8::Value> value;
    int32_t out = 0;
    if (!Field(state_, i, key, value) || !value->IsNumber() ||
        !value->Int32Value(Iso(state_)->GetCurrentContext()).To(&out)) {
        return std::nullopt;
    }
    return out;
}

std::optional<double> Args::FieldNumber(size_t i, std::string_view key) const {
    v8::Local<v8::Value> value;
    double out = 0;
    if (!Field(state_, i, key, value) || !value->IsNumber() ||
        !value->NumberValue(Iso(state_)->GetCurrentContext()).To(&out)) {
        return std::nullopt;
    }
    return out;
}

std::optional<std::string> Args::FieldString(size_t i, std::string_view key) const {
    v8::Local<v8::Value> value;
    if (!Field(state_, i, key, value)) {
        return std::nullopt;
    }
    return api::v8_convert::ToString(Iso(state_), value);
}

void Args::SetReturnValue(int32_t value) {
    Info(state_).GetReturnValue().Set(value);
}
void Args::SetReturnValue(uint32_t value) {
    Info(state_).GetReturnValue().Set(value);
}
void Args::SetReturnValue(double value) {
    Info(state_).GetReturnValue().Set(value);
}
void Args::SetReturnValue(bool value) {
    Info(state_).GetReturnValue().Set(value);
}

void Args::SetReturnValue(std::string_view value) {
    Info(state_).GetReturnValue().Set(api::v8_convert::ToV8(Iso(state_), value));
}

void Args::SetReturnValue(game::Point value) {
    Info(state_).GetReturnValue().Set(api::v8_convert::ToV8(Iso(state_), value));
}

void Args::SetReturnValue(game::Position value) {
    Info(state_).GetReturnValue().Set(api::v8_convert::ToV8(Iso(state_), value));
}

void Args::SetReturnValue(game::Size value) {
    Info(state_).GetReturnValue().Set(api::v8_convert::ToV8(Iso(state_), value));
}

void Args::SetReturnValueNull() {
    Info(state_).GetReturnValue().SetNull();
}
void Args::SetReturnValueUndefined() {
    Info(state_).GetReturnValue().SetUndefined();
}

ObjectBuilder Args::NewObject() {
    auto* isolate = Iso(state_);
    return ObjectBuilder(Call(state_)->Allocate(isolate, v8::Object::New(isolate)));
}

ArrayBuilder Args::NewArray(size_t length) {
    auto* isolate = Iso(state_);
    return ArrayBuilder(Call(state_)->Allocate(isolate, v8::Array::New(isolate, static_cast<int32_t>(length))));
}

void Args::SetReturnValue(const ObjectBuilder& value) {
    if (auto* slot = Slot(value.state_)) {
        Info(state_).GetReturnValue().Set(slot->object);
    }
}

void Args::SetReturnValue(const ArrayBuilder& value) {
    if (auto* slot = Slot(value.state_)) {
        Info(state_).GetReturnValue().Set(slot->object);
    }
}

void Args::Throw(ErrorKind kind, std::string_view message) {
    auto* isolate = Iso(state_);
    switch (kind) {
        case ErrorKind::TypeError:
            api::v8_error::ThrowTypeError(isolate, message);
            return;
        case ErrorKind::RangeError:
            api::v8_error::ThrowRangeError(isolate, message);
            return;
        case ErrorKind::Error:
            break;
    }
    api::v8_error::ThrowError(isolate, message);
}

namespace {

// A builder whose slot could not be allocated drops its writes. That needs more
// than MAX_BUILDERS of nesting, which this API's shapes do not reach.
template <typename T>
void PutField(void* state, std::string_view key, T value) {
    auto* slot = Slot(state);
    if (slot == nullptr) {
        return;
    }
    slot->object
        ->Set(slot->isolate->GetCurrentContext(), Key(slot->isolate, key), api::v8_convert::ToV8(slot->isolate, value))
        .Check();
}

template <typename T>
void PutIndex(void* state, size_t index, T value) {
    auto* slot = Slot(state);
    if (slot == nullptr) {
        return;
    }
    slot->object
        ->Set(slot->isolate->GetCurrentContext(), static_cast<uint32_t>(index),
              api::v8_convert::ToV8(slot->isolate, value))
        .Check();
}

}  // namespace

ObjectBuilder& ObjectBuilder::Set(std::string_view key, int32_t value) {
    PutField(state_, key, value);
    return *this;
}

ObjectBuilder& ObjectBuilder::Set(std::string_view key, uint32_t value) {
    PutField(state_, key, value);
    return *this;
}

ObjectBuilder& ObjectBuilder::Set(std::string_view key, double value) {
    PutField(state_, key, value);
    return *this;
}

ObjectBuilder& ObjectBuilder::Set(std::string_view key, bool value) {
    PutField(state_, key, value);
    return *this;
}

ObjectBuilder& ObjectBuilder::Set(std::string_view key, std::string_view value) {
    PutField(state_, key, value);
    return *this;
}

ObjectBuilder& ObjectBuilder::SetNull(std::string_view key) {
    auto* slot = Slot(state_);
    if (slot == nullptr) {
        return *this;
    }
    slot->object->Set(slot->isolate->GetCurrentContext(), Key(slot->isolate, key), v8::Null(slot->isolate)).Check();
    return *this;
}

ArrayBuilder& ArrayBuilder::Set(size_t index, int32_t value) {
    PutIndex(state_, index, value);
    return *this;
}

ArrayBuilder& ArrayBuilder::Set(size_t index, uint32_t value) {
    PutIndex(state_, index, value);
    return *this;
}

ArrayBuilder& ArrayBuilder::Set(size_t index, double value) {
    PutIndex(state_, index, value);
    return *this;
}

ArrayBuilder& ArrayBuilder::Set(size_t index, bool value) {
    PutIndex(state_, index, value);
    return *this;
}

ArrayBuilder& ArrayBuilder::Set(size_t index, std::string_view value) {
    PutIndex(state_, index, value);
    return *this;
}

namespace {

// Writing one builder into another: both sides already live in the call's slot
// pool, so this only links two existing objects.
void PutBuilderField(void* state, std::string_view key, void* value) {
    auto* slot = Slot(state);
    auto* inner = Slot(value);
    if (slot == nullptr || inner == nullptr) {
        return;
    }
    slot->object->Set(slot->isolate->GetCurrentContext(), Key(slot->isolate, key), inner->object).Check();
}

void PutBuilderIndex(void* state, size_t index, void* value) {
    auto* slot = Slot(state);
    auto* inner = Slot(value);
    if (slot == nullptr || inner == nullptr) {
        return;
    }
    slot->object->Set(slot->isolate->GetCurrentContext(), static_cast<uint32_t>(index), inner->object).Check();
}

}  // namespace

ObjectBuilder& ObjectBuilder::Set(std::string_view key, const ObjectBuilder& value) {
    PutBuilderField(state_, key, value.state_);
    return *this;
}

ObjectBuilder& ObjectBuilder::Set(std::string_view key, const ArrayBuilder& value) {
    PutBuilderField(state_, key, value.state_);
    return *this;
}

ArrayBuilder& ArrayBuilder::Set(size_t index, const ObjectBuilder& value) {
    PutBuilderIndex(state_, index, value.state_);
    return *this;
}

ArrayBuilder& ArrayBuilder::Set(size_t index, const ArrayBuilder& value) {
    PutBuilderIndex(state_, index, value.state_);
    return *this;
}

void Registry::Global(std::string_view name, Native fn) {
    auto* state = static_cast<js::script::RegistryState*>(state_);
    auto* binding = js::script::InternContractFunction(std::string(name), fn);
    auto data = v8::External::New(state->isolate, binding, v8::kExternalPointerTypeTagDefault);
    state->global->Set(Key(state->isolate, name),
                       v8::FunctionTemplate::New(state->isolate, js::script::MethodTrampoline, data));
}

void Registry::Constant(std::string_view name, double value) {
    auto* state = static_cast<js::script::RegistryState*>(state_);
    state->global->Set(Key(state->isolate, name), api::v8_convert::ToV8(state->isolate, value),
                       static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
}

void Registry::Constant(std::string_view name, std::string_view value) {
    auto* state = static_cast<js::script::RegistryState*>(state_);
    state->global->Set(Key(state->isolate, name), api::v8_convert::ToV8(state->isolate, value),
                       static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
}

}  // namespace d2bs::script
