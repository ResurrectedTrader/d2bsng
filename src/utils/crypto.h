#pragma once

#include <filesystem>
#include <string>

namespace d2bs::utils {
// Hash a string using the specified BCrypt algorithm.
// algorithmId: BCRYPT_MD5_ALGORITHM, BCRYPT_SHA1_ALGORITHM, BCRYPT_SHA256_ALGORITHM,
//              BCRYPT_SHA384_ALGORITHM, BCRYPT_SHA512_ALGORITHM
// Returns lowercase hex digest, or empty string on failure.
std::string HashString(const wchar_t *algorithmId, const std::string &data);

// Hash a file's contents using the specified BCrypt algorithm.
// File is opened in text mode ("r") for CRLF->LF translation, matching reference behavior.
// Returns lowercase hex digest, or empty string on failure.
std::string HashFile(const wchar_t *algorithmId, const std::filesystem::path &filePath);
}  // namespace d2bs::utils
