#include "HashFunctions.h"

#include <Windows.h>
#include <bcrypt.h>

#include <string>

#include "ArgChecks.h"
#include "config/AppConfig.h"
#include "scripting/Args.h"
#include "utils/crypto.h"

namespace d2bs::api::globals {

namespace {

bool HashString(script::Args& args, const char* funcName, const wchar_t* algorithm) {
    if (!CheckArgCount(args, 1, funcName)) {
        return false;
    }
    // Deliberately stringifies whatever it is handed rather than demanding a
    // string: hashing null has always hashed "null".
    auto input = args.ToString(0);
    if (!input) {
        return false;
    }
    auto result = utils::HashString(algorithm, *input);
    if (!result.empty()) {
        args.SetReturnValue(std::string_view{result});
    }
    return true;
}

bool HashFile(script::Args& args, const char* funcName, const wchar_t* algorithm) {
    if (!CheckArgCount(args, 1, funcName)) {
        return false;
    }
    auto file = args.ToString(0);
    if (!file) {
        return false;
    }
    auto fullPath = config::GetPathRelScript(*file);
    if (fullPath.empty()) {
        args.Throw(script::ErrorKind::Error, "Invalid file path!");
        return false;
    }
    auto result = utils::HashFile(algorithm, fullPath);
    if (!result.empty()) {
        args.SetReturnValue(std::string_view{result});
    }
    return true;
}

}  // namespace

void RegisterHashFunctions(script::Registry& registry) {
    // String hashing functions
    /// @description MD5 hash of a string.
    /// @signature md5(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - MD5 digest as a lowercase hex string; undefined on failure
    registry.Global("md5", +[](script::Args& args) { return HashString(args, "md5", BCRYPT_MD5_ALGORITHM); });
    /// @description SHA-1 hash of a string.
    /// @signature sha1(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-1 digest as a lowercase hex string; undefined on failure
    registry.Global("sha1", +[](script::Args& args) { return HashString(args, "sha1", BCRYPT_SHA1_ALGORITHM); });
    /// @description SHA-256 hash of a string.
    /// @signature sha256(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-256 digest as a lowercase hex string; undefined on failure
    registry.Global("sha256", +[](script::Args& args) { return HashString(args, "sha256", BCRYPT_SHA256_ALGORITHM); });
    /// @description SHA-384 hash of a string.
    /// @signature sha384(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-384 digest as a lowercase hex string; undefined on failure
    registry.Global("sha384", +[](script::Args& args) { return HashString(args, "sha384", BCRYPT_SHA384_ALGORITHM); });
    /// @description SHA-512 hash of a string.
    /// @signature sha512(input: string)
    /// @param input {string} - data to hash
    /// @returns {string} - SHA-512 digest as a lowercase hex string; undefined on failure
    registry.Global("sha512", +[](script::Args& args) { return HashString(args, "sha512", BCRYPT_SHA512_ALGORITHM); });

    // File hashing functions
    /// @description MD5 hash of a file's contents.
    /// @signature md5_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - MD5 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    registry.Global("md5_file", +[](script::Args& args) { return HashFile(args, "md5_file", BCRYPT_MD5_ALGORITHM); });
    /// @description SHA-1 hash of a file's contents.
    /// @signature sha1_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-1 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    registry.Global(
        "sha1_file", +[](script::Args& args) { return HashFile(args, "sha1_file", BCRYPT_SHA1_ALGORITHM); });
    /// @description SHA-256 hash of a file's contents.
    /// @signature sha256_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-256 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    registry.Global(
        "sha256_file", +[](script::Args& args) { return HashFile(args, "sha256_file", BCRYPT_SHA256_ALGORITHM); });
    /// @description SHA-384 hash of a file's contents.
    /// @signature sha384_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-384 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    registry.Global(
        "sha384_file", +[](script::Args& args) { return HashFile(args, "sha384_file", BCRYPT_SHA384_ALGORITHM); });
    /// @description SHA-512 hash of a file's contents.
    /// @signature sha512_file(path: string)
    /// @param path {string} - file path, resolved relative to the script directory
    /// @returns {string} - SHA-512 digest as a lowercase hex string; undefined on failure (throws on invalid path)
    /// @throws {Error} - if path resolves empty (empty, traversal, or escapes the script directory)
    registry.Global(
        "sha512_file", +[](script::Args& args) { return HashFile(args, "sha512_file", BCRYPT_SHA512_ALGORITHM); });
}

}  // namespace d2bs::api::globals
