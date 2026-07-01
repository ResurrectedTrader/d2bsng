#include "crypto.h"

#include <Windows.h>
#include <bcrypt.h>
#include <fmt/format.h>
#include <fstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace d2bs::utils {
namespace {
// Convert raw hash bytes to lowercase hex string (matches reference "%.2x" format)
std::string BytesToHex(const std::vector<uint8_t> &bytes) {
    std::string result;
    result.reserve(bytes.size() * 2);
    for (auto byte : bytes) {
        result += fmt::format("{:02x}", byte);
    }
    return result;
}

// Compute hash of raw data using BCrypt
std::string ComputeHash(const wchar_t *algorithmId, const uint8_t *data, size_t dataLen) {
    BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;

    if (BCryptOpenAlgorithmProvider(&hAlgorithm, algorithmId, nullptr, 0) != 0) {
        return {};
    }

    DWORD hashLength = 0;    // BCrypt API requires DWORD*
    DWORD resultLength = 0;  // BCrypt API requires DWORD*
    if (BCryptGetProperty(hAlgorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
                          &resultLength, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlgorithm, 0);
        return {};
    }

    if (BCryptCreateHash(hAlgorithm, &hHash, nullptr, 0, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlgorithm, 0);
        return {};
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - BCrypt API takes non-const PUCHAR but doesn't modify data
    if (BCryptHashData(hHash, const_cast<PUCHAR>(data), dataLen, 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlgorithm, 0);
        return {};
    }

    std::vector<uint8_t> hashBytes(hashLength);
    if (BCryptFinishHash(hHash, hashBytes.data(), hashLength, 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlgorithm, 0);
        return {};
    }

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlgorithm, 0);

    return BytesToHex(hashBytes);
}
}  // namespace

std::string HashString(const wchar_t *algorithmId, const std::string &data) {
    return ComputeHash(algorithmId, reinterpret_cast<const uint8_t *>(data.data()), data.size());
}

std::string HashFile(const wchar_t *algorithmId, const std::filesystem::path &filePath) {
    // Open in text mode to match reference behavior - CRLF is translated to LF before hashing
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return {};
    }

    std::string contents((std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());

    return ComputeHash(algorithmId, reinterpret_cast<const uint8_t *>(contents.data()), contents.size());
}
}  // namespace d2bs::utils
