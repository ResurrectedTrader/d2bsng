#pragma once

#include <string>
#include <string_view>

#include <spdlog/logger.h>
#include <v8.h>

#include "V8Convert.h"
#include "components/script/ScriptLogger.h"

// Error and warning utilities for V8

namespace d2bs::api::v8_error {

// Throw a generic Error
inline void ThrowError(v8::Isolate* isolate, std::string_view message) {
    isolate->ThrowException(v8::Exception::Error(v8_convert::ToV8(isolate, message)));
}

// Throw a TypeError (wrong argument type)
inline void ThrowTypeError(v8::Isolate* isolate, std::string_view message) {
    isolate->ThrowException(v8::Exception::TypeError(v8_convert::ToV8(isolate, message)));
}

// Throw a RangeError (value out of range)
inline void ThrowRangeError(v8::Isolate* isolate, std::string_view message) {
    isolate->ThrowException(v8::Exception::RangeError(v8_convert::ToV8(isolate, message)));
}

// Log a warning and set return value to false (caller must return afterward).
inline void WarnAndReturnFalse(const v8::FunctionCallbackInfo<v8::Value>& args, std::string_view message) {
    GetLogger(args.GetIsolate())->warn("{}", message);
    args.GetReturnValue().SetFalse();
}

// Log an error without throwing a JS exception (caller must return afterward).
inline void ReportError(const v8::FunctionCallbackInfo<v8::Value>& args, std::string_view message) {
    GetLogger(args.GetIsolate())->error("{}", message);
}

// ============================================================================
// Argument validation helpers
// ============================================================================

// Check minimum argument count, throw if not met
inline bool CheckArgCount(const v8::FunctionCallbackInfo<v8::Value>& args, int32_t minArgs,
                          const char* funcName = nullptr) {
    if (args.Length() < minArgs) {
        auto* isolate = args.GetIsolate();
        std::string msg = funcName
                              ? std::string(funcName) + " requires at least " + std::to_string(minArgs) + " argument(s)"
                              : "Not enough arguments";
        ThrowTypeError(isolate, msg);
        return false;
    }
    return true;
}

// Check argument is a number
inline bool CheckIsNumber(const v8::FunctionCallbackInfo<v8::Value>& args, int32_t index,
                          const char* argName = nullptr) {
    if (args.Length() <= index || !args[index]->IsNumber()) {
        auto* isolate = args.GetIsolate();
        std::string msg = argName ? std::string(argName) + " must be a number" : "Argument must be a number";
        ThrowTypeError(isolate, msg);
        return false;
    }
    return true;
}

// Check argument is a string
inline bool CheckIsString(const v8::FunctionCallbackInfo<v8::Value>& args, int32_t index,
                          const char* argName = nullptr) {
    if (args.Length() <= index || !args[index]->IsString()) {
        auto* isolate = args.GetIsolate();
        std::string msg = argName ? std::string(argName) + " must be a string" : "Argument must be a string";
        ThrowTypeError(isolate, msg);
        return false;
    }
    return true;
}

// Check argument is a function
inline bool CheckIsFunction(const v8::FunctionCallbackInfo<v8::Value>& args, int32_t index,
                            const char* argName = nullptr) {
    if (args.Length() <= index || !args[index]->IsFunction()) {
        auto* isolate = args.GetIsolate();
        std::string msg = argName ? std::string(argName) + " must be a function" : "Argument must be a function";
        ThrowTypeError(isolate, msg);
        return false;
    }
    return true;
}

}  // namespace d2bs::api::v8_error
