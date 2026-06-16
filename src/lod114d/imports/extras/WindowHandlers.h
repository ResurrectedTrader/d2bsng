#pragma once

#include <Windows.h>  // HWND

#include <cstdint>

// Storm.dll's window/message handler hash tables - d2bs-internal structs.
// Not modelled in D2MOO. Layout matches `reference/d2bs/D2Structs.h`.
//
// `STORM_WindowHandlers` (Storm.dll +0x379300 on 1.14d) is the root
// `WindowHandlerHashTable`. Used by `IsScrollingText` to detect that an NPC
// dialog is scrolling - the test is "is `D2CLIENT_CloseNPCTalk` registered as
// a WM_LBUTTONDOWN (0x201) handler for the game window?".
// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
namespace d2bs::imports::extras {

struct MessageHandlerList {
    uint32_t dwMessage;                     // 0x00
    uint32_t dwUnk4;                        // 0x04
    uint32_t(__stdcall* pfHandler)(void*);  // 0x08
    struct MessageHandlerList* pNext;       // 0x0C
};

static_assert(sizeof(MessageHandlerList) == 0x10, "MessageHandlerList layout mismatch");

struct MessageHandlerHashTable {
    MessageHandlerList** pTable;  // 0x00
    uint32_t dwLength;            // 0x04
};

static_assert(sizeof(MessageHandlerHashTable) == 0x08, "MessageHandlerHashTable layout mismatch");

struct WindowHandlerList {
    uint32_t dwMagic;                       // 0x00 - 'GSMS' (0x534D5347), tag identifying this slot
    HWND hWnd;                              // 0x04
    uint32_t dwUnk8;                        // 0x08
    MessageHandlerHashTable* pMsgHandlers;  // 0x0C
    struct WindowHandlerList* pNext;        // 0x10
};

static_assert(sizeof(WindowHandlerList) == 0x14, "WindowHandlerList layout mismatch");

struct WindowHandlerHashTable {
    WindowHandlerList** pTable;  // 0x00
    uint32_t dwLength;           // 0x04
};

static_assert(sizeof(WindowHandlerHashTable) == 0x08, "WindowHandlerHashTable layout mismatch");

}  // namespace d2bs::imports::extras

namespace d2bs::game {
using ::d2bs::imports::extras::MessageHandlerHashTable;
using ::d2bs::imports::extras::MessageHandlerList;
using ::d2bs::imports::extras::WindowHandlerHashTable;
using ::d2bs::imports::extras::WindowHandlerList;
}  // namespace d2bs::game
// NOLINTEND(readability-identifier-naming)
