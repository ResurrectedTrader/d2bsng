#include "HashFunctions.h"

#include <Windows.h>
#include <bcrypt.h>

#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "api/core/Function.h"
#include "config/AppConfig.h"
#include "utils/crypto.h"

namespace d2bs::api::globals {

namespace {
void HashStringCallback(const ub::CallbackInfo& args, const char* funcName, const wchar_t* algorithm) {
    if (!error::CheckArgCount(args, 1, funcName)) {
        return;
    }
    std::string input = convert::ToString(args.GetContext(), args[0]);
    auto result = utils::HashString(algorithm, input);
    if (!result.empty()) {
        (void)args.GetReturnValue().Set(result);
    }
}

void HashFileCallback(const ub::CallbackInfo& args, const char* funcName, const wchar_t* algorithm) {
    if (!error::CheckArgCount(args, 1, funcName)) {
        return;
    }
    std::string file = convert::ToString(args.GetContext(), args[0]);
    auto fullPath = config::GetPathRelScript(file);
    if (fullPath.empty()) {
        error::ThrowError(args.GetIsolate(), "Invalid file path!");
        return;
    }
    auto result = utils::HashFile(algorithm, fullPath);
    if (!result.empty()) {
        (void)args.GetReturnValue().Set(result);
    }
}
}  // namespace

void RegisterHashFunctions(const ub::Context& context) {
    // String hashing functions
    /// @description MD5 hash of a string.
    /// @signature md5(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - MD5 digest as a lowercase hex string; undefined on failure
    function::Register(
        context, "md5", +[](const ub::CallbackInfo& args) { HashStringCallback(args, "md5", BCRYPT_MD5_ALGORITHM); });
    /// @description SHA-1 hash of a string.
    /// @signature sha1(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-1 digest as a lowercase hex string; undefined on failure
    function::Register(
        context, "sha1",
        +[](const ub::CallbackInfo& args) { HashStringCallback(args, "sha1", BCRYPT_SHA1_ALGORITHM); });
    /// @description SHA-256 hash of a string.
    /// @signature sha256(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-256 digest as a lowercase hex string; undefined on failure
    function::Register(
        context, "sha256",
        +[](const ub::CallbackInfo& args) { HashStringCallback(args, "sha256", BCRYPT_SHA256_ALGORITHM); });
    /// @description SHA-384 hash of a string.
    /// @signature sha384(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-384 digest as a lowercase hex string; undefined on failure
    function::Register(
        context, "sha384",
        +[](const ub::CallbackInfo& args) { HashStringCallback(args, "sha384", BCRYPT_SHA384_ALGORITHM); });
    /// @description SHA-512 hash of a string.
    /// @signature sha512(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-512 digest as a lowercase hex string; undefined on failure
    function::Register(
        context, "sha512",
        +[](const ub::CallbackInfo& args) { HashStringCallback(args, "sha512", BCRYPT_SHA512_ALGORITHM); });

    // File hashing functions
    /// @description MD5 hash of a file's contents.
    /// @signature md5_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - MD5 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    function::Register(
        context, "md5_file",
        +[](const ub::CallbackInfo& args) { HashFileCallback(args, "md5_file", BCRYPT_MD5_ALGORITHM); });
    /// @description SHA-1 hash of a file's contents.
    /// @signature sha1_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-1 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    function::Register(
        context, "sha1_file",
        +[](const ub::CallbackInfo& args) { HashFileCallback(args, "sha1_file", BCRYPT_SHA1_ALGORITHM); });
    /// @description SHA-256 hash of a file's contents.
    /// @signature sha256_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-256 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    function::Register(
        context, "sha256_file",
        +[](const ub::CallbackInfo& args) { HashFileCallback(args, "sha256_file", BCRYPT_SHA256_ALGORITHM); });
    /// @description SHA-384 hash of a file's contents.
    /// @signature sha384_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-384 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    function::Register(
        context, "sha384_file",
        +[](const ub::CallbackInfo& args) { HashFileCallback(args, "sha384_file", BCRYPT_SHA384_ALGORITHM); });
    /// @description SHA-512 hash of a file's contents.
    /// @signature sha512_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-512 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    function::Register(
        context, "sha512_file",
        +[](const ub::CallbackInfo& args) { HashFileCallback(args, "sha512_file", BCRYPT_SHA512_ALGORITHM); });
}

}  // namespace d2bs::api::globals
