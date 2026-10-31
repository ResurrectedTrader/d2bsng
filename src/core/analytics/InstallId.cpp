#include "analytics/InstallId.h"

#include <Windows.h>

#include <bcrypt.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <random>
#include <string>
#include <string_view>

#include "utils/crypto.h"
#include "utils/utils.h"

namespace d2bs::core::analytics {

namespace {

// A REG_SZ value from the 64-bit registry view; empty when absent or unreadable.
std::string ReadRegString(HKEY root, const wchar_t* subKey, const wchar_t* value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return {};
    }
    std::array<wchar_t, 256> buffer{};
    // Leave room for a terminator - RegQueryValueExW does not guarantee one.
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return {};
    }
    return utils::ToStr(std::wstring(buffer.data()), CP_UTF8);
}

uint32_t SystemVolumeSerial() {
    std::array<wchar_t, MAX_PATH> winDir{};
    if (GetSystemWindowsDirectoryW(winDir.data(), winDir.size()) == 0) {
        return 0;
    }
    const std::wstring root = std::wstring(winDir.data()).substr(0, 3);  // "C:\"
    if (root.size() < 3) {
        return 0;
    }
    DWORD serial = 0;
    if (GetVolumeInformationW(root.c_str(), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0) == 0) {
        return 0;
    }
    return serial;
}

std::string ComputerName() {
    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 1> buffer{};
    DWORD size = buffer.size();
    if (GetComputerNameW(buffer.data(), &size) == 0) {
        return {};
    }
    return utils::ToStr(std::wstring(buffer.data(), size), CP_UTF8);
}

}  // namespace

MachineFacts ReadMachineFacts() {
    return {.machineGuid = ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid"),
            .systemVolumeSerial = SystemVolumeSerial(),
            .computerName = ComputerName()};
}

std::string HashInstallId(const MachineFacts& facts) {
    std::string material(ID_SALT);
    material += '|';
    material += facts.machineGuid;
    material += '|';
    material += std::to_string(facts.systemVolumeSerial);
    material += '|';
    material += facts.computerName;
    return utils::HashString(BCRYPT_SHA256_ALGORITHM, material);
}

std::string DeriveInstallId() {
    std::string digest = HashInstallId(ReadMachineFacts());
    return !digest.empty() ? digest : NewRandomId();
}

std::string NewRandomId() {
    std::random_device rd;
    const auto draw64 = [&rd] {
        return (static_cast<uint64_t>(rd()) << 32) | rd();
    };
    const uint64_t hi = draw64();
    const uint64_t lo = draw64();
    return std::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}", static_cast<uint32_t>(hi >> 32),
                       static_cast<uint32_t>((hi >> 16) & 0xffff), static_cast<uint32_t>(hi & 0xffff),
                       static_cast<uint32_t>(lo >> 48), lo & 0xffffffffffffULL);
}

std::string GetEnv(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) {
        return {};
    }
    std::wstring buffer(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, buffer.data(), needed);
    buffer.resize(written);
    return utils::ToStr(buffer, CP_UTF8);
}

bool IsTruthy(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const char first = static_cast<char>(std::tolower(static_cast<unsigned char>(value.front())));
    return first != '0' && first != 'f' && first != 'n';
}

}  // namespace d2bs::core::analytics
