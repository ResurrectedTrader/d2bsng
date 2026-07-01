#pragma once

#include <optional>
#include <string>

#include "config/ProfileData.h"

namespace d2bs::profile {

// Load the named profile from the active ConfigStore. Returns nullopt if the
// profile doesn't exist or the store isn't initialised yet.
std::optional<config::ProfileData> Load(const std::string& name);

// Load the currently-active profile (AppConfig::GetProfileName()).
// Returns nullopt if no active profile name is set or the named profile is
// no longer in the store.
std::optional<config::ProfileData> LoadActive();

// Resolve a profile name to its `character` field. Returns nullopt if the
// profile doesn't exist OR the profile exists but has no (empty) character
// set - caller uses the nullopt to throw "Invalid profile specified", which
// is a documented divergence from reference (reference would pass the literal
// "ERROR" default to OOG_SelectCharacter and fail in-game instead).
std::optional<std::string> ResolveCharacter(const std::string& profileName);

// Switch active profile. Thread-safe from any thread - only performs atomic
// writes against AppConfig (no engine calls, no locks). Returns false if the
// profile doesn't exist. GameLoop picks up the change on its next tick and
// drives the script lifecycle (profile reload, AppConfig.scriptPaths merge,
// starter (re)launch, console-script restart when consoleScript changes).
bool Switch(const std::string& name);

// Write a new profile section to the INI (via ConfigStore::SaveProfile).
// No-op (returns false) if the profile already exists, matching reference
// addProfile semantics. Returns true when the profile was written.
bool Add(const config::ProfileData& profile);

}  // namespace d2bs::profile
