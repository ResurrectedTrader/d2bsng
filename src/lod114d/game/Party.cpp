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
    GameReadLock guard;
    if (id_ == 0) {
        return nullptr;
    }
    if (auto* cached = cache_.Get()) {
        return cached;
    }
    void* resolved = nullptr;
    for (auto* scan = *imports::d2client::gpPlayerUnitList; scan != nullptr; scan = scan->pNext) {
        if (scan->dwUnitId == id_) {
            resolved = scan;
            break;
        }
    }
    cache_.Set(resolved);
    return resolved;
}

Party::operator bool() const {
    return ResolvePtr() != nullptr;
}

Party Party::FromPtr(void* p) {
    if (p == nullptr) {
        return Party();
    }
    return Party(AsRoster(p)->dwUnitId);
}

Position Party::Pos() const {
    auto* p = AsRoster(ResolvePtr());
    if (p == nullptr) {
        return Position::Zero;
    }
    return {.x = p->dwPosX, .y = p->dwPosY};
}

uint32_t Party::LevelId() const {
    auto* p = AsRoster(ResolvePtr());
    return p == nullptr ? 0 : p->dwLevelId;
}

uint32_t Party::Id() const {
    auto* p = AsRoster(ResolvePtr());
    return p == nullptr ? 0 : p->dwUnitId;
}

uint32_t Party::Life() const {
    auto* p = AsRoster(ResolvePtr());
    return p == nullptr ? 0 : p->dwPartyLife;
}

uint32_t Party::PartyFlag() const {
    auto* p = AsRoster(ResolvePtr());
    return p == nullptr ? 0 : p->dwPartyFlags;
}

uint16_t Party::PartyId() const {
    auto* p = AsRoster(ResolvePtr());
    return p == nullptr ? static_cast<uint16_t>(0) : p->wPartyId;
}

std::string Party::Name() const {
    auto* p = AsRoster(ResolvePtr());
    if (p == nullptr) {
        return {};
    }
    return std::string{static_cast<const char*>(p->szName)};
}

uint32_t Party::ClassId() const {
    auto* p = AsRoster(ResolvePtr());
    return p == nullptr ? 0 : p->dwClassId;
}

uint32_t Party::CharacterLevel() const {
    auto* p = AsRoster(ResolvePtr());
    return p == nullptr ? 0 : p->wLevel;
}

Party Party::GetNext() const {
    auto* p = AsRoster(ResolvePtr());
    if (p == nullptr) {
        return Party();
    }
    return Party::FromPtr(p->pNext);
}

std::optional<Party> Party::GetFirst() {
    GameReadLock guard;
    auto* first = *imports::d2client::gpPlayerUnitList;
    if (first == nullptr) {
        return std::nullopt;
    }
    return Party::FromPtr(first);
}

}  // namespace d2bs::game
