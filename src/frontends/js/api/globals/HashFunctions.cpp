#include "HashFunctions.h"

#include <Windows.h>
#include <bcrypt.h>

#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "api/core/V8Function.h"
#include "config/AppConfig.h"
#include "utils/crypto.h"

namespace d2bs::api::globals {

namespace {
void HashStringCallback(const v8::FunctionCallbackInfo<v8::Value>& args, const char* funcName,
                        const wchar_t* algorithm) {
    auto* isolate = args.GetIsolate();
    if (!v8_error::CheckArgCount(args, 1, funcName)) {
        return;
    }
    std::string input = v8_convert::ToString(isolate, args[0]);
    auto result = utils::HashString(algorithm, input);
    if (!result.empty()) {
        args.GetReturnValue().Set(v8_convert::ToV8(isolate, result));
    }
}

void HashFileCallback(const v8::FunctionCallbackInfo<v8::Value>& args, const char* funcName, const wchar_t* algorithm) {
    auto* isolate = args.GetIsolate();
    if (!v8_error::CheckArgCount(args, 1, funcName)) {
        return;
    }
    std::string file = v8_convert::ToString(isolate, args[0]);
    auto fullPath = config::GetPathRelScript(file);
    if (fullPath.empty()) {
        v8_error::ThrowError(isolate, "Invalid file path!");
        return;
    }
    auto result = utils::HashFile(algorithm, fullPath);
    if (!result.empty()) {
        args.GetReturnValue().Set(v8_convert::ToV8(isolate, result));
    }
}
}  // namespace

void RegisterHashFunctions(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global) {
    // String hashing functions
    /// @description MD5 hash of a string.
    /// @signature md5(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - MD5 digest as a lowercase hex string; undefined on failure
    v8_function::Register(
        isolate, global, "md5", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            HashStringCallback(args, "md5", BCRYPT_MD5_ALGORITHM);
        });
    /// @description SHA-1 hash of a string.
    /// @signature sha1(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-1 digest as a lowercase hex string; undefined on failure
    v8_function::Register(
        isolate, global, "sha1", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            HashStringCallback(args, "sha1", BCRYPT_SHA1_ALGORITHM);
        });
    /// @description SHA-256 hash of a string.
    /// @signature sha256(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-256 digest as a lowercase hex string; undefined on failure
    v8_function::Register(
        isolate, global, "sha256", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            HashStringCallback(args, "sha256", BCRYPT_SHA256_ALGORITHM);
        });
    /// @description SHA-384 hash of a string.
    /// @signature sha384(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-384 digest as a lowercase hex string; undefined on failure
    v8_function::Register(
        isolate, global, "sha384", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            HashStringCallback(args, "sha384", BCRYPT_SHA384_ALGORITHM);
        });
    /// @description SHA-512 hash of a string.
    /// @signature sha512(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-512 digest as a lowercase hex string; undefined on failure
    v8_function::Register(
        isolate, global, "sha512", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            HashStringCallback(args, "sha512", BCRYPT_SHA512_ALGORITHM);
        });

    // File hashing functions
    /// @description MD5 hash of a file's contents.
    /// @signature md5_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - MD5 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    v8_function::Register(
        isolate, global, "md5_file", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            HashFileCallback(args, "md5_file", BCRYPT_MD5_ALGORITHM);
        });
    /// @description SHA-1 hash of a file's contents.
    /// @signature sha1_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-1 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    v8_function::Register(
        isolate, global, "sha1_file", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            HashFileCallback(args, "sha1_file", BCRYPT_SHA1_ALGORITHM);
        });
    /// @description SHA-256 hash of a file's contents.
    /// @signature sha256_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-256 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    v8_function::Register(
        isolate, global, "sha256_file", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            HashFileCallback(args, "sha256_file", BCRYPT_SHA256_ALGORITHM);
        });
    /// @description SHA-384 hash of a file's contents.
    /// @signature sha384_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-384 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    v8_function::Register(
        isolate, global, "sha384_file", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            HashFileCallback(args, "sha384_file", BCRYPT_SHA384_ALGORITHM);
        });
    /// @description SHA-512 hash of a file's contents.
    /// @signature sha512_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-512 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    v8_function::Register(
        isolate, global, "sha512_file", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            HashFileCallback(args, "sha512_file", BCRYPT_SHA512_ALGORITHM);
        });
}

}  // namespace d2bs::api::globals
