#pragma once

#include <cassert>
#include <cstdint>
#include <mutex>
#include <vector>

#include "game/GameLock.h"
#include "imports/D2Common.h"
#include "imports/extras/D2ActiveRoomStrc.h"
#include "imports/extras/D2DrlgActStrc.h"
#include "imports/extras/D2DrlgLevelStrc.h"
#include "imports/extras/D2DrlgRoomStrc.h"
#include "imports/extras/D2DrlgStrc.h"

namespace d2bs::game {

using lod114d::imports::extras::D2ActiveRoomStrc;
using lod114d::imports::extras::D2DrlgActStrc;
using lod114d::imports::extras::D2DrlgLevelStrc;
using lod114d::imports::extras::D2DrlgRoomStrc;
using lod114d::imports::extras::D2DrlgStrc;

// RAII helper that keeps a room's data (its D2ActiveRoomStrc, `pRoom`) loaded
// for the guard's lifetime, through D2COMMON_AddRoomData / RemoveRoomData
// (DUNGEON_SetClientIsInSight / DUNGEON_UnsetClientIsInSight).
//
// Ownership: the guard that finds `pRoom` null adds the data and records the
// room in `addedRooms_`. Every guard on a recorded room - from any thread, or
// nested on one - holds a reference to that record, and the last one out
// removes the data. Data the game loaded itself is never recorded, so we never
// remove it.
//
// Locking model:
//
//   1. The guard's first member is `GameReadLock readLock_`, so the read
//      lock is taken on construction and released only after the destructor
//      body has finished - RemoveRoomData therefore runs while the read
//      lock is still held. The acquisition is recursive (no-op when the
//      caller already holds it) and no-ops on the game thread (which holds
//      GameWriteLock instead). While any guard is alive the game thread
//      cannot acquire the write lock to run its frame work, so the game
//      cannot load or unload a room's data behind the records. The guard's
//      scope must never hand the lock back (GameThread::Execute, PollUntil);
//      debug builds assert that.
//
//   2. The Add/Remove pair runs inline on the calling thread (no
//      GameThread::Execute round-trip). Reference d2bs calls these directly
//      from script threads; the engine itself calls them only from the
//      frame body under GameWriteLock, which is mutually exclusive with our
//      GameReadLock. A static mutex serialises Add/Remove and the records
//      between script threads.
//
//   3. A room whose data is already loaded (the common case for the level
//      the player is in) costs one uncontended mutex round-trip and no
//      AddRoomData. `runtime::navigation::GetExits` and the pathfinder's
//      `BuildLevelGrid` both walk every room in the level and construct a
//      guard per room; a GameThread::Execute round-trip per room would be
//      paced by the engine's sleep cadence - hundreds of round-trips -> tens
//      of seconds on large levels like Frigid Highlands.
class RoomDataGuard {
   public:
    explicit RoomDataGuard(D2DrlgRoomStrc* drlgRoom) {
        if (drlgRoom == nullptr || drlgRoom->pLevel == nullptr) {
            return;
        }
        auto* drlg = drlgRoom->pLevel->pDrlg;
        if (drlg == nullptr || drlg->pAct == nullptr) {
            return;
        }

        std::lock_guard lock(mutex_);
        if (auto* added = FindAdded(drlgRoom)) {
            ++added->guards;
            drlgRoom_ = drlgRoom;
            return;
        }
        if (drlgRoom->pRoom != nullptr) {
            return;
        }
        lod114d::imports::d2common::DUNGEON_SetClientIsInSight(drlg->pAct, drlgRoom->pLevel->nLevelId,
                                                               drlgRoom->nTileXPos, drlgRoom->nTileYPos, nullptr);
        addedRooms_.push_back({.room = drlgRoom, .act = drlg->pAct, .guards = 1});
        drlgRoom_ = drlgRoom;
    }
    ~RoomDataGuard() {
        if (drlgRoom_ == nullptr) {
            return;
        }
        assert(epoch_ == GameReadLock::ReleaseEpoch() && "game lock released inside a RoomDataGuard");
        std::lock_guard lock(mutex_);
        auto* added = FindAdded(drlgRoom_);
        assert(added != nullptr && added->guards > 0);
        if (--added->guards != 0) {
            return;
        }
        lod114d::imports::d2common::DUNGEON_UnsetClientIsInSight(added->act, drlgRoom_->pLevel->nLevelId,
                                                                 drlgRoom_->nTileXPos, drlgRoom_->nTileYPos, nullptr);
        *added = addedRooms_.back();
        addedRooms_.pop_back();
    }
    RoomDataGuard(const RoomDataGuard&) = delete;
    RoomDataGuard& operator=(const RoomDataGuard&) = delete;
    RoomDataGuard(RoomDataGuard&&) = delete;
    RoomDataGuard& operator=(RoomDataGuard&&) = delete;

   private:
    struct AddedRoom {
        D2DrlgRoomStrc* room = nullptr;
        D2DrlgActStrc* act = nullptr;
        uint32_t guards = 0;
    };

    // Holds only rooms with a live guard - a handful at most - so a linear scan.
    static AddedRoom* FindAdded(const D2DrlgRoomStrc* drlgRoom) {
        for (auto& added : addedRooms_) {
            if (added.room == drlgRoom) {
                return &added;
            }
        }
        return nullptr;
    }

    // Declared first: constructed before all other members, destroyed last,
    // so the read lock brackets both AddRoomData and RemoveRoomData.
    // Recursive - no-op if the caller already holds a read lock or GameWriteLock.
    // NOLINTBEGIN(clang-diagnostic-padded) - benign alignment pad on stack object
    GameReadLock readLock_;
    // Set while this guard holds a reference on a record in `addedRooms_`.
    D2DrlgRoomStrc* drlgRoom_ = nullptr;
#ifndef NDEBUG
    uint64_t epoch_ = GameReadLock::ReleaseEpoch();
#endif
    // NOLINTEND(clang-diagnostic-padded)

    // Serialises Add/Remove and `addedRooms_` between script threads. The
    // engine only mutates the room chain under GameWriteLock, which is
    // mutually exclusive with readLock_, so the engine never needs it.
    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, clang-diagnostic-unique-object-duplication)
    inline static std::mutex mutex_;
    inline static std::vector<AddedRoom> addedRooms_;
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, clang-diagnostic-unique-object-duplication)
};

}  // namespace d2bs::game
