# Host hooks: extension points for an embedding host

d2bs.dll is injected into Game.exe and runs the game in real time. A program can also *host* the frontend and a
backend itself - for example a simulator that maps Game.exe into its own process and runs the game, the scripts and
the clock deterministically and faster than real time. Such a host needs a handful of places in the shared code to
call it. `src/contract/host/Host.h` (`d2bs::host`) declares them; nothing on main defines a host other than the
in-process one.

## Design: link-time hooks, not a table

The hooks are plain functions resolved when the executable links, like the backend's `game::` contract symbols:

* the contract declares them (`host/Host.h`); the frontend (`runtime`) and the contract's own inline code call them;
  no backend does;
* every executable defines them once. d2bs.dll (and `js_tests`) compile `src/core/host/DefaultHost.cpp`, the
  in-process host: script threads are plain `std::jthread`s, waits are real-time sleeps, everything else does
  nothing. An embedding host compiles its own instead. `DefaultHost.cpp` is not part of `core.lib`, so a host with
  its own definitions links without it.

With LTO the in-process definitions inline into their callers: there is no indirect call, no table, no load, and
d2bs.dll runs the code it ran before the hooks existed. Checked by comparing the disassembly of d2bs.dll's hot paths
against origin/main (cdb `uf` on the image, addresses masked): `MethodTrampoline`, `PropertyGetterTrampoline` and
`PropertySetterTrampoline` (every native call) are instruction-for-instruction identical, and `GameLoop::OnSleep`
(every frame) keeps the same instruction count. A function-pointer table would have cost a load and a compare on
every native call and frame.

## The hooks

Only the points nothing outside the frontend can observe at a deterministic moment: the script threads' lifecycle,
and the hand-offs between the scripts and the game thread.

| Hook | Called from | Why it has to be a hook |
| --- | --- | --- |
| `StartScriptThread(name, body, context)` | `Script::Start`, on the starting thread | the host owns the script's thread: it registers it (in start order, which is deterministic) before the thread exists and runs the body under its schedule; a thread-start callback from outside cannot tell which script a new thread is (V8 and game threads start concurrently) |
| `JoinScriptThread(thread)` | `Script::Join` | `std::jthread::join` is a real wait no detour turns into a hand-off: a host that schedules the script hands out its remaining turns until it ends |
| `ScriptWait(remaining, slice, mustWakeEachSlice, context)` | `Script::ExecuteEvents` (delay) | the wait between event pumps. In process: a real sleep of at most `slice` (`IdleSleepIntervalMs`). A host that wakes the script for every event sleeps to the deadline in one hand-off instead of one per slice (speed only), except for a script with render listeners |
| `OnScriptWake(thread, delayMs)` | `Script::Stop`, `Script::PostEvent` | the other half of `ScriptWait`: wakes a script sleeping to its deadline for an event, timer or stop |
| `OnNativeCall(name)` | every native binding call (the trampolines) | a call quantum on deterministic points (a script that never blocks yields); counting clock reads from outside misses spins on game state |
| `RunFrame()` | `GameLoop::OnSleep`, with the frame's write lock released | the host runs the scripts' turns for the frame instead of the timed wait; on a held-still virtual clock that wait never ends |
| `WaitForTask(state)` | `GameThread::Execute`, with the game read lock released | the wait for a posted task, until its `TaskState` is Done or Cancelled. In process: `state.wait` (an atomic wait), a blocking wait the host cannot see; a host that schedules the thread gives up its turns until the game thread ran or the script cancelled the task |

`ScriptWait`'s long wait and `OnScriptWake` are the only optional part. Without them a script's delay polls in slices
through the host's `Sleep` detour, as d2bs.dll does natively: the native simulator's runs stay deterministic, but take
3.4x the turns and run about 10% slower (157x instead of 175x real time on a fresh sorceress, 3 x 600 virtual s).

## Script order

`getScript()` / `getNext()` and every event fan-out visit the scripts by path (case-insensitive;
`ScriptEngine::SortByPath`), in every build: the scripts are keyed by OS thread id, which differs from run to run.
Paths are unique among the listed scripts, since starting a script stops the one already running at its path. `Script.threadid` is the OS thread id in every build.

## What is not a hook

A host gets the rest from outside, with mechanisms d2bs.dll has anyway:

* **The backend's traffic** (game packets in and out, console output, draw, IPC): the host compiles the backend's
  sources itself and leaves out the files it replaces with its own - `game/GameCallbacks.cpp` (`game::InstallHooks` /
  `RemoveHooks`: wrap the callback table, then `lod114d::hooks::Install`) and `game/Ipc.cpp` (`game::SendIPC`, the
  manager's messages). Those files hold nothing else, so leaving one out removes exactly those functions.
* **Settings**: the backend's `-noconsole` launch option (no console window), `core::speedhack::Remove()` after the
  backend's install, V8 flags through `v8::V8::SetFlagsFromString` before `runtime::Host::Initialize`, and the ini's
  `EngineSingleThreaded`.
* **The game's base**: `lod114d::imports::SetGameModuleBase` before `Bridge::Init` (every import, patch and hook
  offset is relative to `GameModuleBase()`, which Bridge::Init sets to the process image unless it was set first).
## Rules

* Nothing in `runtime`, `contract`, `core`, `utils` or a backend may name a host or its types; a host's state lives
  in the host.
* A hook's in-process definition (`DefaultHost.cpp`) must leave d2bs.dll's behaviour exactly as it was. When adding a
  hook, compare the disassembly of the call site's function with and without it, as above.
* Keep the list short: a hook belongs here only when a host cannot get the same effect from the outside (a
  replaced backend file, a Detours hook, a launch option, an existing API).
