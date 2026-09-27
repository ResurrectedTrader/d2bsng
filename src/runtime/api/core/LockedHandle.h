#pragma once

#include <concepts>
#include <optional>
#include <utility>

#include "game/GameLock.h"

namespace d2bs::game {
class Control;
class Level;
class Party;
class Room;
class StashTab;
class Unit;
}  // namespace d2bs::game

namespace d2bs::runtime::api {

// Native types that are identity handles into live game memory: every accessor resolves
// the handle again, so a validity test and the reads after it only see the same game
// object when one read lock spans them. ClassBase::Unwrap returns a LockedHandle for
// these. Plain value natives (ExitInfo, PresetUnitInfo) and script-owned ones do not
// qualify.
template <typename T>
concept GameHandle = std::same_as<T, game::Unit> || std::same_as<T, game::Room> || std::same_as<T, game::Level> ||
                     std::same_as<T, game::Party> || std::same_as<T, game::Control> || std::same_as<T, game::StashTab>;

// A wrapper's game handle together with a GameReadLock held for this object's lifetime,
// so `if (!*data)` and every `data->...` after it run against one game state: the game
// thread cannot advance a frame or free the object in between.
//
// The lock is shared and re-entrant, so the handle's own accessors (which lock through
// Resolved<T>) nest inside it for free. While one is alive the game thread cannot take
// its write lock, so keep it short: convert the V8 arguments before taking it, copy large
// results (arrays, lists of handles) out and build them after it, and never run script
// (callbacks, getters, valueOf) or wait (WaitForGameReady, GameThread::Execute, a sleep,
// an event pump) while holding it.
//
// Bound to the thread that created it (the lock depth is thread-local).
template <typename T>
class [[nodiscard]] LockedHandle {
    std::optional<game::GameReadLock> lock_;
    T* handle_;

   public:
    explicit LockedHandle(T* handle) : lock_(std::in_place), handle_(handle) {}

    // The destination takes its own (re-entrant, so free) lock before the source drops
    // its one, so the thread holds the lock throughout.
    LockedHandle(LockedHandle&& other) noexcept : lock_(std::in_place), handle_(std::exchange(other.handle_, nullptr)) {
        other.lock_.reset();
    }

    LockedHandle(const LockedHandle&) = delete;
    LockedHandle& operator=(const LockedHandle&) = delete;
    LockedHandle& operator=(LockedHandle&&) = delete;
    ~LockedHandle() = default;

    // False when the receiver was not a wrapper of this class. Says nothing about whether
    // the game object still exists - that is `*handle`'s own bool.
    explicit operator bool() const { return handle_ != nullptr; }

    T* operator->() const { return handle_; }
    T& operator*() const { return *handle_; }
};

}  // namespace d2bs::runtime::api
