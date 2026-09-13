#!/usr/bin/env python3
"""Check PlugY's 1.14d byte patches against d2bsng's hook sites for overlap.

PlugY writes raw bytes at fixed RVAs and verifies the bytes it expects to find
there first, so the two must never share a site, whichever patches first. This walks PlugY's source for every 1.14d write site (the
last entry of each ``R8(...)`` address macro and the ``V114d ? offset + 0x...``
forms, plus the bytes each site writes) and intersects them with d2bsng's
``Intercepts.cpp`` RVA table (with install lengths), the PlugY Init hook site
in ``game/PlugY.cpp``, and the Detours targets.

Usage:
    python scripts/plugy_overlap_audit.py <path-to-PlugY-source>/PlugY

Pure stdlib. Exit status 1 when an overlap is found. See docs/plugy_stash.md.
"""

import argparse
import glob
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOKS_DIR = os.path.join(REPO_ROOT, "src", "backends", "lod114d", "hooks")
GAME_DIR = os.path.join(REPO_ROOT, "src", "backends", "lod114d", "game")
STORM_IMPORTS = os.path.join(REPO_ROOT, "src", "backends", "lod114d", "imports", "Storm.h")

# Detours copies the first few instructions of a hooked function into its
# trampoline; a PlugY write inside that prologue would be copied along with it.
DETOUR_PROLOGUE_ALLOWANCE = 16
NEAR_MISS_WINDOW = 0x40

R8_SITE = re.compile(r"R8\(\s*(\w+)\s*," + r"\s*([0-9A-Fa-f]+)\s*," * 8 + r"\s*([0-9A-Fa-f]+)\s*\)")
EXPLICIT_114D_SITE = re.compile(r"V114d\s*\?\s*offset_\w+\s*\+\s*0x([0-9A-Fa-f]+)")
WRITE_1 = re.compile(r"\bmemt_byte\s*\(")
WRITE_2 = re.compile(r"\bmemt_word\s*\(")
WRITE_4 = re.compile(r"\b(memt_dword|MEMT_REF4|MEMJ_REF4|MEMC_REF4|MEMD_REF4|memt_ref4|memc_ref4|memd_ref4|memj_ref4)\s*\(")


def strip_comment(line):
    return line.split("//", 1)[0]


def plugy_sites(plugy_dir):
    """[(rva, length, where, module)] for every 1.14d write site."""
    sites = []
    for path in sorted(glob.glob(os.path.join(plugy_dir, "*.cpp"))):
        with open(path, encoding="latin-1") as f:
            lines = f.read().split("\n")
        for i, raw in enumerate(lines):
            line = strip_comment(raw)
            if "mem_seek" not in line:
                continue
            rva = None
            module = "?"
            m = R8_SITE.search(line)
            if m:
                module = m.group(1)
                if int(m.group(10), 16) != 0:
                    rva = int(m.group(10), 16)
            else:
                m = EXPLICIT_114D_SITE.search(line)
                if m:
                    rva = int(m.group(1), 16)
                    module = "explicit"
            if rva is None:
                continue
            # Bytes written at this seek position: every memt_* / MEM*_REF4 call
            # until the next seek or the end of the installer. Conditional writes
            # are counted too (conservative).
            length = 0
            for j in range(i + 1, min(i + 60, len(lines))):
                nxt = strip_comment(lines[j])
                if "mem_seek" in nxt or nxt.strip().startswith("isInstalled") or nxt.strip() == "}":
                    break
                length += len(WRITE_1.findall(nxt)) + 2 * len(WRITE_2.findall(nxt)) + 4 * len(WRITE_4.findall(nxt))
            sites.append((rva, max(length, 4), f"{os.path.basename(path)}:{i + 1}", module))
    return sites


def d2bs_sites():
    """{name: (rva, length)} for every place d2bsng writes into Game.exe."""
    sites = {}
    with open(os.path.join(HOOKS_DIR, "Intercepts.cpp"), encoding="utf-8") as f:
        src = f.read()
    for name, val in re.findall(r"constexpr uint32_t (\w+_RVA) = (0x[0-9A-Fa-f]+);", src):
        sites[name] = [int(val, 16), 6]
    for name, length in re.findall(r"InstallSite\(\s*\w+,\s*(\w+_RVA),[^;]*?,\s*(\d+)\s*,\s*/\*isJmp", src, re.S):
        sites[name][1] = int(length)
    with open(os.path.join(HOOKS_DIR, "HookManager.cpp"), encoding="utf-8") as f:
        m = re.search(r"CURSOR_LOCK_OFFSET\s*=\s*(0x[0-9A-Fa-f]+)", f.read())
    if m:
        sites["Detour CursorLock"] = [int(m.group(1), 16), DETOUR_PROLOGUE_ALLOWANCE]
    # The startup LoadLibraryA call d2bsng redirects to run PlugY's Init (game/PlugY.cpp).
    with open(os.path.join(GAME_DIR, "PlugY.cpp"), encoding="utf-8") as f:
        m = re.search(r"STARTUP_LOADLIBRARY_CALL_RVA\s*=\s*(0x[0-9A-Fa-f]+)", f.read())
    if m:
        sites["PlugY Init hook (startup LoadLibraryA)"] = [int(m.group(1), 16), 6]
    with open(STORM_IMPORTS, encoding="utf-8") as f:
        storm = f.read()
    for fn in ("SSTR_RegistryReadValueEx", "RegStoringKeysConfiguration"):
        m = re.search(fn + r"\{(0x[0-9A-Fa-f]+)\}", storm)
        if m:
            sites[f"Detour {fn}"] = [int(m.group(1), 16), DETOUR_PROLOGUE_ALLOWANCE]
    return {name: tuple(v) for name, v in sites.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("plugy_dir", help="PlugY source directory containing InfinityStash.cpp etc.")
    args = parser.parse_args()

    plugy = plugy_sites(args.plugy_dir)
    d2bs = d2bs_sites()
    print(f"PlugY 1.14d write sites: {len(plugy)}   d2bsng sites: {len(d2bs)}")

    overlaps = 0
    for rva, length, where, module in sorted(plugy):
        for name, (drva, dlen) in d2bs.items():
            if rva < drva + dlen and drva < rva + length:
                overlaps += 1
                print(f"OVERLAP  PlugY {where} {module} {rva:#x}+{length}  <->  d2bsng {name} {drva:#x}+{dlen}")
            elif abs(rva - drva) <= NEAR_MISS_WINDOW:
                print(f"near     PlugY {where} {module} {rva:#x}+{length}  ..  d2bsng {name} {drva:#x}+{dlen}")
    print(f"overlaps: {overlaps}")
    return 1 if overlaps else 0


if __name__ == "__main__":
    sys.exit(main())
