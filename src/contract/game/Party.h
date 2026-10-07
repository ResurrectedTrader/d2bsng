#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "game/HandleCache.h"
#include "game/Types.h"

namespace d2bs::game {

// Identity-based handle for D2RosterUnitStrc*.
// Stores the GID as identity and resolves to a game pointer on demand
// with per-frame caching via HandleCache.
class Party {
    uint32_t id_ = 0;
    HandleCache cache_;

    void* ResolvePtr() const;

    // Resolves under a read lock held for the result's lifetime; see Resolved.
    template <typename T>
    Resolved<T> Resolve() const {
        return Resolved<T>([this] { return ResolvePtr(); });
    }

   public:
    explicit Party(uint32_t id = 0) : id_(id) {}
    explicit operator bool() const;

    // Extracts GID from a raw RosterUnit pointer.
    static Party FromPtr(void* p);

    // Properties
    Position Pos() const;
    LevelId LevelId() const;
    uint32_t Id() const;
    uint32_t Life() const;
    PartyState PartyState() const;
    uint16_t PartyId() const;
    std::string Name() const;
    CharacterClass ClassId() const;
    uint16_t CharacterLevel() const;

    // Traversal
    Party GetNext() const;

    // Factory - get first party member from roster list
    static std::optional<Party> GetFirst();

    // === Framework-impl (defined inline in Finders.h) ===
    // Walk the roster chain and match by exact GID. Reference parallel:
    // JSParty.cpp:127.
    static std::optional<Party> FindById(uint32_t id);
    // Walk the roster chain and match by case-insensitive ASCII name.
    // Reference parallel: JSParty.cpp:132 (`_stricmp`).
    static std::optional<Party> FindByName(const std::string& name);
};

}  // namespace d2bs::game
