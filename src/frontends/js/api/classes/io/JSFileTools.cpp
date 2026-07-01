#include "JSFileTools.h"

#include <filesystem>
#include <mutex>
#include <string>

#include <fmt/format.h>

#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "config/AppConfig.h"

namespace d2bs::api::classes::filetools_detail {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex fileMutex;

std::filesystem::path ResolveScriptPath(v8::Isolate* isolate, const std::string& relativePath, const char* errorMsg) {
    auto fullPath = config::GetPathRelScript(relativePath);
    if (fullPath.empty()) {
        v8_error::ThrowError(isolate, errorMsg);
    }
    return fullPath;
}

FILE* FileOpenRelScript(v8::Isolate* isolate, const std::string& relativePath, const wchar_t* mode) {
    auto fullPath = config::GetPathRelScript(relativePath);
    if (fullPath.empty()) {
        v8_error::ThrowError(isolate, "Invalid file name");
        return nullptr;
    }
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, fullPath.c_str(), mode) != 0 || fp == nullptr) {
        v8_error::ThrowError(isolate, "Couldn't open file");
        return nullptr;
    }
    return fp;
}

std::string ValueToString(v8::Isolate* isolate, v8::Local<v8::Value> value) {
    if (value->IsNullOrUndefined()) {
        // Reference writes sizeof(int) zero bytes for null/undefined
        return {sizeof(int32_t), '\0'};
    }
    if (value->IsNumber()) {
        auto context = isolate->GetCurrentContext();
        if (value->IsInt32()) {
            int32_t ival = value->Int32Value(context).FromMaybe(0);
            return fmt::format("{}", ival);
        }
        double dval = value->NumberValue(context).FromMaybe(0.0);
        return fmt::format("{:.16f}", dval);
    }
    auto context = isolate->GetCurrentContext();
    v8::Local<v8::String> str;
    if (value->ToString(context).ToLocal(&str)) {
        return v8_convert::ToString(isolate, str);
    }
    return {};
}

}  // namespace d2bs::api::classes::filetools_detail
