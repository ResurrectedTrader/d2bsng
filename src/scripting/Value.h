#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "Persistent.h"
#include "State.h"

namespace d2bs::script {

// Whether a constructor may be called without `new`.
enum class Construct : uint8_t { RequireNew, AllowPlainCall };

class Value;

// Raw bytes a script receives as a byte array.
struct Bytes {
    std::span<const uint8_t> data;
};

// A value one script serialised for another. Both ends are the same frontend,
// so the blob never crosses engines: it travels opaquely and only the frontend
// that wrote it reads it back.
//
// An empty blob is undefined, which is what a sender that could not clone a
// value stores rather than dropping it and shifting every argument after it.
struct Serialized {
    std::span<const uint8_t> data;
};

// One argument a call hands to a script, named rather than built. Numbers keep
// the width they were written with, because that is what the engine turns into
// an integer or a double.
//
// A Value is the exception: something already in the engine's call frame - an
// argument the binding was handed, a field it read, an object it built. Only a
// binding can name one, and only for the length of its own call, which is
// exactly the reach of Value::Call - an event is assembled outside any call
// frame and has nothing that could make one.
using Argument = std::variant<bool, int32_t, uint32_t, double, std::string_view, Bytes, Serialized, Value>;

// One value, whatever it is and wherever it came from: an argument of the call,
// the value a setter is storing, a field or element read out of another value,
// an object or array this call built, or a wrapper it made around a native.
//
// There is one of these and not several because the engines have one. A value
// read and a value made are the same currency, so a binding can hand an
// argument straight back, forward one to a callback, or move one between
// objects, without the contract needing a conversion for each pair.
//
// It names a slot the engine's own call frame is already holding, by index
// rather than by address: it roots nothing of its own, costs two words to copy,
// and dies with the call. Addressing by index is also what an engine that roots
// exactly can implement - one root holding N entries, grown as the call needs
// them - where a pointer into that collection could not survive the growth.
//
// What an index means is the frontend's own business. The only one a binding
// ever spells is an argument's (args[i]); every other Value it holds came back
// from a call that took the slot for it.
class Value {
   public:
    Value(CallState* state, size_t index) : state_(state), index_(index) {}

    [[nodiscard]] bool IsNumber() const;
    [[nodiscard]] bool IsInt32() const;
    [[nodiscard]] bool IsUint32() const;
    [[nodiscard]] bool IsString() const;
    [[nodiscard]] bool IsBoolean() const;
    [[nodiscard]] bool IsObject() const;
    [[nodiscard]] bool IsArray() const;
    [[nodiscard]] bool IsFunction() const;
    [[nodiscard]] bool IsNullOrUndefined() const;
    // Undefined but not null - the two are different answers, and a binding
    // that treats a missing argument differently from an explicit null needs
    // to tell them apart.
    [[nodiscard]] bool IsUndefined() const;
    [[nodiscard]] bool IsInstance(ClassKey of) const { return Instance(of) != nullptr; }

    // Each reports nothing when the argument is not that type, so a binding
    // validates and reads in one step. Nothing is also what a failed conversion
    // reports.
    [[nodiscard]] std::optional<bool> Bool() const;
    [[nodiscard]] std::optional<std::string> String() const;

    // Stringify whatever is there, the way the language's own String() does:
    // null becomes "null", a number its digits, an object runs its toString.
    // Distinct from String(), which reports "not a string" - several bindings
    // deliberately print or hash whatever they are handed, and taking String()
    // for those would change what they do with null.
    [[nodiscard]] std::optional<std::string> ToString() const;

    // Coercing counterparts of the reads above, for the positions where the API
    // has always accepted what the language would accept - getPath(area, "10",
    // "20") works, and continuing to work is the contract. Nothing only when
    // the conversion itself threw.
    [[nodiscard]] std::optional<int32_t> ToInt32() const;
    [[nodiscard]] std::optional<uint32_t> ToUint32() const;
    [[nodiscard]] std::optional<double> ToNumber() const;

    // Bytes behind an array buffer or typed array. Valid only for this call,
    // and an engine that cannot expose its buffer without copying may copy.
    [[nodiscard]] std::optional<std::span<const uint8_t>> Bytes() const;

    // Hold this value past the end of the call: a callback a binding will
    // invoke later, an object it will hand back a second time.
    //
    // It holds whatever is there. Whether a value that is not a function is
    // worth holding is the binding's question - the ones that only want a
    // callback ask IsFunction() first, which is also where refusing anything
    // else is legible.
    //
    // Object identity is observable - a script comparing two reads, or hanging
    // a property of its own on what it got, sees the difference between the
    // same object twice and two equal ones - so a binding that caches a result
    // caches the object rather than rebuilding it.
    [[nodiscard]] Persistent Persist() const;

    // Structured-clone it to opaque bytes, for a value crossing between
    // scripts. Only the engine that wrote one reads it back, so nothing
    // outside the frontend looks inside.
    //
    // Nothing when the engine cannot clone the value - a function, a symbol.
    // What that means is the caller's to decide: a broadcast substitutes the
    // empty blob so the arguments after it keep their positions, while load()
    // refuses to start a script that would silently be handed undefined.
    [[nodiscard]] std::optional<std::vector<uint8_t>> Serialize() const;

    // The native behind it, if it is an instance of that class.
    [[nodiscard]] void* Instance(ClassKey of) const;

    // One field of an object value, read as a value of its own and asked what
    // it is exactly as an argument is. Undefined when this is not an object or
    // has no such key, so "was it supplied" is IsUndefined rather than a second
    // call.
    //
    // The read coerces nothing by itself: a caller that must not be handed a
    // coerced value guards with IsNumber() / IsBoolean() before converting. Some
    // must - `{insecure: "no"}` may not turn certificate checks off, and a
    // string coerced into a coordinate corrupts it - and where the guard stands
    // is where that decision is legible.
    [[nodiscard]] Value Field(std::string_view key) const;

    // Its own keys, in the order the engine enumerates them, for an object whose
    // shape is the script's to choose. Empty when this is not an object.
    [[nodiscard]] std::vector<std::string> Names() const;

    // How many elements an array value has. Zero for anything that is not one -
    // and for an empty array, which IsArray tells apart, and which a callback
    // answering with one means something by.
    [[nodiscard]] size_t Length() const;

    // One element of it, read as a value of its own exactly as a field is, and
    // asked what it is the same way. Undefined when this is not an array or the
    // index is past its end.
    [[nodiscard]] Value At(size_t index) const;

    // Call this value as a function, with `this` undefined - a plain call, which
    // is what every callback this API hands out is. A receiver belongs here the
    // day a binding needs to name one, and not before.
    //
    // The result reaches `onResult` rather than being returned, because it is a
    // value of the engine's own frame: it, everything read out of it, and
    // everything built inside that callback are released when the callback
    // returns. That is what lets a binding call a script in a loop. A result
    // that outlived the call would leave the frame one value heavier per
    // iteration, and an engine that roots exactly would hold one more root per
    // iteration with it - which is the whole reason the call is shaped this way
    // rather than handing back something to keep.
    //
    // `onResult` does not run at all when this is not a function, or when the
    // call threw or was terminated; what a callback that answered nothing means
    // is the caller's own business, and every one of them already has to decide
    // it. An exception is caught here rather than left pending, because the next
    // iteration has to be able to re-enter the engine.
    //
    // Reports whether the call produced a result, for a caller that would rather
    // ask than set a flag from inside the callback.
    template <typename F>
    bool Call(std::span<const Argument> args, F&& onResult) const {
        using Callback = std::decay_t<F>;
        Callback callback(std::forward<F>(onResult));
        return CallWith(
            args, &callback, +[](void* context, Value result) { (*static_cast<Callback*>(context))(result); });
    }

    // The same call with its arguments written out - fn.Call({args[1]}, ...) -
    // which a span cannot be built from on its own. The list lives as long as
    // the expression that made it, which is this call.
    template <typename F>
    bool Call(std::initializer_list<Argument> args, F&& onResult) const {
        return Call(std::span<const Argument>(args.begin(), args.size()), std::forward<F>(onResult));
    }

    // The two words the frontend resolves a value from. Only it can make sense
    // of them, and only it needs to - a binding names a value, never its slot.
    [[nodiscard]] CallState* State() const { return state_; }
    [[nodiscard]] size_t Index() const { return index_; }

   protected:
    // Names no value at all: what a wrapper the engine refused to build is.
    // Placing one stores nothing, which is the only thing that may be done with
    // it.
    Value() = default;

   private:
    bool CallWith(std::span<const Argument> args, void* context, void (*onResult)(void*, Value)) const;

    CallState* state_ = nullptr;
    size_t index_ = 0;
};

// A wrapper the call has made, waiting to be put somewhere.
//
// It is a value of the engine's own call frame like any other, so it roots
// nothing of its own and cannot outlive the call - and making one and placing
// it are separate steps, so the same handle reaches a return value, an array
// slot or an object key through those destinations' ordinary Set overloads: one
// call per way of owning a native rather than one per (ownership, destination)
// pair.
//
// Nodiscard because a wrapper nobody placed is a wrapper nothing refers to, and
// it takes its native with it when it is collected.
//
// Default-constructed when the engine refused to build the wrapper. Placing one
// of those stores nothing, so a binding never has to ask which it got.
class [[nodiscard]] Instance : public Value {
   public:
    Instance() = default;
    Instance(CallState* state, size_t index) : Value(state, index) {}

    // The native behind it, if it is an instance of that class - the same
    // question Value::Instance answers about an argument, under the name that
    // reads right where the answer is a wrapper's own native. Null for a
    // wrapper of another class, and for one the engine refused to build.
    [[nodiscard]] void* Native(ClassKey of) const { return Value::Instance(of); }
};

}  // namespace d2bs::script
