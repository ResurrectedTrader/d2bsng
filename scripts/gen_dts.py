#!/usr/bin/env python3
"""
Generate a TypeScript declaration file (``d2bsng.d.ts``) from the d2bsng JS API
surface (the JSON produced by extract_api.py).

The output is an *ambient*, non-module declaration file: classes, global
functions, the ``me`` object, constants and event hooks are declared as
globals, exactly how scripts see them. Drop it into a project (or reference it
with ``/// <reference path="d2bsng.d.ts" />``) for editor completion and type
checking of d2bsng scripts. Every member carries a JSDoc block built from the
API descriptions, so hovers show the same docs as the HTML page.

Like gen_api_docs.py this is pure-stdlib and needs no libclang / game toolchain,
so it runs anywhere the JSON can be copied.

Usage:
    python scripts/extract_api.py -o api.json
    python scripts/gen_dts.py api.json -o d2bsng.d.ts
"""

import argparse
import json
import re
import sys
from pathlib import Path

_IDENT_RE = re.compile(r"^[A-Za-z_$][\w$]*$")


# ── type mapping ────────────────────────────────────────────────────


def ts_type(t, *, is_return=False):
    """Map an API doc type to a TS type. The doc vocabulary is already
    TS-shaped (``Unit``, ``Unit[]``, ``Room | null``, ``{x: number, y: number}``),
    so this mostly passes through, with a couple of normalizations."""
    if t is None or str(t).strip() == "":
        return "void" if is_return else "any"
    t = str(t).strip()
    if is_return and t == "null":
        return "void"  # "returns null" is the API's way of saying "no result"
    # a bare `function` param type with no callback detail -> the Function type
    t = re.sub(r"\bfunction\b", "Function", t)
    return t


def ts_callback(cb):
    """Render a callback param's shape as a TS function type."""
    params = ", ".join(ts_param(p) for p in cb.get("params", []))
    ret = ts_type(cb.get("returns"), is_return=True)
    return f"({params}) => {ret}"


def ts_param(p):
    """Render one parameter as ``name: type`` / ``name?: type`` / ``...name: T[]``."""
    raw = p.get("name", "arg")
    spread = raw.startswith("...")
    base = raw[3:] if spread else raw
    if not _IDENT_RE.match(base):
        base = "_" + re.sub(r"\W", "_", base)
    cb = p.get("callback")
    typ = ts_callback(cb) if cb else ts_type(p.get("type"))
    if spread:
        arr = typ if typ.endswith("[]") else (f"({typ})[]" if re.search(r"[ |&]", typ) else f"{typ}[]")
        return f"...{base}: {arr}"
    opt = "?" if p.get("optional") else ""
    return f"{base}{opt}: {typ}"


# ── JSDoc ───────────────────────────────────────────────────────────


def jsdoc(description="", params=None, returns=None, throws=None, indent=""):
    """Build a JSDoc block. Returns "" when there is nothing to say."""
    lines = []
    if description:
        lines.extend(description.split("\n"))
    for p in params or []:
        nm = p.get("name", "")
        desc = p.get("description", "")
        if nm:
            lines.append(f"@param {nm}{(' - ' + desc) if desc else ''}")
    if returns:
        rdesc = returns.get("description", "")
        if rdesc:
            lines.append(f"@returns {rdesc}")
    for t in throws or []:
        td = t.get("description", "")
        if td:
            lines.append(f"@throws {td}")
    if not lines:
        return ""
    if len(lines) == 1:
        return f"{indent}/** {lines[0]} */\n"
    body = "\n".join(f"{indent} * {ln}" for ln in lines)
    return f"{indent}/**\n{body}\n{indent} */\n"


# ── callable (method / function / constructor) ──────────────────────


def emit_callable(entry, *, decl, indent="", ctor=False):
    """Emit JSDoc + one declaration line per documented overload.

    `decl` is the prefix before the parens: e.g. ``"declare function print"``,
    a method name, ``"static foo"``, or ``"constructor"`` (ctor=True drops the
    return type)."""
    doc = entry.get("doc") or {}
    sigs = doc.get("signatures") or []
    out = []
    if not sigs:
        out.append(jsdoc(doc.get("description", ""), indent=indent))
        out.append(f"{indent}{decl}(...args: any[]): {'void' if ctor else 'any'};\n")
        return "".join(out)
    for s in sigs:
        params = ", ".join(ts_param(p) for p in s.get("params", []))
        out.append(
            jsdoc(
                doc.get("description", ""),
                params=s.get("params"),
                returns=s.get("returns"),
                throws=doc.get("throws"),
                indent=indent,
            )
        )
        if ctor:
            out.append(f"{indent}constructor({params});\n")
        else:
            ret = ts_type((s.get("returns") or {}).get("type"), is_return=True)
            out.append(f"{indent}{decl}({params}): {ret};\n")
    return "".join(out)


def emit_property(entry, *, indent=""):
    doc = entry.get("doc") or {}
    ro = "readonly " if entry.get("readonly") else ""
    name = entry["name"] if _IDENT_RE.match(entry["name"]) else json.dumps(entry["name"])
    typ = ts_type(doc.get("type"))
    block = jsdoc(doc.get("description", ""), indent=indent)
    return f"{block}{indent}{ro}{name}: {typ};\n"


# ── document assembly ───────────────────────────────────────────────


def emit_class(name, cls):
    out = []
    out.append(jsdoc(f"d2bsng `{name}` object."))
    kw = "declare abstract class" if cls.get("abstract") else "declare class"
    ext = f" extends {cls['extends']}" if cls.get("extends") else ""
    out.append(f"{kw} {name}{ext} {{\n")

    if cls.get("abstract"):
        pass  # subclasses extend it; no constructor line
    elif cls.get("constructable") is False:
        out.append("    private constructor();\n")
    elif "constructor" in cls:
        out.append(emit_callable(cls["constructor"], decl="constructor", indent="    ", ctor=True))

    for p in sorted(cls.get("properties", []), key=lambda e: e["name"].lower()):
        out.append(emit_property(p, indent="    "))

    for m in sorted(cls.get("methods", []), key=lambda e: e["name"].lower()):
        out.append(emit_callable(m, decl=_member_decl(m["name"]), indent="    "))

    for m in sorted(cls.get("static_methods", []), key=lambda e: e["name"].lower()):
        out.append(emit_callable(m, decl="static " + _member_decl(m["name"]), indent="    "))

    out.append("}\n")
    return "".join(out)


def _member_decl(name):
    return name if _IDENT_RE.match(name) else json.dumps(name)


def emit_namespace_const(name, val):
    out = [jsdoc((val.get("doc") or {}).get("description", ""))]
    out.append(f"declare namespace {name} {{\n")
    for child in val.get("properties", []):
        doc = child.get("doc") or {}
        out.append(jsdoc(doc.get("description", ""), indent="    "))
        out.append(f"    const {child['name']}: {ts_type(doc.get('type'))};\n")
    out.append("}\n")
    return "".join(out)


def emit_const(name, val):
    doc = val.get("doc") or {}
    block = jsdoc(doc.get("description", ""))
    return f"{block}declare const {name}: {ts_type(doc.get('type'))};\n"


def build_dts(data, *, version):
    classes = data.get("classes", {})
    globals_ = data.get("global_functions", [])
    constants = data.get("constants", {})
    me_props = data.get("me_properties", [])
    me_extends = data.get("me_extends")
    events = data.get("events", [])

    out = []
    out.append("// d2bsng JavaScript API - TypeScript declarations\n")
    if version:
        out.append(f"// Version {version}\n")
    out.append(
        "// Generated from the V8 bindings by scripts/extract_api.py + scripts/gen_dts.py.\n"
        "// Ambient global declarations: reference this file (or add it to your\n"
        "// tsconfig 'include') for editor completion of d2bsng scripts.\n\n"
    )

    # Classes (declaration order is irrelevant in an ambient .d.ts; base classes
    # need not precede subclasses).
    out.append("// === Classes ===\n\n")
    for name in sorted(classes, key=str.lower):
        out.append(emit_class(name, classes[name]))
        out.append("\n")

    # The `me` object: the player Unit (extends it) plus session-level extras.
    if me_props:
        out.append("// === The `me` object (player character) ===\n\n")
        ext = f" extends {me_extends}" if me_extends and me_extends in classes else ""
        out.append(f"interface D2Me{ext} {{\n")
        for p in sorted(me_props, key=lambda e: e["name"].lower()):
            out.append(emit_property(p, indent="    "))
        out.append("}\n")
        out.append(jsdoc("The player character.", indent=""))
        out.append("declare const me: D2Me;\n\n")

    # Constants.
    if constants:
        out.append("// === Constants ===\n\n")
        for cname, cval in constants.items():
            if isinstance(cval, dict) and "properties" in cval:
                out.append(emit_namespace_const(cname, cval))
            else:
                out.append(emit_const(cname, cval))
        out.append("\n")

    # Global functions (addEventListener is rebuilt below with typed overloads).
    special = {"addEventListener"}
    out.append("// === Global functions ===\n\n")
    for g in sorted(globals_, key=lambda e: e["name"].lower()):
        if g["name"] in special:
            continue
        out.append(emit_callable(g, decl=f"declare function {_member_decl(g['name'])}"))

    # Events -> a name union + typed addEventListener overloads.
    if events:
        out.append("\n// === Events ===\n\n")
        ev_sorted = sorted(events, key=lambda e: e["name"].lower())
        union = " | ".join(json.dumps(e["name"]) for e in ev_sorted)
        out.append(f"type D2EventName = {union};\n\n")
        if any(g["name"] == "addEventListener" for g in globals_):
            for e in ev_sorted:
                cbparams = ", ".join(ts_param(p) for p in e.get("params", []))
                ret = (
                    ts_type((e.get("returns") or {}).get("type"), is_return=True)
                    if e.get("blockable")
                    else "void"
                )
                out.append(
                    jsdoc(
                        e.get("description", ""),
                        params=e.get("params"),
                        returns=e.get("returns") if e.get("blockable") else None,
                    )
                )
                out.append(
                    f"declare function addEventListener(event: {json.dumps(e['name'])}, "
                    f"callback: ({cbparams}) => {ret}): void;\n"
                )
            out.append(
                "declare function addEventListener(event: D2EventName | string, "
                "callback: (...args: any[]) => any): void;\n"
            )

    return "".join(out)


def main():
    ap = argparse.ArgumentParser(description="Generate d2bsng.d.ts from the API JSON.")
    ap.add_argument("input", help="api.json produced by extract_api.py")
    ap.add_argument("-o", "--output", help="output .d.ts path (default: stdout)")
    ap.add_argument("--version", default="", help="version label for the file header")
    args = ap.parse_args()

    with open(args.input, encoding="utf-8") as f:
        data = json.load(f)

    dts = build_dts(data, version=args.version)

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(dts, encoding="utf-8")
        print(f"Wrote {out} ({len(dts):,} bytes)", file=sys.stderr)
    else:
        sys.stdout.write(dts)


if __name__ == "__main__":
    main()
