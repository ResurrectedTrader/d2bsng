#pragma once

#include <algorithm>
#include <filesystem>
#include <string>

namespace d2bs::config {

// Semantics differ by context:
//   ProfileData: overrides - empty values = inherit from AppConfig.
//     basePath may be relative (joined to install dir) or absolute (replaces).
//   AppConfig: fully resolved runtime values - basePath is absolute,
//     script names are non-empty (populated from [settings] defaults).
struct ScriptPaths {
    std::filesystem::path basePath;
    std::string starterScript;
    std::string gameScript;
    std::string consoleScript;

    // `absolutePath` made relative to basePath, falling back to its bare filename
    // when it isn't under the base (or basePath is unset). The display form used
    // for script names, log lines, and the URLs the inspector shows DevTools.
    [[nodiscard]] std::string RelativeScriptPath(const std::filesystem::path& absolutePath) const {
        if (!basePath.empty()) {
            auto rel = std::filesystem::relative(absolutePath, basePath);
            if (!rel.empty() && rel.native()[0] != L'.') {
                return rel.string();
            }
        }
        return absolutePath.filename().string();
    }

    // RelativeScriptPath as a file:// URL ("file:///" + forward slashes), the
    // form DevTools binds breakpoints against and the url AttachInspector
    // advertises for each target.
    [[nodiscard]] std::string FileUrl(const std::filesystem::path& absolutePath) const {
        std::string rel = RelativeScriptPath(absolutePath);
        std::ranges::replace(rel, '\\', '/');
        return "file:///" + rel;
    }
};

}  // namespace d2bs::config
