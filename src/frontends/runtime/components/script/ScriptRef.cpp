#include "ScriptRef.h"

#include <spdlog/logger.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "Commands.h"
#include "Script.h"
#include "ScriptLogger.h"
#include "api/core/Convert.h"
#include "components/events/BaseEvent.h"
#include "game/Console.h"

namespace d2bs::js::script {

void ReportOffThreadRelease() {
    // No isolate: a reference can outlive the one it came from, and reading a
    // disposed isolate to report a bug would be a worse bug.
    GetLogger()->error(
        "a script function reference was released off its owning script's thread - harmless on V8, but it would "
        "corrupt the root list of an engine that tracks roots per thread");
}

namespace {

// Turns one described argument into a V8 value. An empty handle means the value
// could not be made and the argument is dropped, which only the structured-clone
// case can produce.
v8::Local<v8::Value> ToEngineValue(v8::Isolate* isolate, const Value& value) {
    return std::visit(
        [isolate](const auto& held) -> v8::Local<v8::Value> {
            using HeldT = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<HeldT, Bytes>) {
                auto arrayBuffer = v8::ArrayBuffer::New(isolate, held.data.size());
                std::copy_n(held.data.data(), held.data.size(),
                            static_cast<uint8_t*>(arrayBuffer->GetBackingStore()->Data()));
                return v8::Uint8Array::New(arrayBuffer, 0, held.data.size());
            } else if constexpr (std::is_same_v<HeldT, Serialized>) {
                auto context = isolate->GetCurrentContext();
                v8::ValueDeserializer deserializer(isolate, held.data.data(), held.data.size());
                if (!deserializer.ReadHeader(context).FromMaybe(false)) {
                    GetLogger(isolate)->critical("Failed to read broadcast event header");
                    return {};
                }
                v8::Local<v8::Value> deserialized;
                if (!deserializer.ReadValue(context).ToLocal(&deserialized)) {
                    GetLogger(isolate)->critical("Failed to deserialize broadcast event argument");
                    return {};
                }
                return deserialized;
            } else {
                return api::v8_convert::ToV8(isolate, held);
            }
        },
        value);
}

std::vector<v8::Local<v8::Value>> ToEngineValues(v8::Isolate* isolate, const CallArgs& args) {
    std::vector<v8::Local<v8::Value>> values;
    values.reserve(args.Values().size());
    for (const auto& value : args.Values()) {
        if (auto converted = ToEngineValue(isolate, value); !converted.IsEmpty()) {
            values.push_back(converted);
        }
    }
    return values;
}

// Calls one handler and reports whether it voted to block. Each handler gets its
// own TryCatch, so an exception from one neither reaches the caller nor stops
// the handlers after it.
bool CallHandler(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Function> function,
                 std::vector<v8::Local<v8::Value>>& args, std::string_view name) {
    if (function.IsEmpty() || !function->IsFunction()) {
        return false;
    }

    v8::TryCatch tryCatch(isolate);
    bool voted = false;
    v8::Local<v8::Value> returnValue;
    if (function->Call(context, context->Global(), static_cast<int32_t>(args.size()), args.data())
            .ToLocal(&returnValue)) {
        voted = returnValue->BooleanValue(isolate);
    }
    if (tryCatch.HasCaught()) {
        auto message = tryCatch.Message();
        if (!message.IsEmpty()) {
            v8::String::Utf8Value errorStr(isolate, message->Get());
            GetLogger(isolate)->error("[{}] handler exception: {}", name, std::string(*errorStr, errorStr.length()));
        }
    }
    return voted;
}

void ReportEvaluation(v8::Isolate* isolate, game::console::MessageLevel level, v8::Local<v8::Value> value) {
    v8::String::Utf8Value text(isolate, value);
    game::console::OnMessage({
        .source = game::console::MessageSource::EvaluateResult,
        .name = std::string{COMMAND_LINE_NAME},
        .level = level,
        .text = std::string(*text, text.length()),
    });
}

}  // namespace

std::vector<std::vector<uint8_t>> SerializeArgs(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto* isolate = args.GetIsolate();
    std::vector<std::vector<uint8_t>> values;
    values.reserve(args.Length());
    for (int32_t i = 0; i < args.Length(); i++) {
        v8::ValueSerializer serializer(isolate);
        serializer.WriteHeader();
        if (!serializer.WriteValue(isolate->GetCurrentContext(), args[i]).FromMaybe(false)) {
            // Non-serializable value (function, symbol, etc.) - insert undefined
            // as placeholder to preserve argument positions.
            v8::ValueSerializer undefinedSerializer(isolate);
            undefinedSerializer.WriteHeader();
            undefinedSerializer.WriteValue(isolate->GetCurrentContext(), v8::Undefined(isolate)).Check();
            auto [udData, udSize] = undefinedSerializer.Release();
            values.emplace_back(udData, udData + udSize);
            std::free(udData);  // NOLINT(cppcoreguidelines-no-malloc)
            continue;
        }
        auto [data, size] = serializer.Release();
        values.emplace_back(data, data + size);
        std::free(data);  // NOLINT(cppcoreguidelines-no-malloc) - V8's ValueSerializer allocates with realloc()
    }
    return values;
}

bool Invocation::Run(const BaseEvent& event) {
    v8::HandleScope scope(isolate_);
    CallArgs args;
    event.MakeArgs(args);
    auto values = ToEngineValues(isolate_, args);
    auto context = isolate_->GetCurrentContext();

    bool voted = false;
    for (const auto& handler : handlers_) {
        if (CallHandler(isolate_, context, handler, values, event.Name())) {
            voted = true;
        }
    }
    return voted;
}

bool Invocation::Run(const BaseEvent& event, const Ref& function) {
    if (function.IsEmpty()) {
        return false;
    }
    if (function.isolate_ != isolate_) {
        GetLogger(isolate_)->error("[{}] handler belongs to another script", event.Name());
        return false;
    }
    v8::HandleScope scope(isolate_);
    return Call(event, function.function_.Get(isolate_));
}

bool Invocation::Run(const BaseEvent& event, const drawing::Drawable& drawable, DrawableHandler which) {
    v8::HandleScope scope(isolate_);
    v8::Local<v8::Function> function;
    if (!script_->GetDrawableHandler(drawable, which).ToLocal(&function)) {
        return false;
    }
    return Call(event, function);
}

bool Invocation::Call(const BaseEvent& event, v8::Local<v8::Function> function) {
    v8::HandleScope scope(isolate_);
    CallArgs args;
    event.MakeArgs(args);
    auto values = ToEngineValues(isolate_, args);
    return CallHandler(isolate_, isolate_->GetCurrentContext(), function, values, event.Name());
}

void Invocation::Evaluate(std::string_view code) {
    v8::HandleScope scope(isolate_);
    v8::TryCatch tryCatch(isolate_);

    auto context = isolate_->GetCurrentContext();
    auto source = api::v8_convert::ToV8(isolate_, code);

    v8::ScriptOrigin origin(api::v8_convert::ToV8(isolate_, COMMAND_LINE_NAME));
    v8::Local<v8::Script> snippet;
    v8::Local<v8::Value> result;
    if (v8::Script::Compile(context, source, &origin).ToLocal(&snippet) && snippet->Run(context).ToLocal(&result)) {
        if (!result->IsUndefined()) {
            ReportEvaluation(isolate_, game::console::MessageLevel::Info, result);
        }
    }
    if (tryCatch.HasCaught()) {
        auto message = tryCatch.Message();
        if (!message.IsEmpty()) {
            ReportEvaluation(isolate_, game::console::MessageLevel::Error, message->Get());
        }
    }
}

}  // namespace d2bs::js::script
