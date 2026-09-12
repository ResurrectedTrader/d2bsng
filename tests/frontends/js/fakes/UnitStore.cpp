#include "fakes/UnitStore.h"

#include <utility>

namespace d2bs::test {

namespace {

UnitStore& Store() {
    static UnitStore store;
    return store;
}

void Link(UnitStore& store, const std::vector<uint32_t>& list) {
    for (size_t i = 0; i < list.size(); ++i) {
        store.items.at(list[i]).next = i + 1 < list.size() ? list[i + 1] : 0;
    }
}

}  // namespace

uint32_t UnitStore::Add(FakeItem item) {
    if (item.id == 0) {
        item.id = static_cast<uint32_t>(items.size()) + 1;
    }
    const uint32_t id = item.id;
    if (item.owner == 0) {
        player.items.push_back(id);
    } else {
        items.at(item.owner).children.push_back(id);
    }
    items[id] = std::move(item);
    return id;
}

void UnitStore::Seal() {
    Link(*this, player.items);
    for (const auto& [id, item] : items) {
        Link(*this, item.children);
    }
}

const FakeItem* UnitStore::Find(uint32_t id) const {
    const auto it = items.find(id);
    return it == items.end() ? nullptr : &it->second;
}

UnitStore& Units() {
    return Store();
}

void ResetUnits() {
    Store() = UnitStore{};
}

}  // namespace d2bs::test
