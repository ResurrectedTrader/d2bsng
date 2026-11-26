#pragma once

#include <filesystem>
#include <string>

namespace d2bs::utils {
// Hash a string using the specified BCrypt algorithm.
// algorithmId: BCRYPT_MD5_ALGORITHM, BCRYPT_SHA1_ALGORITHM, BCRYPT_SHA256_ALGORITHM,
//              BCRYPT_SHA384_ALGORITHM, BCRYPT_SHA512_ALGORITHM
// Returns lowercase hex digest, or empty string on failure.
std::string HashString(const wchar_t *algorithmId, const std::string &data);

// Hash a file's exact bytes (binary mode, read in chunks) using the specified BCrypt algorithm.
// Returns lowercase hex digest, or empty string on failure (a missing file, for example).
std::string HashFile(const wchar_t *algorithmId, const std::filesystem::path &filePath);
}  // namespace d2bs::utils
