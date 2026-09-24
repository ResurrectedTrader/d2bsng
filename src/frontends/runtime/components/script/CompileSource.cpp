#include "CompileSource.h"

#include "CodeCache.h"
#include "config/CompatibilityFlags.h"

#include <regex>

namespace d2bs::runtime::script {

std::optional<ub::Script> CompileSource(const ub::Context& context, std::string source, std::string_view originName) {
    auto& compat = config::CompatibilityFlags::Instance();

    // Strip UTF-8 BOM. Always applied - this is source hygiene, not a
    // compatibility behavior, so it is not gated by a flag.
    if (source.size() >= 3 && static_cast<uint8_t>(source[0]) == 0xEF && static_cast<uint8_t>(source[1]) == 0xBB &&
        static_cast<uint8_t>(source[2]) == 0xBF) {
        source.erase(0, 3);
    }

    // kolbot-era `js_strict(true);` shim (flag: jsStrictShim). Prepended without
    // a newline, on purpose: an extra line would shift every line the engine
    // reports off the file the user is editing, and ScriptOrigin's line offset
    // only ever adds, so cancelling it would take a negative offset. Sharing line 1 keeps
    // every line number exact and costs only that line's columns. Still a valid
    // directive prologue - it remains the first statement.
    if (compat.IsEnabled("jsStrictShim") && source.find("js_strict(true);") != std::string::npos) {
        source.insert(0, "\"use strict\";");
    }

    // kolbot-era `const X = new Runnable` -> `var X` rewrite (flag:
    // constRunnableRewrite). const declarations don't bind to the global object
    // in JavaScript; kolbot relies on the global binding for cross-script lookup.
    if (compat.IsEnabled("constRunnableRewrite")) {
        static const std::regex CONST_RUNNABLE(R"(\bconst\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*new\s+Runnable\b)");
        source = std::regex_replace(source, CONST_RUNNABLE, "var $1 = new Runnable");
    }

    const ub::ScriptOrigin origin{.resourceName = originName};
    if (!CodeCache::IsCacheable(source.size())) {
        return ub::Script::Compile(context, source, origin);
    }

    auto& cache = CodeCache::Instance();
    const uint64_t key = cache.MakeKey(originName, source);
    // Held for the whole compile: another script thread's eviction must not free
    // the bytes while the engine reads them.
    const auto blob = cache.Lookup(key);
    // Eager whenever this compile works from source: a blob covers only the
    // functions compiled when it was made, so a lazy compile would store little
    // more than the top level. When the blob is used the option is moot - the
    // blob is what was compiled.
    if (!blob) {
        auto script = ub::Script::Compile(context, source, origin, ub::CompileOptions::EagerCompile);
        if (script) {
            cache.Store(key, *script);
        }
        return script;
    }

    auto script = ub::Script::CompileWithCache(context, source, *blob, origin, ub::CompileOptions::EagerCompile);
    if (!script || !script->UsedCodeCache()) {
        // Refused - by unibind's own blob check or by the engine - and compiled
        // eagerly from source instead: replace the entry with this compile's
        // blob, or just forget it when the fallback compile failed as well.
        cache.Drop(key);
        if (script) {
            cache.Store(key, *script);
        }
    }
    return script;
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

void ApplyCompatibilityPrelude(const ub::Context& context) {
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

    const ub::TryCatch tryCatch(context.GetIsolate());
    if (auto script = CompileSource(context, std::move(prelude), "v8-compatibility.js")) {
        (void)script->Run(context);
    }
}

}  // namespace d2bs::runtime::script
