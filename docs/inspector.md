# V8 inspector (Chrome DevTools debugging)

How d2bs exposes each running script's V8 isolate to the Chrome DevTools
frontend, so a developer can set breakpoints, step, inspect scopes, and
evaluate expressions against a live bot.

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
(`devtools://devtools/bundled/inspector.html?...&ws=127.0.0.1:<port>/<id>`);
pasting it into Chrome's address bar attaches directly, no chrome://inspect
needed.

Every script isolate always registers a debuggable target; the port only governs
whether the server that exposes them is listening. Attaching costs nothing
measurable until a DevTools client actually connects.

All inspector code lives in `src/framework/components/inspector/`. It is wired
into the script lifecycle from `src/framework/components/script/Script.cpp` and
started/stopped from `ScriptEngine`.

## Why WebSocket

The Chrome DevTools frontend only speaks the Chrome DevTools Protocol (CDP) over
WebSocket when attaching to a remote target. V8 ships the inspector engine
(`v8_inspector`, compiled into our monolith) but NOT a transport - the embedder
must move CDP bytes between the frontend and `V8InspectorSession`. We use
[ixwebsocket](https://github.com/machinezone/IXWebSocket) (vcpkg,
`default-features:false` so no TLS is pulled - localhost needs none).

The pipe transport (`--remote-debugging-pipe`) is a Chrome browser-process
feature for programmatic CDP clients (Puppeteer/Playwright), not something the
DevTools frontend UI can attach over, so it is not an option here.

## Pieces

- **`InspectorServer`** (singleton) - one `DualModeServer` on one localhost
  port. `DualModeServer` is `ix::HttpServer`'s HTTP-vs-WebSocket dispatch
  reimplemented on `ix::WebSocketServer`: `ix::HttpServer` (final, so not
  subclassable) compares the `Upgrade` header value case-SENSITIVELY against
  "websocket", and the browser-side proxy that `chrome://inspect`'s inspect
  link attaches through sends `Upgrade: WebSocket` - those upgrades fell into
  the HTTP handler and 404'd. The value is case-insensitive per RFC 6455 4.2.1
  (fix submitted upstream). The single port serves both:
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
  silently split incoming connections between processes. Attach/detach/reject
  events log under the `inspector` logger.

- **`InspectorTarget`** - a `shared_ptr`-managed, thread-safe inbound CDP queue
  plus its identity. The target id is the script's OS thread id - unique among
  live scripts and stable for the script's run (Windows may recycle a tid across
  runs, so a stale DevTools tab could in principle reattach to a different script;
  low impact). The title shown in chrome://inspect is the script name, prefixed
  with the active profile when set, so multi-box targets are distinguishable. It
  owns nothing V8. Keeping the
  queue here (not on `ScriptInspector`) decouples the server's WebSocket threads
  (producers) from the isolate thread (the sole consumer), so the two have clean
  lifetimes across the window where the isolate thread tears the
  `ScriptInspector` down. Pushing an event also schedules a V8 interrupt (see
  below).

- **`ScriptInspector`** - per-isolate glue. Owns the `V8Inspector`, the protocol
  `Channel` (outbound CDP -> `InspectorServer::Send`), the `V8InspectorClient`
  (pause-loop control), and the `V8InspectorSession` that exists only while a
  DevTools client is attached. Created in `Script::SetupIsolate`, destroyed in
  `Script::TeardownIsolate` before the context is reset. contextGroupId is `1`
  (one context per isolate).

## Threading model

Every `v8_inspector` call must happen on the isolate's own thread. The WebSocket
server runs on its own threads. The queue bridges them.

```
DevTools <--ws--> InspectorServer (ws threads) --push--> InspectorTarget queue
                                                              |
                                            drained on the script's isolate thread
                                                              v
                                              ScriptInspector -> V8InspectorSession
```

Inbound CDP is drained and dispatched on the isolate thread from three places,
all calling `ScriptInspector::DrainIncoming` / the shared pause loop:

1. **Normal execution** - `Script::ExecuteEvents` (the `delay()` pump) drains the
   queue each iteration alongside `v8::platform::PumpMessageLoop`.
2. **Busy scripts** - a script in a tight loop never reaches `delay()`, so a
   queue push schedules `v8::Isolate::RequestInterrupt` (coalesced via an atomic
   flag, isolate kept alive via `weak_ptr` like the GC interrupt). The interrupt
   callback resolves the `ScriptInspector` from a fixed isolate data slot
   (`ScriptInspector::ISOLATE_DATA_SLOT`, set in its ctor and cleared in its dtor)
   and drains. This is what lets `Debugger.pause` take effect on a spinning script.
3. **Paused at a breakpoint** - see below.

Outbound CDP (`Channel::sendResponse` / `sendNotification`) runs on the isolate
thread and hands the message to `InspectorServer::Send`, whose `ix::WebSocket`
send is thread-safe.

## Pausing: `runMessageLoopOnPause`

When the VM hits a breakpoint / `debugger` / caught exception, V8 calls
`V8InspectorClient::runMessageLoopOnPause`, which must **block the isolate
thread** and pump inbound CDP until DevTools sends resume/step (V8 then calls
`quitMessageLoopOnPause`). Blocking the script at a breakpoint is the whole point
and fits our one-thread-per-script model.

**The game must not freeze while a script is paused.** The script thread may be
holding a `GameReadLock` (e.g. paused mid-`getUnit()`); blocking with it held
stalls the game thread's `GameWriteLock` or deadlocks against
`GameThread::Execute`. `RunPauseLoop` therefore releases any game lock this
thread holds for the duration of the pause - a `game::GameReadLockReleaser` for
the read lock a script normally holds, plus a `game::GameWriteLockReleaser`
(defensive: a script thread does not normally hold the write lock, but releasing
both keeps the pause correct regardless of which was held). Each reacquires on
the way out and no-ops when nothing is held; see `docs/game_thread_safety.md`.
Game state is live while paused; a resumed script sees current state.

A DevTools client that disconnects while paused is handled by resuming first and
deferring `V8InspectorSession` teardown to the next normal drain - V8 forbids
destroying a session inside the nested run loop.

## Lifecycle

- `ScriptEngine::Initialize` starts `InspectorServer` when `InspectorPort > 0`;
  `Shutdown` stops it after all scripts have joined (so every target is already
  removed).
- `Script::SetupIsolate` always calls `AttachInspector`, creating the
  `ScriptInspector` and registering an `InspectorTarget` with the server - whether
  or not the server is currently listening. `TeardownIsolate` destroys it before
  `context_.Reset()` so `contextDestroyed` sees a live context.
- Runtime toggle: `ScriptEngine::SetInspector(enabled, port)` (the Settings panel)
  writes the sign-encoded port and reconciles the server - stop, then start on the
  new port when enabled. A toggle never touches the targets: they live for the
  script's run, so flipping the inspector on/off or changing the port just
  restarts the server and the existing targets reappear. Stopping the server
  pushes a Disconnect to every target so a script paused at a breakpoint resumes
  instead of blocking on a transport that is going away.

## Limitations / notes

- Targets advertise `type: "page"`, not `"node"`: chrome://inspect opens node
  targets with the tip-of-tree frontend fetched from
  chrome-devtools-frontend.appspot.com ("Direct node targets will always open
  using ToT front-end" - DevToolsWindow::OpenDevToolsWindow), which stalls
  without that network fetch and never attaches. "page" targets open the
  bundled frontend and attach through the browser proxy, which works against
  our raw V8 sessions (DevTools-only domains like DOM/CSS/Network answer with
  "method not found", which the frontend tolerates).
- Sessions attach with `kNotWaitingForDebugger`: connecting DevTools does not
  halt a running script; breakpoints set afterward pause as expected.
- Script source URLs DevTools sees are mapped to `file:///` form relative to
  the script base, e.g. `file:///libs/Town.js` (`InspectorClient::
  resourceNameToUrl`, and the target `url` in `/json` matches), so gutter-set
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
  `console`, so the discriminator is `ScriptInspector::IsEvaluating()` - set via a
  `ReplEvalScope` around `dispatchProtocolMessage`, so it's true exactly while the
  inspector is running a REPL evaluate.
- The port is localhost-only and opt-in; it is an open debug socket on the
  machine while enabled.
