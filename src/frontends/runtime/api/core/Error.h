#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <spdlog/logger.h>

#include "components/script/ScriptLogger.h"
#include "unibind/unibind.h"

// Throwing from a binding, and the argument checks that throw. A throw takes effect when the
// binding returns to the engine, so return promptly after one.

namespace d2bs::api::error {

inline void ThrowError(ub::Isolate& isolate, std::string_view message) {
    ub::Throw(isolate, ub::ErrorKind::Error, message);
}

inline void ThrowTypeError(ub::Isolate& isolate, std::string_view message) {
    ub::Throw(isolate, ub::ErrorKind::TypeError, message);
}

inline void ThrowRangeError(ub::Isolate& isolate, std::string_view message) {
    ub::Throw(isolate, ub::ErrorKind::RangeError, message);
}

inline void WarnAndReturnFalse(const ub::CallbackInfo& args, std::string_view message) {
    GetLogger(&args.GetIsolate())->warn("{}", message);
    args.GetReturnValue().SetFalse();
}

inline void ReportError(const ub::CallbackInfo& args, std::string_view message) {
    GetLogger(&args.GetIsolate())->error("{}", message);
}

inline bool CheckArgCount(const ub::CallbackInfo& args, uint32_t minArgs, const char* funcName = nullptr) {
    if (args.Length() < minArgs) {
        const std::string msg = funcName != nullptr ? std::string(funcName) + " requires at least " +
                                                          std::to_string(minArgs) + " argument(s)"
                                                    : "Not enough arguments";
        args.ThrowTypeError(msg);
        return false;
    }
    return true;
}

inline bool CheckIsNumber(const ub::CallbackInfo& args, uint32_t index, const char* argName = nullptr) {
    if (args.Length() <= index || !args[index].IsNumber()) {
        args.ThrowTypeError(argName != nullptr ? std::string(argName) + " must be a number"
                                               : "Argument must be a number");
        return false;
    }
    return true;
}

inline bool CheckIsString(const ub::CallbackInfo& args, uint32_t index, const char* argName = nullptr) {
    if (args.Length() <= index || !args[index].IsString()) {
        args.ThrowTypeError(argName != nullptr ? std::string(argName) + " must be a string"
                                               : "Argument must be a string");
        return false;
    }
    return true;
}

inline bool CheckIsFunction(const ub::CallbackInfo& args, uint32_t index, const char* argName = nullptr) {
    if (args.Length() <= index || !args[index].IsFunction()) {
        args.ThrowTypeError(argName != nullptr ? std::string(argName) + " must be a function"
                                               : "Argument must be a function");
        return false;
    }
    return true;
}

}  // namespace d2bs::api::error
