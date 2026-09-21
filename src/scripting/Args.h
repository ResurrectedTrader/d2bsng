#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "Persistent.h"
#include "State.h"
#include "Value.h"

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

// What every binding is handed, whatever kind of binding it is: its receiver,
// its error slot, and the runtime's own pointer for the script it is running
// in. A helper that needs no more than these takes this and so serves all four
// kinds.
class Binding {
   public:
    explicit Binding(CallState* state) : state_(state) {}

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
    [[nodiscard]] void* Owner() const;

    // The opaque call the frontend handed us, handed back. Only the frontend can
    // make sense of it, and only its own engine-bound bindings need to - a
    // binding in the runtime has no use for it and no way to read it.
    [[nodiscard]] CallState* State() const { return state_; }

   protected:
    CallState* state_;
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
    // Any value of this call: one it built, one it read, or an argument handed
    // straight back.
    void SetReturnValue(const Value& value) const;
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
    //
    // There is no second way to hand one over. A native two wrappers name is a
    // native both of them own, which the typed layer arranges by handing each a
    // share of it - so no wrapper is ever left pointing at something another
    // one frees.
    [[nodiscard]] Instance NewInstance(ClassKey of, void* owned);
};

// A call with an argument list: a method, a static, a global, a constructor.
// A property access has none, which is why a getter cannot ask for one.
class Args : public Read {
   public:
    using Read::Read;

    [[nodiscard]] size_t Count() const;

    // Read one argument, so a binding writes args[0].ToInt32() rather than
    // naming the index twice. Out-of-range reads as undefined rather than out
    // of range, so a binding can ask about args[3] of a one-argument call.
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
};

// A setter: one incoming value and nowhere to put a result.
class Write : public Binding {
   public:
    using Binding::Binding;

    // The value being stored. A setter has exactly this one, which is why it
    // reads it by name rather than by index.
    [[nodiscard]] script::Value Value() const { return {state_, 0}; }
};

// An object this call is filling in. It is a value of the call's frame like any
// other, so it is read, nested and returned exactly as an argument is; what it
// adds is the vocabulary for putting things into it.
class ObjectBuilder : public Value {
   public:
    ObjectBuilder(CallState* state, size_t index) : Value(state, index) {}

    ObjectBuilder& Set(std::string_view key, int32_t value);
    ObjectBuilder& Set(std::string_view key, uint32_t value);
    ObjectBuilder& Set(std::string_view key, double value);
    ObjectBuilder& Set(std::string_view key, bool value);
    ObjectBuilder& Set(std::string_view key, std::string_view value);
    // See SetReturnValue(const char*): without this, a char pointer is a bool.
    ObjectBuilder& Set(std::string_view key, const char* value) {
        return Set(key, value != nullptr ? std::string_view(value) : std::string_view{});
    }
    // Any other value of this call: one it built, one it read, or an argument
    // moved across unchanged.
    ObjectBuilder& Set(std::string_view key, const Value& value);
    ObjectBuilder& SetBytes(std::string_view key, std::span<const uint8_t> value);
    ObjectBuilder& SetNull(std::string_view key);
};

// An array this call is filling in, on the same terms as ObjectBuilder.
class ArrayBuilder : public Value {
   public:
    ArrayBuilder(CallState* state, size_t index) : Value(state, index) {}

    ArrayBuilder& Set(size_t index, int32_t value);
    ArrayBuilder& Set(size_t index, uint32_t value);
    ArrayBuilder& Set(size_t index, double value);
    ArrayBuilder& Set(size_t index, bool value);
    ArrayBuilder& Set(size_t index, std::string_view value);
    // See SetReturnValue(const char*): without this, a char pointer is a bool.
    ArrayBuilder& Set(size_t index, const char* value) {
        return Set(index, value != nullptr ? std::string_view(value) : std::string_view{});
    }
    ArrayBuilder& Set(size_t index, const Value& value);
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
