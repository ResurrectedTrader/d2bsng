#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
#include "ImportTypes.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <D2Config.h>            // D2ConfigStrc
#include <D2WinControlHeader.h>  // D2WinControlStrc
#include <D2WinTextBox.h>        // D2WinTextBoxStrc
#pragma clang diagnostic pop

#include "imports/extras/CharSel.h"

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::lod114d::imports::d2launch {

// ---- Variables -------------------------------------------------------------
// The launcher's config: account, character and the character-create flags.
inline GameVar<D2ConfigStrc*> gpConfig{0x3795D4};

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
// enable the OK button for a valid name. EDITBOX_SetText invokes the change
// callback but not the key filter.
inline GameVar<D2WinControlStrc*> gpCharCreateNameEdit{0x37934C};
// OK button, form 176 (627, 572, 128, 35); its click handler 0x369F0 reads the
// name from the edit box.
inline GameVar<D2WinControlStrc*> gpCharCreateOkButton{0x379330};
// Checkboxes, each toggling one bit of `D2ConfigStrc::unpackedCTemp.nCharacterSaveFlags` from its click
// handler. Expansion: form 212 (319, 540, 15, 16), handler 0x30770.
inline GameVar<D2WinControlStrc*> gpCharCreateExpansionCheckbox{0x3795C8};
// Hardcore: form 190 (319, 560) or, on a classic install, 203 (319, 540);
// handler 0x30730. Shown only once hardcore is unlocked.
inline GameVar<D2WinControlStrc*> gpCharCreateHardcoreCheckbox{0x3795C4};
// Ladder, Battle.net only: form 192 / 194 / 196 (319, 540 / 560 / 580, by
// install and hardcore unlock); handler 0x30750.
inline GameVar<D2WinControlStrc*> gpCharCreateLadderCheckbox{0x379348};

// Character-select screen. The list lives only while the screen is up: leaving
// or rebuilding it frees every row. Refill (0x38560) and update (0x39210) read
// these.
inline GameVar<extras::CharSelEntry*> gpCharSelList{0x379DBC};
// Rows, the "create new" / "more characters" rows included.
inline GameVar<int32_t> gnCharSelCount{0x379DC4};
// The first visible row; the game keeps it even, one column per parity.
inline GameVar<int32_t> gnCharSelFirst{0x379DC8};
// The scroll bar position the scroll callback last saw; it scrolls by the change from it.
inline GameVar<int32_t> gnCharSelScrollPos{0x379DCC};
// The row OK logs in with.
inline GameVar<int32_t> gnCharSelSelected{0x30CC00};
// Rows on screen: 8 on the expansion screen, two to a line.
inline GameVar<int32_t> gnCharSelVisible{0x30CC0C};
// The text box around the list; its pScrollBar is the screen's scroll bar, made anew each time the screen opens.
inline GameVar<D2WinTextBoxStrc*> gpCharSelListBox{0x379C84};
// OK ignores clicks while set (a login, delete or convert in flight).
inline GameVar<uint32_t> gbCharSelBusy{0x379DA0};
// A realm's list request in flight: the list is torn down and refilled from the reply.
inline GameVar<uint32_t> gbCharSelRealmRequest{0x379DAC};
// The gateway list is open.
inline GameVar<uint32_t> gbCharSelGatewayList{0x379C6C};
// Controls of the open popup (the delete or convert confirmation, the difficulty choice, ...).
inline GameVar<int32_t> gnPopupControls{0x379948};

// ---- Functions -------------------------------------------------------------
// Refill the visible rows from the list and gnCharSelFirst.
inline CdeclFunc<void()> CHARSEL_RefillRows{0x38560};
// Highlight the selected row, set the name label and enable the buttons for it.
inline CdeclFunc<void()> CHARSEL_UpdateSelection{0x39210};

}  // namespace d2bs::lod114d::imports::d2launch
// NOLINTEND(readability-identifier-naming)
