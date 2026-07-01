#pragma once

#include <v8.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "V8Convert.h"
#include "V8Error.h"
#include "V8InstanceTracker.h"
#include "components/script/NativeCallHook.h"

namespace d2bs::api {

// CRTP base template for V8 class bindings
// Derived classes must provide:
//   - static constexpr std::string_view ClassName
//   - static void New(const v8::FunctionCallbackInfo<v8::Value>& args)
//   - static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl)

template <typename Derived, typename NativeType>
class V8ClassBase {
    struct TemplateCache {
        std::unordered_map<v8::Isolate*, v8::Global<v8::FunctionTemplate>> templates;
        std::mutex mutex;
    };

    static TemplateCache& GetCache() {
        static TemplateCache cache;
        return cache;
    }

    // V8 weak callbacks require a two-pass mechanism:
    // - First pass: Reset handle and schedule second pass (no other V8 API calls allowed)
    // - Second pass: Actual cleanup - delete native struct and callback data
    // TeardownIsolate calls LowMemoryNotification() before disposal to ensure
    // all weak callbacks fire and native data is properly freed.
    static void MakeWeak(v8::Isolate* isolate, v8::Local<v8::Object> obj, NativeType* ptr) {
        struct WeakCallbackData {
            v8::Global<v8::Object> handle;
            NativeType* native;
        };

        auto* data = new WeakCallbackData{v8::Global<v8::Object>(isolate, obj), ptr};

        data->handle.SetWeak(
            data,
            [](const v8::WeakCallbackInfo<WeakCallbackData>& info) {
                info.GetParameter()->handle.Reset();
                info.SetSecondPassCallback([](const v8::WeakCallbackInfo<WeakCallbackData>& callbackInfo) {
                    auto* d = callbackInfo.GetParameter();
                    delete d->native;
                    delete d;
                    V8InstanceTracker::Instance().Decrement(Derived::ClassName);
                });
            },
            v8::WeakCallbackType::kParameter);
    }

   public:
    // Returns (or creates) the cached FunctionTemplate for this isolate.
    static v8::Local<v8::FunctionTemplate> GetTemplate(v8::Isolate* isolate) {
        auto& cache = GetCache();
        std::scoped_lock lock(cache.mutex);

        auto it = cache.templates.find(isolate);
        if (it == cache.templates.end()) {
            auto tpl = v8::FunctionTemplate::New(isolate, Derived::New);
            tpl->SetClassName(v8_convert::ToV8(isolate, Derived::ClassName));
            tpl->InstanceTemplate()->SetInternalFieldCount(1);
            Derived::ConfigureTemplate(isolate, tpl);
            cache.templates.emplace(isolate, v8::Global<v8::FunctionTemplate>(isolate, tpl));
            return tpl;
        }
        return it->second.Get(isolate);
    }

    // Clear cached template for this isolate (call before disposal).
    static void ClearCache(v8::Isolate* isolate) {
        auto& cache = GetCache();
        std::scoped_lock lock(cache.mutex);
        cache.templates.erase(isolate);
    }

    // Check if a value is an instance of this class
    static bool IsInstance(v8::Local<v8::Value> value) {
        if (value.IsEmpty() || !value->IsObject()) {
            return false;
        }
        auto* isolate = value.As<v8::Object>()->GetIsolate();
        return GetTemplate(isolate)->HasInstance(value);
    }

    // Unwrap native pointer from V8 object
    static NativeType* Unwrap(v8::Local<v8::Object> obj) {
        if (!IsInstance(obj) || obj->InternalFieldCount() < 1) {
            return nullptr;
        }
        return static_cast<NativeType*>(obj->GetAlignedPointerFromInternalField(0));
    }

    // Wrap native pointer into V8 object's internal field
    static void Wrap(v8::Local<v8::Object> obj, NativeType* ptr) { obj->SetAlignedPointerInInternalField(0, ptr); }

    // Initialize a V8 object with native data and weak GC callback (constructor path).
    static void InitInstance(v8::Isolate* isolate, v8::Local<v8::Object> obj, std::unique_ptr<NativeType> data) {
        V8InstanceTracker::Instance().Increment(Derived::ClassName);
        auto* raw = data.release();
        Wrap(obj, raw);
        MakeWeak(isolate, obj, raw);
    }

    // Create a new instance from heap-allocated data; on failure, data is freed automatically.
    static v8::Local<v8::Object> CreateInstance(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                                std::unique_ptr<NativeType> data) {
        v8::EscapableHandleScope scope(isolate);
        auto tpl = GetTemplate(isolate);
        auto maybeInstance = tpl->InstanceTemplate()->NewInstance(context);
        if (maybeInstance.IsEmpty())
            return {};
        auto obj = maybeInstance.ToLocalChecked();
        InitInstance(isolate, obj, std::move(data));
        return scope.Escape(obj);
    }

   protected:
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
        // Leaked on purpose - per-isolate template setup is bounded.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) - lifetime is isolate-bound, never freed
        auto* accessors = new js::script::PropertyAccessors{.getter = getterFn, .setter = nullptr};
        inst->SetNativeDataProperty(v8_convert::ToV8(isolate, name), &js::script::PropertyGetterTrampoline, nullptr,
                                    v8::External::New(isolate, accessors));
    }

    // Read-write property with getter and setter lambdas.
    template <typename Getter, typename Setter>
    static void Property(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> inst, const char* name, Getter getter,
                         Setter setter) {
        const v8::AccessorNameGetterCallback getterFn = +getter;
        const v8::AccessorNameSetterCallback setterFn = +setter;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) - lifetime is isolate-bound, never freed
        auto* accessors = new js::script::PropertyAccessors{.getter = getterFn, .setter = setterFn};
        inst->SetNativeDataProperty(v8_convert::ToV8(isolate, name), &js::script::PropertyGetterTrampoline,
                                    &js::script::PropertySetterTrampoline, v8::External::New(isolate, accessors));
    }

    // Instance method. Wrapped via MethodTrampoline; user's function pointer
    // travels through V8's `data` slot.
    template <typename Func>
    static void Method(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> proto, const char* name, Func func) {
        const v8::FunctionCallback fnPtr = +func;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - function-pointer through External void*
        auto data = v8::External::New(isolate, reinterpret_cast<void*>(fnPtr));
        proto->Set(isolate, name, v8::FunctionTemplate::New(isolate, &js::script::MethodTrampoline, data),
                   v8::DontEnum);
    }

    // Static method on the constructor function.
    template <typename Func>
    static void StaticMethod(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl, const char* name, Func func) {
        const v8::FunctionCallback fnPtr = +func;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - function-pointer through External void*
        auto data = v8::External::New(isolate, reinterpret_cast<void*>(fnPtr));
        tpl->Set(isolate, name, v8::FunctionTemplate::New(isolate, &js::script::MethodTrampoline, data), v8::DontEnum);
    }
};

// Helper macro for common constructor pattern
// Uses the ClassName constexpr from V8ClassBase<Derived, NativeType>
#define V8_CLASS_CTOR_PROLOGUE                                                                                 \
    auto* isolate = args.GetIsolate();                                                                         \
    if (!args.IsConstructCall()) {                                                                             \
        v8_error::ThrowTypeError(isolate, std::string(ClassName) + " must be called with 'new'"); /* NOLINT */ \
        return;                                                                                                \
    }

// Helper macro for classes that should not be directly constructed
// These classes are obtained via global functions (e.g., getUnit(), getArea())
#define V8_CLASS_NOT_CONSTRUCTABLE                                                                    \
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args) {                                \
        auto* isolate = args.GetIsolate();                                                            \
        v8_error::ThrowError(isolate, std::string(ClassName) + " is not constructable"); /* NOLINT */ \
        return;                                                                                       \
    }

}  // namespace d2bs::api
