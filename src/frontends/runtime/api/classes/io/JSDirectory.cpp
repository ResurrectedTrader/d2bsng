#include "JSDirectory.h"

#include <Shlwapi.h>

#include "utils/utils.h"

#pragma comment(lib, "shlwapi.lib")

namespace d2bs::api::classes::directory_detail {

std::vector<std::string> ListFiles(const std::filesystem::path& fullPath, const std::string& pattern) {
    std::vector<std::string> results;
    std::error_code ec;

    auto iter = std::filesystem::directory_iterator(fullPath, ec);
    if (ec) {
        return results;
    }

    auto widePattern = utils::ToWStr(pattern);

    for (const auto& entry : iter) {
        if (entry.is_directory(ec)) {
            continue;
        }
        auto filename = entry.path().filename().wstring();
        if (PathMatchSpecW(filename.c_str(), widePattern.c_str())) {
            results.push_back(entry.path().filename().string());
        }
    }

    return results;
}

std::vector<std::string> ListFolders(const std::filesystem::path& fullPath, const std::string& pattern) {
    std::vector<std::string> results;
    std::error_code ec;

    auto iter = std::filesystem::directory_iterator(fullPath, ec);
    if (ec) {
        return results;
    }

    auto widePattern = utils::ToWStr(pattern);

    for (const auto& entry : iter) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        auto filename = entry.path().filename().wstring();
        if (PathMatchSpecW(filename.c_str(), widePattern.c_str())) {
            results.push_back(entry.path().filename().string());
        }
    }

    return results;
}

void ReturnNames(const ub::CallbackInfo& args, const std::vector<std::string>& names) {
    const auto& context = args.GetContext();
    auto arr = ub::Array::New(context, static_cast<uint32_t>(names.size()));
    if (!arr) {
        return;
    }
    for (uint32_t i = 0; i < names.size(); ++i) {
        auto name = ub::String::NewFromUtf8(args.GetIsolate(), names[i]);
        if (!name || !arr->Set(context, i, *name)) {
            return;
        }
    }
    args.GetReturnValue().Set(*arr);
}

}  // namespace d2bs::api::classes::directory_detail
