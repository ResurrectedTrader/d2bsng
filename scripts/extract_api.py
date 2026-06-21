#!/usr/bin/env python3
"""
Extract the JavaScript API surface from d2bsng V8 bindings.

Parses C++ source files using libclang to walk the AST and find V8
registration calls (Method, Property, v8_function::Register, etc.),
then outputs a structured JSON description of the entire JS API.

Structured ``/// @tag`` comments immediately above a registration call
are parsed into each entry's ``doc`` (description, mode, type, and a
per-overload ``signatures`` list that groups ``@param``/``@returns`` under
each ``@signature``).

Usage:
    pip install libclang
    python scripts/extract_api.py                 # JSON to stdout
    python scripts/extract_api.py -o api.json     # JSON to file
    python scripts/extract_api.py --verbose        # show parse diagnostics
"""

import json
import os
import re
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

try:
    from clang.cindex import Index, CursorKind, TranslationUnit
except ImportError:
    sys.exit("Required: pip install libclang")

REPO_ROOT = Path(__file__).resolve().parent.parent


# ── Compile flags ───────────────────────────────────────────────────


def find_system_includes():
    """Detect MSVC and Windows SDK include directories via vswhere."""
    includes = []
    vswhere = Path(
        r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    if vswhere.exists():
        try:
            out = subprocess.check_output(
                [str(vswhere), "-latest", "-property", "installationPath"],
                text=True,
            ).strip()
            msvc_root = Path(out) / "VC" / "Tools" / "MSVC"
            if msvc_root.exists():
                latest = sorted(msvc_root.iterdir())[-1]
                inc = latest / "include"
                if inc.exists():
                    includes.append(inc)
        except (subprocess.CalledProcessError, IndexError):
            pass

    sdk_root = Path(r"C:\Program Files (x86)\Windows Kits\10\Include")
    if sdk_root.exists():
        try:
            latest = sorted(sdk_root.iterdir())[-1]
            for sub in ("ucrt", "um", "shared"):
                d = latest / sub
                if d.exists():
                    includes.append(d)
        except IndexError:
            pass

    return includes


def build_compile_flags():
    """Return clang flags sufficient to parse the API headers."""
    flags = [
        "-x",
        "c++",
        "-std=c++23",
        "-fms-compatibility",
        "-fms-extensions",
        "-fdelayed-template-parsing",
        "-D_WIN32",
        "-DWIN32",
        "-D_UNICODE",
        "-DUNICODE",
        "-DNOMINMAX",
        "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH",  # bundled clang may be older
        "-w",  # suppress diagnostics — we only want the AST
    ]
    for d in (
        REPO_ROOT / "src" / "framework",
        REPO_ROOT / "src",
    ):
        if d.exists():
            flags += ["-I", str(d)]
    # V8 and vcpkg use angle-bracket includes → -isystem
    vcpkg_inc = (
        REPO_ROOT
        / "vcpkg_installed"
        / "x86-windows-static"
        / "x86-windows-static"
        / "include"
    )
    for d in (
        REPO_ROOT / "dependencies" / "v8" / "include" / "v8",
        vcpkg_inc,
    ):
        if d.exists():
            flags += ["-isystem", str(d)]
    for d in find_system_includes():
        flags += ["-isystem", str(d)]
    return flags


# ── AST helpers ─────────────────────────────────────────────────────


def strip_quotes(s):
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        return s[1:-1]
    return s


def callee_spelling(call):
    """Best-effort function/method name from a CALL_EXPR."""
    for ch in call.get_children():
        if ch.kind in (CursorKind.DECL_REF_EXPR, CursorKind.MEMBER_REF_EXPR):
            return ch.spelling
        found = callee_spelling(ch)
        if found:
            return found
    return None


def first_string_in(cursor, depth=5):
    """
    Find the first string literal under *cursor*, stopping at lambda /
    compound-statement boundaries so we don't descend into callback bodies.
    """
    if depth <= 0:
        return None
    if cursor.kind == CursorKind.STRING_LITERAL:
        return strip_quotes(cursor.spelling)
    if cursor.kind in (CursorKind.LAMBDA_EXPR, CursorKind.COMPOUND_STMT):
        return None
    for ch in cursor.get_children():
        s = first_string_in(ch, depth - 1)
        if s is not None:
            return s
    return None


def call_args(call):
    """Return argument cursors of a CALL_EXPR (skip callee child)."""
    children = list(call.get_children())
    return children[1:] if children else []


def nth_arg_string(call, n, depth=5):
    """Get the first string literal inside the n-th argument of a call."""
    args = call_args(call)
    if n < len(args):
        return first_string_in(args[n], depth)
    return None


def find_decl_ref(cursor, depth=4):
    """Walk into UNEXPOSED_EXPR wrappers to find a DECL_REF_EXPR spelling."""
    if depth <= 0:
        return None
    if cursor.kind == CursorKind.DECL_REF_EXPR:
        return cursor.spelling
    for ch in cursor.get_children():
        found = find_decl_ref(ch, depth - 1)
        if found:
            return found
    return None


def member_call_object(call):
    """For obj->Set(...), return the variable name 'obj'."""
    children = list(call.get_children())
    if not children:
        return None
    first = children[0]
    if first.kind == CursorKind.MEMBER_REF_EXPR:
        for ch in first.get_children():
            name = find_decl_ref(ch)
            if name and name not in ("operator->",):
                return name
    return None


def count_unary_ops(call):
    """Count direct UNARY_OPERATOR children — each +[] lambda is one."""
    return sum(
        1 for ch in call_args(call) if ch.kind == CursorKind.UNARY_OPERATOR
    )


def readonly_from(doc, call):
    """Property read-only flag: prefer the explicit ``@mode`` tag, else fall
    back to the lambda-count heuristic (a getter-only Property registers one
    ``+[]`` lambda; a read/write Property registers two)."""
    mode = doc.get("mode") if doc else None
    if mode == "readonly":
        return True
    if mode in ("readwrite", "writeonly"):
        return False
    return count_unary_ops(call) < 2


# ── Comment extraction ──────────────────────────────────────────────
#
# Doc comments use a structured tag vocabulary placed immediately above a
# registration call:
#
#   /// @description <summary; may wrap onto further /// lines>
#   /// @type {T}                                  (properties / constants)
#   /// @mode <readonly|readwrite>      (properties; OPTIONAL - the mode is
#         derived from the getter/setter lambda count, so this tag is only
#         needed to override that, e.g. a setter that deliberately no-ops)
#   /// @signature name(arg: type, opt?: type)     (one per call form)
#   /// @param <name> {type} - <desc>              (grouped under the @signature above)
#   /// @returns {type} - <desc>
#   /// @throws {ErrorType} - <when>               (SEMANTIC exceptions only - see below)
#   /// @callback <param>(arg: type, ...) -> {ret} - <desc>   (a function-typed param)
#
# A `{type}` that names an extracted enum or constant namespace (e.g.
# `@type {Difficulty}`, `@param flag {CompatibilityFlag}`) auto-links to that
# option-set's table in the docs - see extract_enums().
#
# @throws convention: a function/method throws a TypeError when a required
# argument is missing or has the wrong type (via the shared CheckArgCount /
# CheckIs* helpers).  That is implied and is NOT documented per entry; @throws
# documents only semantic / domain exceptions beyond basic argument validation
# (e.g. not in a game, a BLOB column, a path escaping the sandbox).
#
# A /// line that does not begin a new @tag is folded onto the previous
# tag's value, so descriptions and param notes may wrap across lines.
# @param/@returns lines attach to the most recent @signature, producing a
# per-overload "signatures" list; any appearing before a @signature fall
# back to top-level "params"/"returns" (the property/constant case).

_TAG_RE = re.compile(r"^@(\w+)\s+(.*)$")
_PARAM_NAME_RE = re.compile(r"^(\.{3})?(\w+)\s*(.*)$")


def _split_braced(val):
    """If *val* starts with a brace-delimited ``{type}`` return
    ``(inner_type, rest_after_close)``; else ``(None, val)``. Braces are
    depth-balanced, so an object-literal type such as ``{x:number,y:number}``
    or ``{Array<{x:number}>}`` is captured whole instead of being cut at its
    first inner ``}``."""
    val = val.strip()
    if not val.startswith("{"):
        return None, val
    depth = 0
    for i, ch in enumerate(val):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return val[1:i], val[i + 1 :]
    return None, val  # unbalanced - caller treats as raw


def _strip_lead_dash(s):
    """Drop the ``-`` separator between a ``{type}`` and its description."""
    s = s.strip()
    return s[1:].strip() if s.startswith("-") else s

# File content cache: resolved_path -> list[str]
_file_lines_cache = {}


def _get_lines(path):
    """Read and cache file lines (1-indexed via index+1)."""
    key = str(path)
    if key not in _file_lines_cache:
        try:
            _file_lines_cache[key] = Path(path).read_text(
                encoding="utf-8", errors="replace"
            ).splitlines()
        except OSError:
            _file_lines_cache[key] = []
    return _file_lines_cache[key]


def _doc_tag_pairs(filepath, line):
    """
    Collect the ``///`` block immediately above *line* (1-based) as an
    ordered list of ``[tag, value]`` pairs, top to bottom.  ``///`` lines
    that do not start a new ``@tag`` are folded onto the previous tag's
    value (line-wrap support).
    """
    lines = _get_lines(filepath)
    if not lines:
        return []

    block = []
    idx = line - 2  # line is 1-based; start on the line above
    while idx >= 0:
        text = lines[idx].strip()
        if not text.startswith("///"):
            break
        block.append(text[3:].strip())  # strip the leading '///'
        idx -= 1
    block.reverse()  # top -> bottom

    pairs = []
    for body in block:
        m = _TAG_RE.match(body)
        if m:
            pairs.append([m.group(1), m.group(2).strip()])
        elif pairs and body:
            pairs[-1][1] = (pairs[-1][1] + " " + body).strip()
        # a continuation line before any tag is ignored
    return pairs


def _parse_param(val):
    """'name {type} - desc' (or '...rest {type} - desc') -> structured dict.
    The type is brace-balanced so object-literal types survive."""
    m = _PARAM_NAME_RE.match(val)
    if not m:
        return {"raw": val}
    spread, pname, rest = m.groups()
    ptype, rest = _split_braced(rest)
    if ptype is None:
        return {"raw": val}
    return {
        "name": (spread or "") + pname,
        "type": ptype.strip(),
        "description": _strip_lead_dash(rest),
    }


def _parse_returns(val):
    """'{type} - desc' -> {'type': ..., 'description': ...}; type is brace-balanced."""
    typ, rest = _split_braced(val)
    if typ is None:
        return {"raw": val}
    return {"type": typ.strip(), "description": _strip_lead_dash(rest)}


def _strip_braces(val):
    val = val.strip()
    if val.startswith("{") and val.endswith("}"):
        return val[1:-1].strip()
    return val


_SIG_RE = re.compile(r"^\s*([\w.]+)\s*\((.*)\)\s*$")
_ARG_RE = re.compile(r"^(\.{3})?(\w+)(\?)?\s*:\s*(.+)$")


def _split_top_level(arglist):
    """Split a signature arg list on commas at bracket depth 0, so commas
    inside ``{...}`` / ``<...>`` / ``(...)`` object or generic types are kept
    intact."""
    args = []
    depth = 0
    buf = []
    for ch in arglist:
        if ch in "([{<":
            depth += 1
        elif ch in ")]}>":
            depth = max(0, depth - 1)
        if ch == "," and depth == 0:
            args.append("".join(buf))
            buf = []
        else:
            buf.append(ch)
    if buf:
        args.append("".join(buf))
    return [a.strip() for a in args if a.strip()]


def _parse_sig_arg(arg):
    """'name?: type' / '...rest: type' / 'name: type' -> structured arg."""
    m = _ARG_RE.match(arg.strip())
    if not m:
        return {"name": arg.strip(), "type": "", "optional": False}
    spread, name, opt, typ = m.groups()
    return {
        "name": (spread or "") + name,
        "type": typ.strip(),
        "optional": bool(opt) or bool(spread),
    }


def _parse_signature(sig):
    """Parse a ``name(arg: type, ...)`` signature string into ``(name, args)``,
    where *args* is the ordered list of structured args.  Returns ``(sig, [])``
    when the parentheses can't be found or the arg list is empty."""
    m = _SIG_RE.match(sig)
    if not m:
        return sig.strip(), []
    name, inside = m.group(1), m.group(2).strip()
    if not inside:
        return name, []
    return name, [_parse_sig_arg(a) for a in _split_top_level(inside)]


# A callable doc form: ``name(arg: type, ...) -> {retType} - description``.
# Shared by @callback (function-param callbacks) and @event (events). The return
# type, when present, is brace-wrapped so it may contain commas/spaces
# (e.g. {[number, number]}).
_CALLABLE_RE = re.compile(
    r"^(\w+)\s*\((.*?)\)\s*(?:->\s*(\{(?:[^{}]|\{[^{}]*\})*\}))?\s*(?:-\s*(.*))?$"
)


def _parse_callable(val):
    """Parse ``name(args) -> {ret} - desc`` into a structured dict (name,
    signature, params, returns, description). Returns None on no match;
    ``returns`` is the brace-stripped type or None."""
    m = _CALLABLE_RE.match(val.strip())
    if not m:
        return None
    name, argstr, ret, desc = m.groups()
    argstr = (argstr or "").strip()
    params = [_parse_sig_arg(a) for a in _split_top_level(argstr)] if argstr else []
    return {
        "name": name,
        "signature": f"{name}({argstr})",
        "params": params,
        "returns": _strip_braces(ret) if ret else None,
        "description": (desc or "").strip(),
    }


def extract_doc_comment(filepath, line):
    """
    Parse the structured ``///`` doc block above *line* into a dict:

        {
          "description": str,
          "mode": "readonly"|"readwrite"|"writeonly",   # properties
          "type": str,                                   # properties / constants
          "signatures": [                                # methods / functions
            {"signature": str,
             "params": [{"name", "type", "description"}],
             "returns": {"type", "description"}}
          ],
          "params":  [...],   # @param(s) with no preceding @signature
          "returns": {...},   # top-level @returns with no @signature
          <other tag>: str,   # any unrecognized @tag, kept verbatim
        }

    Empty when there is no doc block.
    """
    pairs = _doc_tag_pairs(filepath, line)
    doc = {}
    signatures = []
    current = None
    param_docs = {}  # name -> {type, description} from @param lines (any position)
    callback_docs = {}  # param name -> callback dict from @callback lines
    free_returns = None

    for tag, val in pairs:
        if tag == "signature":
            current = {"signature": val, "returns": None}
            signatures.append(current)
        elif tag == "param":
            param = _parse_param(val)
            if "name" in param:
                param_docs.setdefault(param["name"], param)
        elif tag == "callback":
            cb = _parse_callable(val)
            if cb:
                callback_docs.setdefault(cb["name"], cb)
        elif tag == "returns":
            ret = _parse_returns(val)
            if current is not None:
                current["returns"] = ret
            else:
                free_returns = ret
        elif tag == "throws":
            r = _parse_returns(val)
            doc.setdefault("throws", []).append(
                r if "type" in r else {"description": val.strip()}
            )
        elif tag == "type":
            doc["type"] = _strip_braces(val)
        elif tag in ("description", "mode"):
            doc[tag] = val
        else:
            # Preserve any other tag verbatim (list if it repeats).
            if tag in doc:
                if isinstance(doc[tag], list):
                    doc[tag].append(val)
                else:
                    doc[tag] = [doc[tag], val]
            else:
                doc[tag] = val

    # Each overload carries its OWN complete param list, taken from its
    # @signature string (the authoritative per-form arg list) and enriched
    # with @param descriptions matched by name.  A parameter shared by several
    # forms (documented once with @param) therefore appears under every form
    # that takes it, not only the form it happened to be written under.
    for sig in signatures:
        _name, sig_args = _parse_signature(sig["signature"])
        params = []
        for arg in sig_args:
            info = param_docs.get(arg["name"]) or param_docs.get(
                arg["name"].lstrip(".")
            )
            entry = {
                "name": arg["name"],
                "type": arg["type"] or (info or {}).get("type") or "",
                "optional": arg["optional"],
                "description": (info or {}).get("description", ""),
            }
            cb = callback_docs.get(arg["name"]) or callback_docs.get(
                arg["name"].lstrip(".")
            )
            if cb:
                entry["callback"] = {
                    "params": cb["params"],
                    "returns": cb["returns"],
                    "description": cb["description"],
                }
            params.append(entry)
        sig["params"] = params

    # A single documented @returns is shared by all overloads (the convention
    # is to write it once after the last form); copy it onto any form missing
    # one.  When forms genuinely return different things they each carry their
    # own @returns, so there is nothing to fill.
    documented = [s["returns"] for s in signatures if s.get("returns")]
    if len(documented) == 1:
        for sig in signatures:
            if not sig.get("returns"):
                sig["returns"] = documented[0]

    if signatures:
        doc["signatures"] = signatures
    elif param_docs:
        doc["params"] = list(param_docs.values())
    if callback_docs and not signatures:
        # A property whose value is itself a callback (e.g. a drawable's
        # click/hover handler) documents its invocation signature directly.
        cb = next(iter(callback_docs.values()))
        doc["callback"] = {
            "params": cb["params"],
            "returns": cb["returns"],
            "description": cb["description"],
        }
    if free_returns is not None and not signatures:
        doc["returns"] = free_returns
    return doc


# ── Extractor ───────────────────────────────────────────────────────


class ApiExtractor:
    def __init__(self, verbose=False):
        self.verbose = verbose
        self.classes = {}  # js_name -> {...}
        self.globals = []
        self.constants = {}  # top-level name -> value or nested dict
        self.me_properties = []
        self._seen = set()  # (file, line, name) dedup from header re-inclusion
        self._nested_obj_name = {}  # var_name -> [entries]
        self._constructable = {}  # cpp_class -> bool (false = V8_CLASS_NOT_CONSTRUCTABLE)
        self._ctor = {}  # cpp_class -> {file, line, doc} for the documented New
        self._extends = {}  # cpp_class -> base js name (shared-property inheritance)
        self._class_js = {}  # cpp_class -> ClassName (JS-visible name), for consistent keying

    def process(self, index, path, flags):
        tu = index.parse(
            str(path),
            args=flags,
            options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
            | TranslationUnit.PARSE_INCOMPLETE,
        )
        if self.verbose:
            errors = [d for d in tu.diagnostics if d.severity >= 3]
            if errors:
                for e in errors[:3]:
                    print(f"    diag: {e}", file=sys.stderr)
                if len(errors) > 3:
                    print(f"    ... +{len(errors) - 3} more", file=sys.stderr)
        self._walk(tu.cursor, path, ctx=None)

    # ── recursive walk ─────────────────────────────────────────────

    def _walk(self, cur, path, ctx):
        new_ctx = ctx

        # Context-setting function definitions
        if cur.kind in (CursorKind.FUNCTION_DECL, CursorKind.CXX_METHOD):
            name = cur.spelling
            if name == "ConfigureTemplate":
                parent = cur.semantic_parent
                cpp_name = parent.spelling if parent else "Unknown"
                new_ctx = ("class", cpp_name)
            elif name.startswith("Register") and name.endswith("Functions"):
                new_ctx = ("globals", name)
            elif name == "RegisterConstants":
                new_ctx = ("constants",)
            elif name == "CreateMeObject":
                new_ctx = ("me",)
            elif name == "RegisterAllClasses":
                new_ctx = ("class_ctors",)

        # Pick up ClassName constexpr (and the class's constructability)
        if cur.kind == CursorKind.VAR_DECL and cur.spelling == "ClassName":
            parent = cur.semantic_parent
            if parent:
                # ClassName is a `static constexpr std::string_view ClassName =
                # "Name";`. libclang hides the literal behind the string_view ctor
                # (first_string_in returns None), so read it from the tokens. This
                # is the JS-visible name (e.g. JSDirectory -> "Folder"); record it
                # so methods/properties key off it, not the "JS"-stripped name.
                toks = [t.spelling for t in cur.get_tokens()]
                s = strip_quotes(toks[toks.index("=") + 1]) if "=" in toks else None
                if s:
                    self._class_js[parent.spelling] = s
                    self._ensure_class(parent.spelling, js_name=s, path=path)
                self._scan_constructable(parent)

        # Constructor (New): documented overloads sit above its definition.
        if cur.kind == CursorKind.CXX_METHOD and cur.spelling == "New":
            parent = cur.semantic_parent
            if parent and parent.spelling.startswith("JS"):
                self._on_constructor(cur, parent.spelling, path)

        # Registration calls
        if cur.kind == CursorKind.CALL_EXPR and new_ctx:
            self._on_call(cur, new_ctx, path)

        for ch in cur.get_children():
            self._walk(ch, path, new_ctx)

    # ── call dispatch ──────────────────────────────────────────────

    def _on_call(self, call, ctx, path):
        name = callee_spelling(call)
        if not name:
            return

        # Use the actual source location (follows #include expansion)
        src_file = call.location.file
        src_path = Path(src_file.name) if src_file else path
        loc = {
            "file": self._rel(src_path),
            "line": call.location.line,
        }
        # Deduplicate — headers get parsed once standalone and again via includes
        dedup_key = (loc["file"], loc["line"], name)
        if dedup_key in self._seen:
            return
        self._seen.add(dedup_key)

        doc = extract_doc_comment(src_path, call.location.line)

        tag = ctx[0]

        if tag == "class":
            cpp_class = ctx[1]
            if name == "ConfigureCommonProperties":
                # A drawable's ConfigureTemplate calls this base helper; record
                # the inheritance so shared members aren't duplicated per class.
                if cpp_class != "JSDrawableBase":
                    self._extends[cpp_class] = "DrawableBase"
                return
            if name == "Method":
                s = nth_arg_string(call, 2)
                if s:
                    entry = {"name": s, **loc}
                    if doc:
                        entry["doc"] = doc
                    self._ensure_class(cpp_class, path=path)["methods"].append(
                        entry
                    )
            elif name == "Property":
                s = nth_arg_string(call, 2)
                if s:
                    ro = readonly_from(doc, call)
                    doc.setdefault("mode", "readonly" if ro else "readwrite")
                    entry = {"name": s, "readonly": ro, "doc": doc, **loc}
                    self._ensure_class(cpp_class, path=path)[
                        "properties"
                    ].append(entry)
            elif name == "StaticMethod":
                s = nth_arg_string(call, 2)
                if s:
                    entry = {"name": s, **loc}
                    if doc:
                        entry["doc"] = doc
                    self._ensure_class(cpp_class, path=path)[
                        "static_methods"
                    ].append(entry)

        elif tag == "globals":
            category = ctx[1]
            if name == "Register":
                s = nth_arg_string(call, 2)
                if s:
                    entry = {"name": s, "category": category, **loc}
                    if doc:
                        entry["doc"] = doc
                    self.globals.append(entry)

        elif tag == "constants":
            if name == "Set":
                s = nth_arg_string(call, 1)
                if not s:
                    return
                obj = member_call_object(call)
                entry = {"name": s, **loc}
                if doc:
                    entry["doc"] = doc
                if obj and obj != "global":
                    self._nested_obj_name.setdefault(obj, []).append(entry)
                else:
                    nested = None
                    for var, entries in self._nested_obj_name.items():
                        if var.lower() == s.lower() or var.lower().startswith(
                            s.lower()
                        ):
                            nested = entries
                            break
                    if nested:
                        self.constants[s] = {
                            **loc,
                            "properties": nested,
                        }
                        if doc:
                            self.constants[s]["doc"] = doc
                    else:
                        self.constants[s] = entry

        elif tag == "me":
            if name == "SetNativeDataProperty":
                s = nth_arg_string(call, 1, depth=6)
                if s:
                    ro = readonly_from(doc, call)
                    doc.setdefault("mode", "readonly" if ro else "readwrite")
                    entry = {"name": s, "readonly": ro, "doc": doc, **loc}
                    self.me_properties.append(entry)

        elif tag == "class_ctors":
            pass  # ClassName constexpr already captures this

    def _scan_constructable(self, class_cursor):
        """A class is constructable unless its body uses the
        ``V8_CLASS_NOT_CONSTRUCTABLE`` macro (which makes ``new T()`` throw)."""
        cpp = class_cursor.spelling
        if cpp in self._constructable:
            return
        ext = class_cursor.extent
        src = ext.start.file
        if not src:
            return
        lines = _get_lines(Path(src.name))
        body = "\n".join(lines[ext.start.line - 1 : ext.end.line])
        self._constructable[cpp] = "V8_CLASS_NOT_CONSTRUCTABLE" not in body

    def _on_constructor(self, cur, cpp_name, path):
        """Record the documented ``New`` definition as the class constructor.
        The macro-generated New carries no ``///`` block, so only real,
        documented constructors produce an entry."""
        src_file = cur.location.file
        src_path = Path(src_file.name) if src_file else path
        line = cur.location.line
        key = (self._rel(src_path), line, cpp_name + "::New")
        if key in self._seen:
            return
        self._seen.add(key)
        doc = extract_doc_comment(src_path, line)
        if doc and cpp_name not in self._ctor:
            self._ctor[cpp_name] = {
                "file": self._rel(src_path),
                "line": line,
                "doc": doc,
            }

    # ── helpers ────────────────────────────────────────────────────

    def _ensure_class(self, cpp_name, js_name=None, path=None):
        if js_name is None:
            js_name = self._class_js.get(cpp_name) or cpp_name.removeprefix("JS")
        if js_name not in self.classes:
            self.classes[js_name] = {
                "cpp_class": cpp_name,
                "source": self._rel(path) if path else None,
                "methods": [],
                "properties": [],
                "static_methods": [],
            }
        return self.classes[js_name]

    def _rel(self, path):
        try:
            return str(path.relative_to(REPO_ROOT)).replace("\\", "/")
        except ValueError:
            return str(path)

    def to_dict(self):
        for cls in self.classes.values():
            cpp = cls["cpp_class"]
            if cpp in self._constructable:
                cls["constructable"] = self._constructable[cpp]
            if cls.get("constructable") and cpp in self._ctor:
                cls["constructor"] = self._ctor[cpp]
            if cpp in self._extends:
                cls["extends"] = self._extends[cpp]
        return {
            "classes": self.classes,
            "global_functions": self.globals,
            "constants": self.constants,
            "me_properties": self.me_properties,
        }


# ── Parallel extraction ─────────────────────────────────────────────
#
# Parsing dominates runtime: every file re-parses the full V8 + MSVC +
# Windows SDK include set (~4-5s each), and the work is CPU-bound inside
# libclang.  Each file is an independent translation unit, so we fan the
# parse+walk out across worker processes (threads don't help — the AST
# walk is GIL-bound Python over libclang ctypes calls).  Each worker owns
# its own libclang Index and returns a partial API dict; the parent merges
# them in source order.

_worker_index = None
_worker_flags = None


def _init_worker(flags):
    """Pool initializer: one libclang Index + flag set per worker process."""
    global _worker_index, _worker_flags
    _worker_index = Index.create()
    _worker_flags = flags


def _extract_one(path_str):
    """Parse a single file in a worker and return its partial API dict."""
    extractor = ApiExtractor()
    extractor.process(_worker_index, Path(path_str), _worker_flags)
    return extractor.to_dict()


def merge_partials(partials):
    """Merge per-file partial dicts (in source order) into one result.

    Replays the same first-seen ordering and ``(file, line, name)``
    deduplication that the serial walk applied through its shared ``_seen``
    set, so the merged output is identical to parsing every file with a
    single extractor.  ``partials`` must be in the same order as
    ``find_api_sources()`` returned the files.
    """
    classes = {}
    global_functions = []
    constants = {}
    me_properties = []
    seen = set()

    def fresh(entry):
        key = (entry["file"], entry["line"], entry["name"])
        if key in seen:
            return False
        seen.add(key)
        return True

    for part in partials:
        for js_name, cls in part["classes"].items():
            tgt = classes.get(js_name)
            if tgt is None:
                tgt = {
                    "cpp_class": cls["cpp_class"],
                    "source": cls["source"],
                    "methods": [],
                    "properties": [],
                    "static_methods": [],
                }
                classes[js_name] = tgt
            for attr in ("constructable", "constructor", "extends"):
                if attr in cls and attr not in tgt:
                    tgt[attr] = cls[attr]
            for bucket in ("methods", "properties", "static_methods"):
                for entry in cls[bucket]:
                    if fresh(entry):
                        tgt[bucket].append(entry)

        for entry in part["global_functions"]:
            if fresh(entry):
                global_functions.append(entry)

        for name, val in part["constants"].items():
            key = (val.get("file"), val.get("line"), name)
            if key in seen or name in constants:
                continue
            seen.add(key)
            constants[name] = val

        for entry in part["me_properties"]:
            if fresh(entry):
                me_properties.append(entry)

    return {
        "classes": classes,
        "global_functions": global_functions,
        "constants": constants,
        "me_properties": me_properties,
    }


# ── Main ────────────────────────────────────────────────────────────


def find_api_sources():
    api_dir = REPO_ROOT / "src" / "framework" / "api"
    files = []
    for ext in ("*.h", "*.cpp"):
        files.extend(sorted(api_dir.rglob(ext)))
    return files


EVENTS_FILE = REPO_ROOT / "src" / "framework" / "components" / "events" / "Events.h"
TXT_TABLES_FILE = REPO_ROOT / "src" / "framework" / "api" / "globals" / "TxtTables.h"


def extract_txt_tables(path=TXT_TABLES_FILE):
    """Parse the generated TxtTables.h into ``[{name, columns: [...]}, ...]`` -
    the Diablo II .txt table + column schema behind ``getBaseStat``.

    Reads the committed header by text (no libclang), but keys off the code
    structure rather than the cosmetic ``// [i] name`` banners:
      - ``TXT_TABLE_NAMES``      -> table names, in table order
      - ``TXT_COLUMNS_<VAR>``    -> that variable's column-name array
      - ``TXT_COLUMNS_BY_TABLE`` -> table index -> ``TXT_COLUMNS_<VAR>``, in order
    so reformatting or re-commenting the generated header can't desync it."""
    if not Path(path).exists():
        return []
    text = Path(path).read_text(encoding="utf-8", errors="replace")

    def body(decl_re):
        m = re.search(decl_re, text, re.DOTALL)
        return m.group(1) if m else ""

    # Table names, in table order.
    names = re.findall(r'"([^"]+)"', body(r"TXT_TABLE_NAMES\s*=\s*\{(.*?)\};"))
    # Every std::string_view column array, by its variable name. (The std::span
    # TXT_COLUMNS_BY_TABLE has a different element type, so it won't match here.)
    cols_by_var = {
        m.group(1): re.findall(r'"([^"]+)"', m.group(2))
        for m in re.finditer(
            r"std::array<std::string_view,\s*\d+>\s+(TXT_COLUMNS_\w+)\s*=\s*\{(.*?)\};",
            text,
            re.DOTALL,
        )
    }
    # Table index -> column-array variable, read from the BY_TABLE initializer.
    order = re.findall(r"TXT_COLUMNS_\w+", body(r"TXT_COLUMNS_BY_TABLE\s*=\s*\{(.*?)\};"))
    # Array position IS the query index for both tables and columns (the arrays
    # are dense 0..N-1, matching how getBaseStat indexes them).
    return [
        {"index": i, "name": nm, "columns": cols_by_var.get(order[i], []) if i < len(order) else []}
        for i, nm in enumerate(names)
    ]


# Files parsed with libclang (the same toolchain as the API sources) for the
# option-set tables: `enum class` definitions whose enumerators back enum-typed
# `{type}`s, plus the compatibility-flag catalog (RegisterDefaults). Only sets
# referenced by a doc `{type}` somewhere are emitted.
ENUM_SOURCES = [
    REPO_ROOT / "src" / "framework" / "game" / "Types.h",
    REPO_ROOT / "src" / "framework" / "game" / "Constants.h",
]
COMPAT_FLAGS_SOURCE = REPO_ROOT / "src" / "framework" / "components" / "config" / "CompatibilityFlags.cpp"

_INT_LITERAL_RE = re.compile(r"0[xX][0-9a-fA-F]+|\d+")


def _enum_is_flags(filepath, line):
    """A bitfield enum is marked with a `/// @flags` comment line directly above
    it (so it is typed `number` in the d.ts - a value is an OR-combination).
    Explicit, not guessed from the enumerator values."""
    lines = _get_lines(Path(filepath))
    idx = line - 2  # 1-based; the line above the `enum class`
    while idx >= 0:
        text = lines[idx].strip()
        if not text.startswith("///"):
            break
        if "@flags" in text:
            return True
        idx -= 1
    return False


def _strip_comment_markers(raw):
    if not raw:
        return ""
    return " ".join(re.sub(r"(?m)^\s*(///?<?|/\*+|\*+/|\*)", "", raw).split())


def _enumerator_value(cursor):
    """The source token of an enumerator's value (preserves hex like 0x04); falls
    back to the evaluated integer (auto-incremented / non-literal initializers)."""
    toks = [t.spelling for t in cursor.get_tokens()]
    if "=" in toks:
        tok = toks[toks.index("=") + 1]
        if _INT_LITERAL_RE.fullmatch(tok):
            return tok
    return str(cursor.enum_value)


def _enumerator_desc(cursor):
    """An enumerator's description: a leading `///` doc comment, else a trailing
    `// comment` on its source line."""
    raw = _strip_comment_markers(cursor.raw_comment)
    if raw:
        return raw
    src = cursor.location.file
    if src:
        lines = _get_lines(Path(src.name))
        i = cursor.location.line - 1
        if 0 <= i < len(lines):
            m = re.search(r"//<?\s*(.*)$", lines[i])
            if m:
                return m.group(1).strip()
    return ""


def _collect_type_idents(obj, out):
    """Recursively gather identifiers appearing in any "type" field of *obj*."""
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k == "type" and isinstance(v, str):
                out.update(re.findall(r"[A-Za-z_]\w*", v))
            else:
                _collect_type_idents(v, out)
    elif isinstance(obj, list):
        for item in obj:
            _collect_type_idents(item, out)


def _parse_enum_defs(tu, path):
    """Collect the `enum class` option sets *defined in* path from a parsed TU."""
    out = {}
    for cur in tu.cursor.walk_preorder():
        if (
            cur.kind == CursorKind.ENUM_DECL
            and cur.is_definition()
            and cur.location.file
            and Path(cur.location.file.name) == path
        ):
            rows = [
                {"value": _enumerator_value(c), "name": c.spelling, "description": _enumerator_desc(c)}
                for c in cur.get_children()
                if c.kind == CursorKind.ENUM_CONSTANT_DECL
            ]
            if rows:
                kind = "flags" if _enum_is_flags(path, cur.location.line) else "enum"
                out[cur.spelling] = {"name": cur.spelling, "kind": kind, "rows": rows}
    return out


def _parse_compat_flag_rows(tu):
    """Collect the CompatibilityFlag rows from RegisterDefaults()'s documented
    `Register("name")` calls (read the same way as RegisterConstants)."""
    rows = []
    for cur in tu.cursor.walk_preorder():
        if (
            cur.spelling == "RegisterDefaults"
            and cur.is_definition()
            and cur.kind in (CursorKind.CXX_METHOD, CursorKind.FUNCTION_DECL)
        ):
            for call in cur.walk_preorder():
                if call.kind == CursorKind.CALL_EXPR and callee_spelling(call) == "Register":
                    name = nth_arg_string(call, 0)
                    if not name:
                        continue
                    src = call.location.file
                    doc = extract_doc_comment(Path(src.name), call.location.line) if src else {}
                    rows.append({"name": name, "description": (doc or {}).get("description", "")})
    return rows


def extract_enums(result, flags):
    """Build the `{name -> option set}` map (enums + the CompatibilityFlag set),
    parsed with libclang, filtered to those referenced by a doc `{type}` in
    *result*. Each set: `{name, kind, rows:[{value?, name, description?}]}`."""
    index = Index.create()
    opts = TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD | TranslationUnit.PARSE_INCOMPLETE

    defs = {}
    for path in ENUM_SOURCES:
        if path.exists():
            defs.update(_parse_enum_defs(index.parse(str(path), args=flags, options=opts), path))

    if COMPAT_FLAGS_SOURCE.exists():
        rows = _parse_compat_flag_rows(index.parse(str(COMPAT_FLAGS_SOURCE), args=flags, options=opts))
        if rows:
            defs["CompatibilityFlag"] = {"name": "CompatibilityFlag", "kind": "flags", "rows": rows}

    referenced = set()
    _collect_type_idents(result, referenced)
    return {name: d for name, d in defs.items() if name in referenced}


def _find_string_literal(cur, depth=8):
    """First string literal anywhere under *cur*.  Unlike first_string_in this
    descends into function bodies, so it can read a ``Name() { return "x"; }``
    return value."""
    if cur.kind == CursorKind.STRING_LITERAL:
        return strip_quotes(cur.spelling)
    if depth <= 0:
        return None
    for ch in cur.get_children():
        found = _find_string_literal(ch, depth - 1)
        if found is not None:
            return found
    return None


def _event_class_info(cur):
    """If *cur* is an event class (derives from BaseEvent/BlockableEvent),
    return ``(name, blockable)``; else None.  ``name`` is the ``Name()``
    override's string literal - the event analog of a class's ClassName."""
    bases = [
        ch.type.spelling
        for ch in cur.get_children()
        if ch.kind == CursorKind.CXX_BASE_SPECIFIER
    ]
    if not any(b.endswith(("BaseEvent", "BlockableEvent")) for b in bases):
        return None
    blockable = any(b.endswith("BlockableEvent") for b in bases)
    name = None
    for ch in cur.get_children():
        if ch.kind == CursorKind.CXX_METHOD and ch.spelling == "Name":
            name = _find_string_literal(ch)
            break
    return name, blockable


def _event_doc(path, class_line, name, blockable):
    """Build an event dict from the ``/// @event`` doc block above
    *class_line*.  Returns None when there is no @event tag (internal event)."""
    has_event = False
    description = ""
    returns = None
    params = []
    for tag, val in _doc_tag_pairs(path, class_line):
        if tag == "event":
            has_event = True
            if val:
                description = val
        elif tag == "description":
            description = val
        elif tag == "param":
            p = _parse_param(val)
            if "name" in p:
                params.append(
                    {
                        "name": p["name"],
                        "type": p["type"],
                        "description": p["description"],
                    }
                )
        elif tag == "returns":
            returns = _parse_returns(val)
    if not has_event:
        return None
    argstr = ", ".join(f"{p['name']}: {p['type']}" for p in params)
    ev = {
        "name": name,
        "signature": f"{name}({argstr})" if name else None,
        "description": description,
        "params": params,
        "blockable": blockable,
    }
    if returns is not None:
        ev["returns"] = returns
    return ev


def _walk_events(cur, path, rel, events, seen):
    if (
        cur.kind in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL)
        and cur.is_definition()
        and cur.location.file
        and Path(cur.location.file.name).name == Path(path).name
    ):
        info = _event_class_info(cur)
        if info is not None:
            name, blockable = info
            ev = _event_doc(path, cur.location.line, name, blockable)
            if ev is not None and name and name not in seen:
                seen.add(name)
                ev["file"] = rel
                ev["line"] = cur.location.line
                events.append(ev)
    for ch in cur.get_children():
        _walk_events(ch, path, rel, events, seen)


def extract_events(events_path=EVENTS_FILE):
    """Parse *events_path* with libclang and collect USER-FACING events.  An
    event is a class deriving from BaseEvent/BlockableEvent whose doc block
    carries a ``/// @event <description>`` tag; the name comes from its
    ``Name()`` override (like ClassName for classes) and ``blockable`` from the
    base.  Internal events (no @event tag) are excluded.

        /// @event <description>
        /// @param <arg> {type} - <desc>
        /// @returns {type} - <desc>        (blockable events only)"""
    if not Path(events_path).exists():
        return []
    try:
        rel = str(Path(events_path).relative_to(REPO_ROOT)).replace("\\", "/")
    except ValueError:
        rel = str(events_path)
    try:
        index = Index.create()
        tu = index.parse(
            str(events_path),
            args=build_compile_flags(),
            options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
            | TranslationUnit.PARSE_INCOMPLETE,
        )
    except Exception:  # noqa: BLE001 - parse failure -> no events, not fatal
        return []
    events = []
    _walk_events(tu.cursor, events_path, rel, events, set())
    return events


_DRAWABLE_BASE_FILE = (
    REPO_ROOT / "src" / "framework" / "api" / "classes" / "drawing" / "JSDrawableBase.h"
)
_DRAWABLE_REG_RE = re.compile(
    r'Base::(Property|Method)\(\s*isolate\s*,\s*\w+\s*,\s*"([^"]+)"'
)


def extract_drawable_base(path=_DRAWABLE_BASE_FILE):
    """Build the shared 'DrawableBase' class by text-scanning the
    ``Base::Property`` / ``Base::Method`` registrations in
    ConfigureCommonProperties.  These live in a template-method body that
    libclang skips under -fdelayed-template-parsing, so the AST walk misses
    them; the drawable classes carry ``extends: DrawableBase`` instead of
    duplicating these members."""
    lines = _get_lines(path)
    if not lines:
        return None
    try:
        rel = str(Path(path).relative_to(REPO_ROOT)).replace("\\", "/")
    except ValueError:
        rel = str(path)
    text = "\n".join(lines)
    regs = []
    for m in _DRAWABLE_REG_RE.finditer(text):
        line_no = text.count("\n", 0, m.start()) + 1
        regs.append((line_no, m.group(1), m.group(2)))
    if not regs:
        return None
    props, methods = [], []
    for i, (line_no, kind, name) in enumerate(regs):
        doc = extract_doc_comment(path, line_no)
        end = regs[i + 1][0] - 1 if i + 1 < len(regs) else min(line_no + 40, len(lines))
        n_lambdas = "\n".join(lines[line_no - 1 : end]).count("+[]")
        entry = {"name": name, "file": rel, "line": line_no, "doc": doc}
        if kind == "Property":
            readonly = n_lambdas < 2
            doc.setdefault("mode", "readonly" if readonly else "readwrite")
            entry["readonly"] = readonly
            props.append(entry)
        else:
            methods.append(entry)
    return {
        "cpp_class": "JSDrawableBase",
        "source": rel,
        "constructable": False,
        "abstract": True,
        "methods": methods,
        "properties": props,
        "static_methods": [],
    }


def main():
    verbose = "-v" in sys.argv or "--verbose" in sys.argv

    output_path = None
    jobs = None
    args = sys.argv[1:]
    for i, arg in enumerate(args):
        if arg in ("-o", "--output") and i + 1 < len(args):
            output_path = args[i + 1]
        elif arg in ("-j", "--jobs") and i + 1 < len(args):
            try:
                jobs = max(1, int(args[i + 1]))
            except ValueError:
                jobs = None

    flags = build_compile_flags()
    if verbose:
        print(f"Flags: {' '.join(flags)}", file=sys.stderr)

    sources = find_api_sources()
    if jobs is None:
        jobs = min(len(sources), os.cpu_count() or 1)
    print(
        f"Parsing {len(sources)} API source files on {jobs} worker(s) ...",
        file=sys.stderr,
    )

    if jobs == 1:
        index = Index.create()
        extractor = ApiExtractor(verbose=verbose)
        for src in sources:
            if verbose:
                print(f"  {src.relative_to(REPO_ROOT)}", file=sys.stderr)
            extractor.process(index, src, flags)
        result = extractor.to_dict()
    else:
        with ProcessPoolExecutor(
            max_workers=jobs, initializer=_init_worker, initargs=(flags,)
        ) as pool:
            partials = list(pool.map(_extract_one, [str(s) for s in sources]))
        result = merge_partials(partials)

    result["events"] = extract_events()

    # The global `me` object resolves to the player Unit (MyUnit::Resolve), so it
    # exposes the whole Unit surface plus the session-level me_properties. Encode
    # that so the docs/d.ts present `me` as extending Unit.
    if "Unit" in result["classes"]:
        result["me_extends"] = "Unit"

    result["tables"] = extract_txt_tables()

    # Option sets (enum / flag value tables) referenced by doc `{type}`s. Must run
    # after the result is assembled so referenced-type filtering can see all docs.
    result["enums"] = extract_enums(result, flags)

    drawable_base = extract_drawable_base()
    if drawable_base and "DrawableBase" not in result["classes"]:
        result["classes"]["DrawableBase"] = drawable_base

    nc = len(result["classes"])
    nm = sum(len(c["methods"]) for c in result["classes"].values())
    np = sum(len(c["properties"]) for c in result["classes"].values())
    ns = sum(len(c["static_methods"]) for c in result["classes"].values())
    ng = len(result["global_functions"])
    nk = len(result["constants"])
    nme = len(result["me_properties"])
    nctor = sum(1 for c in result["classes"].values() if "constructor" in c)
    nnoctor = sum(
        1 for c in result["classes"].values() if c.get("constructable") is False
    )
    nev = len(result.get("events", []))
    nen = len(result.get("enums", {}))
    print(
        f"Found: {nc} classes ({nm} methods, {np} properties, {ns} static, "
        f"{nctor} constructors, {nnoctor} non-constructable), "
        f"{ng} global functions, {nk} constants, {nme} 'me' properties, "
        f"{nev} events, {nen} enums",
        file=sys.stderr,
    )

    if output_path:
        with open(output_path, "w", encoding="utf-8") as f:
            json.dump(result, f, indent=2)
            f.write("\n")
        print(f"Written to {output_path}", file=sys.stderr)
    else:
        json.dump(result, sys.stdout, indent=2)
        print()  # trailing newline


if __name__ == "__main__":
    main()
