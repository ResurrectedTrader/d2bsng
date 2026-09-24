#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "unibind/unibind.h"

namespace d2bs::runtime::script {

// Compile a JavaScript source string, applying source-level kolbot
// compatibility transforms and the code cache. Used by the per-script execution
// path and by Sandbox compile/include - anywhere user-authored source enters the
// engine. Empty if the source did not compile; the syntax error is then pending.
//
// Transforms applied:
//   1. UTF-8 BOM strip (always; source hygiene, not a compatibility flag).
//   2. `js_strict(true);` detection - prepends `"use strict";` onto the first
//      line (no newline, so reported line numbers still match the file on disk)
//      (Compatibility flag: jsStrictShim).
//   3. `const X = new Runnable` -> `var X = new Runnable` regex rewrite. const
//      declarations don't bind to the global object; kolbot relies on the
//      global binding for cross-script lookup (Compatibility flag:
//      constRunnableRewrite).
std::optional<ub::Script> CompileSource(const ub::Context& context, std::string source, std::string_view originName);

// Run the per-context kolbot compatibility prelude. Installs the enabled subset
// of the SpiderMonkey-era shims (String/Array.prototype.contains, the
// Error.prepareStackTrace formatter + raised stackTraceLimit, the non-standard
// Error properties, and Object.prototype.toSource), each gated by its
// Compatibility flag; the delay wrapper is always installed.
//
// Call once per context after globals are registered, before any user script
// runs, with the context entered. Failure is non-fatal - the prelude is
// best-effort.
void ApplyCompatibilityPrelude(const ub::Context& context);

}  // namespace d2bs::runtime::script
