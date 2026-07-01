#pragma once

#include <v8.h>

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "config/AppConfig.h"

namespace d2bs::api::classes {

// Helpers defined in JSFileTools.cpp (single definition, shared mutex)
namespace filetools_detail {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern std::mutex fileMutex;
std::filesystem::path ResolveScriptPath(v8::Isolate* isolate, const std::string& relativePath, const char* errorMsg);
FILE* FileOpenRelScript(v8::Isolate* isolate, const std::string& relativePath, const wchar_t* mode);
std::string ValueToString(v8::Isolate* isolate, v8::Local<v8::Value> value);
}  // namespace filetools_detail

// FileTools has no instance data - static utility class
struct FileToolsData {};

// JSFileTools - V8 wrapper for static file utility operations
// This class only has static methods, no instances are created
class JSFileTools : public V8ClassBase<JSFileTools, FileToolsData> {
   public:
    static constexpr std::string_view ClassName = "FileTools";

    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        /// @description Delete a file under the script base directory.
        /// @signature remove(path: string)
        /// @param path {string} - file name relative to the script base directory.
        /// @returns {undefined} - throws "Invalid file name" on bad path; a missing file is ignored.
        /// @throws {Error} - path is empty or resolves outside the script base directory.
        StaticMethod(
            isolate, tpl, "remove", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!v8_error::CheckArgCount(args, 1, "FileTools.remove")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "You must supply a file name");
                    return;
                }

                std::string path = v8_convert::ToString(isolate, args[0]);
                auto fullPath = filetools_detail::ResolveScriptPath(isolate, path, "Invalid file name");
                if (fullPath.empty()) {
                    return;
                }

                std::error_code ec;
                std::filesystem::remove(fullPath, ec);
            });

        /// @description Rename or move a file within the script base directory.
        /// @signature rename(oldName: string, newName: string)
        /// @param oldName {string} - existing file name relative to the script base directory.
        /// @param newName {string} - destination file name relative to the script base directory.
        /// @returns {undefined} - throws "Invalid original/new file name" on bad path; filesystem errors are ignored.
        /// @throws {Error} - oldName is empty or resolves outside the script base directory.
        /// @throws {Error} - newName is empty or resolves outside the script base directory.
        StaticMethod(
            isolate, tpl, "rename", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!v8_error::CheckArgCount(args, 2, "FileTools.rename")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "You must supply an original file name");
                    return;
                }
                if (!args[1]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "You must supply a new file name");
                    return;
                }

                std::string oldName = v8_convert::ToString(isolate, args[0]);
                std::string newName = v8_convert::ToString(isolate, args[1]);

                auto oldPath = filetools_detail::ResolveScriptPath(isolate, oldName, "Invalid original file name");
                if (oldPath.empty()) {
                    return;
                }
                auto newPath = filetools_detail::ResolveScriptPath(isolate, newName, "Invalid new file name");
                if (newPath.empty()) {
                    return;
                }

                std::error_code ec;
                std::filesystem::rename(oldPath, newPath, ec);
            });

        /// @description Copy a file within the script base directory (text-mode, so CRLF/LF translation may apply).
        /// @signature copy(original: string, copy: string, skipIfExists?: boolean)
        /// @param original {string} - source file name relative to the script base directory.
        /// @param copy {string} - destination file name relative to the script base directory.
        /// @param skipIfExists {boolean} - optional, default false; when true, skip the copy if the destination exists.
        /// @returns {undefined} - throws "Invalid new file name", "Couldn't open file", or "File copy failed".
        /// @throws {Error} - original or copy is empty or resolves outside the script base directory.
        /// @throws {Error} - the source or destination file cannot be opened.
        /// @throws {Error} - copying fails partway (a read or write error occurs).
        StaticMethod(
            isolate, tpl, "copy", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!v8_error::CheckArgCount(args, 2, "FileTools.copy")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "You must supply an original file name");
                    return;
                }
                if (!args[1]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "You must supply a new file name");
                    return;
                }

                std::string original = v8_convert::ToString(isolate, args[0]);
                std::string copyName = v8_convert::ToString(isolate, args[1]);
                bool skipIfExists = v8_convert::ToBool(isolate, args.Length() > 2 ? args[2] : v8::Local<v8::Value>());

                auto dstPath = filetools_detail::ResolveScriptPath(isolate, copyName, "Invalid new file name");
                if (dstPath.empty()) {
                    return;
                }

                // Reference: if "overwrite" is true and dest exists, skip (inverted semantic)
                std::error_code ec;
                if (skipIfExists && std::filesystem::exists(dstPath, ec)) {
                    return;
                }

                // Text-mode char-by-char copy matching reference fgetc/fputc behavior
                FILE* src = filetools_detail::FileOpenRelScript(isolate, original, L"r");
                if (src == nullptr) {
                    return;
                }
                FILE* dst = filetools_detail::FileOpenRelScript(isolate, copyName, L"w");
                if (dst == nullptr) {
                    fclose(src);
                    return;
                }

                bool success = true;
                while (!feof(src)) {
                    int32_t ch = fgetc(src);
                    if (ferror(src)) {
                        success = false;
                        break;
                    }
                    if (!feof(src)) {
                        fputc(ch, dst);
                        if (ferror(dst)) {
                            success = false;
                            break;
                        }
                    }
                }

                fflush(dst);
                fclose(dst);
                fclose(src);

                if (!success) {
                    std::error_code removeEc;
                    std::filesystem::remove(dstPath, removeEc);
                    v8_error::ThrowError(isolate, "File copy failed");
                    return;
                }
            });

        /// @description Test whether a file or directory exists under the script base directory.
        /// @signature exists(path: string)
        /// @param path {string} - file name relative to the script base directory.
        /// @returns {boolean} - true if the resolved path exists; throws "Invalid file name" on bad path.
        /// @throws {Error} - path is empty or resolves outside the script base directory.
        StaticMethod(
            isolate, tpl, "exists", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!v8_error::CheckArgCount(args, 1, "FileTools.exists")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "Invalid file name");
                    return;
                }

                std::string path = v8_convert::ToString(isolate, args[0]);
                auto fullPath = filetools_detail::ResolveScriptPath(isolate, path, "Invalid file name");
                if (fullPath.empty()) {
                    return;
                }

                std::error_code ec;
                args.GetReturnValue().Set(std::filesystem::exists(fullPath, ec));
            });

        /// @description Read a whole file and return its text (text-mode decoded, UTF-8 BOM stripped).
        /// @signature readText(path: string)
        /// @param path {string} - file name relative to the script base directory.
        /// @returns {string} - the file contents; throws "Invalid file name", "Couldn't open file", or "Read failed".
        /// @throws {Error} - path is empty or resolves outside the script base directory.
        /// @throws {Error} - the file cannot be opened (e.g. it does not exist).
        /// @throws {Error} - reading the file fails.
        StaticMethod(
            isolate, tpl, "readText", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!v8_error::CheckArgCount(args, 1, "FileTools.readText")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "You must supply a file name");
                    return;
                }

                std::string path = v8_convert::ToString(isolate, args[0]);
                FILE* fp = filetools_detail::FileOpenRelScript(isolate, path, L"r");
                if (fp == nullptr) {
                    return;
                }

                // Get file size (may overestimate due to CRLF->LF in text mode)
                fseek(fp, 0, SEEK_END);
                auto pos = ftell(fp);
                if (pos < 0) {
                    fclose(fp);
                    v8_error::ThrowError(isolate, "Read failed");
                    return;
                }
                auto size = static_cast<size_t>(pos);
                fseek(fp, 0, SEEK_SET);

                std::string contents(size, '\0');
                auto bytesRead = fread(contents.data(), 1, size, fp);

                if (ferror(fp)) {
                    fclose(fp);
                    v8_error::ThrowError(isolate, "Read failed");
                    return;
                }
                fclose(fp);

                contents.resize(bytesRead);

                // Skip UTF-8 BOM if present
                if (contents.size() >= 3 && contents[0] == '\xEF' && contents[1] == '\xBB' && contents[2] == '\xBF') {
                    contents.erase(0, 3);
                }

                args.GetReturnValue().Set(v8_convert::ToV8(isolate, contents));
            });

        /// @description Write one or more values to a file, overwriting existing contents.
        /// @signature writeText(path: string, ...data: any)
        /// @param path {string} - file name relative to the script base directory.
        /// @param data {any} - one or more values to write, each stringified (numbers formatted, null/undefined -> 4
        ///   zero bytes, others via toString). At least one is required.
        /// @returns {boolean} - true if all values wrote fully; throws "Invalid file name" or "Couldn't open file".
        /// @throws {Error} - path is empty or resolves outside the script base directory.
        /// @throws {Error} - the file cannot be opened for writing.
        StaticMethod(
            isolate, tpl, "writeText", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!v8_error::CheckArgCount(args, 2, "FileTools.writeText")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "You must supply a file name");
                    return;
                }

                std::string path = v8_convert::ToString(isolate, args[0]);

                std::scoped_lock lock(filetools_detail::fileMutex);

                FILE* fp = filetools_detail::FileOpenRelScript(isolate, path, L"w");
                if (fp == nullptr) {
                    return;
                }

                bool result = true;
                for (int32_t i = 1; i < args.Length(); i++) {
                    std::string text = filetools_detail::ValueToString(isolate, args[i]);
                    if (!text.empty()) {
                        if (fwrite(text.data(), 1, text.size(), fp) != text.size()) {
                            result = false;
                        }
                    }
                }

                fflush(fp);
                fclose(fp);

                args.GetReturnValue().Set(result);
            });

        /// @description Append one or more values to the end of a file, creating it if absent.
        /// @signature appendText(path: string, ...data: any)
        /// @param path {string} - file name relative to the script base directory.
        /// @param data {any} - one or more values to append, each stringified (numbers formatted, null/undefined -> 4
        ///   zero bytes, others via toString). At least one is required.
        /// @returns {boolean} - true if all values wrote fully; throws "Invalid file name" or "Couldn't open file".
        /// @throws {Error} - path is empty or resolves outside the script base directory.
        /// @throws {Error} - the file cannot be opened for appending.
        StaticMethod(
            isolate, tpl, "appendText", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!v8_error::CheckArgCount(args, 2, "FileTools.appendText")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "You must supply a file name");
                    return;
                }

                std::string path = v8_convert::ToString(isolate, args[0]);

                std::scoped_lock lock(filetools_detail::fileMutex);

                FILE* fp = filetools_detail::FileOpenRelScript(isolate, path, L"a+");
                if (fp == nullptr) {
                    return;
                }

                bool result = true;
                for (int32_t i = 1; i < args.Length(); i++) {
                    std::string text = filetools_detail::ValueToString(isolate, args[i]);
                    if (!text.empty()) {
                        if (fwrite(text.data(), 1, text.size(), fp) != text.size()) {
                            result = false;
                        }
                    }
                }

                fflush(fp);
                fclose(fp);

                args.GetReturnValue().Set(result);
            });
    }
};

}  // namespace d2bs::api::classes
