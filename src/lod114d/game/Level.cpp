#include "game/Level.h"

#include "DrlgHelpers.h"
#include "game/GameLock.h"
#include "game/Room.h"
#include "imports/D2Common.h"
#include "imports/extras/D2DrlgLevelStrc.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <DataTbls/LevelsTbls.h>  // D2LevelsTxt
#pragma clang diagnostic pop

#include <algorithm>
#include <cstdint>

namespace d2bs::game {

using d2bs::imports::extras::D2DrlgLevelStrc;

namespace {

inline D2DrlgLevelStrc* AsLevel(void* p) noexcept {
    return static_cast<D2DrlgLevelStrc*>(p);
}

}  // namespace

void* Level::ResolvePtr() const {
    GameReadLock guard;
    if (id_ == 0) {
        return nullptr;
    }
    if (auto* cached = cache_.Get()) {
        return cached;
    }
    auto* resolved = FindLevelInChain(id_);
    cache_.Set(resolved);
    return resolved;
}

Level::operator bool() const {
    return ResolvePtr() != nullptr;
}

std::string Level::Name() const {
    if (ResolvePtr() == nullptr) {
        return {};
    }
    auto* txt = imports::d2common::DATATBLS_GetLevelsTxtRecord(id_);
    if (txt == nullptr) {
        return {};
    }
    return std::string{static_cast<const char*>(txt->szLevelName)};
}

Rect Level::Bounds() const {
    auto* level = AsLevel(ResolvePtr());
    if (level == nullptr) {
        return Rect::Zero;
    }
    // nPosX/nPosY are signed in D2MOO and stored as -1 for "uninitialised";
    // treat negatives as 0 game-coords per the level Bounds contract.
    constexpr int32_t SUBTILE_SCALE = 5;
    const auto posX = std::max<int32_t>(level->nPosX, 0) * SUBTILE_SCALE;
    const auto posY = std::max<int32_t>(level->nPosY, 0) * SUBTILE_SCALE;
    const auto width = std::max<int32_t>(level->nWidth, 0) * SUBTILE_SCALE;
    const auto height = std::max<int32_t>(level->nHeight, 0) * SUBTILE_SCALE;
    return {.origin = {.x = static_cast<uint32_t>(posX), .y = static_cast<uint32_t>(posY)},
            .size = {.width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height)}};
}

Room Level::GetFirstRoom() const {
    auto* level = AsLevel(ResolvePtr());
    if (level == nullptr) {
        return Room{};
    }
    return Room::FromPtr(level->pFirstRoomEx);
}

std::optional<Level> Level::Get(uint32_t levelNo) {
    if (levelNo == 0) {
        return std::nullopt;
    }
    if (FindLevelInChain(levelNo) == nullptr) {
        return std::nullopt;
    }
    return Level(levelNo);
}

}  // namespace d2bs::game
