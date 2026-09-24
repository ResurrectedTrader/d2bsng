#include "JSFileTools.h"

#include <filesystem>
#include <mutex>
#include <string>

#include <fmt/format.h>

#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "config/AppConfig.h"

namespace d2bs::api::classes::filetools_detail {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex fileMutex;

std::filesystem::path ResolveScriptPath(ub::Isolate& isolate, const std::string& relativePath, const char* errorMsg) {
    auto fullPath = config::GetPathRelScript(relativePath);
    if (fullPath.empty()) {
        error::ThrowError(isolate, errorMsg);
    }
    return fullPath;
}

FILE* FileOpenRelScript(ub::Isolate& isolate, const std::string& relativePath, const wchar_t* mode) {
    auto fullPath = config::GetPathRelScript(relativePath);
    if (fullPath.empty()) {
        error::ThrowError(isolate, "Invalid file name");
        return nullptr;
    }
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, fullPath.c_str(), mode) != 0 || fp == nullptr) {
        error::ThrowError(isolate, "Couldn't open file");
        return nullptr;
    }
    return fp;
}

std::string ValueToString(const ub::Context& context, const ub::Local<ub::Value>& value) {
    if (value.IsNullOrUndefined()) {
        // Reference writes sizeof(int) zero bytes for null/undefined
        return {sizeof(int32_t), '\0'};
    }
    if (value.IsNumber()) {
        if (value.IsInt32()) {
            return fmt::format("{}", convert::ToInt32(context, value));
        }
        return fmt::format("{:.16f}", convert::ToDouble(context, value));
    }
    return convert::ToString(context, value);
}

}  // namespace d2bs::api::classes::filetools_detail
