#include "JSDirectory.h"

#include <Shlwapi.h>

#include "utils/utils.h"

#pragma comment(lib, "shlwapi.lib")

namespace d2bs::runtime::api::classes::directory_detail {

bool IsValidSearchPattern(std::string_view pattern) {
    if (pattern.find_first_of("\\/:") != std::string_view::npos) {
        return false;
    }
    return pattern.empty() || pattern.find_first_not_of(". ") != std::string_view::npos;
}

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

}  // namespace d2bs::runtime::api::classes::directory_detail
