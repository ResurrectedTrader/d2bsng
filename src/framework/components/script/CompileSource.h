#pragma once

#include <v8.h>

#include <string>
#include <string_view>

namespace d2bs::framework::script {

// Compile a JavaScript source string into a v8::Script with kolbot-era
// compatibility hacks applied. Used by the per-script execution path and
// by Sandbox compile/include - anywhere user-authored source enters V8.
//
// Hacks applied (TODO(compatibility): later gate these behind script-level
// compatibility flags so vanilla scripts can opt out):
//   1. UTF-8 BOM strip.
//   2. `js_strict(true);` detection - prepends `"use strict";\n` and
//      offsets the script origin's line by 1 so error messages line up.
//   3. `const X = new Runnable` -> `var X = new Runnable` regex rewrite.
//      const declarations don't bind to the global object in V8; kolbot
//      relies on the global binding for cross-script lookup.
v8::MaybeLocal<v8::Script> CompileSource(v8::Isolate* isolate, v8::Local<v8::Context> context, std::string source,
                                         std::string_view originName);

// Run the per-context kolbot compatibility prelude. Installs SpiderMonkey-era
// String/Array.prototype.contains aliases, bumps Error.stackTraceLimit, and
// installs an Error.prepareStackTrace formatter that require.js depends on.
//
// Call once per V8 context after globals are registered, before any user
// script runs. Failure is non-fatal - the prelude is best-effort.
//
// TODO(compatibility): gate behind a per-script compatibility flag once the
// flag system lands.
void ApplyCompatibilityPrelude(v8::Isolate* isolate, v8::Local<v8::Context> context);

}  // namespace d2bs::framework::script
