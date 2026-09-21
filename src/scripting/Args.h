#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "CallArgs.h"
#include "Instance.h"
#include "Persistent.h"

namespace d2bs::script {

// Which error constructor a binding wants. Engines agree on these three; a
// frontend maps them to its own.
enum class ErrorKind : uint8_t { Error, TypeError, RangeError };

class ObjectBuilder;
class ArrayBuilder;

// One call into a binding.
//
// Every accessor takes and returns C++ types - never an engine value. That is
// the whole point. V8 hands out Local<Value> freely because a Local is a
// HandleScope slot, but SpiderMonkey requires JS::Rooted in strict stack order
// with conversions written through a MutableHandleValue. A portable value
// handle would have to heap-root there and would still be easy to misuse.
// Keeping every value inside the engine's own call frame satisfies both engines
// structurally, rather than asking binding authors to remember a discipline
// only one of them enforces.
//
// Unlike Engine, none of this is virtual. It is the per-call hot path, so the
// frontend defines each member out of line and LTO inlines them back across the
// static libs, exactly as it already does for the game:: wrappers. Getting that
// granularity backwards is what makes an engine abstraction either slow or
// enormous.
//
// One context per shape the language calls a binding in - Read, Args,
// Construction, Write below - so that a getter cannot read an argument list it
// does not have and a setter cannot write a result nothing would read.

// One value a binding is handed: an argument of a call, or the value a setter
// is storing.
//
// Indexed off Args, so a binding reads args[0].ToInt32() rather than naming the
// index twice. An argument that was not passed reads as undefined rather than
// out of range, so a binding can ask about args[3] of a one-argument call.
class Value {
   public:
    Value(void* state, size_t index) : state_(state), index_(index) {}

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

   private:
    bool CallWith(std::span<const Argument> args, void* context, void (*onResult)(void*, Value)) const;

    void* state_;
    size_t index_;
};

// What every binding is handed, whatever kind of binding it is: its receiver,
// its error slot, and the runtime's own pointer for the script it is running
// in. A helper that needs no more than these takes this and so serves all four
// kinds.
class Binding {
   public:
    explicit Binding(void* state) : state_(state) {}

    // The receiver, as a value. It names a slot the call already holds, so
    // asking for it allocates nothing; Instance::Native reads the native back
    // out of it exactly as Value::Instance does for an argument.
    [[nodiscard]] Instance This() const;

    void Throw(ErrorKind kind, std::string_view message) const;

    // Whatever the runtime attached to the engine this call is running in -
    // its own per-script object, which it casts back.
    //
    // The contract deliberately has no idea what this is. A script's identity,
    // its name, its mode and everything else about it belong to the runtime,
    // and an engine has no business describing them; all an engine can do is
    // carry the pointer back to the binding that needs it. Null when the
    // runtime attached nothing, which is how the engine's own bootstrapping
    // runs.
    [[nodiscard]] void* Internal() const;

    // The opaque call the frontend handed us, handed back. Only the frontend can
    // make sense of it, and only its own engine-bound bindings need to - a
    // binding in the runtime has no use for it and no way to read it.
    [[nodiscard]] void* State() const { return state_; }

   protected:
    void* state_;
};

// A binding whose result the language keeps: everything but a setter, whose
// return is the store-succeeded flag and not a value a script can observe.
class Read : public Binding {
   public:
    using Binding::Binding;

    // --- making values ---------------------------------------------------

    // Valid only until this call returns: both build into the engine's current
    // frame and root nothing of their own. Creating a value and returning it
    // are separate steps, so a value can be built, nested inside another, and
    // returned - or not returned at all.
    [[nodiscard]] ObjectBuilder NewObject();
    [[nodiscard]] ArrayBuilder NewArray(size_t length);

    void SetReturnValue(int32_t value) const;
    void SetReturnValue(uint32_t value) const;
    void SetReturnValue(double value) const;
    void SetReturnValue(bool value) const;
    void SetReturnValue(std::string_view value) const;
    // A char pointer converts to bool by a standard conversion and to
    // string_view only by a user-defined one, so without this overload the
    // language silently picks bool and the value becomes `true`. Null is the
    // empty string, because a C API returning null means "no text", not UB.
    void SetReturnValue(const char* value) const {
        SetReturnValue(value != nullptr ? std::string_view(value) : std::string_view{});
    }
    // Hand back a value the script gave us earlier, or one a binding retained.
    // The engine made the reference, so it is the one thing it can turn back
    // into a value.
    void SetReturnValue(const Persistent& value) const;
    // Whatever this call built - an object, an array, a wrapper.
    void SetReturnValue(const Slot& value) const;
    // Sixteen-bit elements, not bytes: a collision grid is read a cell at a
    // time, and splitting each cell into two bytes would change what index i
    // means to a script.
    void SetReturnValueUint16(std::span<const uint16_t> value) const;
    void SetReturnValueNull() const;
    void SetReturnValueUndefined() const;

    // --- bound instances --------------------------------------------------
    //
    // Untyped here and typed by Class<T>, which is the only place a native is
    // cast. A binding never names an engine template: it names a ClassKey, and
    // the frontend resolves that to whatever it needs inside the call.

    // A wrapper of the class `of` over a native the wrapper owns. The native is
    // freed by that class's own Destroy whether the wrapper gets built or the
    // engine refuses to build one, so failing here can neither leak it nor
    // strand whatever the class recorded when it handed it over.
    [[nodiscard]] Instance NewInstance(ClassKey of, void* owned);

    // A wrapper over a native something else owns and outlives it by. Nothing
    // frees it when the wrapper goes.
    [[nodiscard]] Instance BorrowInstance(ClassKey of, void* borrowed, KeepAlive keep);
};

// A call with an argument list: a method, a static, a global, a constructor.
// A property access has none, which is why a getter cannot ask for one.
class Args : public Read {
   public:
    using Read::Read;

    [[nodiscard]] size_t Count() const;

    // Read one argument. Out-of-range reads as undefined, as in the language.
    [[nodiscard]] Value operator[](size_t i) const { return {state_, i}; }

    [[nodiscard]] void* Instance(size_t i, ClassKey of) const { return (*this)[i].Instance(of); }
    [[nodiscard]] bool IsInstance(size_t i, ClassKey of) const { return (*this)[i].IsInstance(of); }

    // Sever the receiver from its native, leaving a wrapper that resolves to
    // nothing. What removing a drawable does.
    void DetachThis();
};

// A constructor body, and the only thing that can adopt.
//
// Adopting anywhere else fails silently rather than loudly. Where the receiver
// is not one this class may stamp - a global, an instance of another class -
// the native goes straight back to the class's Destroy and an empty wrapper
// comes out; where it is one, whatever it already wrapped is overwritten.
// Making the call unspellable outside a constructor is what stops either from
// being written.
class Construction : public Args {
   public:
    using Args::Args;

    // Whether the language invoked this with `new`. Only one declared
    // Construct::AllowPlainCall can be told otherwise.
    [[nodiscard]] bool IsConstructCall() const;

    // Attach the native to the object the language already made for `new`.
    // Nothing else a constructor body can do.
    //
    // The class is named rather than read back off the receiver: the receiver
    // has not been stamped yet, which is what this call is for, and a receiver
    // that is not an instance of `of` at all must be refused rather than
    // stamped - `Reflect.construct` can hand a constructor any object.
    [[nodiscard]] script::Instance AdoptInstance(ClassKey of, void* owned);
    [[nodiscard]] script::Instance AdoptBorrowedInstance(ClassKey of, void* borrowed);
};

// A setter: one incoming value and nowhere to put a result.
class Write : public Binding {
   public:
    using Binding::Binding;

    // The value being stored. A setter has exactly this one, which is why it
    // reads it by name rather than by index.
    [[nodiscard]] script::Value Value() const { return {state_, 0}; }
};

class ObjectBuilder : public Slot {
   public:
    explicit ObjectBuilder(void* state) : Slot(state) {}

    ObjectBuilder& Set(std::string_view key, int32_t value);
    ObjectBuilder& Set(std::string_view key, uint32_t value);
    ObjectBuilder& Set(std::string_view key, double value);
    ObjectBuilder& Set(std::string_view key, bool value);
    ObjectBuilder& Set(std::string_view key, std::string_view value);
    // See SetReturnValue(const char*): without this, a char pointer is a bool.
    ObjectBuilder& Set(std::string_view key, const char* value) {
        return Set(key, value != nullptr ? std::string_view(value) : std::string_view{});
    }
    ObjectBuilder& Set(std::string_view key, const Slot& value);
    ObjectBuilder& SetBytes(std::string_view key, std::span<const uint8_t> value);
    ObjectBuilder& SetNull(std::string_view key);

    // Hold this object past the end of the call, so a binding can hand the
    // same one back again. Object identity is observable - a script comparing
    // two reads, or hanging a property of its own on what it got, sees the
    // difference between the same object twice and two equal ones - so a
    // binding that caches a result caches the object rather than rebuilding it.
    [[nodiscard]] Persistent Persist() const;
};

class ArrayBuilder : public Slot {
   public:
    explicit ArrayBuilder(void* state) : Slot(state) {}

    ArrayBuilder& Set(size_t index, int32_t value);
    ArrayBuilder& Set(size_t index, uint32_t value);
    ArrayBuilder& Set(size_t index, double value);
    ArrayBuilder& Set(size_t index, bool value);
    ArrayBuilder& Set(size_t index, std::string_view value);
    // See SetReturnValue(const char*): without this, a char pointer is a bool.
    ArrayBuilder& Set(size_t index, const char* value) {
        return Set(index, value != nullptr ? std::string_view(value) : std::string_view{});
    }
    ArrayBuilder& Set(size_t index, const Slot& value);
};

// What a binding is, one kind per shape the language calls it in. Engine-free
// by construction: the only thing a binding can touch is the context it was
// handed, and the context says what kind of call this is.
//
// Each returns false to mean "stop" - either an exception is pending, or the
// script is being terminated. V8's own callback returns void because it signals
// termination out of band through TerminateExecution(); an engine with no such
// channel says it here instead, by returning false with nothing pending.
// Modelling V8's void return would discard that answer. Returning true after
// Throw() is a bug; return false.
using Native = bool (*)(Args&);
using Getter = bool (*)(Read&);
using Setter = bool (*)(Write&);
using Ctor = bool (*)(Construction&);

}  // namespace d2bs::script
