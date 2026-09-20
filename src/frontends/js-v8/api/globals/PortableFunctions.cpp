#include "PortableFunctions.h"

#include <chrono>

#include "game/GameHelpers.h"
#include "game/Types.h"
#include "scripting/Args.h"

namespace d2bs::api::globals {

namespace {

void GetTickCount(script::Args& args) {
    // NOTE: reference uses GetTickCount(), we use std::chrono
    auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    args.Return(static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
}

void ScreenToAutomap(script::Args& args) {
    if (args.Count() < 1) {
        args.Throw(script::ErrorKind::TypeError, "screenToAutomap requires at least 1 argument");
        return;
    }

    // Strict: reject non-numeric input (string->int coercion silently corrupted coords).
    auto point = game::Point::Zero;
    if (args.Count() == 1 && args.IsObject(0)) {
        auto x = args.FieldInt32(0, "x");
        auto y = args.FieldInt32(0, "y");
        if (!x || !y) {
            args.Throw(script::ErrorKind::TypeError, "Input has an x or y, but they aren't the correct type!");
            return;
        }
        point = {.x = *x, .y = *y};
    } else if (args.Count() >= 2 && args.IsNumber(0) && args.IsNumber(1)) {
        point = {.x = args.Int32(0).value_or(0), .y = args.Int32(1).value_or(0)};
    } else {
        args.Throw(script::ErrorKind::TypeError, "Invalid arguments for screenToAutomap");
        return;
    }

    args.Return(game::ScreenToAutomap(point));
}

void GetRealms(script::Args& args) {
    const auto realms = game::GetRealms();
    auto array = args.ReturnArray(realms.size());
    size_t i = 0;
    for (const auto& realm : realms) {
        array.SetObject(i++).Set("name", realm.name).Set("host", realm.host);
    }
}

}  // namespace

void RegisterPortableFunctions(script::Registry& registry) {
    /// @description Returns a monotonic millisecond timestamp for timing/elapsed measurements.
    /// @signature getTickCount()
    /// @returns {number} - a monotonically increasing millisecond counter; only differences are meaningful (not
    ///                     wall-clock time)
    registry.Global("getTickCount", &GetTickCount);

    /// @description Convert screen coordinates to automap coordinates.
    /// @signature screenToAutomap(point: {x:number,y:number})
    /// @param point {object} - a {x, y} object
    /// @signature screenToAutomap(x: number, y: number)
    /// @param x {number} - screen x coordinate
    /// @param y {number} - screen y coordinate
    /// @returns {{x:number,y:number}} - converted automap coordinates
    registry.Global("screenToAutomap", &ScreenToAutomap);

    /// @description Lists the custom Battle.net gateways injected into the realm list.
    /// @signature getRealms()
    /// @returns {Array<{name:string,host:string}>} - one entry per registered realm
    registry.Global("getRealms", &GetRealms);
}

}  // namespace d2bs::api::globals
