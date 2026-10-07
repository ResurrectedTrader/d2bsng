#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include "extras/BnetData.h"
#include "extras/D2WinControlStrc.h"

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::lod114d::imports::d2launch {

// ---- Variables -------------------------------------------------------------
inline GameVar<extras::BnetData*> gpBnetData{0x3795D4};

// How the character list was reached: 0 single player, 1 a Battle.net realm,
// 2 TCP/IP, 3 Open Battle.net. Written by the main-menu entry points (stores at
// 0x35CD0 = 0, 0x35D30 = 1, 0x35DB4 = 2, 0x35D10 = 3); the character-create
// builder (0x35580) and its OK handler (0x365B0) branch on it, the realm (1)
// alone getting the ladder checkbox.
inline GameVar<uint32_t> gnCharSelectMode{0x3795EC};

// Character-create screen controls, stored by the screen's builder (0x35580) as
// it creates each form from the 48-byte form table at 0x308D10; stale once the
// screen is torn down. Hardcore and ladder share the checkbox rect (319, 560)
// while hardcore is locked, so these pointers are the only unambiguous handles.
//
// Name edit box, form 204 (318, 510, 157, 16). 0x35580 installs 0x30590 as its
// per-key filter and 0x33BD0 as its change callback, which runs 0x30620 to
// enable the OK button for a valid name. CONTROL_SetText invokes the change
// callback but not the key filter.
inline GameVar<extras::D2WinControlStrc*> gpCharCreateNameEdit{0x37934C};
// OK button, form 176 (627, 572, 128, 35); its click handler 0x369F0 reads the
// name from the edit box.
inline GameVar<extras::D2WinControlStrc*> gpCharCreateOkButton{0x379330};
// Checkboxes, each toggling one `BnetData::nCharFlags` bit from its click
// handler. Expansion: form 212 (319, 540, 15, 16), handler 0x30770.
inline GameVar<extras::D2WinControlStrc*> gpCharCreateExpansionCheckbox{0x3795C8};
// Hardcore: form 190 (319, 560) or, on a classic install, 203 (319, 540);
// handler 0x30730. Shown only once hardcore is unlocked.
inline GameVar<extras::D2WinControlStrc*> gpCharCreateHardcoreCheckbox{0x3795C4};
// Ladder, Battle.net only: form 192 / 194 / 196 (319, 540 / 560 / 580, by
// install and hardcore unlock); handler 0x30750.
inline GameVar<extras::D2WinControlStrc*> gpCharCreateLadderCheckbox{0x379348};

}  // namespace d2bs::lod114d::imports::d2launch
// NOLINTEND(readability-identifier-naming)
