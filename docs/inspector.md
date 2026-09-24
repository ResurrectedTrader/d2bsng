# Script inspector (Chrome DevTools debugging)

How d2bs exposes each running script's isolate to the Chrome DevTools
frontend, so a developer can set breakpoints, step, inspect scopes, and
evaluate expressions against a live bot.

Available on the V8 build only: see "Where it lives" below.

Disabled by default. A single `[settings]/InspectorPort` controls it: a positive
value runs the server on that port; `0` (the default) leaves it off. Toggle at
runtime from the console Settings panel ("Debugging (V8 inspector)") - the
checkbox flips the sign (a disabled value keeps the port as a negative magnitude
so re-enabling restores it) and the port field sets the magnitude.

The default port is `9229` - the Node.js inspector default, which
`chrome://inspect` watches out of the box, so each running script appears as a
"Remote Target" with no manual setup. For a non-default port, add
`127.0.0.1:<port>` under `chrome://inspect` -> "Discover network targets" ->
Configure. Prefer `127.0.0.1` over `localhost` in that list: `localhost` can
resolve to `::1` first and the server binds IPv4 loopback only, which stalls
both discovery and click-inspect (Chrome connects to the configured host, not
the host in our URLs).

Each target's `/json` entry also carries a ready-made `devtoolsFrontendUrl`
(`devtools://devtools/bundled/js_app.html?...&ws=127.0.0.1:<port>/<id>`);
pasting it into Chrome's address bar attaches directly, no chrome://inspect
needed.

Every script isolate always registers a debuggable target; the port only governs
whether the server that exposes them is listening. Attaching costs nothing
measurable until a DevTools client actually connects.

## Where it lives: engine-neutral, over `ub::Inspector`

The frontend (`src/frontends/runtime/`) is written against unibind and names no
engine; which engine a build runs on is decided by which glue DLL links it
(`glue/js-v8-lod114d` links `unibind_backend_v8.lib` + V8, `glue/js-sm-lod114d`
links `unibind_backend_spidermonkey.lib` + SpiderMonkey). The inspector is no
exception: it is written against unibind's `unibind/inspector.h`, an
engine-neutral Chrome DevTools inspector shaped after V8's `v8_inspector` -
`ub::Inspector` (one per isolate), `ub::InspectorSession` (one per DevTools
connection) and `ub::InspectorClient` (what the inspector calls back into,
written by us).

Whether there is an inspector is a run-time answer, not a build-time one:
`ub::Inspector::Supported()` is true on V8 and false on SpiderMonkey, whose
debugging surface is a JavaScript `Debugger` object with no protocol to attach
to. Both glue DLLs link the same frontend and the same inspector code; on
SpiderMonkey it simply never does anything.

All of it lives in `components/inspector/`: `InspectorServer`,
`InspectorTarget`, `ScriptInspector` (below). No engine header anywhere. The
script lifecycle in `components/script/Script.cpp` creates each script's
`ScriptInspector` (`ScriptInspector::Create`, null where there is no inspector)
and `ScriptEngine` starts / stops the server, only where
`ub::Inspector::Supported()`; the console hides the debugging settings where it
is false.

## Why WebSocket

The Chrome DevTools frontend only speaks the Chrome DevTools Protocol (CDP) over
WebSocket when attaching to a remote target. The engine ships the inspector
(`v8_inspector`, compiled into the V8 monolith, reached through
`ub::Inspector`) but NOT a transport - the embedder must move CDP bytes between
the frontend and the `ub::InspectorSession`. We use
[ixwebsocket](https://github.com/machinezone/IXWebSocket) (vcpkg,
`default-features:false` so no TLS is pulled - localhost needs none). The
frontend library compiles it; both glue DLLs link `ixwebsocket.lib`.

The pipe transport (`--remote-debugging-pipe`) is a Chrome browser-process
feature for programmatic CDP clients (Puppeteer/Playwright), not something the
DevTools frontend UI can attach over, so it is not an option here.

## Pieces

- **`InspectorServer`** (singleton) - one `ix::HttpServer` on one localhost
  port, serving both:
  - HTTP `GET /json`, `/json/list`, `/json/version` - the discovery endpoints
    `chrome://inspect` polls. `/json` lists every registered target with its
    `webSocketDebuggerUrl`. Answered with `Connection: close` (the server
    closes after each response, so it tells the poller not to reuse).
  - WebSocket upgrades at `/<targetId>` - routed by URL path to the matching
    script.
  Owns the `targetId -> active WebSocket` map and `Send()`. One DevTools client
  per target at a time (a second connection is closed). `Start()` probes the
  port with `SO_EXCLUSIVEADDRUSE` first: ixwebsocket binds with `SO_REUSEADDR`,
  which on Windows lets a second multi-boxed instance bind the same port and
  silently split incoming connections between processes. The probe is retried
  for up to two seconds, because a restart (the Settings toggle, a port change)
  binds again immediately and the old listener's sockets can outlive `stop()`
  by a moment - long enough to refuse a port that is about to be free. A port
  another instance really holds stays busy for the whole window. Attach /
  detach / reject events log under the `inspector` logger.

- **`InspectorTarget`** - a `shared_ptr`-managed, thread-safe inbound CDP queue
  plus its identity. The target id is the script's OS thread id - unique among
  live scripts and stable for the script's run (Windows may recycle a tid across
  runs, so a stale DevTools tab could in principle reattach to a different script;
  low impact). The title shown in chrome://inspect is the script name, prefixed
  with the active profile when set, so multi-box targets are distinguishable. It
  owns nothing of the engine. Keeping the
  queue here (not on `ScriptInspector`) decouples the server's WebSocket threads
  (producers) from the isolate thread (the sole consumer), so the two have clean
  lifetimes across the window where the isolate thread tears the
  `ScriptInspector` down. Pushing an event also requests an inspector dispatch
  (see below).

- **`ScriptInspector`** - per-isolate glue: the `ub::InspectorClient`. Owns the `ub::Inspector` for the script's isolate
  and the `ub::InspectorSession` that exists only while a DevTools client is
  attached. As the client it forwards outbound CDP
  (`SendProtocolMessage` -> `InspectorServer::Send`), runs pauses
  (`RunMessageLoopOnPause` / `QuitMessageLoopOnPause`), supplies timestamps
  (`CurrentTimeMs`) and maps script names to URLs (`ResourceNameToUrl`). Created
  by `ScriptInspector::Create` when the script sets up its isolate (it announces the
  script's context with `ContextCreated`, titled like the target), destroyed
  before the script's context is reset (`ContextDestroyed` first). It holds the
  script's `shared_ptr<ub::Isolate>`, so the isolate outlives the inspector, as
  unibind requires.

## Threading model

Every `ub::Inspector` / `ub::InspectorSession` call must happen on the
isolate's own thread; other threads reach it only through the
`ub::InspectorDispatcher` that `Inspector::Dispatcher()` hands out. The WebSocket server
runs on its own threads. The queue bridges them.

```
DevTools <--ws--> InspectorServer (ws threads) --push--> InspectorTarget queue
                                                              |
                                            drained on the script's isolate thread
                                                              v
                                            ScriptInspector -> ub::InspectorSession
```

Inbound CDP is drained and dispatched on the isolate thread from three places,
all calling `ScriptInspector::DrainIncoming` / the shared pause loop:

1. **Normal execution** - `Script::ExecuteEvents` (the `delay()` pump) calls
   `ScriptInspector::DrainIncoming` each iteration alongside the engine's job pump.
2. **Busy scripts** - a script in a tight loop never reaches `delay()`, so a
   queue push requests an inspector dispatch (`ub::InspectorDispatcher::RequestDispatch`,
   coalesced via an atomic flag). unibind runs it on the isolate thread at the
   next safe point - inside running script, or at the next `PumpJobs` if the
   isolate is idle - and, unlike `ub::Isolate::RequestInterrupt`'s callback, it
   may dispatch protocol messages, which run script (a DevTools evaluate). The
   callback carries the `ScriptInspector` as its `ub::CallbackData`; that is
   never stale, because requests still waiting when the `ub::Inspector` is
   destroyed are dropped. The target holds the dispatcher as a `shared_ptr`
   taken when it is made; the dispatcher is safe to call from any thread even
   while, or after, the inspector is destroyed (it declines then), so the target
   is never detached and needs no lock around it. This is what lets
   `Debugger.pause` take effect on a spinning script.
3. **Paused at a breakpoint** - see below.

Events are taken off the target in batches into a backlog and processed one at
a time. An event that ends a pause (a resume, or a disconnect) leaves the rest
of the batch in the backlog for whoever runs next - the pause level it returns
to, or the script once the pause unwinds, which is woken with a dispatch
request rather than left waiting for the next message.

Outbound CDP (`InspectorClient::SendProtocolMessage`) runs on the isolate thread
and hands the message to `InspectorServer::Send`, whose `ix::WebSocket` send is
thread-safe.

## Pausing: `RunMessageLoopOnPause`

When the VM hits a breakpoint / `debugger` / caught exception, the inspector
calls `InspectorClient::RunMessageLoopOnPause`, which must **block the isolate
thread** and pump inbound CDP until DevTools sends resume/step (the inspector
then calls `QuitMessageLoopOnPause`). Blocking the script at a breakpoint is the
whole point and fits our one-thread-per-script model. Pauses nest - an evaluate
run during a pause can pause again - so the loop saves and restores its flags
per level.

**The game must not freeze while a script is paused.** The script thread may be
holding a `GameReadLock` (e.g. paused mid-`getUnit()`); blocking with it held
stalls the game thread's `GameWriteLock` or deadlocks against
`GameThread::Execute`. `RunMessageLoopOnPause` therefore releases any game lock
this thread holds for the duration of the pause - a `game::GameReadLockReleaser`
for the read lock a script normally holds, plus a `game::GameWriteLockReleaser`
(defensive: a script thread does not normally hold the write lock, but releasing
both keeps the pause correct regardless of which was held). Each reacquires on
the way out and no-ops when nothing is held; see `docs/game_thread_safety.md`.
Game state is live while paused; a resumed script sees current state.

A DevTools client that disconnects while paused is handled by resuming every
pause level (`InspectorSession::Resume`) and deferring the session's teardown to
the next normal drain - a session is not destroyed inside a pause. A reconnect
that arrives in the meantime waits in the backlog until the pause has unwound.

## Lifecycle

- `ScriptEngine::Initialize` starts the server (`InspectorServer::Start`) when
  `InspectorPort > 0` and the engine has an inspector; `Shutdown` stops it after
  all scripts have joined (so every target is already removed).
- Every script calls `ScriptInspector::Create` while setting up its isolate,
  creating the `ScriptInspector` and registering an `InspectorTarget` with the server -
  whether or not the server is currently listening. The script destroys it before
  resetting its context so `ContextDestroyed` sees a live context. With no
  inspector (SpiderMonkey) `Attach` returns null and the script runs without one.
- Runtime toggle: `ScriptEngine::SetInspector(enabled, port)` (the Settings panel)
  writes the sign-encoded port and reconciles the server - stop, then start on the
  new port when enabled. A toggle never touches the targets: they live for the
  script's run, so flipping the inspector on/off or changing the port just
  restarts the server and the existing targets reappear. Stopping the server
  pushes a Disconnect to every target so a script paused at a breakpoint resumes
  instead of blocking on a transport that is going away.

## Limitations / notes

- Targets advertise `type: "node"` with the `js_app.html` frontend - Node's own
  combination, and the two halves only work together. `type` is what decides
  whether DevTools believes the target has a DOM: a `"page"` target opens on an
  Elements panel that can never fill, and `v8only=true` does not undo that
  because `inspector.html` ignores it. `js_app.html` is the V8-only frontend
  (Sources, Console, Memory, Profiler) and does read `v8only`, but only a
  `"node"` target is routed to it - a `"page"` target goes through the browser
  proxy to Chrome's own frontend regardless of the URL we advertise. Changing
  one half alone gets you an empty Elements panel or a window that never
  attaches. DevTools-only domains like DOM/CSS/Network answer with "method not
  found", which the frontend tolerates.
- `ub::Inspector::Connect` makes a fully trusted session that is not waiting
  for a debugger (V8's `kNotWaitingForDebugger`): connecting DevTools does not
  halt a running script; breakpoints set afterward pause as expected.
- Timestamps (`ScriptInspector::CurrentTimeMs`) are real wall-clock time: the
  script thread opts into the speedhack, so the call runs under a
  `SpeedhackDisabledScope`, or timestamps would race ahead at speed > 1.
- Script source URLs DevTools sees are mapped to `file:///` form relative to
  the script base, e.g. `file:///libs/Town.js` (`ScriptInspector::
  ResourceNameToUrl`, and the target `url` in `/json` matches), so gutter-set
  breakpoints bind in the Sources panel, the install path stays out of
  DevTools, and the URLs are stable across machines. Only the URL changes - the
  compile origin stays the raw Windows path (kolbot's require.js regex depends
  on it).
- Console is routed by caller. `SetupIsolate` installs `console` as an accessor
  (`InstallConsoleRouting`): a script's `console.log` resolves to the script's own
  print-routed polyfill (kolbot installs one via `global.console = global.console
  || ...`, which the accessor's setter captures), landing in the d2bs console;
  `console.log` typed in the DevTools Console resolves to V8's built-in console
  and shows in the DevTools Console panel. Both callers share one global
  `console`, so the discriminator is `ScriptInspector::IsEvaluating()` - set via a `ReplEvalScope` around
  `InspectorSession::DispatchProtocolMessage`, so it's true exactly while the
  inspector is running a REPL evaluate. On SpiderMonkey it is always false.
- The port is localhost-only and opt-in; it is an open debug socket on the
  machine while enabled.
