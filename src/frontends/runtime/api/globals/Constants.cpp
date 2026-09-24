#include "Constants.h"

#include <optional>
#include <string_view>

#include "api/core/Convert.h"
#include "game/Types.h"

namespace d2bs::api::globals {

namespace {

constexpr ub::PropertyAttribute READ_ONLY = ub::PropertyAttribute::ReadOnly | ub::PropertyAttribute::DontDelete;

bool Define(const ub::Context& context, const ub::Local<ub::Object>& target, std::string_view name,
            const ub::Local<ub::Value>& value) {
    auto key = ub::String::New(context.GetIsolate(), name);
    return key && !value.IsEmpty() && target.DefineOwnProperty(context, *key, value, READ_ONLY).value_or(false);
}

bool Define(const ub::Context& context, const ub::Local<ub::Object>& target, std::string_view name, int32_t value) {
    return Define(context, target, name, convert::ToJS(context.GetIsolate(), value));
}

}  // namespace

void RegisterConstants(const ub::Context& context) {
    const auto global = context.GlobalObject();

    // File Mode Constants
    /// @description File.open() mode that opens an existing file for reading. Value 0.
    /// @type {number}
    Define(context, global, "FILE_READ", static_cast<int32_t>(FileMode::Read));
    /// @description File.open() mode that creates/truncates a file for writing. Value 1.
    /// @type {number}
    Define(context, global, "FILE_WRITE", static_cast<int32_t>(FileMode::Write));
    /// @description File.open() mode that opens a file for appending at end of file. Value 2.
    /// @type {number}
    Define(context, global, "FILE_APPEND", static_cast<int32_t>(FileMode::Append));

    // ProfileType object (ProfileType.singlePlayer, ProfileType.battleNet, etc.)
    if (auto profileType = ub::Object::New(context)) {
        /// @description Single-player game connection type. Value 1.
        /// @type {number}
        Define(context, *profileType, "singlePlayer", static_cast<int32_t>(ProfileType::SinglePlayer));
        /// @description Closed Battle.net realm connection type. Value 2.
        /// @type {number}
        Define(context, *profileType, "battleNet", static_cast<int32_t>(ProfileType::BattleNet));
        /// @description Open Battle.net connection type. Value 3.
        /// @type {number}
        Define(context, *profileType, "openBattleNet", static_cast<int32_t>(ProfileType::OpenBattleNet));
        /// @description Host a TCP/IP game connection type. Value 4.
        /// @type {number}
        Define(context, *profileType, "tcpIpHost", static_cast<int32_t>(ProfileType::TcpIpHost));
        /// @description Join a TCP/IP game connection type. Value 5.
        /// @type {number}
        Define(context, *profileType, "tcpIpJoin", static_cast<int32_t>(ProfileType::TcpIpJoin));
        /// @description Namespace of profile connection-type constants for the Profile class and menu/login functions.
        /// @type {object}
        Define(context, global, "ProfileType", *profileType);
    }

    // StashTabKind object (StashTabKind.personal, StashTabKind.shared)
    if (auto stashTabKind = ub::Object::New(context)) {
        /// @description A tab of your own stash. Value 0.
        /// @type {number}
        Define(context, *stashTabKind, "personal", static_cast<int32_t>(game::StashTabKind::Personal));
        /// @description A tab of the account-wide shared stash, where the game or mod has one. Value 1.
        /// @type {number}
        Define(context, *stashTabKind, "shared", static_cast<int32_t>(game::StashTabKind::Shared));
        /// @description Namespace of stash-tab kind constants; the `kind` of a StashTab.
        /// @type {object}
        Define(context, global, "StashTabKind", *stashTabKind);
    }

    // StashTabType object (StashTabType.normal, StashTabType.advancedStash, StashTabType.chronicle)
    if (auto stashTabType = ub::Object::New(context)) {
        /// @description A plain item tab; every LoD tab. Value 0.
        /// @type {number}
        Define(context, *stashTabType, "normal", static_cast<int32_t>(game::StashTabType::Normal));
        /// @description An item tab with stackable-item support (D2R). Value 1.
        /// @type {number}
        Define(context, *stashTabType, "advancedStash", static_cast<int32_t>(game::StashTabType::AdvancedStash));
        /// @description The Chronicle tab (D2R), which tracks found set / unique / runeword items and holds no items.
        /// Value 2.
        /// @type {number}
        Define(context, *stashTabType, "chronicle", static_cast<int32_t>(game::StashTabType::Chronicle));
        /// @description Namespace of stash-tab type constants; the `type` of a StashTab.
        /// @type {object}
        Define(context, global, "StashTabType", *stashTabType);
    }
}

}  // namespace d2bs::api::globals
