#include "config/RealmRegistry.h"

#include <algorithm>

namespace d2bs::core::config {

RealmRegistry& RealmRegistry::Instance() {
    static RealmRegistry instance;
    return instance;
}

std::vector<RealmRegistry::Realm> RealmRegistry::All() const {
    std::scoped_lock lock(mutex_);
    return realms_;
}

bool RealmRegistry::Add(std::string_view name, std::string_view host) {
    if (name.empty() || host.empty()) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    auto it = std::ranges::find_if(realms_, [&](const Realm& realm) { return realm.name == name; });
    if (it != realms_.end()) {
        it->host = std::string(host);
    } else {
        realms_.push_back(Realm{.name = std::string(name), .host = std::string(host)});
    }
    return true;
}

bool RealmRegistry::AddSpec(std::string_view spec) {
    const auto colon = spec.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    const std::string_view name = spec.substr(0, colon);
    const std::string_view host = spec.substr(colon + 1);
    if (name.empty() || host.empty() || host.find(':') != std::string_view::npos) {
        return false;  // only name:host is accepted (a host with a ':' - e.g. a port - is rejected)
    }
    return Add(name, host);
}

}  // namespace d2bs::core::config
