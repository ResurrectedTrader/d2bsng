#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <v8.h>

#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "components/config/AppConfig.h"

namespace d2bs::api::classes {

// Helpers defined in JSDirectory.cpp
namespace directory_detail {
std::vector<std::string> ListFiles(const std::filesystem::path& fullPath, const std::string& pattern);
std::vector<std::string> ListFolders(const std::filesystem::path& fullPath, const std::string& pattern);
}  // namespace directory_detail

// Internal directory data structure
struct DirectoryData {
    std::filesystem::path path;  // Directory name/path relative to scripts folder

    explicit DirectoryData(const std::filesystem::path& dirPath) : path(dirPath) {}
};

// JSDirectory - V8 wrapper for directory operations
class JSDirectory : public V8ClassBase<JSDirectory, DirectoryData> {
   public:
    static constexpr std::string_view ClassName = "Folder";

    // Directory objects are created via dopen() or Directory.create(), not direct construction
    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        auto proto = tpl->PrototypeTemplate();

        /// @description Directory name/path stored on this Folder, relative to the scripts folder.
        /// @type {string}
        Property(
            isolate, inst, "name", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data) {
                    info.GetReturnValue().SetEmptyString();
                    return;
                }

                info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->path.string()));
            });

        /// @description Creates a subdirectory under this directory.
        /// @signature create(name: string)
        /// @param name {string} - Name of the subdirectory to create, relative to this directory.
        /// @returns {Folder} - New Folder for the created subdirectory; throws on empty/invalid name or create failure.
        /// @throws {Error} - When name is empty.
        /// @throws {Error} - When the resolved path is invalid.
        /// @throws {Error} - When the directory could not be created.
        Method(
            isolate, proto, "create", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data) {
                    v8_error::ThrowError(isolate, "Invalid directory object");
                    return;
                }

                if (!v8_error::CheckArgCount(args, 1, "Directory.create")) {
                    return;
                }

                if (!args[0]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "No path passed to dir.create()");
                    return;
                }

                std::string name = v8_convert::ToString(isolate, args[0]);

                if (name.empty()) {
                    v8_error::ThrowError(isolate, "Invalid directory name");
                    return;
                }

                auto relativePath = (data->path / name).string();
                auto fullPath = d2bs::config::GetPathRelScript(relativePath);
                if (fullPath.empty()) {
                    v8_error::ThrowError(isolate, "Invalid directory path");
                    return;
                }

                std::error_code ec;
                std::filesystem::create_directory(fullPath, ec);
                if (ec) {
                    auto msg = "Couldn't create directory " + name + ", path '" + fullPath.string() + "' not found";
                    v8_error::ThrowError(isolate, msg);
                    return;
                }

                // Create and return new Directory object for the subdirectory
                // Store just the subdirectory name (not full relative path) matching reference behavior
                auto newData = std::make_unique<DirectoryData>(name);
                auto newDir = CreateInstance(isolate, context, std::move(newData));
                if (newDir.IsEmpty())
                    return;
                args.GetReturnValue().Set(newDir);
            });

        /// @description Removes this directory, which must be empty.
        /// @signature remove()
        /// @returns {boolean} - true on successful removal; throws on invalid path, non-empty directory, or failure.
        /// @throws {Error} - When the resolved path is invalid.
        /// @throws {Error} - When the directory is not empty or is the current working directory.
        /// @throws {Error} - When removal fails.
        /// @throws {Error} - When the path is not found.
        Method(
            isolate, proto, "remove", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data) {
                    v8_error::ThrowError(isolate, "Invalid directory object");
                    return;
                }

                auto fullPath = d2bs::config::GetPathRelScript(data->path.string());
                if (fullPath.empty()) {
                    v8_error::ThrowError(isolate, "Invalid directory path");
                    return;
                }

                std::error_code ec;
                bool removed = std::filesystem::remove(fullPath, ec);
                if (ec) {
                    if (ec == std::errc::directory_not_empty) {
                        v8_error::ThrowError(
                            isolate,
                            "Tried to delete directory, but it is not empty or is the current working directory");
                    } else {
                        auto msg = "Failed to remove directory: " + ec.message();
                        v8_error::ThrowError(isolate, msg);
                    }
                    return;
                }
                if (!removed) {
                    v8_error::ThrowError(isolate, "Path not found");
                    return;
                }

                args.GetReturnValue().Set(v8_convert::ToV8(isolate, true));
            });

        /// @description Lists file names in this directory matching a glob pattern.
        /// @signature getFiles(pattern?: string)
        /// @param pattern {string} - Optional glob pattern to match files against; defaults to "*.*".
        /// @returns {string[]} - Matching file names; empty array when the object or path is invalid.
        Method(
            isolate, proto, "getFiles", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data) {
                    args.GetReturnValue().Set(v8::Array::New(isolate, 0));
                    return;
                }

                if (args.Length() > 1) {
                    return;
                }

                // Default pattern matches reference behavior (PathMatchSpecW)
                std::string pattern = "*.*";
                if (args.Length() > 0) {
                    pattern = v8_convert::ToString(isolate, args[0]);
                }

                auto fullPath = d2bs::config::GetPathRelScript(data->path.string());
                if (fullPath.empty()) {
                    args.GetReturnValue().Set(v8::Array::New(isolate, 0));
                    return;
                }

                auto files = directory_detail::ListFiles(fullPath, pattern);

                auto arr = v8::Array::New(isolate, static_cast<int32_t>(files.size()));
                for (uint32_t i = 0; i < files.size(); ++i) {
                    arr->Set(context, i, v8_convert::ToV8(isolate, files[i])).Check();
                }
                args.GetReturnValue().Set(arr);
            });

        /// @description Lists subdirectory names in this directory matching a glob pattern.
        /// @signature getFolders(pattern?: string)
        /// @param pattern {string} - Optional glob pattern to match folders against; defaults to "*.*".
        /// @returns {string[]} - Matching subdirectory names; empty array when the object or path is invalid.
        Method(
            isolate, proto, "getFolders", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data) {
                    args.GetReturnValue().Set(v8::Array::New(isolate, 0));
                    return;
                }

                if (args.Length() > 1) {
                    return;
                }

                // Default pattern matches reference behavior (PathMatchSpecW)
                std::string pattern = "*.*";
                if (args.Length() > 0) {
                    pattern = v8_convert::ToString(isolate, args[0]);
                }

                auto fullPath = d2bs::config::GetPathRelScript(data->path.string());
                if (fullPath.empty()) {
                    args.GetReturnValue().Set(v8::Array::New(isolate, 0));
                    return;
                }

                auto folders = directory_detail::ListFolders(fullPath, pattern);

                auto arr = v8::Array::New(isolate, static_cast<int32_t>(folders.size()));
                for (uint32_t i = 0; i < folders.size(); ++i) {
                    arr->Set(context, i, v8_convert::ToV8(isolate, folders[i])).Check();
                }
                args.GetReturnValue().Set(arr);
            });
    }
};

}  // namespace d2bs::api::classes
