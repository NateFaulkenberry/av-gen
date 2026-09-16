#!/usr/bin/env python3
"""Render a Quality Lab metrics.json as a self-contained HTML page.

Standard library only, and that is a constraint rather than a preference: every one of the thirty
Python tools in `tools/` is stdlib-only, `image_stats.py` hand-rolls a PNG decoder out of `zlib` and
`struct` specifically to avoid a dependency, and this machine has a system Python with no venv and
no numpy. No framework, no CDN, no web fonts -- the page has to open from a file:// URL on a machine
with no network, because that is where a render sits.

**The page is organised around three refusals**, all of them from ADR-250 and all of them visible in
the layout rather than only in the prose:

1. There is no overall score and no place the eye can mistake for one. The dimensions are shown
   SEPARATELY, in their own blocks, because a reader who is given a headline number will read the
   headline number.
2. `temporalAlternation` and `spatialLaplacian` are rendered in the same block, always, and the
   block says why. ADR-243 is what that rule cost.
3. Measured fact and inferred cause are different sections with different styling. A hypothesis says
   it is a hypothesis, in the page, next to its confidence.

Usage:
    python3 tools/quality-lab/report.py <run-dir> [-o report.html]
"""

import argparse
import html
import json
import os
import sys

# The pairing ADR-250 §3 makes a harness rule. The generator enforces it too, because a report that
# is refused by the C++ writer and then hand-assembled here would route around the rule.
PAIRED = ("temporal.temporalAlternation", "detail.spatialLaplacian")

GROUPS = [
    ("Spatial fidelity", "spatial.", "How much information this configuration lost relative to a "
     "better-sampled render of the same model. Not how good the image is: the reference converges "
     "the sampling error and nothing else, so where the model is wrong the reference is wrong in "
     "exactly the same way, with more samples."),
    ("Detail and aliasing", "detail.", "High-frequency spatial energy. A ratio ABOVE 1 means the "
     "candidate carries more of it than the better-sampled reference -- which is aliasing or "
     "sharpening, and is not detail. This measure cannot tell those apart and is meaningful only "
     "between arms of one view."),
    ("Temporal", "temporal.", "Reported together and never apart. `temporalAlternation` is the "
     "second difference of luma in time; authored smooth motion makes it large, and a correct "
     "measurement of it once ranked two anti-aliasing remedies backwards against a human reviewer "
     "(ADR-243). `motionCompensatedResidual` asks the engine's own velocity buffer where each pixel "
     "came from before differencing, so it is near zero on authored motion however fast. "
     "`disocclusionFraction` is a validity gate, not a quality number."),
    ("Per-class (AOV-gated)", "perClass.", "One residual under four masks, not four detectors. Each "
     "row's coverage says what share of the frame it describes; a residual over half a percent of "
     "the frame is a statement about half a percent of the frame."),
    ("Colour", "color.", "CIEDE2000 is defined for surface colour under a reference illuminant and "
     "is used here on tone-mapped emissive content, so it is comparable between arms and against no "
     "published threshold."),
    ("Banding", "banding.", "Two instruments with non-overlapping blind spots. CAMBI answers for "
     "single-code banding; the native quantisationSteps proxy answers for coarse posterisation and "
     "scores dither as banding."),
    ("Delivery (compression resilience only)", "delivery.", "VMAF is trained on compression and "
     "scaling of camera-captured video. It is in domain for 'did the encode survive' and out of "
     "domain for 'is this renderer configuration better'. It MAY NOT be used to rank a renderer "
     "configuration."),
]

CSS = """
:root { color-scheme: light dark;
  --bg:#fbfbfa; --fg:#16181d; --muted:#5a6270; --rule:#dfe1e6; --card:#ffffff;
  --warn:#8a5a00; --warnbg:#fff6e5; --hyp:#4a3a7a; --hypbg:#f2effc; }
@media (prefers-color-scheme: dark) { :root {
  --bg:#14161a; --fg:#e8eaee; --muted:#9aa3b2; --rule:#2a2e36; --card:#1b1e24;
  --warn:#e0b25c; --warnbg:#2a2415; --hyp:#c3b6ef; --hypbg:#221e33; } }
* { box-sizing:border-box; }
body { margin:0; background:var(--bg); color:var(--fg); font:14px/1.55 -apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif; }
.wrap { max-width:1100px; margin:0 auto; padding:32px 20px 80px; }
h1 { font-size:22px; margin:0 0 4px; letter-spacing:-0.01em; }
h2 { font-size:15px; margin:34px 0 6px; letter-spacing:0.02em; text-transform:uppercase; color:var(--muted); }
h3 { font-size:14px; margin:0 0 6px; }
.sub { color:var(--muted); margin:0 0 22px; }
.note { color:var(--muted); margin:0 0 12px; max-width:78ch; }
.card { background:var(--card); border:1px solid var(--rule); border-radius:8px; padding:14px 16px; margin:0 0 14px; }
table { border-collapse:collapse; width:100%; font-variant-numeric:tabular-nums; }
th,td { text-align:right; padding:5px 8px; border-bottom:1px solid var(--rule); }
th:first-child, td:first-child { text-align:left; font-family:ui-monospace,SFMono-Regular,Menlo,monospace; font-size:12.5px; }
th { color:var(--muted); font-weight:600; font-size:11.5px; text-transform:uppercase; letter-spacing:0.04em; }
tr:last-child td { border-bottom:none; }
.unavail td { color:var(--muted); font-style:italic; }
.lim { color:var(--muted); font-size:12.5px; padding:0 8px 8px 8px; text-align:left; }
.lim li { margin:2px 0; }
.kv { display:grid; grid-template-columns:200px 1fr; gap:2px 14px; font-size:13px; }
.kv dt { color:var(--muted); }
.kv dd { margin:0; font-family:ui-monospace,SFMono-Regular,Menlo,monospace; font-size:12.5px; word-break:break-all; }
.warnbox { background:var(--warnbg); border:1px solid var(--warn); color:var(--warn); border-radius:8px; padding:12px 16px; margin:0 0 16px; }
.hypbox { background:var(--hypbg); border:1px solid var(--hyp); color:var(--hyp); border-radius:8px; padding:12px 16px; margin:0 0 10px; }
.tag { display:inline-block; font-size:10.5px; letter-spacing:0.08em; text-transform:uppercase; border:1px solid currentColor; border-radius:999px; padding:1px 7px; margin-right:8px; }
.shots { display:grid; grid-template-columns:repeat(auto-fill,minmax(310px,1fr)); gap:14px; }
.shots figure { margin:0; }
.shots img { width:100%; border:1px solid var(--rule); border-radius:6px; display:block; background:#000; }
.shots figcaption { color:var(--muted); font-size:12px; margin-top:5px; font-family:ui-monospace,Menlo,monospace; }
"""


def esc(value):
    return html.escape(str(value))


def fmt(value):
    if value is None:
        return "&mdash;"
    if isinstance(value, float):
        if abs(value) >= 1000 or (value != 0 and abs(value) < 0.001):
            return f"{value:.4g}"
        return f"{value:.4f}"
    return esc(value)


def metric_rows(metrics, prefix):
    rows = []
    for metric in metrics:
        if not metric["name"].startswith(prefix):
            continue
        name = esc(metric["name"][len(prefix):])
        if not metric.get("available", True):
            rows.append(
                f'<tr class="unavail"><td>{name}</td>'
                f'<td colspan="5">unavailable &mdash; {esc(metric.get("reason", ""))}</td></tr>')
            continue
        pooled = metric.get("pooled")
        value = metric.get("value")
        note = metric.get("valueNote")
        shown = f"&infin;" if note == "infinite" else fmt(value)
        if pooled:
            rows.append(
                f"<tr><td>{name}</td><td>{shown}</td><td>{fmt(pooled['p5'])}</td>"
                f"<td>{fmt(pooled['p95'])}</td><td>{fmt(pooled['min'])} / {fmt(pooled['max'])}</td>"
                f"<td>{pooled['worstFrameIndex']}</td></tr>")
        else:
            rows.append(f"<tr><td>{name}</td><td>{shown}</td>"
                        f'<td colspan="4">{esc(metric.get("pooling", ""))}</td></tr>')
        if metric.get("limitations"):
            items = "".join(f"<li>{esc(text)}</li>" for text in metric["limitations"])
            rows.append(f'<tr><td colspan="6" class="lim"><ul>{items}</ul></td></tr>')
    return rows


def build(report, run_dir):
    run = report["run"]
    renderer = report["renderer"]
    metrics = report["metrics"]
    names = {m["name"] for m in metrics if m.get("available", True)}
    parts = [f"<style>{CSS}</style>", '<div class="wrap">']
    parts.append(f'<h1>Quality Lab &mdash; {esc(run["scene"] or "unnamed scene")}</h1>')
    parts.append(
        f'<p class="sub">{esc(run["timestamp"])} &middot; commit '
        f'{esc((run["gitCommit"] or "unknown")[:12])} &middot; {run["framesAnalysed"]} frames, '
        f'stride {run["frameStride"]} &middot; schema {esc(report["schemaVersion"])}</p>')

    if PAIRED[0] in names and PAIRED[1] not in names:
        parts.append(
            '<div class="warnbox"><strong>This report is malformed.</strong> '
            'temporal.temporalAlternation appears without detail.spatialLaplacian beside it. '
            'ADR-243: a temporal measure reported alone ranked two anti-aliasing remedies backwards '
            'against a human reviewer, on both arms.</div>')

    if run["candidateSequenceHash"] and run["candidateSequenceHash"] == run["referenceSequenceHash"]:
        parts.append(
            '<div class="warnbox"><strong>The candidate and the reference are the same render.</strong> '
            'Their sequence hashes are identical, so every full-reference number below is an identity '
            'check and not a comparison. An arm that cannot differ is void rather than measured '
            '(ADR-182).</div>')

    parts.append('<h2>Run</h2><div class="card"><dl class="kv">')
    for label, value in [
            ("target profile", run["targetProfile"] or "(none declared)"),
            ("configuration", renderer["configuration"] or "(not recorded)"),
            ("candidate", run["candidateDirectory"]),
            ("reference", run["referenceDirectory"]),
            ("candidate sequence hash", run["candidateSequenceHash"] or "(not supplied)"),
            ("reference sequence hash", run["referenceSequenceHash"] or "(not supplied)"),
            ("contention witness", renderer["contentionWitness"] or "(none)")]:
        parts.append(f"<dt>{esc(label)}</dt><dd>{esc(value)}</dd>")
    parts.append("</dl></div>")

    for title, prefix, blurb in GROUPS:
        rows = metric_rows(metrics, prefix)
        if not rows:
            continue
        parts.append(f"<h2>{esc(title)}</h2>")
        parts.append(f'<p class="note">{esc(blurb)}</p>')
        parts.append('<div class="card"><table><thead><tr><th>metric</th><th>mean</th>'
                     "<th>p5</th><th>p95</th><th>min / max</th><th>worst frame</th>"
                     "</tr></thead><tbody>")
        parts.extend(rows)
        parts.append("</tbody></table></div>")

    if report["findings"]:
        parts.append("<h2>Findings</h2>")
        parts.append('<p class="note">Measured fact. A number and a region; no cause and no '
                     "recommendation.</p>")
        for finding in report["findings"]:
            frame = finding.get("frameIndex")
            suffix = f" (frame {frame})" if frame is not None else ""
            parts.append(
                f'<div class="card"><span class="tag">measurement</span>'
                f'<code>{esc(finding["metric"])}</code><br>{esc(finding["statement"])}{suffix}</div>')

    parts.append("<h2>Hypotheses</h2>")
    if report["hypotheses"]:
        parts.append('<p class="note">Inferred cause, labelled as inference. Nothing promotes one of '
                     "these to a finding.</p>")
        for hypothesis in report["hypotheses"]:
            evidence = "".join(f"<li>{esc(e)}</li>" for e in hypothesis["evidence"])
            parts.append(
                f'<div class="hypbox"><span class="tag">hypothesis</span>'
                f'confidence {hypothesis["confidence"]:.2f}<br>{esc(hypothesis["statement"])}'
                f"<ul>{evidence}</ul></div>")
    else:
        parts.append('<p class="note">None recorded. A run with no hypothesis is a run that measured '
                     "something and did not guess why, which is the normal case.</p>")

    if report["diagnostics"]:
        parts.append("<h2>Diagnostics</h2>")
        parts.append('<p class="note">Not presentation. A post chain judged on its final frame '
                     "cannot be debugged, and neither can a quality vector judged on its pooled "
                     "numbers. Every heatmap is absolute-scaled: one normalised to its own maximum "
                     "looks identical for a catastrophic frame and a clean one.</p>")
        parts.append('<div class="shots">')
        for path in report["diagnostics"]:
            parts.append(f'<figure><img src="{esc(path)}" alt="{esc(path)}" loading="lazy">'
                         f"<figcaption>{esc(os.path.basename(path))}</figcaption></figure>")
        parts.append("</div>")

    parts.append("<h2>Limitations</h2>")
    parts.append('<p class="note">Every one of these travels with the numbers above. A number '
                 "without its caveat becomes a false authority the moment it is read by someone who "
                 "was not there.</p>")
    parts.append('<div class="card"><ul>')
    for limitation in report["limitations"]:
        parts.append(f"<li>{esc(limitation)}</li>")
    unavailable = [m for m in metrics if not m.get("available", True)]
    for metric in unavailable:
        parts.append(f'<li><code>{esc(metric["name"])}</code> &mdash; {esc(metric.get("reason",""))}</li>')
    parts.append("</ul></div>")
    parts.append("</div>")
    return "\n".join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", help="a run directory containing metrics.json, or the file itself")
    parser.add_argument("-o", "--out", default=None, help="output HTML (default: <run>/report.html)")
    args = parser.parse_args()

    path = args.run
    run_dir = path
    if os.path.isdir(path):
        path = os.path.join(path, "metrics.json")
    else:
        run_dir = os.path.dirname(path) or "."
    if not os.path.exists(path):
        sys.stderr.write(f"no metrics.json at {path}\n")
        return 1
    with open(path, "r", encoding="utf-8") as handle:
        report = json.load(handle)

    out = args.out or os.path.join(run_dir, "report.html")
    page = ("<!doctype html><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'>"
            f"<title>Quality Lab &mdash; {html.escape(report['run']['scene'] or 'run')}</title>"
            + build(report, run_dir))
    with open(out, "w", encoding="utf-8") as handle:
        handle.write(page)
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
