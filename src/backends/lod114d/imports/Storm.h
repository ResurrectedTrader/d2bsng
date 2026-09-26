#pragma once

#include "D2MOOConfig.h"
#include "ImportTypes.h"
#include "extras/WindowHandlers.h"

#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::lod114d::imports::storm {

// ---- Variables -------------------------------------------------------------
inline GameVar<extras::WindowHandlerHashTable> gWindowHandlers{0x379300};

// ---- Functions -------------------------------------------------------------
// Storm-style registry helpers over HKCU\Software\Battle.net\<subkey>. Reads
// fill a caller buffer (or, with buf==null, report the size in *outLen);
// writes take a (data, len) pair. Both take the subkey and value name as the
// first two args. Hooked to inject / hide custom gateways in D2's in-memory
// gateway list without touching the persisted registry value.
inline StdcallFunc<int(const char*, const char*, int, void*, int, uint32_t*)> SSTR_RegistryReadValueEx{0x14DE0};
inline StdcallFunc<int(const char*, const char*, char, const char*, int)> RegStoringKeysConfiguration{0x15000};

}  // namespace d2bs::lod114d::imports::storm
// NOLINTEND(readability-identifier-naming)
