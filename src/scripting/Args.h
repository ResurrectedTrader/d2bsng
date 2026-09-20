#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "game/Types.h"

namespace d2bs::script {

// Which JS error constructor a binding wants. Engines agree on these three; a
// frontend maps them to its own.
enum class ErrorKind : uint8_t { Error, TypeError, RangeError };

class ObjectBuilder;
class ArrayBuilder;

// One call into a binding: its arguments, its return slot, and its error slot.
//
// Every accessor takes and returns C++ types - never an engine value. That is
// the whole point. V8 hands out `Local<Value>` freely because a Local is a
// HandleScope slot, but SpiderMonkey requires `JS::Rooted` in strict stack
// order and conversions that write through a `MutableHandleValue`. A portable
// value handle would have to heap-root on SpiderMonkey and would still be easy
// to misuse. Keeping every value inside the engine's own call frame satisfies
// both engines structurally, rather than by asking binding authors to remember
// a discipline only one engine enforces.
//
// The frontend defines every member; `state_` points at whatever that frontend
// needs (for V8, the FunctionCallbackInfo). LTO inlines these across the static
// libs in the shipped DLL, exactly as it already does for the game:: wrappers.
class Args {
   public:
    explicit Args(void* state) : state_(state) {}

    [[nodiscard]] size_t Count() const;

    [[nodiscard]] bool IsNumber(size_t i) const;
    [[nodiscard]] bool IsString(size_t i) const;
    [[nodiscard]] bool IsBool(size_t i) const;
    [[nodiscard]] bool IsObject(size_t i) const;
    [[nodiscard]] bool IsFunction(size_t i) const;
    [[nodiscard]] bool IsNullOrUndefined(size_t i) const;

    // Each returns nullopt when the argument is absent or not convertible,
    // so a binding validates and reads in one step.
    [[nodiscard]] std::optional<int32_t> Int32(size_t i) const;
    [[nodiscard]] std::optional<uint32_t> Uint32(size_t i) const;
    [[nodiscard]] std::optional<double> Number(size_t i) const;
    [[nodiscard]] std::optional<bool> Bool(size_t i) const;
    [[nodiscard]] std::optional<std::string> String(size_t i) const;

    // Read one field off an object argument. The shapes the API accepts are
    // fixed and documented, so a typed field read covers them without exposing
    // the object itself.
    [[nodiscard]] std::optional<int32_t> FieldInt32(size_t i, std::string_view key) const;
    [[nodiscard]] std::optional<double> FieldNumber(size_t i, std::string_view key) const;
    [[nodiscard]] std::optional<std::string> FieldString(size_t i, std::string_view key) const;

    void Return(int32_t value);
    void Return(uint32_t value);
    void Return(double value);
    void Return(bool value);
    void Return(std::string_view value);
    void Return(game::Point value);
    void Return(game::Position value);
    void Return(game::Size value);
    void ReturnNull();
    void ReturnUndefined();

    // Both builders are valid only until this call returns - they build into
    // the engine's current frame and root nothing of their own.
    [[nodiscard]] ObjectBuilder ReturnObject();
    [[nodiscard]] ArrayBuilder ReturnArray(size_t length);

    void Throw(ErrorKind kind, std::string_view message);

   private:
    void* state_;
};

class ObjectBuilder {
   public:
    explicit ObjectBuilder(void* state) : state_(state) {}

    ObjectBuilder& Set(std::string_view key, int32_t value);
    ObjectBuilder& Set(std::string_view key, uint32_t value);
    ObjectBuilder& Set(std::string_view key, double value);
    ObjectBuilder& Set(std::string_view key, bool value);
    ObjectBuilder& Set(std::string_view key, std::string_view value);
    ObjectBuilder& SetNull(std::string_view key);

   private:
    void* state_;
};

class ArrayBuilder {
   public:
    explicit ArrayBuilder(void* state) : state_(state) {}

    ArrayBuilder& Set(size_t index, int32_t value);
    ArrayBuilder& Set(size_t index, uint32_t value);
    ArrayBuilder& Set(size_t index, double value);
    ArrayBuilder& Set(size_t index, bool value);
    ArrayBuilder& Set(size_t index, std::string_view value);
    [[nodiscard]] ObjectBuilder SetObject(size_t index);

   private:
    void* state_;
};

// What a binding is. Engine-free by construction: the only thing it can touch
// is the Args it was handed.
using Native = void (*)(Args&);

}  // namespace d2bs::script
