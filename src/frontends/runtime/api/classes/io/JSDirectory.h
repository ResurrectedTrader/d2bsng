#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "config/AppConfig.h"

namespace d2bs::api::classes {

// Helpers defined in JSDirectory.cpp
namespace directory_detail {
std::vector<std::string> ListFiles(const std::filesystem::path& fullPath, const std::string& pattern);
std::vector<std::string> ListFolders(const std::filesystem::path& fullPath, const std::string& pattern);
void ReturnNames(const ub::CallbackInfo& args, const std::vector<std::string>& names);
}  // namespace directory_detail

// Internal directory data structure
struct DirectoryData {
    std::filesystem::path path;  // Directory name/path relative to scripts folder

    explicit DirectoryData(const std::filesystem::path& dirPath) : path(dirPath) {}
};

// JSDirectory - script wrapper for directory operations
class JSDirectory : public ClassBase<JSDirectory, DirectoryData> {
   public:
    static constexpr std::string_view ClassName = "Folder";

    // Directory objects are created via dopen() or Directory.create(), not direct construction
    static void Configure(const ub::Class<DirectoryData>& cls) {
        /// @description Directory name/path stored on this Folder, relative to the scripts folder.
        /// @type {string}
        Property(
            cls, "name", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto& isolate = info.GetIsolate();
                auto* data = Unwrap(info.This());

                if (!data) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, ""));
                    return;
                }

                info.GetReturnValue().Set(convert::ToJS(isolate, data->path.string()));
            });

        /// @description Creates a subdirectory under this directory.
        /// @signature create(name: string)
        /// @param name {string} - Name of the subdirectory to create, relative to this directory.
        /// @returns {Folder} - New Folder for the created subdirectory; throws on empty/invalid name or create failure.
        /// @throws {Error} - When name is empty.
        /// @throws {Error} - When the resolved path is invalid.
        /// @throws {Error} - When the directory could not be created.
        Method(
            cls, "create", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                const auto& context = args.GetContext();
                auto* data = Unwrap(args.This());

                if (!data) {
                    error::ThrowError(isolate, "Invalid directory object");
                    return;
                }

                if (!error::CheckArgCount(args, 1, "Directory.create")) {
                    return;
                }

                if (!args[0].IsString()) {
                    error::ThrowTypeError(isolate, "No path passed to dir.create()");
                    return;
                }

                std::string name = convert::ToString(context, args[0]);

                if (name.empty()) {
                    error::ThrowError(isolate, "Invalid directory name");
                    return;
                }

                auto relativePath = (data->path / name).string();
                auto fullPath = config::GetPathRelScript(relativePath);
                if (fullPath.empty()) {
                    error::ThrowError(isolate, "Invalid directory path");
                    return;
                }

                std::error_code ec;
                std::filesystem::create_directory(fullPath, ec);
                if (ec) {
                    auto msg = "Couldn't create directory " + name + ", path '" + fullPath.string() + "' not found";
                    error::ThrowError(isolate, msg);
                    return;
                }

                // Create and return new Directory object for the subdirectory
                // Store just the subdirectory name (not full relative path) matching reference behavior
                auto newDir = Wrap(context, std::make_shared<DirectoryData>(name));
                if (!newDir) {
                    return;
                }
                args.GetReturnValue().Set(*newDir);
            });

        /// @description Removes this directory, which must be empty.
        /// @signature remove()
        /// @returns {boolean} - true on successful removal; throws on invalid path, non-empty directory, or failure.
        /// @throws {Error} - When the resolved path is invalid.
        /// @throws {Error} - When the directory is not empty or is the current working directory.
        /// @throws {Error} - When removal fails.
        /// @throws {Error} - When the path is not found.
        Method(
            cls, "remove", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                auto* data = Unwrap(args.This());

                if (!data) {
                    error::ThrowError(isolate, "Invalid directory object");
                    return;
                }

                auto fullPath = config::GetPathRelScript(data->path.string());
                if (fullPath.empty()) {
                    error::ThrowError(isolate, "Invalid directory path");
                    return;
                }

                std::error_code ec;
                bool removed = std::filesystem::remove(fullPath, ec);
                if (ec) {
                    if (ec == std::errc::directory_not_empty) {
                        error::ThrowError(
                            isolate,
                            "Tried to delete directory, but it is not empty or is the current working directory");
                    } else {
                        auto msg = "Failed to remove directory: " + ec.message();
                        error::ThrowError(isolate, msg);
                    }
                    return;
                }
                if (!removed) {
                    error::ThrowError(isolate, "Path not found");
                    return;
                }

                args.GetReturnValue().Set(true);
            });

        /// @description Lists file names in this directory matching a glob pattern.
        /// @signature getFiles(pattern?: string)
        /// @param pattern {string} - Optional glob pattern to match files against; defaults to "*.*".
        /// @returns {string[]} - Matching file names; empty array when the object or path is invalid.
        Method(
            cls, "getFiles", +[](const ub::CallbackInfo& args) {
                const auto& context = args.GetContext();
                auto* data = Unwrap(args.This());

                if (!data) {
                    directory_detail::ReturnNames(args, {});
                    return;
                }

                if (args.Length() > 1) {
                    return;
                }

                // Default pattern matches reference behavior (PathMatchSpecW)
                std::string pattern = "*.*";
                if (args.Length() > 0) {
                    pattern = convert::ToString(context, args[0]);
                }

                auto fullPath = config::GetPathRelScript(data->path.string());
                if (fullPath.empty()) {
                    directory_detail::ReturnNames(args, {});
                    return;
                }

                directory_detail::ReturnNames(args, directory_detail::ListFiles(fullPath, pattern));
            });

        /// @description Lists subdirectory names in this directory matching a glob pattern.
        /// @signature getFolders(pattern?: string)
        /// @param pattern {string} - Optional glob pattern to match folders against; defaults to "*.*".
        /// @returns {string[]} - Matching subdirectory names; empty array when the object or path is invalid.
        Method(
            cls, "getFolders", +[](const ub::CallbackInfo& args) {
                const auto& context = args.GetContext();
                auto* data = Unwrap(args.This());

                if (!data) {
                    directory_detail::ReturnNames(args, {});
                    return;
                }

                if (args.Length() > 1) {
                    return;
                }

                // Default pattern matches reference behavior (PathMatchSpecW)
                std::string pattern = "*.*";
                if (args.Length() > 0) {
                    pattern = convert::ToString(context, args[0]);
                }

                auto fullPath = config::GetPathRelScript(data->path.string());
                if (fullPath.empty()) {
                    directory_detail::ReturnNames(args, {});
                    return;
                }

                directory_detail::ReturnNames(args, directory_detail::ListFolders(fullPath, pattern));
            });
    }
};

}  // namespace d2bs::api::classes
