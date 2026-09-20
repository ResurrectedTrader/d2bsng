#pragma once

#include <string>

#include "scripting/Args.h"

namespace d2bs::api::globals {

// Argument checks for bindings written against the scripting contract, wording
// identical to the V8-shaped helpers in api/core/V8Error.h. Scripts string-match
// these messages, so the text is the contract, not the intent.

inline bool CheckArgCount(script::Args& args, size_t minArgs, const char* funcName = nullptr) {
    if (args.Count() < minArgs) {
        args.Throw(script::ErrorKind::TypeError,
                   funcName ? std::string(funcName) + " requires at least " + std::to_string(minArgs) + " argument(s)"
                            : std::string("Not enough arguments"));
        return false;
    }
    return true;
}

inline bool CheckIsNumber(script::Args& args, size_t index, const char* argName = nullptr) {
    if (args.Count() <= index || !args.IsNumber(index)) {
        args.Throw(script::ErrorKind::TypeError,
                   argName ? std::string(argName) + " must be a number" : std::string("Argument must be a number"));
        return false;
    }
    return true;
}

inline bool CheckIsString(script::Args& args, size_t index, const char* argName = nullptr) {
    if (args.Count() <= index || !args.IsString(index)) {
        args.Throw(script::ErrorKind::TypeError,
                   argName ? std::string(argName) + " must be a string" : std::string("Argument must be a string"));
        return false;
    }
    return true;
}

}  // namespace d2bs::api::globals
