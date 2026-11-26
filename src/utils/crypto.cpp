#include "crypto.h"

#include <Windows.h>
#include <bcrypt.h>
#include <format>
#include <fstream>
#include <functional>
#include <span>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace d2bs::utils {
namespace {
// Convert raw hash bytes to lowercase hex string (matches reference "%.2x" format)
std::string BytesToHex(const std::vector<uint8_t> &bytes) {
    std::string result;
    result.reserve(bytes.size() * 2);
    for (auto byte : bytes) {
        result += std::format("{:02x}", byte);
    }
    return result;
}

// Hash the data `feed` hands to its sink, in as many pieces as it likes, using BCrypt. `feed` returns false when it
// could not read all of its data.
std::string ComputeHash(const wchar_t *algorithmId,
                        const std::function<bool(const std::function<bool(std::span<const uint8_t>)> &)> &feed) {
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

    const bool isFed = feed([hHash](std::span<const uint8_t> data) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - BCrypt API takes non-const PUCHAR but doesn't modify
        return BCryptHashData(hHash, const_cast<PUCHAR>(data.data()), data.size(), 0) == 0;
    });

    std::vector<uint8_t> hashBytes(hashLength);
    const bool isFinished = isFed && BCryptFinishHash(hHash, hashBytes.data(), hashLength, 0) == 0;

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlgorithm, 0);

    return isFinished ? BytesToHex(hashBytes) : std::string{};
}
}  // namespace

std::string HashString(const wchar_t *algorithmId, const std::string &data) {
    return ComputeHash(algorithmId, [&data](const auto &sink) {
        return sink(std::span(reinterpret_cast<const uint8_t *>(data.data()), data.size()));
    });
}

std::string HashFile(const wchar_t *algorithmId, const std::filesystem::path &filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    return ComputeHash(algorithmId, [&file](const auto &sink) {
        constexpr size_t CHUNK_SIZE = 64 * 1024;
        std::vector<char> chunk(CHUNK_SIZE);
        while (file) {
            file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto count = static_cast<size_t>(file.gcount());
            if (count > 0 && !sink(std::span(reinterpret_cast<const uint8_t *>(chunk.data()), count))) {
                return false;
            }
        }
        return file.eof() && !file.bad();
    });
}
}  // namespace d2bs::utils
