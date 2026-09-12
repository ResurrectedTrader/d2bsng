#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <nlohmann/json.hpp>

#include "game/Unit.h"

namespace d2bs::js::characterstate {

// How much of an item to emit. Structural is a strict subset of Full produced by the same
// traversal, so hashing it can never miss a field Full would have sent. It drops the
// derived presentation fields - Description() composes the whole tooltip, which is what
// makes rebuilding an item expensive - and statsLists, whose base array carries live
// durability and quantity. Wearers emit the same document either way.
enum class Detail : uint8_t {
    Structural,
    Full,
};

// One traversal of a unit's serialisable fields, driving two sinks: the wire document
// (JsonVisitor, in UnitJson.cpp) and the change-detection hash (HashVisitor, in
// Fingerprint.cpp). Because both go through this single list of fields, a field added here
// is picked up by both - the fingerprint cannot silently stop covering a field the payload
// still sends, which a second hand-written hash walk could.
//
// It is a flat event stream: scalars carry a key (the JSON sink needs it; the hash sink
// ignores it and relies on the fixed visit order), and nesting is bracketed by
// BeginArray/EndArray around a run of BeginElement/EndElement objects.
class UnitVisitor {
   public:
    UnitVisitor() = default;
    virtual ~UnitVisitor() = default;
    UnitVisitor(const UnitVisitor&) = delete;
    UnitVisitor& operator=(const UnitVisitor&) = delete;
    UnitVisitor(UnitVisitor&&) = delete;
    UnitVisitor& operator=(UnitVisitor&&) = delete;

    virtual void Int(std::string_view key, int64_t value) = 0;
    virtual void Str(std::string_view key, std::string_view value) = 0;
    // A fixed-size numeric array, emitted as a JSON array (magic prefix/suffix slots).
    virtual void Ints(std::string_view key, std::span<const uint16_t> values) = 0;
    // A named array of objects; each element is one BeginElement/EndElement pair.
    virtual void BeginArray(std::string_view key) = 0;
    virtual void BeginElement() = 0;
    virtual void EndElement() = 0;
    virtual void EndArray() = 0;
};

// Drives `visitor` over `unit`'s fields (see UnitVisitor). No-op for an unresolved unit.
void VisitUnit(UnitVisitor& visitor, const game::Unit& unit, Detail detail);

// Serialises a unit to the document the manager consumes - VisitUnit through a JSON sink.
// Every unit carries unitType and classId; the rest is chosen by type. An item adds its
// capture fields (code, quality, itemFlags, affixes, statsLists, ...) plus the fields the
// manager renders with, and recurses into its socket fillers under `sockets`, positional by
// socket index. A player or merc adds identity, skills and - for the player - area and the
// active weapon set; its numbers come from WearerStats instead, which carries the merged
// values the requirement checks actually compare against.
nlohmann::json UnitToJson(const game::Unit& unit, Detail detail = Detail::Full);

// The wearer's merged stat values, which GetStat reads off FullStats so they carry the
// gear contributions an unmerged stat list cannot. Volatile, so this rides its own
// fingerprint rather than the unit document's.
nlohmann::json WearerStats(const game::Unit& wearer);

}  // namespace d2bs::js::characterstate
