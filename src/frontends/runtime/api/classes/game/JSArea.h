#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <tuple>

#include "JSExit.h"
#include "Receiver.h"
#include "api/core/Class.h"
#include "api/core/Error.h"
#include "game/Level.h"
#include "navigation/ExitFinder.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Area class - represents a game area/level
// Areas contain rooms and provide information about level layout
class JSArea : public ClassBase<JSArea, game::Level> {
   public:
    static constexpr std::string_view ClassName = "Area";

    static void Configure(const ub::Class<Native>& cls) {
        /// @description Exits leading out of this area, each as an Exit object.
        /// @type {Array<Exit>}
        Property(
            cls, "exits", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSArea>(info);
                if (data == nullptr) {
                    return;
                }
                const auto& context = info.GetContext();
                if (!*data) {
                    if (auto empty = ub::Array::New(context)) {
                        info.GetReturnValue().Set(*empty);
                    }
                    return;
                }

                auto exits = navigation::GetExits(*data);
                auto array = ub::Array::New(context, static_cast<uint32_t>(exits.size()));
                if (!array) {
                    return;
                }

                for (uint32_t i = 0; i < exits.size(); ++i) {
                    auto exitObj = JSExit::Wrap(context, std::make_shared<navigation::ExitInfo>(exits[i]));
                    if (!exitObj) {
                        error::ThrowError(info.GetIsolate(), "Failed to build exit array");
                        return;
                    }
                    if (!array->Set(context, i, *exitObj).value_or(false)) {
                        return;
                    }
                }

                info.GetReturnValue().Set(*array);
            });

        /// @description Level number identifying this area, matching the Areas constant.
        /// @type {number}
        Property(
            cls, "id", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSArea>(info);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->Id()));
            });

        /// @description Human-readable level name for this area.
        /// @type {string}
        Property(
            cls, "name", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSArea>(info);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    std::ignore = info.GetReturnValue().SetEmptyString();
                    return;
                }
                std::ignore = info.GetReturnValue().Set(data->Name());
            });

        // reference d2bs parity: area.x/y/xsize/ysize are exposed as subtiles; Level::Bounds() returns game-coords (see
        // docs/coords.md).
        /// @description X origin of the area in subtiles.
        /// @type {number}
        Property(
            cls, "x", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSArea>(info);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().origin.x / 5U);
            });

        /// @description Y origin of the area in subtiles.
        /// @type {number}
        Property(
            cls, "y", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSArea>(info);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().origin.y / 5U);
            });

        /// @description Width of the area in subtiles.
        /// @type {number}
        Property(
            cls, "xsize", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSArea>(info);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().size.width / 5U);
            });

        /// @description Height of the area in subtiles.
        /// @type {number}
        Property(
            cls, "ysize", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSArea>(info);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().size.height / 5U);
            });
    }
};

}  // namespace d2bs::api::classes
