#pragma once

#include <utility>

// Inline-patch intercepts.
//
// Each intercept is a 5-byte JMP/CALL written into a mid-function patch site
// inside Game.exe. The destination is a `__declspec(naked)` thunk in
// Intercepts.cpp that:
//   1. Saves caller registers (`pushad`/`popad`).
//   2. Calls a C dispatcher that fires a callback in
//      `hooks::GetActiveCallbacks()` (the framework-owned `GameCallbacks`).
//   3. Tail-calls the original target (or returns early to block the action).
//
// The defensive intercepts (P5/P9/P18/P20-22) have no callback - they're
// behavioural patches the game needs to run cleanly under d2bs.
//
// This header exposes only lifecycle hooks and the WithSelectedUnit scope
// helper; callers should never invoke the thunks directly.

struct D2UnitStrc;

namespace d2bs::hooks::intercepts {

// Snapshot resolved addresses from the imports::Registry into the file-local
// pointers the naked thunks use for tail jumps. Must run after
// imports::Registry::Get().ResolveAll(). Called from Bridge::Init. Returns
// false (after popping a MessageBoxW naming the unresolved entry) if the
// module base or any required intercept import is zero.
[[nodiscard]] bool Init();

// Apply every patch (idempotent). Saves the original bytes per-site so
// RemoveAll can restore them. Called from HookManager::Install.
void InstallAll();

// Restore every patched site to its original bytes. Idempotent.
void RemoveAll();

namespace detail {

// RAII guard for the P5 GetSelectedUnit intercept's click-target override.
// Construction stages `unit` (which may be nullptr - meaning "force NULL", i.e.
// a click at empty ground regardless of any cached hover state); destruction
// clears the override and returns P5 to passthrough mode. Implementation in
// Intercepts.cpp; not for direct use - go through WithSelectedUnit.
class ClickActionScope {
   public:
    explicit ClickActionScope(D2UnitStrc* unit);
    ~ClickActionScope();
    ClickActionScope(const ClickActionScope&) = delete;
    ClickActionScope& operator=(const ClickActionScope&) = delete;
    ClickActionScope(ClickActionScope&&) = delete;
    ClickActionScope& operator=(ClickActionScope&&) = delete;
};

}  // namespace detail

// Run `fn` with `unit` staged as the click target for the P5
// (GetSelectedUnit) intercept. While `fn` is executing, any call into
// D2CLIENT_GetSelectedUnit from inside D2CLIENT_ClickMap returns `unit`
// directly instead of falling through to the game's hover-driven selection.
// Passing nullptr forces "no unit selected" (= coord-only click).
//
// Reference parity: reference/d2bs/Core.cpp:155-173 (Vars.bClickAction +
// Vars.dwSelectedUnitId framing around D2CLIENT_ClickMap).
template <typename F>
decltype(auto) WithSelectedUnit(D2UnitStrc* unit, F&& fn) {
    detail::ClickActionScope scope{unit};
    return std::forward<F>(fn)();
}

}  // namespace d2bs::hooks::intercepts
