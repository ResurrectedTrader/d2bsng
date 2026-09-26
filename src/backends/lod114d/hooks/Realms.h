#pragma once

// Injects the framework's custom realms (config::RealmRegistry) into Diablo II's
// Battle.net server list purely in memory, without persisting to the registry
// (which is shared by every game instance). D2 calls that list its "gateways".
//
// The client reads the list from HKCU\Software\Battle.net\Configuration, value
// "Diablo II Battle.net gateways" (a REG_MULTI_SZ: version, selected-index, then
// a [host, GMT-zone, display-name] triple per entry), via the Storm helper
// SSTR_RegistryReadValueEx. Detouring that read splices the registry realms into
// the blob the client parses, so they appear in the server selector and the
// client connects to them - but the on-disk value is unchanged. A matching
// detour on the write helper (RegStoringKeysConfiguration) strips our realms from
// any list write the client makes, so a user selection never persists them to the
// shared registry either.
//
// The client caches the parsed list in its BNGatewayAccess singleton after the
// first read, so a realm added mid-session appears the next time the login screen
// reads the list, not in an already-rendered dropdown.
namespace d2bs::lod114d::hooks::realms {

// Seed RealmRegistry from `-realm` launch options. Call once at backend init,
// before scripts run.
void Init();

// Detour the registry read/write helpers so custom realms are injected in memory
// only. Call once from HookManager, before the client reads its server list.
void Install();

// Remove the registry-helper detours. Idempotent.
void Remove();

}  // namespace d2bs::lod114d::hooks::realms
