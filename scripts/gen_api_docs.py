#!/usr/bin/env python3
"""
Emit the d2bsng API documentation site shell: a single self-contained HTML page
that renders the JS API surface (the JSON from extract_api.py) in the browser.

The page is a static shell with NO external/CDN dependencies and makes NO
runtime calls to the GitHub API (no rate limits, no CORS, no visitor auth). It
runs in *bundle* mode: on load it fetches a SAME-ORIGIN ``versions.json``
manifest and the chosen version's ``data/<tag>.json``, populates a version
selector, and renders it client-side. A "Changelog" tab shows the selected
version's release notes (from the manifest) plus an automatic API-surface diff
(added / removed / changed symbols) against the previous version. The manifest
and per-version JSON are produced at deploy time by build_docs_site.py, which
gathers them from the repo's releases server-side.

A baked-in api.json (the positional INPUT) is embedded as the offline/fallback
dataset: it renders immediately (instant paint) and is shown when there is no
``versions.json`` alongside the page (e.g. an offline single-file build).

Rendering lives in the embedded JS (so the browser can render any fetched
version); this script just assembles the shell + config + inline data. It is
pure-stdlib and needs no libclang / game toolchain.

Usage:
    python scripts/extract_api.py -o api.json
    # offline single-version page (renders the embedded dataset):
    python scripts/gen_api_docs.py api.json -o api.html --version 1.2.3
    # versioned site shell (build_docs_site.py wraps this for the Pages deploy):
    python scripts/gen_api_docs.py api.json -o site/index.html \\
        --version 1.2.3 --repo OWNER/REPO
"""

import argparse
import html
import json
import sys
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description="Emit the API documentation site (static shell + JS renderer).")
    ap.add_argument("input", nargs="?", help="api.json to embed as the offline/fallback dataset (optional)")
    ap.add_argument("-o", "--output", help="output HTML path (default: stdout)")
    ap.add_argument("--title", default="d2bsng API", help="page title")
    ap.add_argument("--version", default="", help="version label of the embedded dataset")
    ap.add_argument("--repo", default="", help="OWNER/REPO, used to build per-version 'source' links")
    ap.add_argument(
        "--source-base",
        default="",
        help="base URL for 'source' links in offline mode, e.g. "
        "https://github.com/OWNER/REPO/blob/TAG/ (dynamic mode derives this per version)",
    )
    args = ap.parse_args()

    inline = None
    if args.input:
        with open(args.input, encoding="utf-8") as f:
            inline = json.load(f)

    page = build_page(
        inline,
        title=args.title,
        version=args.version,
        repo=args.repo,
        source_base=args.source_base,
    )

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(page, encoding="utf-8")
        print(f"Wrote {out} ({len(page):,} bytes)", file=sys.stderr)
    else:
        sys.stdout.write(page)


def _json_for_script(obj):
    """Serialize for embedding in a <script> tag: escape '<' so a stray
    '</script>' in the data can't terminate the block early."""
    return json.dumps(obj, separators=(",", ":")).replace("<", "\\u003c")


def build_page(inline, *, title, version, repo, source_base):
    cfg = {"repo": repo, "version": version, "sourceBase": source_base, "title": title}
    cfg_json = _json_for_script(cfg)
    inline_block = (
        f'<script id="inline-data" type="application/json">{_json_for_script(inline)}</script>'
        if inline is not None
        else ""
    )
    esc = html.escape
    return (
        "<!DOCTYPE html>\n"
        '<html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1">'
        f"<title>{esc(title)}</title>\n<style>{CSS}</style></head><body>"
        '<button id="menu" aria-label="menu">&#9776;</button>'
        '<nav id="sidebar">'
        f'<div class="brand"><a href="#top">{esc(title)}</a></div>'
        '<div class="verbar"><select id="ver" aria-label="version"></select>'
        '<span id="verstatus" class="verstatus"></span></div>'
        '<input id="search" type="search" placeholder="Filter (press /)" '
        'autocomplete="off" spellcheck="false">'
        '<ul id="nav"></ul>'
        '<div class="no-results" hidden>No matches</div></nav>'
        '<main id="content">'
        '<div id="tabs" class="tabs">'
        '<button class="tab active" data-view="api">API reference</button>'
        '<button class="tab" data-view="changelog">Changelog</button></div>'
        '<div id="api-view"></div>'
        '<div id="changelog-view" hidden></div>'
        '<div id="status" class="status"></div></main>'
        f'<script id="cfg" type="application/json">{cfg_json}</script>'
        f"{inline_block}"
        f"<script>{JS}</script></body></html>\n"
    )


CSS = r"""
:root{
  --bg:#ffffff;--fg:#1f2328;--muted:#656d76;--accent:#0969da;--accent-soft:#ddf4ff;
  --border:#d1d9e0;--code-bg:#f6f8fa;--side-bg:#f6f8fa;--sel:#fff8c5;--sidew:310px;
  --add:#1a7f37;--del:#cf222e;--chg:#9a6700;
}
@media (prefers-color-scheme:dark){:root{
  --bg:#0d1117;--fg:#e6edf3;--muted:#9198a1;--accent:#4493f8;--accent-soft:#121d2f;
  --border:#3d444d;--code-bg:#151b23;--side-bg:#0d1117;--sel:#3a2d00;
  --add:#3fb950;--del:#f85149;--chg:#d29922;
}}
*{box-sizing:border-box}
html{scroll-behavior:smooth}
body{margin:0;font:15px/1.6 -apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif;
  color:var(--fg);background:var(--bg)}
code,pre,.mono{font-family:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,monospace}
a{color:var(--accent);text-decoration:none}
a:hover{text-decoration:underline}
::selection{background:var(--sel)}

#sidebar{position:fixed;top:0;left:0;width:var(--sidew);height:100vh;overflow-y:auto;
  border-right:1px solid var(--border);background:var(--side-bg);padding:14px 10px 40px;z-index:20}
.brand{font-size:16px;font-weight:600;padding:4px 8px 8px}
.brand a{color:var(--fg)}
.verbar{display:flex;align-items:center;gap:8px;padding:0 8px 10px}
#ver{flex:1;min-width:0;padding:5px 8px;border:1px solid var(--border);border-radius:6px;
  background:var(--bg);color:var(--fg);font-size:13px}
.verstatus{font-size:11px;color:var(--muted);white-space:nowrap}
#search{width:100%;padding:7px 10px;margin:0 0 10px;border:1px solid var(--border);
  border-radius:6px;background:var(--bg);color:var(--fg);font-size:13px}
#search:focus{outline:2px solid var(--accent);border-color:var(--accent)}
#nav,.kids{list-style:none;margin:0;padding:0}
.kids{margin-left:14px;border-left:1px solid var(--border)}
.group.collapsed>.kids{display:none}
.grp-row{display:flex;align-items:center;gap:2px;border-radius:6px}
.grp-row:hover{background:var(--accent-soft)}
.tw{flex:0 0 auto;width:18px;height:22px;border:0;background:none;cursor:pointer;color:var(--muted);padding:0;font-size:10px}
.tw::before{content:"\25B8";display:inline-block;transition:transform .12s}
.group:not(.collapsed)>.grp-row>.tw::before{transform:rotate(90deg)}
.grp-label{font-size:13px;font-weight:600;padding:4px 4px;flex:1;color:var(--fg);cursor:pointer}
a.grp-link{color:var(--fg)}
.grp-label .n{font-weight:400;color:var(--muted);font-size:11px}
.leaf>a{display:block;padding:3px 8px 3px 22px;font-size:13px;color:var(--muted);
  border-radius:6px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.leaf>a:hover{background:var(--accent-soft);color:var(--fg);text-decoration:none}
.leaf>a.active{color:var(--accent);background:var(--accent-soft);font-weight:600}
.no-results{color:var(--muted);font-size:13px;padding:8px 10px}

#content{margin-left:var(--sidew);max-width:920px;padding:0 40px 120px}
.tabs{position:sticky;top:0;background:var(--bg);display:flex;gap:4px;padding:14px 0 0;z-index:10;
  border-bottom:1px solid var(--border);margin-bottom:8px}
.tab{border:0;background:none;font:600 14px/1 inherit;color:var(--muted);padding:10px 14px;cursor:pointer;
  border-bottom:2px solid transparent;margin-bottom:-1px}
.tab:hover{color:var(--fg)}
.tab.active{color:var(--accent);border-bottom-color:var(--accent)}
.status{color:var(--muted);font-size:14px;padding:20px 0}
.status.err{color:var(--del)}

h1{font-size:30px;margin:.2em 0 .3em}
.lead{font-size:16px;color:var(--fg)}
.lead-note{color:var(--muted);font-size:13px}
.area{margin:0 0 14px}
.area>h2{font-size:23px;border-bottom:1px solid var(--border);padding-bottom:6px;margin-top:36px}
.area-intro{color:var(--muted);font-size:14px}
.cat-head{font-size:18px;color:var(--muted);margin-top:28px;text-transform:uppercase;letter-spacing:.04em;font-weight:600}
.class-block{margin:10px 0 26px}
.class-head{font-size:21px;margin-top:30px;border-top:2px solid var(--border);padding-top:18px}
.class-meta{color:var(--muted);font-size:12px;margin:-6px 0 8px}
.class-meta code{background:var(--code-bg);padding:2px 6px;border-radius:5px}
.extends{font-size:13px;font-weight:400;color:var(--muted)}
.bucket{font-size:13px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted);margin:20px 0 6px;font-weight:700}

.sym-head{display:flex;align-items:baseline;flex-wrap:wrap;gap:8px;scroll-margin-top:60px;font-size:17px;margin:18px 0 4px}
.sym-name{font-family:ui-monospace,monospace;font-weight:600}
.anchor{opacity:0;margin-left:-18px;padding-right:6px;color:var(--muted);font-weight:400}
.sym-head:hover .anchor{opacity:1}
:target{background:var(--accent-soft);border-radius:6px}
.member{padding:2px 0}
.desc{margin:.3em 0}
.sig{margin:8px 0 4px}
.sig-code{background:var(--code-bg);border:1px solid var(--border);border-radius:6px;padding:9px 12px;margin:6px 0;overflow-x:auto;font-size:13px}
.sig-code code{white-space:pre}
ul.params{list-style:none;margin:6px 0;padding:0 0 0 4px}
ul.params>li{margin:3px 0;font-size:14px}
.pname{background:var(--code-bg);padding:1px 5px;border-radius:4px;font-weight:600}
.ptype{color:var(--accent);margin-left:6px}
.tref{color:inherit;text-decoration:none;border-bottom:1px dotted currentColor}
.tref:hover{text-decoration:none;border-bottom-style:solid}
.badge-type .tref{color:inherit}
.opt{color:var(--muted);font-size:12px;margin-left:6px;font-style:italic}
.cb{margin:3px 0 3px 14px;font-size:13px;color:var(--muted)}
.cb-label{font-size:11px;text-transform:uppercase;letter-spacing:.04em;background:var(--code-bg);border:1px solid var(--border);border-radius:4px;padding:1px 5px}
.returns,.throws{margin:6px 0;font-size:14px}
.r-label,.t-label{font-weight:600;color:var(--muted);margin-right:6px;font-size:12px;text-transform:uppercase;letter-spacing:.04em}
.throws ul{margin:4px 0;padding-left:18px}
.member-foot{margin:4px 0 2px}
.src{font-size:12px;color:var(--muted)}
.src-plain{cursor:help}

.badge{display:inline-block;font:600 11px/1.4 ui-monospace,monospace;border-radius:20px;padding:2px 8px;border:1px solid var(--border);color:var(--muted);background:var(--code-bg)}
.badge-mode{color:var(--add);border-color:#1a7f3733}
.badge-type{color:var(--accent);border-color:var(--accent)}
.badge-static{color:#8250df;border-color:#8250df55}
.badge-blockable{color:#bc4c00;border-color:#bc4c0055}

/* inherited members */
.inherited{margin:16px 0 4px}
.inh{margin:4px 0;font-size:14px;line-height:2}
.inh-k{font-size:12px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted);font-weight:700;margin-right:6px}
.inh code{background:var(--code-bg);padding:1px 5px;border-radius:4px}

/* data tables */
details.tbl{border:1px solid var(--border);border-radius:6px;margin:6px 0;padding:6px 12px}
details.tbl[id]{scroll-margin-top:60px}
details.tbl>summary{cursor:pointer;font-size:15px}
.tbl-i{color:var(--muted);font-family:ui-monospace,monospace;font-size:13px;margin-right:4px}
.tbl-n{color:var(--muted);font-size:12px;margin-left:8px}
ul.cols{list-style:none;margin:10px 0 2px;padding:0;display:flex;flex-wrap:wrap;gap:4px 16px}
ul.cols>li{display:flex;align-items:baseline;gap:7px;font-size:12px;min-width:190px}
.ci{color:var(--muted);font-family:ui-monospace,monospace;font-variant-numeric:tabular-nums;min-width:2.4em;text-align:right}
ul.cols code{background:var(--code-bg);padding:1px 5px;border-radius:4px}

/* changelog */
.cl-notes{margin:10px 0 26px}
.cl-notes h1,.cl-notes h2,.cl-notes h3{border:0;margin:18px 0 6px}
.cl-notes pre{background:var(--code-bg);border:1px solid var(--border);border-radius:6px;padding:10px;overflow-x:auto}
.cl-notes code{background:var(--code-bg);padding:1px 5px;border-radius:4px}
.cl-diff h3{margin:20px 0 6px}
.diff-group{margin:6px 0 18px}
.diff-item{padding:3px 0;font-size:14px}
.diff-item code{font-family:ui-monospace,monospace}
.diff-add{color:var(--add)}
.diff-del{color:var(--del)}
.diff-chg{color:var(--chg)}
.diff-sym{font-weight:600}
.diff-none{color:var(--muted);font-size:14px}

#menu{display:none;position:fixed;top:10px;left:10px;z-index:30;font-size:20px;background:var(--bg);border:1px solid var(--border);border-radius:6px;width:38px;height:38px;cursor:pointer;color:var(--fg)}
@media(max-width:900px){
  #menu{display:block}
  #sidebar{transform:translateX(-100%);transition:transform .2s;box-shadow:2px 0 12px #0003}
  body.nav-open #sidebar{transform:none}
  #content{margin-left:0;padding:0 18px 100px}
  .tabs{padding-left:48px}
}
"""


JS = r"""
(function(){
"use strict";
var CFG = JSON.parse(document.getElementById('cfg').textContent);
var INLINE = null, inlineEl = document.getElementById('inline-data');
if (inlineEl) { try { INLINE = JSON.parse(inlineEl.textContent); } catch(e){} }

var nav = document.getElementById('nav');
var search = document.getElementById('search');
var noRes = document.querySelector('.no-results');
var apiView = document.getElementById('api-view');
var clView = document.getElementById('changelog-view');
var statusEl = document.getElementById('status');
var verSel = document.getElementById('ver');
var verStatus = document.getElementById('verstatus');

var CATEGORY_LABELS = {RegisterCoreFunctions:'Core',RegisterGameFunctions:'Game',RegisterMenuFunctions:'Menu',RegisterHashFunctions:'Hashing'};
var CATEGORY_ORDER = ['RegisterCoreFunctions','RegisterGameFunctions','RegisterMenuFunctions','RegisterHashFunctions'];

var state = {releases:[], tag:null, data:null, prev:null, body:'', view:'api'};
var TYPE_LINKS = {};

// ---- helpers ----
function esc(s){ return s==null ? '' : String(s).replace(/[&<>"]/g, function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];}); }
function slug(){ var p=[]; for(var i=0;i<arguments.length;i++){ var a=arguments[i]; if(a!=null&&a!=='')p.push(a);} return p.join('.').replace(/[^A-Za-z0-9_.\-]/g,'-'); }
function hay(){ var p=[]; for(var i=0;i<arguments.length;i++){ if(arguments[i])p.push(arguments[i]);} return esc(p.join(' ').toLowerCase()); }
function linkType(t){
  if(!t) return '';
  var parts=String(t).split(/([A-Za-z_]\w*)/), out=[];
  for(var i=0;i<parts.length;i++){
    if(i%2===1 && TYPE_LINKS[parts[i]]) out.push('<a class="tref" href="#'+esc(TYPE_LINKS[parts[i]])+'">'+esc(parts[i])+'</a>');
    else out.push(esc(parts[i]));
  }
  return out.join('');
}
function badge(text,kind){ return '<span class="badge'+(kind?' badge-'+kind:'')+'">'+esc(text)+'</span>'; }
function typeBadge(t){ return t ? '<span class="badge badge-type">'+linkType(t)+'</span>' : ''; }
function sourceBase(){
  if(state.tag && CFG.repo) return 'https://github.com/'+CFG.repo+'/blob/'+state.tag+'/';
  return CFG.sourceBase || '';
}
function sourceLink(e){
  var f=e.file, line=e.line; if(!f) return '';
  var loc = line ? f+'#L'+line : f, base=sourceBase();
  if(base) return '<a class="src" href="'+esc(base.replace(/\/$/,'')+'/'+loc)+'" target="_blank" rel="noopener">source</a>';
  var label = line ? f+':'+line : f;
  return '<span class="src src-plain" title="'+esc(label)+'">'+esc(f.split('/').pop())+'</span>';
}
function head(level,anchor,titleHtml,badges){
  return '<h'+level+' id="'+esc(anchor)+'" class="sym-head">'+
    '<a class="anchor" href="#'+esc(anchor)+'" aria-label="permalink">#</a>'+titleHtml+(badges||'')+'</h'+level+'>';
}
function hasOwn(o,k){ return Object.prototype.hasOwnProperty.call(o,k); }
// Compact "Inherited from <Base>" sections, walking the extends chain. Members
// link to the base class's own anchors (no duplication of full docs).
function renderInherited(baseName, classes){
  var out=[], b=baseName, guard=0;
  while(b && classes[b] && guard++<20){
    var bc=classes[b];
    var props=(bc.properties||[]).map(function(p){ return '<a href="#'+esc(slug(b,p.name))+'"><code>'+esc(p.name)+'</code></a>'; });
    var meths=(bc.methods||[]).map(function(m){ return '<a href="#'+esc(slug(b,m.name))+'"><code>'+esc(m.name)+'()</code></a>'; });
    var stats=(bc.static_methods||[]).map(function(m){ return '<a href="#'+esc(slug(b,'static',m.name))+'"><code>'+esc(m.name)+'()</code></a>'; });
    out.push('<div class="inherited"><h3 class="bucket">Inherited from <a href="#'+esc(slug('class',b))+'">'+esc(b)+'</a></h3>'+
      (props.length?'<p class="inh"><span class="inh-k">Properties</span> '+props.join(', ')+'</p>':'')+
      (meths.length?'<p class="inh"><span class="inh-k">Methods</span> '+meths.join(', ')+'</p>':'')+
      (stats.length?'<p class="inh"><span class="inh-k">Static</span> '+stats.join(', ')+'</p>':'')+'</div>');
    b=bc.extends;
  }
  return out.join('');
}
function descHtml(doc){ return (doc&&doc.description) ? '<p class="desc">'+esc(doc.description)+'</p>' : ''; }
function cbType(cb){ var p=(cb.params||[]).map(function(a){return (a.name||'')+': '+(a.type||'');}).join(', '); return '('+esc(p)+')'; }

// ---- member renderers ----
function renderParams(params){
  if(!params||!params.length) return '';
  return '<ul class="params">'+params.map(function(p){
    var name=esc(p.name||''), typ=p.type||'';
    var opt=p.optional?'<span class="opt">optional</span>':'';
    var desc=esc(p.description||''), cb=p.callback, cbHtml='';
    if(cb){
      var ret=cb.returns, rh=ret?' &rarr; <code>'+linkType(ret)+'</code>':'';
      var cd=esc(cb.description||'');
      cbHtml='<div class="cb"><span class="cb-label">callback</span> <code>'+cbType(cb)+'</code>'+rh+(cd?' &mdash; '+cd:'')+'</div>';
    }
    var th=typ?'<code class="ptype">'+linkType(typ)+'</code>':'';
    return '<li><code class="pname">'+name+'</code>'+th+opt+(desc?' &mdash; '+desc:'')+cbHtml+'</li>';
  }).join('')+'</ul>';
}
function renderReturns(ret){
  if(!ret) return '';
  var typ=ret.type||'', desc=esc(ret.description||'');
  if(!typ&&!desc) return '';
  var th=typ?'<code>'+linkType(typ)+'</code>':'';
  return '<div class="returns"><span class="r-label">Returns</span> '+th+(desc?' &mdash; '+desc:'')+'</div>';
}
function renderThrows(doc){
  var t=doc&&doc.throws; if(!t||!t.length) return '';
  return '<div class="throws"><span class="t-label">Throws</span><ul>'+t.map(function(x){
    var ty=esc(x.type||'Error'), d=esc(x.description||''); return '<li><code>'+ty+'</code>'+(d?' &mdash; '+d:'')+'</li>';
  }).join('')+'</ul></div>';
}
function renderSignatures(doc){
  var sigs=(doc&&doc.signatures)||[]; if(!sigs.length) return '';
  return sigs.map(function(s){
    return '<div class="sig"><pre class="sig-code"><code>'+esc(s.signature||'')+'</code></pre>'+
      renderParams(s.params)+renderReturns(s.returns)+'</div>';
  }).join('');
}
function renderCallable(e,anchor,extraBadges){
  var doc=e.doc||{};
  var h=head(3,anchor,'<span class="sym-name">'+esc(e.name||'')+'</span>',(extraBadges||''));
  return '<div class="member" data-hay="'+hay(e.name,doc.description)+'">'+h+
    '<div class="member-body">'+descHtml(doc)+renderSignatures(doc)+renderThrows(doc)+
    '<div class="member-foot">'+sourceLink(e)+'</div></div></div>';
}
function renderProperty(e,anchor){
  var doc=e.doc||{};
  var badges=badge(e.readonly?'read-only':'read/write','mode');
  if(doc.type) badges+=typeBadge(doc.type);
  var body=descHtml(doc), cb=doc.callback;
  if(cb){
    var ret=cb.returns, rh=ret?' &rarr; <code>'+linkType(ret)+'</code>':'', cd=esc(cb.description||'');
    body+='<div class="cb"><span class="cb-label">callback</span> <code>'+cbType(cb)+'</code>'+rh+(cd?' &mdash; '+cd:'')+'</div>';
  }
  var h=head(3,anchor,'<span class="sym-name">'+esc(e.name||'')+'</span>',badges);
  return '<div class="member" data-hay="'+hay(e.name,doc.description)+'">'+h+
    '<div class="member-body">'+body+'<div class="member-foot">'+sourceLink(e)+'</div></div></div>';
}
function renderEvent(e,anchor){
  var badges=(e.blockable?badge('blockable','blockable'):'');
  var h=head(3,anchor,'<span class="sym-name">'+esc(e.name||'')+'</span>',badges);
  var body=(e.description?'<p class="desc">'+esc(e.description)+'</p>':'')+
    '<pre class="sig-code"><code>'+esc(e.signature||e.name)+'</code></pre>'+
    renderParams(e.params)+renderReturns(e.returns);
  return '<div class="member" data-hay="'+hay(e.name,e.description)+'">'+h+
    '<div class="member-body">'+body+'<div class="member-foot">'+sourceLink(e)+'</div></div></div>';
}
function renderConstLeaf(e,anchor){
  var doc=e.doc||{}, badges=(doc.type?typeBadge(doc.type):'');
  var h=head(3,anchor,'<span class="sym-name">'+esc(e.name||'')+'</span>',badges);
  return '<div class="member" data-hay="'+hay(e.name,doc.description)+'">'+h+
    '<div class="member-body">'+descHtml(doc)+'<div class="member-foot">'+sourceLink(e)+'</div></div></div>';
}

// ---- sidebar builders ----
function navLeaves(leaves){ return leaves.map(function(l){ return '<li class="leaf" data-hay="'+l[2]+'"><a href="#'+esc(l[0])+'">'+esc(l[1])+'</a></li>'; }).join(''); }
function navGroup(anchor,label,leaves){
  var n=' <span class="n">'+leaves.length+'</span>';
  return '<li class="group collapsed"><div class="grp-row"><button class="tw" aria-label="toggle"></button>'+
    '<a class="grp-label grp-link" href="#'+esc(anchor)+'">'+esc(label)+n+'</a></div><ul class="kids">'+navLeaves(leaves)+'</ul></li>';
}
function navSubgroup(anchor,label,leaves,childHtml){
  var n=leaves.length?' <span class="n">'+leaves.length+'</span>':'';
  return '<li class="group collapsed" data-hay="'+hay(label)+'"><div class="grp-row"><button class="tw" aria-label="toggle"></button>'+
    '<a class="grp-label grp-link" href="#'+esc(anchor)+'">'+esc(label)+n+'</a></div>'+
    '<ul class="kids">'+navLeaves(leaves)+(childHtml||'')+'</ul></li>';
}

// ---- whole-API render ----
function buildApi(data){
  TYPE_LINKS={};
  var classes=data.classes||{}, globals=data.global_functions||[], constants=data.constants||{}, me=data.me_properties||[], events=data.events||[];
  Object.keys(classes).forEach(function(n){ TYPE_LINKS[n]=slug('class',n); });
  Object.keys(constants).forEach(function(c){ if(constants[c]&&constants[c].properties) TYPE_LINKS[c]=slug('const',c); });

  var navHtml=[], body=[];
  var nM=0,nP=0,nS=0;
  Object.keys(classes).forEach(function(k){ nM+=classes[k].methods.length; nP+=classes[k].properties.length; nS+=classes[k].static_methods.length; });
  var stats=Object.keys(classes).length+' classes &middot; '+nM+' methods &middot; '+nP+' properties &middot; '+nS+' static &middot; '+globals.length+' global functions &middot; '+events.length+' events';
  body.push('<section class="area intro-area"><h1 id="top">'+esc(CFG.title)+'</h1>'+
    '<p class="lead">The JavaScript API exposed by d2bsng. '+stats+'.</p></section>');

  // globals by category
  var byCat={}; globals.forEach(function(g){ (byCat[g.category||'other']=byCat[g.category||'other']||[]).push(g); });
  var cats=CATEGORY_ORDER.filter(function(c){return byCat[c];}).concat(Object.keys(byCat).sort().filter(function(c){return CATEGORY_ORDER.indexOf(c)<0;}));
  body.push('<section class="area"><h2 id="globals" class="spy">Global functions</h2><p class="area-intro">Top-level functions callable from any script.</p>');
  var globSub=[];
  cats.forEach(function(cat){
    var label=CATEGORY_LABELS[cat]||cat, fns=byCat[cat].slice().sort(byName), ca=slug('globals',label);
    body.push('<h3 id="'+esc(ca)+'" class="cat-head">'+esc(label)+'</h3>');
    var leaves=[];
    fns.forEach(function(fn){ var a=slug('fn',fn.name); body.push(renderCallable(fn,a,'')); leaves.push([a,fn.name,hay(fn.name,(fn.doc||{}).description)]); });
    globSub.push(navSubgroup(ca,label,leaves));
  });
  body.push('</section>');
  navHtml.push('<li class="group collapsed"><div class="grp-row"><button class="tw"></button><a class="grp-label grp-link" href="#globals">Global functions <span class="n">'+globals.length+'</span></a></div><ul class="kids">'+globSub.join('')+'</ul></li>');

  // classes
  body.push('<section class="area"><h2 id="classes" class="spy">Classes</h2><p class="area-intro">Object types exposed to scripts. A class marked <em>not constructable</em> is only ever returned by the API.</p>');
  var clsSub=[];
  Object.keys(classes).sort(ci).forEach(function(name){
    var c=classes[name], canchor=slug('class',name), badges='';
    if(c.abstract) badges+=badge('abstract');
    if(c.constructable===false) badges+=badge('not constructable');
    var ext=c.extends?' <span class="extends">extends <a href="#'+esc(slug('class',c.extends))+'">'+esc(c.extends)+'</a></span>':'';
    body.push('<div class="class-block">');
    body.push(head(2,canchor,'<span class="sym-name">'+esc(name)+'</span>'+ext,badges).replace('class="sym-head"','class="sym-head class-head spy"'));
    body.push('<div class="class-meta"><code>'+esc(c.cpp_class||'')+'</code> '+sourceLink(c.constructor||{file:c.source})+'</div>');
    var leaves=[];
    if(hasOwn(c,'constructor')){ var a=slug(name,'constructor'); body.push('<h3 class="bucket">Constructor</h3>'); var ce=Object.assign({}, c.constructor, {name:'new '+name}); body.push(renderCallable(ce,a,'')); leaves.push([a,'constructor',hay('constructor new',name)]); }
    if(c.properties.length){ body.push('<h3 class="bucket">Properties</h3>'); c.properties.slice().sort(byName).forEach(function(p){ var a=slug(name,p.name); body.push(renderProperty(p,a)); leaves.push([a,p.name,hay(p.name,(p.doc||{}).description)]); }); }
    if(c.methods.length){ body.push('<h3 class="bucket">Methods</h3>'); c.methods.slice().sort(byName).forEach(function(m){ var a=slug(name,m.name); body.push(renderCallable(m,a,'')); leaves.push([a,m.name,hay(m.name,(m.doc||{}).description)]); }); }
    if(c.static_methods.length){ body.push('<h3 class="bucket">Static methods</h3>'); c.static_methods.slice().sort(byName).forEach(function(m){ var a=slug(name,'static',m.name); body.push(renderCallable(m,a,badge('static','static'))); leaves.push([a,name+'.'+m.name,hay(m.name,'static')]); }); }
    if(c.extends) body.push(renderInherited(c.extends, classes));
    body.push('</div>');
    clsSub.push(navSubgroup(canchor,name,leaves));
  });
  body.push('</section>');
  navHtml.push('<li class="group collapsed"><div class="grp-row"><button class="tw"></button><a class="grp-label grp-link" href="#classes">Classes <span class="n">'+Object.keys(classes).length+'</span></a></div><ul class="kids">'+clsSub.join('')+'</ul></li>');

  // me (the player Unit, plus session-level extras)
  var meExt=data.me_extends;
  var meIntro=(meExt&&classes[meExt])
    ? ' <code>me</code> is a <a href="#'+esc(slug('class',meExt))+'">'+esc(meExt)+'</a> (the player), with these additional properties:'
    : ' Properties of the global <code>me</code> unit.';
  body.push('<section class="area"><h2 id="me" class="spy">The <code>me</code> object</h2><p class="area-intro">The player character.'+meIntro+'</p>');
  var meLeaves=[];
  me.slice().sort(byName).forEach(function(p){ var a=slug('me',p.name); body.push(renderProperty(p,a)); meLeaves.push([a,p.name,hay(p.name,(p.doc||{}).description)]); });
  if(meExt) body.push(renderInherited(meExt, classes));
  body.push('</section>');
  navHtml.push(navGroup('me','me (player)',meLeaves));

  // constants
  body.push('<section class="area"><h2 id="constants" class="spy">Constants</h2>');
  var constLeaves=[];
  Object.keys(constants).forEach(function(cn){
    var cv=constants[cn];
    if(cv&&cv.properties){
      var a=slug('const',cn);
      body.push('<div class="class-block">');
      body.push(head(2,a,'<span class="sym-name">'+esc(cn)+'</span>',badge('namespace','type')).replace('class="sym-head"','class="sym-head class-head"'));
      body.push(descHtml(cv.doc));
      var sub=[];
      cv.properties.forEach(function(ch){ var ca=slug('const',cn,ch.name); body.push(renderConstLeaf(ch,ca)); sub.push([ca,cn+'.'+ch.name,hay(ch.name,cn)]); });
      body.push('</div>');
      constLeaves.push(['raw',navSubgroup(a,cn,sub)]);
    } else {
      var a2=slug('const',cn); body.push(renderConstLeaf(cv,a2)); constLeaves.push([a2,cn,hay(cn,(cv.doc||{}).description)]);
    }
  });
  body.push('</section>');
  // nav for constants (handle nested 'raw' entries)
  var clItems=constLeaves.map(function(l){ return l[0]==='raw' ? l[1] : '<li class="leaf" data-hay="'+l[2]+'"><a href="#'+esc(l[0])+'">'+esc(l[1])+'</a></li>'; }).join('');
  navHtml.push('<li class="group collapsed"><div class="grp-row"><button class="tw" aria-label="toggle"></button><a class="grp-label grp-link" href="#constants">Constants <span class="n">'+Object.keys(constants).length+'</span></a></div><ul class="kids">'+clItems+'</ul></li>');

  // events
  body.push('<section class="area"><h2 id="events" class="spy">Events</h2><p class="area-intro">Names for <code>addEventListener(name, fn)</code>. A <em>blockable</em> event may return a value to suppress the default game handling.</p>');
  var evLeaves=[];
  events.slice().sort(byName).forEach(function(e){ var a=slug('event',e.name); body.push(renderEvent(e,a)); evLeaves.push([a,e.name,hay(e.name,e.description)]); });
  body.push('</section>');
  navHtml.push(navGroup('events','Events',evLeaves));

  // data tables (Diablo II .txt schema behind getBaseStat)
  var tables=data.tables||[];
  if(tables.length){
    body.push('<section class="area"><h2 id="tables" class="spy">Data tables</h2>'+
      '<p class="area-intro">Diablo II <code>.txt</code> table and column names behind <code>getBaseStat</code>, '+
      'with their query indexes (tables and columns are both 0-based - <code>getBaseStat</code> takes a table '+
      'name or index, then a column index). '+tables.length+' tables. Click a table to see its columns.</p>');
    var tblLeaves=[];
    tables.forEach(function(t){
      var tid=slug('table',t.name), cols=t.columns||[];
      var colHtml=cols.map(function(c,j){ return '<li><span class="ci">'+j+'</span><code>'+esc(c)+'</code></li>'; }).join('');
      body.push('<details class="tbl" id="'+esc(tid)+'"><summary><span class="tbl-i">['+t.index+']</span> '+
        '<code class="sym-name">'+esc(t.name)+'</code> <span class="tbl-n">'+cols.length+' columns</span></summary>'+
        (colHtml?'<ul class="cols">'+colHtml+'</ul>':'<p class="diff-none">no columns</p>')+'</details>');
      tblLeaves.push([tid,t.name,hay(t.name,cols.join(' '))]);
    });
    body.push('</section>');
    navHtml.push(navGroup('tables','Data tables',tblLeaves));
  }

  nav.innerHTML=navHtml.join('');
  apiView.innerHTML=body.join('');
  initInteractions();
}
function byName(a,b){ return a.name.toLowerCase()<b.name.toLowerCase()?-1:1; }
function ci(a,b){ return a.toLowerCase()<b.toLowerCase()?-1:1; }

// ---- API diff (changelog) ----
function flatten(data){
  // key -> {label, fp} ; fp = fingerprint to detect "changed"
  var out={};
  if(!data) return out;
  function sigFp(doc){ return JSON.stringify((doc&&doc.signatures)||[]); }
  var cs=data.classes||{};
  Object.keys(cs).forEach(function(n){
    var c=cs[n]; out['class '+n]={label:'class '+n, fp:(c.extends||'')+'|'+(c.constructable!==false)};
    c.methods.forEach(function(m){ out[n+'.'+m.name+'()']={label:n+'.'+m.name+'()', fp:sigFp(m.doc)}; });
    c.static_methods.forEach(function(m){ out[n+'.'+m.name+'() [static]']={label:n+'.'+m.name+'() [static]', fp:sigFp(m.doc)}; });
    c.properties.forEach(function(p){ out[n+'.'+p.name]={label:n+'.'+p.name, fp:((p.doc||{}).type||'')+'|'+!!p.readonly}; });
    if(hasOwn(c,'constructor')) out['new '+n+'()']={label:'new '+n+'()', fp:sigFp(c.constructor.doc)};
  });
  (data.global_functions||[]).forEach(function(g){ out[g.name+'()']={label:g.name+'()', fp:sigFp(g.doc)}; });
  (data.me_properties||[]).forEach(function(p){ out['me.'+p.name]={label:'me.'+p.name, fp:((p.doc||{}).type||'')+'|'+!!p.readonly}; });
  Object.keys(data.constants||{}).forEach(function(cn){
    var cv=data.constants[cn];
    if(cv&&cv.properties) cv.properties.forEach(function(ch){ out[cn+'.'+ch.name]={label:cn+'.'+ch.name, fp:(ch.doc||{}).type||''}; });
    else out[cn]={label:cn, fp:(cv.doc||{}).type||''};
  });
  (data.events||[]).forEach(function(e){ out['event '+e.name]={label:'event '+e.name, fp:(e.signature||'')+'|'+!!e.blockable}; });
  return out;
}
function diffData(curr,prev){
  var a=flatten(prev), b=flatten(curr), added=[],removed=[],changed=[];
  Object.keys(b).forEach(function(k){ if(!(k in a)) added.push(b[k].label); else if(a[k].fp!==b[k].fp) changed.push(b[k].label); });
  Object.keys(a).forEach(function(k){ if(!(k in b)) removed.push(a[k].label); });
  added.sort(); removed.sort(); changed.sort();
  return {added:added, removed:removed, changed:changed};
}
function diffGroup(title,cls,items){
  if(!items.length) return '';
  return '<div class="diff-group"><h3 class="'+cls+'">'+esc(title)+' ('+items.length+')</h3>'+
    items.map(function(s){ return '<div class="diff-item '+cls+'"><span class="diff-sym">'+esc(s)+'</span></div>'; }).join('')+'</div>';
}

// minimal markdown (headings, lists, code, links, bold, inline code)
function md(src){
  if(!src) return '<p class="diff-none">No release notes.</p>';
  var lines=String(src).replace(/\r\n/g,'\n').split('\n'), out=[], inList=false, inCode=false, code=[];
  function inline(t){
    t=esc(t);
    t=t.replace(/`([^`]+)`/g,'<code>$1</code>');
    t=t.replace(/\*\*([^*]+)\*\*/g,'<strong>$1</strong>');
    t=t.replace(/\[([^\]]+)\]\((https?:\/\/[^)\s]+)\)/g,'<a href="$2" target="_blank" rel="noopener">$1</a>');
    t=t.replace(/(^|[\s(])(https?:\/\/[^\s)]+)/g,'$1<a href="$2" target="_blank" rel="noopener">$2</a>');
    return t;
  }
  for(var i=0;i<lines.length;i++){
    var ln=lines[i];
    if(/^```/.test(ln)){ if(inCode){ out.push('<pre><code>'+esc(code.join('\n'))+'</code></pre>'); code=[]; inCode=false; } else inCode=true; continue; }
    if(inCode){ code.push(ln); continue; }
    var h=ln.match(/^(#{1,4})\s+(.*)$/);
    var li=ln.match(/^\s*[-*]\s+(.*)$/);
    if(li){ if(!inList){ out.push('<ul>'); inList=true; } out.push('<li>'+inline(li[1])+'</li>'); continue; }
    if(inList){ out.push('</ul>'); inList=false; }
    if(h){ var lvl=Math.min(h[1].length+1,4); out.push('<h'+lvl+'>'+inline(h[2])+'</h'+lvl+'>'); continue; }
    if(ln.trim()==='') continue;
    out.push('<p>'+inline(ln)+'</p>');
  }
  if(inList) out.push('</ul>');
  if(inCode) out.push('<pre><code>'+esc(code.join('\n'))+'</code></pre>');
  return out.join('\n');
}

function buildChangelog(){
  var prevLabel = state.prevTag ? ' vs '+esc(state.prevTag) : '';
  var notes='<section class="area cl-notes"><h2>Release notes'+(state.tag?' &middot; '+esc(state.tag):'')+'</h2>'+md(state.body)+'</section>';
  var diffHtml;
  if(!state.prev){
    diffHtml='<p class="diff-none">No previous version to compare - this is the earliest version with API data.</p>';
  } else {
    var d=diffData(state.data, state.prev);
    if(!d.added.length&&!d.removed.length&&!d.changed.length) diffHtml='<p class="diff-none">No API changes from '+esc(state.prevTag)+'.</p>';
    else diffHtml=diffGroup('Added','diff-add',d.added)+diffGroup('Removed','diff-del',d.removed)+diffGroup('Changed','diff-chg',d.changed);
  }
  clView.innerHTML=notes+'<section class="area cl-diff"><h2>API changes'+prevLabel+'</h2>'+diffHtml+'</section>';
}

// ---- interactions (re-bound after each API render) ----
function initInteractions(){
  var current=null;
  // Clicking anywhere on a group row (twisty, label, or its section link)
  // toggles that group; a label that is a section link also navigates (the
  // anchor default fires too). Leaf links just navigate. Nothing auto-expands,
  // so a manual collapse stays collapsed.
  nav.onclick=function(e){
    var row=e.target.closest('.grp-row');
    if(row) row.parentElement.classList.toggle('collapsed');
    var a=e.target.closest('a');
    if(a){ document.body.classList.remove('nav-open');
      var tgt=document.getElementById((a.getAttribute('href')||'').slice(1));
      if(tgt && tgt.tagName==='DETAILS') tgt.open=true;  // reveal a data-table's columns
    }
  };
  filter();
  // scrollspy: highlight the section in view; never changes collapse state
  var spies=apiView.querySelectorAll('.spy[id]'), linkFor={};
  nav.querySelectorAll('a[href^="#"]').forEach(function(a){ linkFor[a.getAttribute('href').slice(1)]=a; });
  if(window._io) window._io.disconnect();
  window._io=new IntersectionObserver(function(ents){
    ents.forEach(function(en){ if(en.isIntersecting){ var a=linkFor[en.target.id];
      if(a&&a!==current){ if(current)current.classList.remove('active'); a.classList.add('active'); current=a; } } });
  },{rootMargin:'0px 0px -75% 0px',threshold:0});
  spies.forEach(function(s){ window._io.observe(s); });
}
function norm(s){ return (s||'').toLowerCase().replace(/\s+/g,' ').trim(); }
function filter(){
  var q=norm(search.value), any=false;
  nav.querySelectorAll('.leaf').forEach(function(li){ var ok=!q||(li.dataset.hay||'').indexOf(q)>=0; li.hidden=!ok; if(ok)any=true; });
  Array.prototype.slice.call(nav.querySelectorAll('.group')).reverse().forEach(function(g){
    var selfHit=q&&(g.dataset.hay||'').indexOf(q)>=0;
    var vis=g.querySelector('.leaf:not([hidden])')||g.querySelector('.group:not([hidden])');
    g.hidden=!(!q||!!vis||selfHit);
    if(q){ if(vis||selfHit) g.classList.remove('collapsed'); } else g.classList.add('collapsed');
  });
  noRes.hidden=!(q&&!any);
}
search.addEventListener('input',filter);
search.addEventListener('keydown',function(e){ if(e.key==='Escape'){ search.value=''; filter(); search.blur(); } });
document.addEventListener('keydown',function(e){
  if(e.key==='/'&&document.activeElement!==search&&!/^(INPUT|TEXTAREA)$/.test(document.activeElement.tagName)){ e.preventDefault(); search.focus(); search.select(); }
});
var menu=document.getElementById('menu');
if(menu) menu.addEventListener('click',function(){ document.body.classList.toggle('nav-open'); });

// tabs
document.getElementById('tabs').addEventListener('click',function(e){
  var t=e.target.closest('.tab'); if(!t) return;
  setView(t.dataset.view);
});
function setView(v){
  state.view=v;
  document.querySelectorAll('.tab').forEach(function(t){ t.classList.toggle('active',t.dataset.view===v); });
  apiView.hidden = v!=='api';
  clView.hidden = v!=='changelog';
  if(v==='changelog') buildChangelog();
}

// ---- data loading (bundle mode: same-origin versions.json + data/<tag>.json) ----
function fetchJson(url){ return fetch(url,{cache:'no-cache'}).then(function(r){ if(!r.ok) throw new Error('fetch '+r.status); return r.json(); }); }
function semverKey(tag){ var m=String(tag).replace(/^v/,'').split('.').map(Number); return (m[0]||0)*1e6+(m[1]||0)*1e3+(m[2]||0); }
function dataUrl(tag){ return 'data/'+encodeURIComponent(tag)+'.json'; }

function setStatus(msg,isErr){ statusEl.textContent=msg||''; statusEl.className='status'+(isErr?' err':''); }

function renderInline(){
  if(!INLINE){ setStatus('No API data available.',true); return; }
  state.tag=CFG.version?('v'+CFG.version):null; state.data=INLINE; state.prev=null; state.prevTag=null; state.body='';
  buildApi(INLINE); setStatus('');
  verSel.innerHTML='<option>'+esc(CFG.version||'local')+'</option>'; verSel.disabled=true;
}

// versions.json: {versions:[{tag,name,date,body}, ...]}
function loadVersions(){
  setStatus('Loading versions...');
  return fetchJson('versions.json').then(function(manifest){
    var vers=(manifest&&manifest.versions)||[];
    vers.sort(function(a,b){ return semverKey(b.tag)-semverKey(a.tag); });
    state.releases=vers;
    if(!vers.length){ verStatus.textContent='no versions'; renderInline(); return; }
    verSel.innerHTML=vers.map(function(v,i){ return '<option value="'+esc(v.tag)+'">'+esc(v.name||v.tag)+(i===0?' (latest)':'')+'</option>'; }).join('');
    verSel.disabled=false; verStatus.textContent='';
    return selectVersion(vers[0].tag);
  });
}
function selectVersion(tag){
  var idx=state.releases.findIndex(function(v){ return v.tag===tag; });
  if(idx<0) return;
  var rel=state.releases[idx], prevRel=state.releases[idx+1]||null;
  setStatus('Loading '+tag+'...');
  state.tag=tag; state.body=rel.body||'';
  return fetchJson(dataUrl(tag)).then(function(data){
    state.data=data;
    buildApi(data); setStatus(''); setView(state.view);
    state.prev=null; state.prevTag=prevRel?prevRel.tag:null;
    if(prevRel){ fetchJson(dataUrl(prevRel.tag)).then(function(pd){ state.prev=pd; if(state.view==='changelog') buildChangelog(); }).catch(function(){}); }
  }).catch(function(err){ setStatus('Could not load '+tag+': '+err.message+'.',true); });
}
verSel.addEventListener('change',function(){ if(!verSel.disabled) selectVersion(verSel.value); });

// ---- boot ----
if(INLINE) renderInline();          // instant paint from the baked-in dataset
loadVersions().catch(function(){    // bundle mode; fall back to the inline dataset offline
  if(INLINE){ verStatus.textContent=CFG.version||'local'; }
  else setStatus('No data: provide versions.json + data/, or build with an api.json.',true);
});
})();
"""


if __name__ == "__main__":
    main()
