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

    // Stringify whatever is there, the way the language's own String() does:
    // null becomes "null", a number becomes its digits, an object runs its
    // toString. Distinct from String(), which reports "not a string" - several
    // bindings deliberately hash or print whatever they are handed, and taking
    // String() for those would change what they do with null. Returns nullopt
    // only when converting threw, which a user-defined toString can do.
    [[nodiscard]] std::optional<std::string> ToString(size_t i) const;

    // Read one field off an object argument. The shapes the API accepts are
    // fixed and documented, so a typed field read covers them without exposing
    // the object itself.
    [[nodiscard]] std::optional<int32_t> FieldInt32(size_t i, std::string_view key) const;
    [[nodiscard]] std::optional<double> FieldNumber(size_t i, std::string_view key) const;
    [[nodiscard]] std::optional<std::string> FieldString(size_t i, std::string_view key) const;

    // True when the engine already has an exception waiting. A read that
    // returns nullopt means either "not that type" or "converting it threw",
    // and only this tells them apart.
    //
    // On V8 the reads gate on the type first, so almost nothing user-defined
    // runs and this is almost always false. On an engine where converting a
    // value can call a script's own toString, it is the difference between
    // propagating the script's exception and silently replacing it with ours.
    // A binding that sees it set should return false rather than throw over it.
    [[nodiscard]] bool HasPendingException() const;

    // Make a value. Valid only until this call returns: both build into the
    // engine's current frame and root nothing of their own. Creating a value
    // and returning it are separate steps, so a value can be built, nested
    // inside another, and returned - or not returned at all.
    [[nodiscard]] ObjectBuilder NewObject();
    [[nodiscard]] ArrayBuilder NewArray(size_t length);

    void SetReturnValue(int32_t value);
    void SetReturnValue(uint32_t value);
    void SetReturnValue(double value);
    void SetReturnValue(bool value);
    void SetReturnValue(std::string_view value);
    void SetReturnValue(game::Point value);
    void SetReturnValue(game::Position value);
    void SetReturnValue(game::Size value);
    void SetReturnValue(const ObjectBuilder& value);
    void SetReturnValue(const ArrayBuilder& value);
    void SetReturnValueNull();
    void SetReturnValueUndefined();

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
    ObjectBuilder& Set(std::string_view key, const ObjectBuilder& value);
    ObjectBuilder& Set(std::string_view key, const ArrayBuilder& value);
    ObjectBuilder& SetNull(std::string_view key);

   private:
    friend class Args;
    friend class ArrayBuilder;

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
    ArrayBuilder& Set(size_t index, const ObjectBuilder& value);
    ArrayBuilder& Set(size_t index, const ArrayBuilder& value);

   private:
    friend class Args;
    friend class ObjectBuilder;

    void* state_;
};

// What a binding is. Engine-free by construction: the only thing it can touch
// is the Args it was handed.
//
// Returns false to mean "stop" - either an exception is pending, or the script
// is being terminated. V8's own callback returns void because it signals
// termination out of band through TerminateExecution(), but an engine that has
// no such channel says it here instead, by returning false with nothing
// pending. Modelling V8's void return would throw that answer away, so a
// binding says it explicitly and the frontend maps it to whatever its engine
// expects. Returning true after Throw() is a bug; return false.
using Native = bool (*)(Args&);

}  // namespace d2bs::script
