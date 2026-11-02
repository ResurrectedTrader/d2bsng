#pragma once

#include <cppgc/allocation.h>
#include <cppgc/prefinalizer.h>
#include <cppgc/visitor.h>
#include <v8-cppgc.h>
#include <v8.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include "Convert.h"
#include "Error.h"
#include "InstanceTracker.h"
#include "LockedHandle.h"
#include "components/script/NativeCallHook.h"

namespace d2bs::runtime::api {

namespace detail {

// Seeds each class's type tag (see ClassBase::typeTag_). ClassRegistry uses it to assert at
// compile time that no two classes hash alike.
constexpr uint32_t Fnv1a(std::string_view text) {
    uint32_t hash = 2166136261U;
    for (const char ch : text) {
        hash ^= static_cast<uint8_t>(ch);
        hash *= 16777619U;
    }
    return hash;
}

// The C++ side of every wrapper: an object on the isolate's C++ heap (cppgc) that v8::Object::Wrap attaches to the
// JS object, so the two are collected together. Unwrap tells the wrapper's class from the tag and finds the native
// through the pointer, both plain reads - no virtual call.
//
// Destructors of cells (and so of the natives they own) run inside cppgc sweeping, possibly within an allocation,
// and at isolate disposal after V8 has detached the heap. They must not touch V8 or game state, and reach another
// cell's native only through a registration whose own destructor removes it (DBStatement / SQLite).
class CellBase : public v8::Object::Wrappable {
   public:
    CellBase(const uint32_t* classTag, void* native) : classTag_(classTag), native_(native) {}

    [[nodiscard]] const uint32_t* ClassTag() const { return classTag_; }
    [[nodiscard]] void* Native() const { return native_; }
    void Clear() { native_ = nullptr; }

   private:
    const uint32_t* classTag_;
    void* native_;
};

inline constexpr v8::CppHeapPointerTag CELL_TAG = v8::CppHeapPointerTag::kFirstObjectWrappableTag;

}  // namespace detail

// CRTP base template for V8 class bindings
// Derived classes must provide:
//   - static constexpr std::string_view ClassName
//   - static void New(const v8::FunctionCallbackInfo<v8::Value>& args)
//   - static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl)
//
// A native that holds V8 values keeps them in v8::TracedReference members and exposes
// `void Trace(cppgc::Visitor*) const` tracing them; its cell forwards to it.
//
// A native that exposes `void PreFinalize()` gets it called in the GC pause that finds its cell dead, before any
// sweeping, so it can leave a list another native keeps (DBStatement / SQLite) before anything reads it there dead.
// The same rules as for destructors apply.

template <typename Derived, typename NativeType>
class ClassBase {
    struct TemplateCache {
        std::unordered_map<v8::Isolate*, v8::Global<v8::FunctionTemplate>> templates;
        std::mutex mutex;
    };

    static TemplateCache& GetCache() {
        static TemplateCache cache;
        return cache;
    }

    // Address-only marker, one per Derived, carried by every cell of this class so Unwrap
    // identifies its own instances with a pointer compare.
    //
    // Its value is seeded from the class name, which is what keeps the tags at distinct
    // addresses: the DLL links with /OPT:ICF (EnableCOMDATFolding in d2bs.vcxproj), which folds
    // COMDATs holding identical bytes. Uniform contents would let every class share one address
    // and Unwrap would then accept any wrapper of any class. ClassName is therefore required to
    // be unique across classes.
    // NOLINTNEXTLINE(cert-err58-cpp) - constinit makes this compile-time; the check misses that
    inline static constinit uint32_t typeTag_ = detail::Fnv1a(Derived::ClassName);

    // A cell that owns its native, counted in the instance tracker from construction to finalization. The tracker
    // row rides along so the count returns to the row that took it, whichever thread finalizes the cell (isolate
    // disposal can run on another thread).
    class OwnedCell : public detail::CellBase {
       public:
        template <typename... Args>
        explicit OwnedCell(InstanceTracker::Row& row, Args&&... args)
            : CellBase(&typeTag_, &value_), value_(std::forward<Args>(args)...), row_(&row) {}
        OwnedCell(const OwnedCell&) = delete;
        OwnedCell& operator=(const OwnedCell&) = delete;
        OwnedCell(OwnedCell&&) = delete;
        OwnedCell& operator=(OwnedCell&&) = delete;
        ~OwnedCell() override { InstanceTracker::Instance().Decrement(*row_, InstanceClassId()); }

        void Trace(cppgc::Visitor* visitor) const override {
            if constexpr (requires(const NativeType& native) { native.Trace(visitor); }) {
                value_.Trace(visitor);
            }
        }

       protected:
        NativeType value_;

       private:
        InstanceTracker::Row* row_;
    };

    // Registers a pre-finalizer, which costs every allocation of the class, hence only for natives that ask.
    class PreFinalizedCell final : public OwnedCell {
        CPPGC_USING_PRE_FINALIZER(PreFinalizedCell, PreFinalize);

       public:
        using OwnedCell::OwnedCell;

        void PreFinalize() { this->value_.PreFinalize(); }
    };

    using Cell =
        std::conditional_t<requires(NativeType& native) { native.PreFinalize(); }, PreFinalizedCell, OwnedCell>;

    // A cell over a native something else owns (a drawable the script's registry holds, a statement its database
    // holds). Not counted: the owner counts the native.
    class BorrowedCell final : public detail::CellBase {
       public:
        explicit BorrowedCell(NativeType* native) : CellBase(&typeTag_, native) {}
    };

    // Unwrap reads the slot only on an API wrapper, the only kind that has one. An API wrapper never wrapped (a plain
    // instance of another template) has no cell.
    static detail::CellBase* CellOf(v8::Isolate* isolate, v8::Local<v8::Object> obj) {
        if (obj.IsEmpty() || !obj->IsApiWrapper()) {
            return nullptr;
        }
        auto* cell = v8::Object::Unwrap<detail::CELL_TAG, detail::CellBase>(isolate, obj);
        return cell != nullptr && cell->ClassTag() == &typeTag_ ? cell : nullptr;
    }

    static NativeType* NativeOf(v8::Isolate* isolate, v8::Local<v8::Object> obj) {
        auto* cell = CellOf(isolate, obj);
        return cell != nullptr ? static_cast<NativeType*>(cell->Native()) : nullptr;
    }

   public:
    // Instance-tracker row for this class, resolved once. ClassId takes a lock and scans the name
    // table, which must not happen per object - this is on the construction path of every wrapper.
    static int32_t InstanceClassId() {
        static const int32_t ID = InstanceTracker::ClassId(Derived::ClassName);
        return ID;
    }

    // Returns (or creates) the cached FunctionTemplate for this isolate.
    static v8::Local<v8::FunctionTemplate> GetTemplate(v8::Isolate* isolate) {
        auto& cache = GetCache();
        std::scoped_lock lock(cache.mutex);
        auto it = cache.templates.find(isolate);
        if (it == cache.templates.end()) {
            auto tpl = v8::FunctionTemplate::New(isolate, Derived::New);
            tpl->SetClassName(convert::ToJS(isolate, Derived::ClassName));
            Derived::ConfigureTemplate(isolate, tpl);
            it = cache.templates.emplace(isolate, v8::Global<v8::FunctionTemplate>(isolate, tpl)).first;
        }
        return it->second.Get(isolate);
    }

    // Clear cached template for this isolate (call before disposal).
    static void ClearCache(v8::Isolate* isolate) {
        auto& cache = GetCache();
        std::scoped_lock lock(cache.mutex);
        cache.templates.erase(isolate);
    }

    // Check if a value is an instance of this class, by the class tag its cell carries - the same test Unwrap applies
    // to its receiver, without the template cache's lock.
    static bool IsInstance(v8::Isolate* isolate, v8::Local<v8::Value> value) {
        return !value.IsEmpty() && value->IsObject() && CellOf(isolate, value.As<v8::Object>()) != nullptr;
    }

    // Unwrap native pointer from V8 object.
    //
    // Runs on every property read and method call, so it identifies the receiver by its
    // cell's class tag rather than by IsInstance's template-chain walk.
    //
    // V8 14 removed AccessorSignature, so a script that lifts a getter onto a foreign
    // receiver reaches the binding with it, and this returns nullptr.
    //
    // A game handle (GameHandle) comes back inside a LockedHandle instead, so the binding's
    // validity test and its reads run under one read lock; see LockedHandle for the rules
    // while it is held.
    static NativeType* Unwrap(v8::Isolate* isolate, v8::Local<v8::Object> obj)
        requires(!GameHandle<NativeType>)
    {
        return NativeOf(isolate, obj);
    }

    static LockedHandle<NativeType> Unwrap(v8::Isolate* isolate, v8::Local<v8::Object> obj)
        requires GameHandle<NativeType>
    {
        return LockedHandle<NativeType>(NativeOf(isolate, obj));
    }

    // Constructor path: gives `obj` (the construct call's receiver) a cell owning a native built from args, and
    // returns the native.
    template <typename... Args>
    static NativeType* Attach(v8::Isolate* isolate, v8::Local<v8::Object> obj, Args&&... args) {
        auto& row = InstanceTracker::Instance().Increment(InstanceClassId());
        auto* cell = cppgc::MakeGarbageCollected<Cell>(isolate->GetCppHeap()->GetAllocationHandle(), row,
                                                       std::forward<Args>(args)...);
        v8::Object::Wrap<detail::CELL_TAG>(isolate, obj, cell);
        return static_cast<NativeType*>(cell->Native());
    }

    // Gives `obj` a cell over a native it does not own. The owner clears the returned cell once it drops the
    // native, unless the cell keeps the owner alive.
    static detail::CellBase* Borrow(v8::Isolate* isolate, v8::Local<v8::Object> obj, NativeType* native) {
        auto* cell = cppgc::MakeGarbageCollected<BorrowedCell>(isolate->GetCppHeap()->GetAllocationHandle(), native);
        v8::Object::Wrap<detail::CELL_TAG>(isolate, obj, cell);
        return cell;
    }

    // A new instance owning a native built from args. On failure no native is built, so a caller that passed a
    // resource in args still owns it.
    template <typename... Args>
    static v8::Local<v8::Object> CreateInstance(v8::Isolate* isolate, v8::Local<v8::Context> context, Args&&... args) {
        v8::EscapableHandleScope scope(isolate);
        auto tpl = GetTemplate(isolate);
        v8::Local<v8::Object> obj;
        if (!tpl->InstanceTemplate()->NewInstance(context).ToLocal(&obj)) {
            return {};
        }
        Attach(isolate, obj, std::forward<Args>(args)...);
        return scope.Escape(obj);
    }

    // Property on one already-built instance rather than on the class template. The `me`
    // global is a JSUnit instance carrying extra own accessors; registering them here rather
    // than with a raw SetNativeDataProperty puts them on the same trampoline as Property(),
    // so per-call stack capture reaches them.
    template <typename Getter>
    static void InstanceProperty(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> obj,
                                 const char* name, Getter getter) {
        const v8::AccessorNameGetterCallback getterFn = +getter;
        auto* accessors = script::InternAccessors(BindingName(name), getterFn, nullptr);
        obj->SetNativeDataProperty(context, convert::ToJS(isolate, name), &script::PropertyGetterTrampoline, nullptr,
                                   v8::External::New(isolate, accessors, v8::kExternalPointerTypeTagDefault),
                                   v8::PropertyAttribute::ReadOnly)
            .Check();
    }

    template <typename Getter, typename Setter>
    static void InstanceProperty(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> obj,
                                 const char* name, Getter getter, Setter setter) {
        const v8::AccessorNameGetterCallback getterFn = +getter;
        const v8::AccessorNameSetterCallbackV2 setterFn = +setter;
        auto* accessors = script::InternAccessors(BindingName(name), getterFn, setterFn);
        obj->SetNativeDataProperty(context, convert::ToJS(isolate, name), &script::PropertyGetterTrampoline,
                                   &script::PropertySetterTrampoline,
                                   v8::External::New(isolate, accessors, v8::kExternalPointerTypeTagDefault))
            .Check();
    }

   protected:
    // "Unit.x" rather than "x" in the Profiling panel's binding table.
    [[nodiscard]] static std::string BindingName(const char* name) {
        return std::string(Derived::ClassName) + "." + name;
    }

    // ========================================================================
    // Property registration with lambda getters/setters
    // V8 v14+ uses SetNativeDataProperty with AccessorNameGetterCallback
    // ========================================================================

    // Read-only property with getter lambda.
    // Routes through NativeCallHook trampolines so per-script stack capture
    // sees every getter invocation.
    template <typename Getter>
    static void Property(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> inst, const char* name, Getter getter) {
        const v8::AccessorNameGetterCallback getterFn = +getter;
        auto* accessors = script::InternAccessors(BindingName(name), getterFn, nullptr);
        inst->SetNativeDataProperty(convert::ToJS(isolate, name), &script::PropertyGetterTrampoline, nullptr,
                                    v8::External::New(isolate, accessors, v8::kExternalPointerTypeTagDefault));
    }

    // Read-write property with getter and setter lambdas.
    template <typename Getter, typename Setter>
    static void Property(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> inst, const char* name, Getter getter,
                         Setter setter) {
        const v8::AccessorNameGetterCallback getterFn = +getter;
        const v8::AccessorNameSetterCallbackV2 setterFn = +setter;
        auto* accessors = script::InternAccessors(BindingName(name), getterFn, setterFn);
        inst->SetNativeDataProperty(convert::ToJS(isolate, name), &script::PropertyGetterTrampoline,
                                    &script::PropertySetterTrampoline,
                                    v8::External::New(isolate, accessors, v8::kExternalPointerTypeTagDefault));
    }

    // Instance method. Wrapped via MethodTrampoline; user's function pointer
    // travels through V8's `data` slot.
    template <typename Func>
    static void Method(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> proto, const char* name, Func func) {
        const v8::FunctionCallback fnPtr = +func;
        auto data = v8::External::New(isolate, script::InternFunction(BindingName(name), fnPtr),
                                      v8::kExternalPointerTypeTagDefault);
        proto->Set(isolate, name, v8::FunctionTemplate::New(isolate, &script::MethodTrampoline, data), v8::DontEnum);
    }

    // Static method on the constructor function.
    template <typename Func>
    static void StaticMethod(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl, const char* name, Func func) {
        const v8::FunctionCallback fnPtr = +func;
        auto data = v8::External::New(isolate, script::InternFunction(BindingName(name), fnPtr),
                                      v8::kExternalPointerTypeTagDefault);
        tpl->Set(isolate, name, v8::FunctionTemplate::New(isolate, &script::MethodTrampoline, data), v8::DontEnum);
    }
};

// Helper macro for common constructor pattern
// Uses the ClassName constexpr from ClassBase<Derived, NativeType>
#define V8_CLASS_CTOR_PROLOGUE                                                                              \
    auto* isolate = args.GetIsolate();                                                                      \
    if (!args.IsConstructCall()) {                                                                          \
        error::ThrowTypeError(isolate, std::string(ClassName) + " must be called with 'new'"); /* NOLINT */ \
        return;                                                                                             \
    }

// Helper macro for classes that should not be directly constructed
// These classes are obtained via global functions (e.g., getUnit(), getArea())
#define V8_CLASS_NOT_CONSTRUCTABLE                                                                 \
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args) {                             \
        auto* isolate = args.GetIsolate();                                                         \
        error::ThrowError(isolate, std::string(ClassName) + " is not constructable"); /* NOLINT */ \
        return;                                                                                    \
    }

}  // namespace d2bs::runtime::api
