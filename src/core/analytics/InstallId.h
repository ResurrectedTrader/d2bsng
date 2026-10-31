#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// The anonymous identities every analytics event carries, and the environment
// reads that decide whether analytics runs. Shared by every program that
// reports: d2bs.dll's frontend and an embedding host derive the same installId
// on the same machine. docs/analytics.md, "The installId", lists the rules.

namespace d2bs::core::analytics {

// Salt mixed into the derived install id. Fixed, and scoped to the publisher
// rather than to d2bsng: it makes the digest useless for correlating this
// machine against its identifier in anyone else's software, while letting our
// own tools (the bot manager) derive the same id and be joined to it. Bumping it
// re-buckets every install as new.
inline constexpr std::string_view ID_SALT = "ResurrectedTrader-analytics-v1";

// The three machine facts the install id is derived from, each empty / 0 when
// unavailable.
struct MachineFacts {
    // HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid, read from the 64-bit
    // registry view (a 32-bit process would otherwise be redirected to
    // WOW6432Node, where it does not live).
    std::string machineGuid;
    // Serial of the volume Windows is installed on. Survives reboots and app
    // reinstalls; changes on a reformat.
    uint32_t systemVolumeSerial = 0;
    std::string computerName;
};

MachineFacts ReadMachineFacts();

// SHA-256 (lowercase hex) of "<ID_SALT>|<machineGuid>|<systemVolumeSerial>|<computerName>";
// empty when hashing fails.
std::string HashInstallId(const MachineFacts& facts);

// Stable anonymous install id, *derived* rather than stored - analytics writes
// nothing to disk or the registry. Three independent machine facts are combined
// so that a missing or constant one (some Wine prefixes report no MachineGuid)
// still leaves the id distinct, then salted and hashed: only the digest ever
// leaves the machine, and the salt keeps it from matching the same machine's
// identifier in any other software. Falls back to a per-launch random id if
// hashing itself fails, which overcounts rather than collapsing every affected
// install into one bucket.
std::string DeriveInstallId();

// A random 128-bit id formatted as a canonical GUID string. Not security
// sensitive (an anonymous session / fallback install marker), so a non-crypto
// RNG is fine. Throws when the OS exposes no entropy source.
std::string NewRandomId();

// An environment variable as UTF-8; empty when unset or empty.
std::string GetEnv(const wchar_t* name);

// A launcher-supplied flag counts as "on" for any value except a clear falsey
// one, so both `D2BS_ANALYTICS_DISABLE=1` and `=true` opt out.
bool IsTruthy(std::string_view value);

}  // namespace d2bs::core::analytics
