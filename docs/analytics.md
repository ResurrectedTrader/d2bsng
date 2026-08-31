# Usage analytics

d2bsng can report **anonymous usage analytics** to [Aptabase](https://aptabase.com)
(a privacy-focused, GDPR-friendly app-telemetry service). The goal is narrow: a
maintainer-visible count of active installs and the version / OS / game-version
distribution, so support and release decisions rest on data instead of guesses.

It is **on by default and opt-out** (`-noanalytics` / `D2BS_ANALYTICS_DISABLE`),
and stays entirely anonymous - the only identity an event carries is a derived
`installId`. A launch produces one startup event plus one event per distinct
profile it runs. There is no per-action tracking, no keystroke/screen capture,
and no game data of any kind.

- Component: `src/frontends/js/components/analytics/Analytics.h` / `.cpp`
- Launch switches: `src/backends/lod114d/game/LaunchOptions.cpp`
  (`-noanalytics`)
- Contract surface: `game::GetAnalyticsLaunchOptions()`,
  `game::GetActiveFeatures()` and `game::GetBackendVersion()` in
  `src/contract/game/GameHelpers.h`
- Lifecycle: started in `Host::DoInitialize`, stopped in `Host::Shutdown`,
  alongside the update checker.

## What is collected

Two event types. `session_start` is emitted once per launch (after a short settle
delay); `profile_active` follows for each distinct profile the launch runs (see
[The `profileHash`](#the-profilehash)). Both carry the `installId` so they join up
per install; all events from one launch share a `sessionId`.

`session_start` carries:

**System properties** (Aptabase's first-class fields):

| Field | Example | Why |
|-------|---------|-----|
| `appVersion` | `2.1.0` | d2bsng version adoption |
| `osName` / `osVersion` | `Windows` / `10.0.26100` | which Windows builds to support |
| `locale` | `en-US` | coarse geographic distribution |
| `isDebug` | `true` on `-dev` / pre-release builds | keep dev traffic in a separate bucket |
| `sdkVersion` | `d2bsng@2.1.0` | identifies this integration |
| `appBuildNumber` | `""` | always empty - Aptabase expects the field; d2bsng has no build number separate from `appVersion` |

**Custom properties:**

| Field | Example | Why |
|-------|---------|-----|
| `installId` | salted hash (see below) | count unique installs across launches |
| `backendVersion` | `1.14d` | which game backend / DLL variant is loaded |
| `arch` | `x86` | future-proofing (x64 D2R backends) |
| `managerVersion` | `1.4.2` | *only when the manager sets `D2BOTNG_VERSION`* - which manager versions are in the field, and whether a d2bsng change can rely on a newer one |

**Runtime capabilities** (environment, hardware, and which features are in use):

| Field | Example | Why |
|-------|---------|-----|
| `isWine` / `wineVersion` | `true` / `9.0` | how many installs run on Wine (Linux) vs native Windows - decides whether Wine compatibility is worth maintaining |
| `cpuCores` | `8` | hardware profile; informs V8 thread-pool defaults and perf expectations |
| `ramMb` | `16384` | hardware profile; informs memory-limit defaults |
| `features` | `inspector,realm,speedhack` | which optional features are active this session |
| `compatOverrides` | `-objectToSource` | which compatibility shims scripts turn off (or on) - says whether a legacy shim is still load-bearing |

`features` is a **sorted, comma-joined list of active feature tags**. It is a
list (not one boolean prop per feature) so it stays backend-agnostic: the
framework assembles it from its own toggles plus whatever tags the backend
reports through `game::GetActiveFeatures()`, without the contract enumerating
them. A tag only ever names a feature, never the value behind it. Current tags:

| Tag | Meaning | Source |
|-----|---------|--------|
| `inspector` | V8 debug port (`InspectorPort`) enabled | framework |
| `speedhack` | non-1.0 game speed at session start | framework |
| `waitForProfile` | `UseProfileScript` - script start deferred until a profile is poked in | framework |
| `unsupported` | `enableUnsupported` set | framework |
| `v8SingleThreaded` | `V8SingleThreadedPlatform` - no V8 worker pool | framework |
| `multiInstance` | launched with `-multi` | backend |
| `proxy` | launched with `-proxy` (address never sent) | backend |
| `realm` | launched with a custom `-realm` (host never sent) | backend |
| `profileArg` | launched with `-profile` (name never sent) | backend |
| `windowTitle` | launched with `-title` (title never sent) | backend |
| `cdkey` | launched with `-d2c` / `-d2x` (keys never sent) | backend |
| `failToJoin` | launched with `-ftj` | backend |
| `bnetCacheFix` | launched with `-cachefix` | backend |

(It is a joined string rather than a JSON array because Aptabase custom props
are scalar; the dashboard groups on the combined value and "contains"-filters on
individual tags.)

`compatOverrides` is the same shape and lists only flags **away from their
registered default** - `-name` for one turned off, `+name` for one turned on -
so the common case is an empty string. Every flag sitting at its default would
otherwise drown the signal. The `Compatibility` JS object is the only thing that
toggles these, so the value is the state as of the settle delay; a script that
flips a flag later in the run is not re-reported.

`profile_active` carries one custom property of its own:

| Field | Example | Why |
|-------|---------|-----|
| `profileHash` | `9f2c41ab77e05d3c` | count distinct profiles per install ("how many bots is one user running") |

Aptabase derives coarse country from the ingesting IP address; d2bsng itself
never sends an IP.

### What is deliberately not collected

No IP sent by us, no account name, character name, realm, profile name, or file
paths, and nothing about in-game activity. No raw machine identifier is ever
transmitted: machine facts are used *only* as input to the one-way salted hash
described under [The `installId`](#the-installid), and the digest is what leaves
the machine. Profile names get the same treatment - see
[The `profileHash`](#the-profilehash). The data set is kept to the minimum that
answers "how many users, on what versions, running how many bots."

One caveat worth stating plainly: analytics does **not** go through `-proxy`.
The POST uses WinHTTP and so bypasses the game's SOCKS5 detour (see
[Threading and transport](#threading-and-transport)), which means a launch that
routes D2 through a proxy still reaches Aptabase from the machine's real egress
IP - and Aptabase derives coarse country from it. The event also carries the
`proxy` feature tag. Nothing about the proxy itself is sent (no address, no
credentials), but if hiding the egress IP is the point of the proxy, use
`-noanalytics` alongside it. Routing analytics through the proxy is deliberately
not done: it would make d2bsng's own telemetry depend on user-supplied proxy
config, and a broken proxy would turn a best-effort background POST into a
support issue.

### The `installId`

**Nothing is persisted.** Analytics writes no file and no registry key - the id
is *derived* on each launch instead, so the feature leaves no trace on disk.

It is a salted SHA-256 digest over three read-only machine facts:

```
sha256("ResurrectedTrader-analytics-v1" | MachineGuid | systemVolumeSerial | computerName)
```

Only the digest is ever sent. The salt is scoped to the publisher rather than to
d2bsng, so the value cannot be matched against the same machine's identifier in
anyone else's software, and the hash cannot be reversed to recover the inputs.
Three inputs are combined rather than one so that a missing or constant component
still leaves the id distinct.

Publisher-scoping is deliberate: our own tools - the bot manager in particular -
can derive the **same** `installId` and be joined to d2bsng's data even though
they report under a different Aptabase app key (Aptabase has no cross-app join,
so the join happens on exported events). Anything reimplementing the derivation
has to match it byte for byte:

- the same three inputs in this order, `|`-separated, with the salt first;
- **UTF-8 bytes** - the wide values are converted to UTF-8 before hashing, so a
  UTF-16 hash of the same text will not match;
- SHA-256, lowercase hex;
- `MachineGuid` read from the **64-bit** registry view. d2bsng is a 32-bit DLL
  and passes `KEY_WOW64_64KEY` explicitly; an x86 reader without that flag gets
  the redirected `WOW6432Node` value and every digest diverges;
- `systemVolumeSerial` is the `GetVolumeInformationW` serial of the system
  volume rendered as an **unsigned decimal integer** - not the hex `XXXX-XXXX`
  form Windows shows in Explorer or `dir`. On failure it contributes the literal
  string `0`, not an empty field;
- `computerName` is `GetComputerNameW` - the **NetBIOS** name (uppercase, at most
  15 chars), not the DNS hostname, which can differ on a domain-joined machine;
- a fact that can't be read contributes an **empty** field, with its `|`
  separators still in place - the material never collapses to fewer fields.

If `HashString` itself fails, the id falls back to a freshly generated GUID for
that launch. It is then 36 chars with dashes rather than 64 hex chars, and is not
stable across launches - a consumer parsing the id should tolerate both shapes
rather than assume 64 hex chars.

Bumping the salt string re-buckets every install as new, and invalidates any such
reimplementation at the same time.

The trade-offs of deriving instead of storing:

- Stable across d2bsng reinstalls and folder moves, and unaffected by a
  read-only install directory - none of which a stored file survives.
- Cannot be shared by accident. A stored id file would be copied along with a
  redistributed bot-pack folder, silently collapsing every user of that pack
  into one install.
- Reformatting the Windows volume or renaming the machine produces a new id
  (counted as a new install).
- Cloned VM images share machine facts, so a farm built by cloning one image may
  report as a single install. Cross-check `isWine` / session volume per id if a
  single id looks implausibly busy.

### The `profileHash`

The active profile name is what distinguishes one bot from another on the same
machine, so hashing it turns "how many profiles is this user running" into a
`count(distinct profileHash) group by installId` on the dashboard - without the
name itself ever being sent:

```
sha256("ResurrectedTrader-analytics-v1" | "profile" | installId | profileName)[0..16]
```

The `installId` is part of the material on purpose: the digest is scoped to one
install, so the same profile name on two machines produces different values. That
blocks both cross-user correlation ("these two installs both run `sorc1`") and
recovery by guessing - hashing a likely name gets you nothing without that
install's id. It is truncated to 16 hex chars; 64 bits separates one install's
handful of profiles with room to spare. The name is **lowercased before
hashing** - `d2bs.ini` profile lookup is case-insensitive, so `Sorc1` and `sorc1`
are one profile and must not hash into two. A reimplementation must lowercase
too, and must hash the *raw* name as configured, not a trimmed or display form.

Timing is why this is a separate event rather than a `session_start` property.
The name is only published when a script calls `login()` or a manager switches
profiles, which is normally *after* `session_start` has already gone out - and a
long-lived process may run several profiles in turn. The reporter thread
therefore stays alive polling the active name, emitting one `profile_active` the
first time it sees each distinct hash. Consequences worth knowing:

- A launch that never logs in (idle at the menu, manual play) emits no
  `profile_active` at all - expect fewer of these than `session_start`.
- A profile is reported once per launch, not once per switch, so the event count
  is a count of distinct profiles used, not of switches.
- Delivery failures retry the same few times as `session_start`, but on the 5s
  poll interval rather than its 30s one; after that the profile is dropped and
  not re-attempted later in the launch.

## Configuration

Analytics is enabled when **an app key is resolved** and the **opt-out is
absent**.

### The app key

The key is **baked into the DLL at build time only** - there is no runtime key
switch or environment variable. An **empty key disables analytics** outright: the
component returns before starting its reporter thread, so a build without the
define is a no-op. (The source still `#ifndef`-defaults the `D2BS_ANALYTICS_KEY`
macro to an empty string, which is what makes that the safe fallback.)

The project's own key is committed as the default in `Directory.Build.props`, so
every build reports - IDE builds included - not just released ones. That is
deliberate: an Aptabase app key is a client-side, write-only ingest key, and this
one already ships verbatim inside every published DLL, so it is not a credential.
The consequence is that a fork or a dev build reports into this project unless it
overrides the key.

"Build time only" means the *compiler command line*, not `build.ps1`. The key is
resolved by `Directory.Build.props` at the repo root, which MSBuild imports into
every project - so an IDE build (Visual Studio, Rider), which never runs
`build.ps1`, resolves it the same way. Precedence, highest first:

1. `-p:D2bsAnalyticsKey=...` on the MSBuild command line - a global property,
   which nothing in a project file can override. This is what `build.ps1` and CI
   use.
2. `d2bs.local.props` at the repo root - gitignored, per-developer. Copy
   `d2bs.local.props.example` to it and fill in a key. This is the way to get a
   key into IDE builds.
3. The `D2BS_ANALYTICS_KEY` environment variable (MSBuild surfaces environment
   variables as properties). Note IDEs snapshot the environment at launch, so a
   newly-set user variable needs an IDE restart - which is why (2) is usually
   less trouble.
4. The committed default in `Directory.Build.props`.

`build.ps1` passes `-p:D2bsAnalyticsKey` **only when it has a key**: an empty
`-p:` would set the global property to empty and silently shadow (2) and (3),
since a global property wins over `Directory.Build.props`.

To build with analytics compiled out, override the default with an empty value
(`-p:D2bsAnalyticsKey=`, or an empty `D2bsAnalyticsKey` in `d2bs.local.props`).
To keep it compiled in but silent, opt out at runtime instead.

`build.ps1` defaults `-AnalyticsKey` to the `D2BS_ANALYTICS_KEY` environment
variable (read on the build machine), so a CI job need only export the secret.
`release.yml`'s build step does exactly that:

```yaml
# release.yml build step
env:
  D2BS_ANALYTICS_KEY: ${{ secrets.APTABASE_APP_KEY }}
```

Set `APTABASE_APP_KEY` under **Settings -> Secrets and variables -> Actions**. An
unset secret expands to the empty string, which is the same no-op as a build with
no define at all - so the wiring is safe to land before the secret exists.
`ci.yml` / `ci-build.yml` deliberately do **not** pass a key: PR and branch builds
never report.

With no key compiled in, the reporter thread never starts and no network request
is made.

The key is a plaintext literal in the shipped DLL and anyone can recover it with
`strings`. That is expected: an Aptabase app key is write-only, so the worst a
third party can do with it is post junk events into the project - it grants no
read access to collected data. It is kept in a CI secret to avoid publishing it
casually, not because leaking it compromises anything.

### Per-launch settings

The opt-out is per-launch and can come from an environment variable or a
command-line switch. It is an OR - either source forces analytics off, and there
is no switch that re-enables it against `D2BS_ANALYTICS_DISABLE`. Environment
variables keep values off the visible command line (Task Manager), and a manager
that spawns the game (e.g. D2BotNG) can set either.

| Purpose | Environment variable | Command-line switch |
|---------|----------------------|---------------------|
| Force off | `D2BS_ANALYTICS_DISABLE` (any truthy value) | `-noanalytics` |
| Ingest host override | `D2BS_ANALYTICS_HOST` | *(none)* |
| Bot-manager version | `D2BOTNG_VERSION` | *(none)* |

`D2BOTNG_VERSION` is the one variable d2bsng only *reads* - the manager sets it
on the game process it spawns, and it is reported verbatim as `managerVersion`.
It is absent for a hand-launched game, so treat "no `managerVersion`" as "no
manager, or a manager that predates the variable".

### Ingest endpoint

The app key's region selects the Aptabase ingest host automatically:

- `A-US-...` -> `https://us.aptabase.com`
- `A-EU-...` -> `https://eu.aptabase.com`

For a **self-hosted** Aptabase instance (`A-SH-...`) or a dev key (`A-DEV-...`),
set `D2BS_ANALYTICS_HOST` to the base URL (e.g. `https://analytics.example.com`);
the event is posted to `<host>/api/v0/event`. Without a resolvable host the
component logs a warning and stays off.

`D2BS_ANALYTICS_HOST` is checked **first and unconditionally** - it overrides the
`A-US-` / `A-EU-` region routing as well, not just the key types that need it.

### No user correlation

There is no way to attach a bot-manager account identifier to an event. Every
event is anonymous: the only identity it carries is the derived `installId`.

## Opting out

Pass `-noanalytics`, or set `D2BS_ANALYTICS_DISABLE=1`. Either forces analytics
off for that launch even when an app key is configured. Shipping a build with no
app key configured also leaves it permanently off.

## Threading and transport

All work - resolving the install id (a registry read plus
`GetVolumeInformationW` / `GetComputerNameW`; no file I/O), building the JSON,
and the HTTPS POST - runs on a dedicated `std::jthread`, never the game thread or
a V8 isolate. The request goes through the V8-free
`api::classes::PerformHttpRequest` (WinHTTP), which bypasses the game's SOCKS5
proxy detour, exactly like the update checker. The send is best-effort: a failed
POST (e.g. the network isn't up yet at inject time) is retried a few times at a
fixed 30s interval, then abandoned. A failure never blocks or crashes the
framework - the reporter loop is wrapped in a catch-all, since it is the top
frame of its thread and an escaping exception would terminate the process.

`Stop()` joins that thread, and a POST already in flight is not cancellable, so
shutdown can block for up to the 15s request timeout on an unreachable network.
