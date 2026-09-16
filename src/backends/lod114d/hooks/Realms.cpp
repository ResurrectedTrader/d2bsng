#include "hooks/Realms.h"

#include <Windows.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "config/RealmRegistry.h"
#include "detour/Hook.h"
#include "game/GameHelpers.h"
#include "game/GameThread.h"
#include "game/LaunchOptions.h"
#include "imports/BnClient.h"
#include "imports/Storm.h"
#include "utils/utils.h"

namespace d2bs::hooks::realms {

namespace {

spdlog::logger& Log() {
    static const auto LOGGER = utils::GetLogger("hooks.realms");
    return *LOGGER;
}

// The registry values D2 stores its Battle.net server list in, under
// HKCU\Software\Battle.net\Configuration (D2 names them "...gateways").
// "Override..." wins over the standard value when present. Matched
// case-insensitively.
constexpr const char* REALM_LIST_VALUE = "Diablo II Battle.net gateways";
constexpr const char* REALM_LIST_VALUE_OVERRIDE = "Override Battle.net gateways";

// A list-version at or above this is required; below it the client discards the
// list. We only bump a too-low version we are already handing back in memory.
constexpr uint32_t MIN_VERSION = 1000;
constexpr const char* DEFAULT_VERSION = "1009";
// Custom realms carry no GMT bias; 0 is a neutral zone the picker tolerates.
constexpr const char* DEFAULT_ZONE = "0";

// One entry of D2's parsed server-list blob: a host, a GMT zone bias, and the
// display name.
struct RealmEntry {
    std::string host;
    std::string zone;
    std::string name;
};

// D2's server-list blob parsed into its parts.
struct RealmList {
    std::string version = DEFAULT_VERSION;
    std::string selected = "00";
    std::vector<RealmEntry> entries;
};

bool IsRealmListValue(const char* valueName) {
    return valueName != nullptr &&
           (_stricmp(valueName, REALM_LIST_VALUE) == 0 || _stricmp(valueName, REALM_LIST_VALUE_OVERRIDE) == 0);
}

// Parse a server-list blob (NUL-separated strings: version, selected, then a
// host/zone/name triple per entry). A short / malformed blob yields the
// defaults with no entries.
RealmList ParseBlob(const char* data, size_t len) {
    std::vector<std::string> strings;
    for (size_t i = 0; i < len && data[i] != '\0';) {
        // Bound by the buffer: a REG_MULTI_SZ / client write is not guaranteed
        // NUL-terminated, so never scan past `len`.
        std::string s(data + i, strnlen(data + i, len - i));
        i += s.size() + 1;
        strings.push_back(std::move(s));
    }
    RealmList list;
    if (strings.size() < 2) {
        return list;
    }
    list.version = strings[0];
    list.selected = strings[1];
    for (size_t i = 2; i + 2 < strings.size(); i += 3) {
        list.entries.push_back(RealmEntry{.host = strings[i], .zone = strings[i + 1], .name = strings[i + 2]});
    }
    return list;
}

void EnsureValidVersion(RealmList& list) {
    uint32_t version = 0;
    const auto* begin = list.version.data();
    const auto* end = begin + list.version.size();
    const auto [ptr, ec] = std::from_chars(begin, end, version);
    if (ec != std::errc{} || ptr != end || version < MIN_VERSION) {
        list.version = DEFAULT_VERSION;
    }
}

// Merge every RealmRegistry realm into the list by display name (overwrite an
// existing entry's host, else append).
void MergeCustomRealms(RealmList& list) {
    for (const auto& realm : config::RealmRegistry::Instance().All()) {
        bool merged = false;
        for (auto& entry : list.entries) {
            if (entry.name == realm.name) {
                entry.host = realm.host;
                if (entry.zone.empty()) {
                    entry.zone = DEFAULT_ZONE;
                }
                merged = true;
                break;
            }
        }
        if (!merged) {
            list.entries.push_back(RealmEntry{.host = realm.host, .zone = DEFAULT_ZONE, .name = realm.name});
        }
    }
}

// Drop any entry whose display name matches a registered realm, so a write the
// game triggers (e.g. recording a realm selection) never persists our injected
// entries to the shared registry.
void StripCustomRealms(RealmList& list) {
    const auto registered = config::RealmRegistry::Instance().All();
    std::erase_if(list.entries, [&](const RealmEntry& entry) {
        return std::ranges::any_of(registered,
                                   [&](const config::RealmRegistry::Realm& realm) { return realm.name == entry.name; });
    });
}

// Serialize to a NUL-separated blob: each string NUL-terminated, the whole
// double-NUL terminated (the REG_MULTI_SZ shape D2 reads back).
std::vector<char> Serialize(const RealmList& list) {
    std::vector<char> blob;
    const auto append = [&blob](const std::string& s) {
        blob.insert(blob.end(), s.begin(), s.end());
        blob.push_back('\0');
    };
    append(list.version);
    append(list.selected);
    for (const auto& entry : list.entries) {
        append(entry.host);
        append(entry.zone);
        append(entry.name);
    }
    blob.push_back('\0');
    return blob;
}

// Read D2's server-list blob directly from the registry (independent of the read
// hook's trampoline), preferring the "Override" value the client also prefers.
// Used for enumeration, which can run before the hook is installed.
bool ReadRegistryBlob(std::vector<char>& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Battle.net\\Configuration", 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    const std::array<const char*, 2> names = {REALM_LIST_VALUE_OVERRIDE, REALM_LIST_VALUE};
    bool ok = false;
    for (const char* name : names) {
        DWORD type = 0;
        DWORD size = 0;
        if (RegQueryValueExA(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || size == 0 ||
            (type != REG_MULTI_SZ && type != REG_BINARY)) {
            continue;
        }
        out.assign(size, '\0');
        DWORD readSize = size;
        if (RegQueryValueExA(key, name, nullptr, nullptr, reinterpret_cast<BYTE*>(out.data()), &readSize) ==
            ERROR_SUCCESS) {
            out.resize(readSize);
            ok = true;
            break;
        }
    }
    RegCloseKey(key);
    return ok;
}

using ReadFn = int(__stdcall*)(const char*, const char*, int, void*, int, uint32_t*);
using StoreFn = int(__stdcall*)(const char*, const char*, char, const char*, int);

int __stdcall HookedRead(const char* subkey, const char* valueName, int type, void* buf, int len, uint32_t* outLen);
int __stdcall HookedStore(const char* subkey, const char* valueName, char type, const char* data, int len);

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - hook owns module-level state
// Both Storm helpers are resolved from the import registry at install time.
detour::Hook<ReadFn> readHook{&HookedRead};
detour::Hook<StoreFn> storeHook{&HookedStore};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Read the real server-list blob into `out`. Returns false if the value is
// absent / empty, in which case the caller passes the read straight through so
// the client rebuilds defaults from gateways.txt (our next read then injects).
bool ReadRealBlob(const char* subkey, const char* valueName, int type, std::vector<char>& out) {
    uint32_t size = 0;
    if (readHook(subkey, valueName, type, nullptr, 0, &size) == 0 || size == 0) {
        return false;
    }
    out.assign(size, '\0');
    return readHook(subkey, valueName, type, out.data(), static_cast<int>(size), nullptr) != 0;
}

// Detour of SSTR_RegistryReadValueEx: hands D2 the real server list with the
// framework's realms spliced in, so they appear in the in-memory list without
// ever being written to the registry.
int __stdcall HookedRead(const char* subkey, const char* valueName, int type, void* buf, int len, uint32_t* outLen) {
    if (!IsRealmListValue(valueName)) {
        return readHook(subkey, valueName, type, buf, len, outLen);
    }
    std::vector<char> real;
    if (!ReadRealBlob(subkey, valueName, type, real)) {
        return readHook(subkey, valueName, type, buf, len, outLen);  // absent: let the client seed defaults
    }
    RealmList list = ParseBlob(real.data(), real.size());
    EnsureValidVersion(list);
    MergeCustomRealms(list);
    const std::vector<char> merged = Serialize(list);

    if (outLen != nullptr) {
        *outLen = static_cast<uint32_t>(merged.size());
    }
    if (buf != nullptr) {
        const size_t n = std::min(static_cast<size_t>(len), merged.size());
        std::memcpy(buf, merged.data(), n);
    }
    return 1;
}

// Detour of RegStoringKeysConfiguration: strips the framework's realms from any
// server-list write the client makes, so a persisted value never carries them.
int __stdcall HookedStore(const char* subkey, const char* valueName, char type, const char* data, int len) {
    if (!IsRealmListValue(valueName) || data == nullptr || len <= 0) {
        return storeHook(subkey, valueName, type, data, len);
    }
    RealmList list = ParseBlob(data, static_cast<size_t>(len));
    StripCustomRealms(list);
    const std::vector<char> stripped = Serialize(list);
    return storeHook(subkey, valueName, type, stripped.data(), static_cast<int>(stripped.size()));
}

}  // namespace

void Install() {
    // No -realm entries: leave D2's server-list reads/writes untouched (mirrors
    // socks5, which no-ops without -proxy). RealmRegistry is seeded by Init() in
    // Bridge::Init, before HookManager installs, so it is populated by now.
    if (readHook.IsAttached() || config::RealmRegistry::Instance().All().empty()) {
        return;
    }
    if (!imports::storm::SSTR_RegistryReadValueEx.IsResolved() ||
        !imports::storm::RegStoringKeysConfiguration.IsResolved()) {
        Log().error("realms: registry imports unresolved; custom realms will not appear in-game");
        return;
    }
    readHook.SetTarget(imports::storm::SSTR_RegistryReadValueEx.Ptr());
    storeHook.SetTarget(imports::storm::RegStoringKeysConfiguration.Ptr());

    if (const int32_t err = detour::AttachAll({&readHook, &storeHook}); err != 0) {
        Log().error("realms: failed to detour registry helpers ({})", err);
    }
}

void Remove() {
    if (const int32_t err = detour::DetachAll({&readHook, &storeHook}); err != 0) {
        Log().error("realms: failed to remove the registry detours ({})", err);
    }
}

void Init() {
    auto& registry = config::RealmRegistry::Instance();
    for (const auto& spec : game::GetLaunchOptions().realms) {
        if (!registry.AddSpec(spec)) {
            Log().warn("realms: ignoring malformed -realm spec '{}' (expected name:host)", spec);
        }
    }
}

}  // namespace d2bs::hooks::realms

namespace d2bs::game {

std::vector<RealmInfo> GetRealms() {
    // Grab the server-list blob on the game thread, where the menu/UI reads it,
    // rather than from a raw pointer on a script thread. The buffer is
    // (re)allocated by the client's Load / SaveAndUnload, which run on the
    // Battle.net connect-worker thread, so this narrows but does not fully close
    // a torn-read window if a login is (re)loading the list at the same instant;
    // the registry fallback below covers the common not-yet-loaded case (blob
    // pointer still null).
    const std::vector<char> blob = GameThread::Execute([]() -> std::vector<char> {
        const auto& singleton = imports::bnclient::gBNGatewayAccess;
        if (singleton.IsResolved()) {
            const auto* state = singleton.Ptr();
            if (state->blob != nullptr && state->blobLength > 0) {
                return {state->blob, state->blob + state->blobLength};
            }
        }
        std::vector<char> fromRegistry;
        hooks::realms::ReadRegistryBlob(fromRegistry);  // leaves it empty if absent
        return fromRegistry;
    });

    std::vector<RealmInfo> result;
    if (!blob.empty()) {
        const hooks::realms::RealmList list = hooks::realms::ParseBlob(blob.data(), blob.size());
        for (const auto& entry : list.entries) {
            result.push_back(RealmInfo{.name = entry.name, .host = entry.host});
        }
    }

    // Overlay the -realm additions (override an existing realm's host by name,
    // else append). Idempotent when the blob already carries them (via the read
    // hook); guarantees they appear even if that injection didn't run.
    for (const auto& realm : config::RealmRegistry::Instance().All()) {
        auto it = std::ranges::find_if(result, [&](const RealmInfo& info) { return info.name == realm.name; });
        if (it != result.end()) {
            it->host = realm.host;
        } else {
            result.push_back(RealmInfo{.name = realm.name, .host = realm.host});
        }
    }
    return result;
}

}  // namespace d2bs::game
