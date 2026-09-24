#pragma once

#include <cstdint>
#include <string_view>

#include "Receiver.h"
#include "api/core/Class.h"
#include "navigation/ExitFinder.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Exit class - represents an exit point from one area to another
// Exits are obtained from Area.exits property
class JSExit : public ClassBase<JSExit, navigation::ExitInfo> {
   public:
    static constexpr std::string_view ClassName = "Exit";

    static void Configure(const ub::Class<Native>& cls) {
        /// @description The exit's X coordinate in world coordinates.
        /// @type {number}
        Property(
            cls, "x", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSExit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->pos.x);
            });

        /// @description The exit's Y coordinate in world coordinates.
        /// @type {number}
        Property(
            cls, "y", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSExit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->pos.y);
            });

        /// @description The destination this exit leads to, interpreted according to `type`.
        /// @type {number}
        Property(
            cls, "target", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSExit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->target);
            });

        /// @description The kind of exit: 1 (linkage) or 2 (tile).
        /// @type {number}
        Property(
            cls, "type", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSExit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<uint32_t>(data->type));
            });

        /// @description The tile id associated with this exit.
        /// @type {number}
        Property(
            cls, "tileid", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSExit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->tileId);
            });

        /// @description The level number this exit belongs to.
        /// @type {number}
        Property(
            cls, "level", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSExit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->level);
            });
    }
};

}  // namespace d2bs::api::classes
