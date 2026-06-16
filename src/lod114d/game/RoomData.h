#pragma once

#include <mutex>

#include "game/GameLock.h"
#include "imports/D2Common.h"
#include "imports/extras/D2ActiveRoomStrc.h"
#include "imports/extras/D2DrlgActStrc.h"
#include "imports/extras/D2DrlgLevelStrc.h"
#include "imports/extras/D2DrlgRoomStrc.h"
#include "imports/extras/D2DrlgStrc.h"

namespace d2bs::game {

using d2bs::imports::extras::D2ActiveRoomStrc;
using d2bs::imports::extras::D2DrlgActStrc;
using d2bs::imports::extras::D2DrlgLevelStrc;
using d2bs::imports::extras::D2DrlgRoomStrc;
using d2bs::imports::extras::D2DrlgStrc;

// RAII helper: D2COMMON_AddRoomData on construction when pRoom is null,
// D2COMMON_RemoveRoomData on destruction if we were the one to add it.
//
// Locking model:
//
//   1. The guard's first member is `GameReadLock readLock_`, so the read
//      lock is taken on construction and released only after the destructor
//      body has finished - RemoveRoomData therefore runs while the read
//      lock is still held. The acquisition is recursive (no-op when the
//      caller already holds it) and no-ops on the game thread (which holds
//      GameWriteLock instead). With the read lock held, the game thread
//      cannot acquire the write lock and re-stream rooms underneath us, so
//      `pRoom` cannot transition between observation and dereference.
//
//   2. The Add/Remove pair runs inline on the calling thread (no
//      GameThread::Execute round-trip). Reference d2bs calls these directly
//      from script threads; the engine itself calls them only from the
//      frame body under GameWriteLock, which is mutually exclusive with our
//      GameReadLock. A static mutex serialises Add/Remove between script
//      threads so two threads cannot race on the same room's allocator
//      state.
//
//   3. Fast path: when `pRoom != nullptr` on entry (the common case for
//      the level the player is in - every room is already loaded), the
//      ctor returns without taking the static mutex or calling
//      AddRoomData. This is the critical optimisation: `Level::GetExits`
//      and the pathfinder's `BuildLevelGrid` both walk every room in the
//      level and construct a guard per room, which otherwise costs one
//      GameThread::Execute round-trip per room (paced by the engine's
//      sleep cadence - hundreds of round-trips -> tens of seconds on
//      large levels like Frigid Highlands).
//
// Known race (pre-existing): when two script threads concurrently observe
// `pRoom == nullptr`, only the first to take the mutex calls AddRoomData;
// the second sees pRoom non-null in its own check and proceeds without
// taking ownership. If the first guard then exits scope and runs
// RemoveRoomData while the second is still using the room data, the second
// thread reads freed memory. Reference d2bs has the same race. Fixing this
// requires per-room reference counting.
class RoomDataGuard {
   public:
    explicit RoomDataGuard(D2DrlgRoomStrc* drlgRoom) : drlgRoom_(drlgRoom) {
        if (drlgRoom_ == nullptr || drlgRoom_->pLevel == nullptr) {
            return;
        }
        if (drlgRoom_->pRoom != nullptr) {
            return;
        }
        auto* drlg = drlgRoom_->pLevel->pDrlg;
        if (drlg == nullptr || drlg->pAct == nullptr) {
            return;
        }
        act_ = drlg->pAct;

        std::lock_guard lock(mutex_);
        // Re-check under the mutex: another script thread may have added the
        // data between our unlocked observation above and acquiring the lock.
        if (drlgRoom_->pRoom != nullptr) {
            return;
        }
        imports::d2common::DUNGEON_SetClientIsInSight(act_, drlgRoom_->pLevel->nLevelId, drlgRoom_->nTileXPos,
                                                      drlgRoom_->nTileYPos, nullptr);
        owned_ = true;
    }
    ~RoomDataGuard() {
        if (!owned_ || act_ == nullptr || drlgRoom_ == nullptr) {
            return;
        }
        std::lock_guard lock(mutex_);
        imports::d2common::DUNGEON_UnsetClientIsInSight(act_, drlgRoom_->pLevel->nLevelId, drlgRoom_->nTileXPos,
                                                        drlgRoom_->nTileYPos, nullptr);
    }
    RoomDataGuard(const RoomDataGuard&) = delete;
    RoomDataGuard& operator=(const RoomDataGuard&) = delete;
    RoomDataGuard(RoomDataGuard&&) = delete;
    RoomDataGuard& operator=(RoomDataGuard&&) = delete;

   private:
    // Declared first: constructed before all other members, destroyed last,
    // so the read lock brackets both AddRoomData and RemoveRoomData.
    // Recursive - no-op if the caller already holds a read lock or GameWriteLock.
    // NOLINTBEGIN(clang-diagnostic-padded) - benign alignment pad on stack object
    GameReadLock readLock_;
    bool owned_ = false;
    D2DrlgRoomStrc* drlgRoom_ = nullptr;
    D2DrlgActStrc* act_ = nullptr;
    // NOLINTEND(clang-diagnostic-padded)

    // Serialises Add/Remove between script threads. The engine only mutates
    // the room chain under GameWriteLock, which is mutually exclusive with
    // readLock_, so the engine never needs to take this mutex.
    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, clang-diagnostic-unique-object-duplication)
    inline static std::mutex mutex_;
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, clang-diagnostic-unique-object-duplication)
};

}  // namespace d2bs::game
