#pragma once

#include <v8.h>

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "config/AppConfig.h"

namespace d2bs::runtime::api::classes {

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
class JSFileTools : public ClassBase<JSFileTools, FileToolsData> {
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
                if (!error::CheckArgCount(args, 1, "FileTools.remove")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    error::ThrowTypeError(isolate, "You must supply a file name");
                    return;
                }

                std::string path = convert::ToString(isolate, args[0]);
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
                if (!error::CheckArgCount(args, 2, "FileTools.rename")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    error::ThrowTypeError(isolate, "You must supply an original file name");
                    return;
                }
                if (!args[1]->IsString()) {
                    error::ThrowTypeError(isolate, "You must supply a new file name");
                    return;
                }

                std::string oldName = convert::ToString(isolate, args[0]);
                std::string newName = convert::ToString(isolate, args[1]);

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

        /// @description Copy a file byte for byte within the script base directory. An existing destination is
        /// replaced only when overwrite is true; a failed copy leaves no partial destination behind.
        /// @signature copy(original: string, copy: string, overwrite?: boolean)
        /// @param original {string} - source file name relative to the script base directory.
        /// @param copy {string} - destination file name relative to the script base directory.
        /// @param overwrite {boolean} - optional, default false; true replaces an existing destination.
        /// @returns {boolean} - true when the file was copied; false when it was not: a missing or unreadable source,
        /// an existing destination without overwrite, a destination that is the source or held open, or a failed
        /// read or write.
        /// @throws {Error} - original or copy is empty or resolves outside the script base directory ("Invalid
        /// original file name" / "Invalid new file name").
        StaticMethod(
            isolate, tpl, "copy", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!error::CheckArgCount(args, 2, "FileTools.copy")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    error::ThrowTypeError(isolate, "You must supply an original file name");
                    return;
                }
                if (!args[1]->IsString()) {
                    error::ThrowTypeError(isolate, "You must supply a new file name");
                    return;
                }

                std::string original = convert::ToString(isolate, args[0]);
                std::string copyName = convert::ToString(isolate, args[1]);
                const bool overwrite = args.Length() > 2 && args[2]->IsBoolean() && args[2]->BooleanValue(isolate);

                auto srcPath = filetools_detail::ResolveScriptPath(isolate, original, "Invalid original file name");
                if (srcPath.empty()) {
                    return;
                }
                auto dstPath = filetools_detail::ResolveScriptPath(isolate, copyName, "Invalid new file name");
                if (dstPath.empty()) {
                    return;
                }

                // Filesystem failures return false rather than throw: scripts copy files that may be missing.
                args.GetReturnValue().SetFalse();
                // A self-copy must never reach the truncating open.
                if (_wcsicmp(srcPath.c_str(), dstPath.c_str()) == 0) {
                    return;
                }

                FILE* src = nullptr;
                if (_wfopen_s(&src, srcPath.c_str(), L"rb") != 0 || src == nullptr) {
                    return;
                }
                // _O_EXCL keeps an existing destination atomically; _O_TRUNC replaces it only when asked. Deny-all
                // sharing makes a second copy to the same destination fail instead of interleaving.
                int32_t fd = -1;
                if (_wsopen_s(&fd, dstPath.c_str(), _O_WRONLY | _O_CREAT | _O_BINARY | (overwrite ? _O_TRUNC : _O_EXCL),
                              _SH_DENYRW, _S_IREAD | _S_IWRITE) != 0) {
                    fclose(src);
                    return;
                }
                FILE* dst = _wfdopen(fd, L"wb");
                if (dst == nullptr) {
                    _close(fd);
                    fclose(src);
                    _wremove(dstPath.c_str());
                    return;
                }

                constexpr size_t CHUNK_SIZE = 4096;
                std::array<char, CHUNK_SIZE> chunk{};
                bool isCopied = true;
                size_t count = 0;
                while ((count = fread(chunk.data(), 1, chunk.size(), src)) > 0) {
                    if (fwrite(chunk.data(), 1, count, dst) != count) {
                        isCopied = false;
                        break;
                    }
                }
                if (ferror(src) != 0) {
                    isCopied = false;
                }
                // fclose flushes; a failed flush leaves the destination incomplete.
                if (fclose(dst) != 0) {
                    isCopied = false;
                }
                fclose(src);

                // A partial destination must not look like a finished copy.
                if (!isCopied) {
                    _wremove(dstPath.c_str());
                }
                args.GetReturnValue().Set(isCopied);
            });

        /// @description Test whether a file or directory exists under the script base directory.
        /// @signature exists(path: string)
        /// @param path {string} - file name relative to the script base directory.
        /// @returns {boolean} - true if the resolved path exists; throws "Invalid file name" on bad path.
        /// @throws {Error} - path is empty or resolves outside the script base directory.
        StaticMethod(
            isolate, tpl, "exists", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!error::CheckArgCount(args, 1, "FileTools.exists")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    error::ThrowTypeError(isolate, "Invalid file name");
                    return;
                }

                std::string path = convert::ToString(isolate, args[0]);
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
                if (!error::CheckArgCount(args, 1, "FileTools.readText")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    error::ThrowTypeError(isolate, "You must supply a file name");
                    return;
                }

                std::string path = convert::ToString(isolate, args[0]);
                FILE* fp = filetools_detail::FileOpenRelScript(isolate, path, L"r");
                if (fp == nullptr) {
                    return;
                }

                // Get file size (may overestimate due to CRLF->LF in text mode)
                fseek(fp, 0, SEEK_END);
                auto pos = ftell(fp);
                if (pos < 0) {
                    fclose(fp);
                    error::ThrowError(isolate, "Read failed");
                    return;
                }
                auto size = static_cast<size_t>(pos);
                fseek(fp, 0, SEEK_SET);

                std::string contents(size, '\0');
                auto bytesRead = fread(contents.data(), 1, size, fp);

                if (ferror(fp)) {
                    fclose(fp);
                    error::ThrowError(isolate, "Read failed");
                    return;
                }
                fclose(fp);

                contents.resize(bytesRead);

                // Skip UTF-8 BOM if present
                if (contents.size() >= 3 && contents[0] == '\xEF' && contents[1] == '\xBB' && contents[2] == '\xBF') {
                    contents.erase(0, 3);
                }

                args.GetReturnValue().Set(convert::ToJS(isolate, contents));
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
                if (!error::CheckArgCount(args, 2, "FileTools.writeText")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    error::ThrowTypeError(isolate, "You must supply a file name");
                    return;
                }

                std::string path = convert::ToString(isolate, args[0]);

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
                if (!error::CheckArgCount(args, 2, "FileTools.appendText")) {
                    return;
                }
                if (!args[0]->IsString()) {
                    error::ThrowTypeError(isolate, "You must supply a file name");
                    return;
                }

                std::string path = convert::ToString(isolate, args[0]);

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

}  // namespace d2bs::runtime::api::classes
