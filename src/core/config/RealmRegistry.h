#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace d2bs::core::config {

// Store of the extra Battle.net realms added via the `-realm name:host` launch
// option. Seeded once at backend init; thereafter read-only.
//
// A game-version backend reads these to inject them into D2's in-memory gateway
// list (so the client shows and connects to them) and to enumerate the full
// realm list for scripts. See the lod114d backend's realms hook (hooks/Realms).
class RealmRegistry {
   public:
    struct Realm {
        std::string name;  // display name shown in the gateway selector
        std::string host;  // hostname or dotted-quad IP of the BNCS server
    };

    static RealmRegistry& Instance();

    // Snapshot of all realms, in insertion order.
    [[nodiscard]] std::vector<Realm> All() const;

    // Add or replace the realm `name` with a host. A duplicate name overwrites
    // in place (keeping its position). Empty name or host is rejected (returns
    // false).
    bool Add(std::string_view name, std::string_view host);

    // Add or replace from a single "name:host" spec, as passed to `-realm`.
    // Returns false if it can't be split into a non-empty name and host (a host
    // containing a ':' - e.g. an included port - is rejected; only name:host is
    // accepted).
    bool AddSpec(std::string_view spec);

   private:
    RealmRegistry() = default;

    mutable std::mutex mutex_;
    std::vector<Realm> realms_;  // insertion order preserved; small N, linear scan
};

}  // namespace d2bs::core::config
