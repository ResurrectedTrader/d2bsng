# Coordinate Spaces

Coordinate-space handling in the framework. The subtile -> game-coord conversion
lives entirely inside the game-layer stubs (`src/backends/lod114d/game/*.cpp`); framework code
never multiplies by 5.

## Background: two coordinate spaces

D2's internal structures mix two spaces:

- **Subtiles** - coarse grid. Stored in `pRoom2->dwPosX/dwPosY/dwSizeX/dwSizeY`,
  `pLevel->dwPosX/dwPosY/dwSizeX/dwSizeY`, and `pRoom1->dwXStart/dwYStart/dwXSize/dwYSize`
  (Room1's size fields are also subtile-valued despite the name).
- **Game coordinates** - fine grid. 1 subtile = 5 game-coords. Used by moving-unit paths
  (`pUnit->pPath->xPos/yPos/xTarget/yTarget`), preset units (`PresetUnit->dwPosX/dwPosY`),
  collision-grid origins (`pRoom1->Coll->dwPosGameX/dwPosGameY/dwSizeGameX/dwSizeGameY`),
  and all pathfinding input/output.

Reference d2bs resolves this by sprinkling `* 5` throughout higher-level code (pathfinding,
automap, JS API). We want that conversion to happen exactly once, inside the game-layer
wrappers, and nowhere else.

## Agreed convention

All position/size getters on `game::Room`, `game::Level`, and `game::Unit` return
**game coordinates**. The game-layer implementation (`src/backends/lod114d/game/*.cpp`) is the
only place that knows about the `* 5` factor.

Rectangle-shaped types (`Room`, `Level`, `Control`) expose a single `Bounds()`
method returning `Rect { Position origin; Size size; }` - one resolve, one source
of truth, no `Pos()`/`Size()` pair.

| Wrapper method | Returns | Underlying source |
|---|---|---|
| `Unit::Pos()` | game-coords | `fn::GetUnitX/Y(pUnit)` - already game-coords |
| `Unit::TargetPos()` | game-coords | `pPath->xTarget/yTarget` - already game-coords |
| `Room::Bounds()` | `Rect` (game-coords) | origin `{dwPosX * 5, dwPosY * 5}`, size `{dwSizeX * 5, dwSizeY * 5}` |
| `Level::Bounds()` | `Rect` (game-coords) | origin `{dwPosX * 5, dwPosY * 5}` (treat -1 as 0), size `{dwSizeX * 5, dwSizeY * 5}` |
| `Control::Bounds()` | `Rect` (pixels) | origin `{dwPosX, dwPosY}`, size `{dwSizeX, dwSizeY}` |
| `PresetUnitInfo::posInRoom` | game-coords | `preset->dwPosX/dwPosY` - already game-coords |
| `ExitInfo::pos` | game-coords | derived from room/level offsets already scaled |

Consequences:
- `src/frontends/js/` contains zero `* 5` coordinate scaling.
- `Room::Bounds()` and `Level::Bounds()` each produce a valid game-coord bounding
  box in a single resolve.
- `LevelGrid` input/output is game-coords throughout.

## Exception: `Room::GetStat(statIndex)`

Reference d2bs `JSRoom.cpp` exposes raw game-memory fields per stat index and intentionally
does **not** scale. Scripts rely on these raw values. We preserve that contract:

| statIndex | Field | Unit | Notes |
|---|---|---|---|
| 0 | `pRoom1->dwXStart` | subtiles | despite the name, subtile-valued |
| 1 | `pRoom1->dwYStart` | subtiles | |
| 2 | `pRoom1->dwXSize` | subtiles | |
| 3 | `pRoom1->dwYSize` | subtiles | |
| 4 | `pRoom2->dwPosX` | subtiles | |
| 5 | `pRoom2->dwPosY` | subtiles | |
| 6 | `pRoom2->dwSizeX` | subtiles | |
| 7 | `pRoom2->dwSizeY` | subtiles | |
| 9 | `pRoom1->Coll->dwPosGameX` | game-coords | |
| 10 | `pRoom1->Coll->dwPosGameY` | game-coords | |
| 11 | `pRoom1->Coll->dwSizeGameX` | game-coords | |
| 12 | `pRoom1->Coll->dwSizeGameY` | game-coords | |
| 13 | `pRoom1->Coll->dwPosRoomX` | subtiles | |
| 14 | `pRoom1->Coll->dwPosRoomY` | subtiles | Y twin of index 13; we return this, not the reference's typo (which duplicated index 10's game-coord Y) |
| 15 | `pRoom1->Coll->dwSizeRoomX` | subtiles | |
| 16 | `pRoom1->Coll->dwSizeRoomY` | subtiles | |

`GetStat` is the only sanctioned raw-subtile surface on `Room`. Document this on the
declaration in `src/contract/game/Room.h`.

## JS API surface

Reference d2bs script contract (current behavior - Option A applied):

- `room.x`, `room.y` -> **subtiles** (raw `dwPosX/dwPosY`)
- `room.xsize`, `room.ysize` -> **game-coords** (already `* 5`)
- `level.x/y/xsize/ysize` -> **subtiles** for all four

At the JS-API boundary (`src/frontends/js/api/classes/game/JSRoom.h`,
`src/frontends/js/api/classes/game/JSArea.h`), the game-coord values from `Bounds()`
are divided by `5U` where reference exposed raw-subtile. `JSRoom` divides `x/y` only;
`JSArea` divides all four. This preserves reference-d2bs script compatibility.

The long-term goal is to remove `/5U` from the JS API and
expose game-coords to scripts too; the game-layer `Bounds()` contract is already
forward-compatible with that change.
