#include "game/Party.h"

#include "game/GameLock.h"
#include "imports/D2Client.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <D2Roster.h>  // D2RosterUnitStrc
#pragma clang diagnostic pop

namespace d2bs::game {

namespace {

inline D2RosterUnitStrc* AsRoster(void* p) noexcept {
    return static_cast<D2RosterUnitStrc*>(p);
}

}  // namespace

void* Party::ResolvePtr() const {
    assert(IsGameLockHeld() && "resolve under the game lock - use Resolve<T>()");
    if (id_ == 0) {
        return nullptr;
    }
    if (auto* cached = cache_.Get()) {
        return cached;
    }
    void* resolved = nullptr;
    for (auto* scan = *lod114d::imports::d2client::gpPlayerUnitList; scan != nullptr; scan = scan->pNext) {
        if (scan->dwUnitId == id_) {
            resolved = scan;
            break;
        }
    }
    cache_.Set(resolved);
    return resolved;
}

Party::operator bool() const {
    GameReadLock guard;
    return ResolvePtr() != nullptr;
}

Party Party::FromPtr(void* p) {
    if (p == nullptr) {
        return Party();
    }
    Party handle(AsRoster(p)->dwUnitId);
    handle.cache_.Set(p);
    return handle;
}

Position Party::Pos() const {
    const auto p = Resolve<D2RosterUnitStrc>();
    if (!p) {
        return Position::Zero;
    }
    return {.x = p->dwPosX, .y = p->dwPosY};
}

uint32_t Party::LevelId() const {
    const auto p = Resolve<D2RosterUnitStrc>();
    return p ? p->dwLevelId : 0;
}

uint32_t Party::Id() const {
    const auto p = Resolve<D2RosterUnitStrc>();
    return p ? p->dwUnitId : 0;
}

uint32_t Party::Life() const {
    const auto p = Resolve<D2RosterUnitStrc>();
    return p ? p->dwPartyLife : 0;
}

uint32_t Party::PartyFlag() const {
    const auto p = Resolve<D2RosterUnitStrc>();
    return p ? p->dwPartyFlags : 0;
}

uint16_t Party::PartyId() const {
    const auto p = Resolve<D2RosterUnitStrc>();
    return p ? p->wPartyId : static_cast<uint16_t>(0);
}

std::string Party::Name() const {
    const auto p = Resolve<D2RosterUnitStrc>();
    if (!p) {
        return {};
    }
    return std::string{static_cast<const char*>(p->szName)};
}

uint32_t Party::ClassId() const {
    const auto p = Resolve<D2RosterUnitStrc>();
    return p ? p->dwClassId : 0;
}

uint32_t Party::CharacterLevel() const {
    const auto p = Resolve<D2RosterUnitStrc>();
    return p ? p->wLevel : 0;
}

Party Party::GetNext() const {
    const auto p = Resolve<D2RosterUnitStrc>();
    if (!p) {
        return Party();
    }
    return FromPtr(p->pNext);
}

std::optional<Party> Party::GetFirst() {
    GameReadLock guard;
    auto* first = *lod114d::imports::d2client::gpPlayerUnitList;
    if (first == nullptr) {
        return std::nullopt;
    }
    return FromPtr(first);
}

}  // namespace d2bs::game
