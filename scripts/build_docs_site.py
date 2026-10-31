#!/usr/bin/env python3
"""
Assemble the versioned API documentation site for GitHub Pages.

Bundle model (no runtime GitHub API calls from the page): this gathers every
release's ``api.json`` server-side and writes a self-contained site:

    site/
      index.html            the shell (from gen_api_docs.py), latest baked in
      versions.json         {"versions":[{tag,name,date,body}, ...]} newest-first
      data/<tag>.json       one api.json per version

The page then fetches only same-origin ``versions.json`` + ``data/<tag>.json``,
so there are no rate limits, no CORS, and no visitor auth.

Inputs:
  --releases   JSON array of release objects (e.g. `gh api repos/O/R/releases
               --paginate`). Each needs tag_name, name, body, published_at,
               draft, assets[].name.
  --data-dir   directory holding (or to download) per-tag ``<tag>.json`` files.
  --download   if set, `gh release download <tag> -p api.json` any release that
               has an api.json asset but no local ``<tag>.json`` yet.
  --repo       OWNER/REPO (for `gh` + source links).
  --out        output site directory.

Only releases that are not drafts, expose an ``api.json`` asset, AND have a
resolved ``<tag>.json`` are included. Pure-stdlib apart from optional `gh`.

A release that also carries a ``sim.json`` asset (the kolbot simulator's
options, environment variables and guide) gets it as ``<tag>.sim.json`` in
--data-dir, downloaded like api.json, and its ``data/<tag>.json`` is the api.json
with that document under ``simulator``; the page renders it as a Simulator
section. A release without one is published exactly as before.

Usage (CI):
    gh api repos/$REPO/releases --paginate > releases.json
    python scripts/build_docs_site.py --releases releases.json --data-dir data \\
        --repo $REPO --out site --download
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

import gen_api_docs  # same directory

_SEMVER = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)$")


def semver_key(tag):
    m = _SEMVER.match(str(tag))
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)


def has_asset(rel, name):
    return any(a.get("name") == name for a in rel.get("assets", []))


def has_api_asset(rel):
    return has_asset(rel, "api.json")


def download_asset(repo, tag, dest, name="api.json"):
    """`gh release download <tag> -p <name>` -> dest. Returns True on success."""
    try:
        subprocess.run(
            ["gh", "release", "download", tag, "--repo", repo, "-p", name, "-O", str(dest), "--clobber"],
            check=True,
            capture_output=True,
            text=True,
        )
        return dest.exists()
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        msg = getattr(e, "stderr", "") or str(e)
        print(f"  ! download {tag} {name}: {msg.strip()}", file=sys.stderr)
        return False


def load_sim(rel, data_dir, repo, download):
    """The release's sim.json, or None when it has none (or it cannot be read)."""
    if not has_asset(rel, "sim.json"):
        return None
    tag = rel["tag_name"]
    local = data_dir / f"{tag}.sim.json"
    if not local.exists() and download:
        download_asset(repo, tag, local, "sim.json")
    if not local.exists():
        print(f"  - {tag}: no sim.json available locally", file=sys.stderr)
        return None
    try:
        return json.loads(local.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        print(f"  ! {tag}: invalid sim.json ({e})", file=sys.stderr)
        return None


def main():
    ap = argparse.ArgumentParser(description="Assemble the versioned docs site from releases.")
    ap.add_argument("--releases", required=True, help="JSON array of release objects (gh api ...)")
    ap.add_argument("--data-dir", required=True, help="dir with / for per-tag <tag>.json files")
    ap.add_argument("--out", required=True, help="output site directory")
    ap.add_argument("--repo", default="", help="OWNER/REPO")
    ap.add_argument("--title", default="d2bsng API", help="page title")
    ap.add_argument("--download", action="store_true", help="gh release download missing api.json assets")
    args = ap.parse_args()

    releases = json.loads(Path(args.releases).read_text(encoding="utf-8"))
    data_dir = Path(args.data_dir)
    data_dir.mkdir(parents=True, exist_ok=True)
    out = Path(args.out)
    (out / "data").mkdir(parents=True, exist_ok=True)

    versions = []
    for rel in releases:
        if rel.get("draft") or not has_api_asset(rel):
            continue
        tag = rel["tag_name"]
        local = data_dir / f"{tag}.json"
        if not local.exists() and args.download:
            if not args.repo:
                sys.exit("--download requires --repo")
            download_asset(args.repo, tag, local)
        if not local.exists():
            print(f"  - skip {tag}: no api.json available locally", file=sys.stderr)
            continue
        # validate it parses, then publish into the site
        try:
            data = json.loads(local.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            print(f"  ! skip {tag}: invalid JSON ({e})", file=sys.stderr)
            continue
        sim = load_sim(rel, data_dir, args.repo, args.download)
        if sim is None:
            shutil.copyfile(local, out / "data" / f"{tag}.json")
        else:
            data["simulator"] = sim
            (out / "data" / f"{tag}.json").write_text(
                json.dumps(data, separators=(",", ":"), ensure_ascii=False), encoding="utf-8"
            )
        versions.append(
            {
                "tag": tag,
                "name": rel.get("name") or tag,
                "date": rel.get("published_at", ""),
                "body": rel.get("body", "") or "",
                "_data": data,  # kept transiently to pick the latest for inline
            }
        )

    versions.sort(key=lambda v: semver_key(v["tag"]), reverse=True)
    if not versions:
        sys.exit("No releases with a resolvable api.json - nothing to publish.")

    latest = versions[0]
    inline = latest["_data"]
    latest_label = latest["tag"].lstrip("v")

    manifest = {"versions": [{k: v[k] for k in ("tag", "name", "date", "body")} for v in versions]}
    (out / "versions.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    page = gen_api_docs.build_page(
        inline, title=args.title, version=latest_label, repo=args.repo, source_base=""
    )
    (out / "index.html").write_text(page, encoding="utf-8")

    print(
        f"Built {out}/ with {len(versions)} version(s): "
        f"{', '.join(v['tag'] for v in versions)} (latest baked in: {latest['tag']})",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
