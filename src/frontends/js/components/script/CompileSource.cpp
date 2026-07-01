#include "CompileSource.h"

#include "api/core/V8Convert.h"
#include "config/CompatibilityFlags.h"

#include <regex>

namespace d2bs::js::script {

v8::MaybeLocal<v8::Script> CompileSource(v8::Isolate* isolate, v8::Local<v8::Context> context, std::string source,
                                         std::string_view originName) {
    auto& compat = config::CompatibilityFlags::Instance();

    // Strip UTF-8 BOM. Always applied - this is source hygiene, not a
    // compatibility behavior, so it is not gated by a flag.
    if (source.size() >= 3 && static_cast<uint8_t>(source[0]) == 0xEF && static_cast<uint8_t>(source[1]) == 0xBB &&
        static_cast<uint8_t>(source[2]) == 0xBF) {
        source.erase(0, 3);
    }

    // kolbot-era `js_strict(true);` shim (flag: jsStrictShim) - prepend
    // "use strict";\n and offset the origin line by 1 so error messages
    // reference the script's original line numbers.
    int32_t lineOffset = 0;
    if (compat.IsEnabled("jsStrictShim") && source.find("js_strict(true);") != std::string::npos) {
        source.insert(0, "\"use strict\";\n");
        lineOffset = 1;
    }

    // kolbot-era `const X = new Runnable` -> `var X` rewrite (flag:
    // constRunnableRewrite). const declarations don't bind to the global object
    // in V8; kolbot relies on the global binding for cross-script lookup.
    if (compat.IsEnabled("constRunnableRewrite")) {
        static const std::regex CONST_RUNNABLE(R"(\bconst\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*new\s+Runnable\b)");
        source = std::regex_replace(source, CONST_RUNNABLE, "var $1 = new Runnable");
    }

    auto sourceStr = v8::String::NewFromUtf8(isolate, source.c_str(), v8::NewStringType::kNormal,
                                             static_cast<int32_t>(source.size()));
    if (sourceStr.IsEmpty()) {
        return {};
    }

    auto originNameStr = api::v8_convert::ToV8(isolate, originName);
    v8::ScriptOrigin origin(originNameStr, lineOffset);
    v8::ScriptCompiler::Source compilerSource(sourceStr.ToLocalChecked(), origin);
    return v8::ScriptCompiler::Compile(context, &compilerSource, v8::ScriptCompiler::kEagerCompile);
}

// The kolbot-era prelude, split into per-feature snippets. Each is gated by a
// Compatibility flag in ApplyCompatibilityPrelude, except the delay wrapper,
// which is always installed (see its comment / docs/compatibility.md).
namespace {

// flag: stringContains
constexpr std::string_view PRELUDE_STRING_CONTAINS = R"(
// SpiderMonkey had its own extensions for these, which later became standard as 'includes'
String.prototype.contains = String.prototype.includes;
Array.prototype.contains = Array.prototype.includes;
)";

// flag: errorStackTrace
constexpr std::string_view PRELUDE_ERROR_STACK_TRACE = R"(
// Adjust default error stack trace limit
Error.stackTraceLimit = 100;

// require.js / LazyLoader.js parse the stacktrace to find where they were called from
// for relative-import resolution. They expect SpiderMonkey's traditional
// `funcname@file:line:col` format - no spaces around `@`. LazyLoader's regex
// (/@([a-zA-Z]:.+?)\.js:/) is strict about `@` being immediately followed by
// the drive letter; spaces around `@` break it silently.
Error.prepareStackTrace = function (error, frames) {
  var out = '';
  frames.forEach((f, i) => {
    out += `${i === 0 ? '' : '\n'}${f.getFunctionName() ?? '<anonymous>'}@${f.getFileName() ?? '<anonymous>'}:${f.getLineNumber()}:${f.getColumnNumber()}`;
  });
  return out;
};
)";

// flag: errorSpiderMonkeyProps
constexpr std::string_view PRELUDE_ERROR_SM_PROPS = R"(
// Polyfill that provides SpiderMonkey-specific fileName/lineNumber/columnNumber properties on
// Error objects. Extract them out of the stacktrace.
Object.defineProperties(Error.prototype, {
  fileName: {
    get() {
      const match = this.stack?.match(/^[^@\r\n]*@(.+):(\d+):(\d+)/m);
      return match ? match[1] : undefined;
    },
    configurable: true
  },
  lineNumber: {
    get() {
      const match = this.stack?.match(/^[^@\r\n]*@(.+):(\d+):(\d+)/m);
      return match ? Number(match[2]) : undefined;
    },
    configurable: true
  },
  columnNumber: {
    get() {
      const match = this.stack?.match(/^[^@\r\n]*@(.+):(\d+):(\d+)/m);
      return match ? Number(match[3]) : undefined;
    },
    configurable: true
  }
});
)";

// flag: objectToSource
constexpr std::string_view PRELUDE_OBJECT_TO_SOURCE = R"(
// SpiderMonkey shipped Object.prototype.toSource - an eval-roundtrippable repr
// available on every value. V8 dropped it. Kolbot calls it in three error-
// logging spots (e.g. AutoBuildThread.js, ConfigOverrides.js) plus one buffer
// serialization in StorageOverrides.js; without this shim those throw
// "X.toSource is not a function" inside the catch handler itself, silently
// suppressing the diagnostic the script was trying to emit.
(() => {
  const IDENT = /^[A-Za-z_$][A-Za-z0-9_$]*$/;

  const escapeString = (s) =>
    '"' + s.replace(/\\/g, '\\\\').replace(/"/g, '\\"')
           .replace(/\n/g, '\\n').replace(/\r/g, '\\r').replace(/\t/g, '\\t') + '"';

  const sourceOf = (v, seen) => {
    if (v === null) return 'null';
    if (v === undefined) return '(void 0)';

    const t = typeof v;
    if (t === 'number') {
      if (v !== v) return '(0/0)';
      if (v === Infinity) return '(1/0)';
      if (v === -Infinity) return '(-1/0)';
      return String(v);
    }
    if (t === 'boolean') return String(v);
    if (t === 'string') return escapeString(v);
    if (t === 'function') return Function.prototype.toString.call(v);
    if (t === 'symbol') return 'Symbol(' + escapeString(v.description || '') + ')';

    // SpiderMonkey throws on deep cycles and returns weird shallow strings;
    // a "{}" placeholder is the cheap-and-cheerful approximation.
    if (seen.indexOf(v) !== -1) return '{}';
    seen.push(v);
    try {
      if (Array.isArray(v)) {
        return '[' + v.map((x) => sourceOf(x, seen)).join(', ') + ']';
      }
      if (v instanceof Error) {
        const args = [escapeString(v.message || '')];
        if (v.fileName) args.push(escapeString(v.fileName));
        if (v.lineNumber !== undefined) args.push(String(v.lineNumber));
        return '(new ' + (v.name || 'Error') + '(' + args.join(', ') + '))';
      }
      if (v instanceof Date) return '(new Date(' + v.getTime() + '))';
      if (v instanceof RegExp) return String(v);

      const pairs = Object.keys(v).map((k) => {
        const keyStr = IDENT.test(k) ? k : escapeString(k);
        return keyStr + ':' + sourceOf(v[k], seen);
      });
      return '({' + pairs.join(', ') + '})';
    } finally {
      seen.pop();
    }
  };

  Object.defineProperty(Object.prototype, 'toSource', {
    value: function () { return sourceOf(this, []); },
    enumerable: false, writable: true, configurable: true,
  });
})();
)";

// Always installed (not flag-gated). If you just do:
//
//   while(true) { delay(1000); }
//
// you end up with no JS stack frames, just a native call. TerminateExecution() only fires
// while unwinding a JS stackframe - wrap delay in a dummy function that provides one so
// stop() can break out of tight loops.
constexpr std::string_view PRELUDE_DELAY = R"(
(() => {
    let originalDelay = globalThis.delay;
    globalThis.delay = function(...args) {
      return originalDelay(...args);
    };
})();
)";

}  // namespace

void ApplyCompatibilityPrelude(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    auto& compat = config::CompatibilityFlags::Instance();

    std::string prelude;
    if (compat.IsEnabled("stringContains")) {
        prelude += PRELUDE_STRING_CONTAINS;
    }
    if (compat.IsEnabled("errorStackTrace")) {
        prelude += PRELUDE_ERROR_STACK_TRACE;
    }
    if (compat.IsEnabled("errorSpiderMonkeyProps")) {
        prelude += PRELUDE_ERROR_SM_PROPS;
    }
    if (compat.IsEnabled("objectToSource")) {
        prelude += PRELUDE_OBJECT_TO_SOURCE;
    }
    prelude += PRELUDE_DELAY;

    v8::Local<v8::Script> script;
    if (CompileSource(isolate, context, std::move(prelude), "v8-compatibility.js").ToLocal(&script)) {
        v8::Local<v8::Value> dummy;
        (void)script->Run(context).ToLocal(&dummy);
    }
}

}  // namespace d2bs::js::script
