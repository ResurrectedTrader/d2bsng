#include "Constants.h"

#include "api/core/V8Convert.h"

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
}

}  // namespace d2bs::api::globals
