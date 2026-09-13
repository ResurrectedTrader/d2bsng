#include "Constants.h"

#include "api/core/V8Convert.h"
#include "game/Types.h"

namespace d2bs::api::globals {

void RegisterConstants(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global) {
    auto readOnly = static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);

    // File Mode Constants
    /// @description File.open() mode that opens an existing file for reading. Value 0.
    /// @type {number}
    global->Set(isolate, "FILE_READ", v8_convert::ToV8(isolate, static_cast<int32_t>(FileMode::Read)), readOnly);
    /// @description File.open() mode that creates/truncates a file for writing. Value 1.
    /// @type {number}
    global->Set(isolate, "FILE_WRITE", v8_convert::ToV8(isolate, static_cast<int32_t>(FileMode::Write)), readOnly);
    /// @description File.open() mode that opens a file for appending at end of file. Value 2.
    /// @type {number}
    global->Set(isolate, "FILE_APPEND", v8_convert::ToV8(isolate, static_cast<int32_t>(FileMode::Append)), readOnly);

    // ProfileType object (ProfileType.singlePlayer, ProfileType.battleNet, etc.)
    auto profileType = v8::ObjectTemplate::New(isolate);
    /// @description Single-player game connection type. Value 1.
    /// @type {number}
    profileType->Set(isolate, "singlePlayer",
                     v8_convert::ToV8(isolate, static_cast<int32_t>(ProfileType::SinglePlayer)), readOnly);
    /// @description Closed Battle.net realm connection type. Value 2.
    /// @type {number}
    profileType->Set(isolate, "battleNet", v8_convert::ToV8(isolate, static_cast<int32_t>(ProfileType::BattleNet)),
                     readOnly);
    /// @description Open Battle.net connection type. Value 3.
    /// @type {number}
    profileType->Set(isolate, "openBattleNet",
                     v8_convert::ToV8(isolate, static_cast<int32_t>(ProfileType::OpenBattleNet)), readOnly);
    /// @description Host a TCP/IP game connection type. Value 4.
    /// @type {number}
    profileType->Set(isolate, "tcpIpHost", v8_convert::ToV8(isolate, static_cast<int32_t>(ProfileType::TcpIpHost)),
                     readOnly);
    /// @description Join a TCP/IP game connection type. Value 5.
    /// @type {number}
    profileType->Set(isolate, "tcpIpJoin", v8_convert::ToV8(isolate, static_cast<int32_t>(ProfileType::TcpIpJoin)),
                     readOnly);
    /// @description Namespace of profile connection-type constants for the Profile class and menu/login functions.
    /// @type {object}
    global->Set(isolate, "ProfileType", profileType, readOnly);

    // StashTabKind object (StashTabKind.personal, StashTabKind.shared)
    auto stashTabKind = v8::ObjectTemplate::New(isolate);
    /// @description A tab of your own stash. Value 0.
    /// @type {number}
    stashTabKind->Set(isolate, "personal",
                      v8_convert::ToV8(isolate, static_cast<int32_t>(game::StashTabKind::Personal)), readOnly);
    /// @description A tab of the account-wide shared stash, where the game or mod has one. Value 1.
    /// @type {number}
    stashTabKind->Set(isolate, "shared", v8_convert::ToV8(isolate, static_cast<int32_t>(game::StashTabKind::Shared)),
                      readOnly);
    /// @description Namespace of stash-tab kind constants; the `kind` of a StashTab.
    /// @type {object}
    global->Set(isolate, "StashTabKind", stashTabKind, readOnly);

    // StashTabType object (StashTabType.normal, StashTabType.advancedStash, StashTabType.chronicle)
    auto stashTabType = v8::ObjectTemplate::New(isolate);
    /// @description A plain item tab; every LoD tab. Value 0.
    /// @type {number}
    stashTabType->Set(isolate, "normal", v8_convert::ToV8(isolate, static_cast<int32_t>(game::StashTabType::Normal)),
                      readOnly);
    /// @description An item tab with stackable-item support (D2R). Value 1.
    /// @type {number}
    stashTabType->Set(isolate, "advancedStash",
                      v8_convert::ToV8(isolate, static_cast<int32_t>(game::StashTabType::AdvancedStash)), readOnly);
    /// @description The Chronicle tab (D2R), which tracks found set / unique / runeword items and holds no items.
    /// Value 2.
    /// @type {number}
    stashTabType->Set(isolate, "chronicle",
                      v8_convert::ToV8(isolate, static_cast<int32_t>(game::StashTabType::Chronicle)), readOnly);
    /// @description Namespace of stash-tab type constants; the `type` of a StashTab.
    /// @type {object}
    global->Set(isolate, "StashTabType", stashTabType, readOnly);
}

}  // namespace d2bs::api::globals
