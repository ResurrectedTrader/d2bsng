#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "InstanceTracker.h"
#include "components/script/NativeCallHook.h"
#include "unibind/unibind.h"

namespace d2bs::api {

// CRTP base for a script-visible class whose instances carry a native `NativeType`.
//
// Derived provides:
//   - static constexpr std::string_view ClassName
//   - static void Configure(const ub::Class<NativeType>& cls), declaring its members through the
//     Property / Method / StaticMethod helpers below
// and, if script may construct it:
//   - static std::unique_ptr<NativeType> New(const ub::CallbackInfo& args)
//     or static std::shared_ptr<NativeType> New(const ub::CallbackInfo& args)
//     returning null after throwing to refuse the construction. Without one the class is not
//     constructable from script. Calling it without `new` is a TypeError unless Derived also
//     declares `static constexpr bool CALLABLE_WITHOUT_NEW = true;`, in which case a plain call
//     makes an instance too and New tells the two apart with `args.IsConstructCall()`.
//
// Instances own a share of their native: ub::Class holds a std::shared_ptr, and the native goes
// with its last share. Every wrapper - constructed or handed out by Wrap - is counted in the
// InstanceTracker for the console's Scripts panel, through the deleter of the share it holds.
template <typename Derived, typename NativeType>
class ClassBase {
   public:
    using Native = NativeType;

    // The class for this isolate, declared on first use. One isolate per script thread, so a
    // thread-local slot is a per-isolate one; it is keyed on the isolate as well, so a thread that
    // hosts a second isolate after the first is gone declares afresh rather than reusing a class
    // from a dead heap.
    static ub::Class<NativeType> Get(ub::Isolate& isolate) {
        auto& slot = Slot();
        if (slot.isolate != &isolate || !slot.cls) {
            auto cls = ub::Class<NativeType>::New(isolate, Derived::ClassName);
            if constexpr (requires(const ub::CallbackInfo& info) { Derived::New(info); }) {
                if constexpr (requires { Derived::CALLABLE_WITHOUT_NEW; }) {
                    static_assert(Derived::CALLABLE_WITHOUT_NEW, "declare it only to set it");
                    cls.template ConstructOrCall<&Construct>();
                } else {
                    cls.template Construct<&Construct>();
                }
            }
            Derived::Configure(cls);
            slot.isolate = &isolate;
            slot.cls = cls;
        }
        return *slot.cls;
    }

    // Forget this thread's class. Called while the isolate is torn down, so a later isolate on the
    // same thread cannot be handed a class whose record went with the old heap.
    static void ClearCache() { Slot() = {}; }

    // The native behind `value`, or null if it is not an instance of this class. Exact: an
    // instance of another class, or a plain object, is null.
    template <class U>
    [[nodiscard]] static NativeType* Unwrap(const ub::Local<U>& value) noexcept {
        return ub::Class<NativeType>::Unwrap(value);
    }

    // A share of the native behind `value`, for keeping it past the wrapper.
    template <class U>
    [[nodiscard]] static std::shared_ptr<NativeType> UnwrapShared(const ub::Local<U>& value) noexcept {
        return ub::Class<NativeType>::UnwrapShared(value);
    }

    template <class U>
    [[nodiscard]] static bool IsInstance(const ub::Local<U>& value) noexcept {
        return Unwrap(value) != nullptr;
    }

    // A new wrapper holding a share of `native`, without running the constructor.
    static std::optional<ub::Local<ub::Object>> Wrap(const ub::Context& context, std::shared_ptr<NativeType> native) {
        return Get(context.GetIsolate()).Wrap(context, Counted(std::move(native)));
    }

    // Instance-tracker row for this class, resolved once: ClassId takes a lock and scans the name
    // table, which must not happen per object.
    static int32_t InstanceClassId() {
        static const int32_t ID = InstanceTracker::ClassId(Derived::ClassName);
        return ID;
    }

    // "Unit.x" rather than "x" in the Profiling panel's binding table.
    [[nodiscard]] static std::string BindingName(std::string_view name) {
        return std::string(Derived::ClassName) + "." + std::string(name);
    }

    // An accessor on one already-made object rather than on the class. The `me` global is a Unit
    // instance carrying members no other unit has; declaring them here rather than with a raw
    // SetAccessor puts them on the same trampoline as Property(), so per-call stack capture and
    // the Profiling panel reach them.
    template <typename Getter>
    static bool InstanceProperty(const ub::Context& context, const ub::Local<ub::Object>& object, std::string_view name,
                                 Getter getter) {
        const ub::AccessorGetterCallback getterFn = +getter;
        auto* accessors = runtime::script::InternAccessors(BindingName(name), getterFn, nullptr);
        return object
            .SetAccessor(context, name, &runtime::script::PropertyGetterTrampoline, nullptr,
                         ub::CallbackData::For(*accessors))
            .value_or(false);
    }

    template <typename Getter, typename Setter>
    static bool InstanceProperty(const ub::Context& context, const ub::Local<ub::Object>& object, std::string_view name,
                                 Getter getter, Setter setter) {
        const ub::AccessorGetterCallback getterFn = +getter;
        const ub::AccessorSetterCallback setterFn = +setter;
        auto* accessors = runtime::script::InternAccessors(BindingName(name), getterFn, setterFn);
        return object
            .SetAccessor(context, name, &runtime::script::PropertyGetterTrampoline,
                         &runtime::script::PropertySetterTrampoline, ub::CallbackData::For(*accessors))
            .value_or(false);
    }

   protected:
    // Read-only property. Routed through the NativeCallHook trampolines so per-script stack capture
    // and the Profiling panel see every read.
    //
    // Declared on the instance template, so it is an own property of every instance: scripts copy
    // game objects with for...in + hasOwnProperty (kolbot's copyObj), Object.keys or
    // JSON.stringify, and an accessor on the prototype would be invisible to all three.
    template <typename Getter>
    static void Property(const ub::Class<NativeType>& cls, std::string_view name, Getter getter) {
        const ub::AccessorGetterCallback getterFn = +getter;
        auto* accessors = runtime::script::InternAccessors(BindingName(name), getterFn, nullptr);
        cls.InstanceTemplate().SetAccessor(name, &runtime::script::PropertyGetterTrampoline, nullptr,
                                           ub::CallbackData::For(*accessors));
    }

    // Read-write property.
    template <typename Getter, typename Setter>
    static void Property(const ub::Class<NativeType>& cls, std::string_view name, Getter getter, Setter setter) {
        const ub::AccessorGetterCallback getterFn = +getter;
        const ub::AccessorSetterCallback setterFn = +setter;
        auto* accessors = runtime::script::InternAccessors(BindingName(name), getterFn, setterFn);
        cls.InstanceTemplate().SetAccessor(name, &runtime::script::PropertyGetterTrampoline,
                                           &runtime::script::PropertySetterTrampoline,
                                           ub::CallbackData::For(*accessors));
    }

    // Instance method, on the prototype.
    template <typename Func>
    static void Method(const ub::Class<NativeType>& cls, std::string_view name, Func func) {
        const ub::FunctionCallback fn = +func;
        auto* binding = runtime::script::InternFunction(BindingName(name), fn);
        cls.Method(name, &runtime::script::MethodTrampoline, ub::CallbackData::For(*binding));
    }

    // Method under a well-known symbol - `Symbol.iterator` is the reason this exists.
    template <typename Func>
    static void SymbolMethod(const ub::Class<NativeType>& cls, ub::WellKnownSymbol key, std::string_view name,
                             Func func) {
        const ub::FunctionCallback fn = +func;
        auto* binding = runtime::script::InternFunction(BindingName(name), fn);
        cls.SymbolMethod(key, &runtime::script::MethodTrampoline, ub::CallbackData::For(*binding));
    }

    // Static method, on the constructor.
    template <typename Func>
    static void StaticMethod(const ub::Class<NativeType>& cls, std::string_view name, Func func) {
        const ub::FunctionCallback fn = +func;
        auto* binding = runtime::script::InternFunction(BindingName(name), fn);
        cls.StaticMethod(name, &runtime::script::MethodTrampoline, ub::CallbackData::For(*binding));
    }

   private:
    struct CacheSlot {
        ub::Isolate* isolate = nullptr;
        std::optional<ub::Class<NativeType>> cls;
    };

    static CacheSlot& Slot() {
        static thread_local CacheSlot slot;
        return slot;
    }

    // A share whose release gives the tracker back its count. The original share rides inside the
    // deleter, so whoever else holds the native keeps it alive exactly as before; this one only
    // adds the bookkeeping. The row is recorded rather than looked up again so the count returns
    // to the thread that took it whichever thread lets the last share go.
    static std::shared_ptr<NativeType> Counted(std::shared_ptr<NativeType> native) {
        if (!native) {
            return nullptr;
        }
        NativeType* raw = native.get();
        auto& row = InstanceTracker::Instance().Increment(InstanceClassId());
        return {raw, [held = std::move(native), row = &row](NativeType* /*native*/) mutable {
                    held.reset();
                    InstanceTracker::Instance().Decrement(*row, InstanceClassId());
                }};
    }

    static std::shared_ptr<NativeType> Construct(const ub::CallbackInfo& info) {
        auto made = Derived::New(info);
        if (!made) {
            return nullptr;
        }
        return Counted(std::shared_ptr<NativeType>(std::move(made)));
    }
};

}  // namespace d2bs::api
