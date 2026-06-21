#pragma once

#include <v8.h>

#include <string>
#include <string_view>

namespace d2bs::framework::script {

// Compile a JavaScript source string into a v8::Script, applying source-level
// kolbot compatibility transforms. Used by the per-script execution path and
// by Sandbox compile/include - anywhere user-authored source enters V8.
//
// Transforms applied:
//   1. UTF-8 BOM strip (always; source hygiene, not a compatibility flag).
//   2. `js_strict(true);` detection - prepends `"use strict";\n` and offsets
//      the script origin's line by 1 so error messages line up
//      (Compatibility flag: jsStrictShim).
//   3. `const X = new Runnable` -> `var X = new Runnable` regex rewrite. const
//      declarations don't bind to the global object in V8; kolbot relies on the
//      global binding for cross-script lookup (Compatibility flag:
//      constRunnableRewrite).
v8::MaybeLocal<v8::Script> CompileSource(v8::Isolate* isolate, v8::Local<v8::Context> context, std::string source,
                                         std::string_view originName);

// Run the per-context kolbot compatibility prelude. Installs the enabled subset
// of the SpiderMonkey-era shims (String/Array.prototype.contains, the
// Error.prepareStackTrace formatter + raised stackTraceLimit, the non-standard
// Error properties, and Object.prototype.toSource), each gated by its
// Compatibility flag; the delay wrapper is always installed.
//
// Call once per V8 context after globals are registered, before any user
// script runs. Failure is non-fatal - the prelude is best-effort.
void ApplyCompatibilityPrelude(v8::Isolate* isolate, v8::Local<v8::Context> context);

}  // namespace d2bs::framework::script
