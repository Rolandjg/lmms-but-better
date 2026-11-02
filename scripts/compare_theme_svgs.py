#!/usr/bin/env python3
"""Rasterize SVG candidates at PNG dimensions and audit appearance/transparency.

Requires Pillow, numpy and the cairosvg command (or --renderer inkscape).
The source images are never changed. Results are heuristic similarity gates,
not proof that an SVG scales well; inspect the native and enlarged previews too.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import html
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image, ImageDraw

try:
    import cairosvg
except ImportError:
    cairosvg = None


def render_svg(svg: Path, width: int, height: int, renderer: str) -> Image.Image:
    """Set the viewport to the PNG's exact native size, respecting aspect ratio."""
    if renderer == "cairosvg" and cairosvg is not None:
        raw = cairosvg.svg2png(bytestring=svg.read_bytes(), url=str(svg),
                              output_width=width, output_height=height)
        image = Image.open(io.BytesIO(raw)).convert("RGBA")
        if image.size != (width, height):
            raise ValueError(f"Renderer returned {image.size}, expected {(width, height)}")
        return image
    if renderer == "cairosvg":
        command = ["cairosvg", str(svg), "-s", "1", "--output-width", str(width),
                   "--output-height", str(height), "-f", "png"]
    else:
        command = ["inkscape", str(svg), "--export-type=png", "--export-area-page",
                   f"--export-width={width}", f"--export-height={height}", "--export-filename=-"]
    result = subprocess.run(command, capture_output=True, timeout=60)
    if result.returncode:
        raise ValueError(result.stderr.decode(errors="replace")[:600])
    image = Image.open(io.BytesIO(result.stdout)).convert("RGBA")
    if image.size != (width, height):
        raise ValueError(f"Renderer returned {image.size}, expected {(width, height)}")
    return image


def vector_issues(svg: Path) -> list[str]:
    """Reject embedded/linked rasters and content that depends on outside files."""
    raw = svg.read_text()
    if "<!ENTITY" in raw or "<!DOCTYPE" in raw:
        raise ValueError("DTD/entities are unsupported")
    root = ET.fromstring(raw)
    if root.tag.split("}")[-1] != "svg":
        raise ValueError("Root element must be svg")
    issues = []
    if not root.get("viewBox"):
        issues.append("missing viewBox")
    for element in root.iter():
        tag = element.tag.split("}")[-1]
        if tag in {"image", "foreignObject", "script"}:
            issues.append(f"non-vector element: {tag}")
        if tag == "text":
            issues.append("font-dependent text (outline it)")
        for name, value in element.attrib.items():
            if name.split("}")[-1] == "href" and value and not value.startswith("#"):
                issues.append("external/embedded href")
            if "url(" in value and ("url(#" not in value or "http" in value or "data:" in value):
                issues.append("external CSS reference")
    return sorted(set(issues))


def compare_images(source: Image.Image, candidate: Image.Image, pixel_threshold=32 / 255) -> dict:
    a = np.asarray(source.convert("RGBA"), dtype=np.float64) / 255
    b = np.asarray(candidate.convert("RGBA"), dtype=np.float64) / 255
    if a.shape != b.shape:
        raise ValueError("Comparison requires identical dimensions")
    aa, ba = a[..., 3:4], b[..., 3:4]
    premul_a, premul_b = a[..., :3] * aa, b[..., :3] * ba
    black = np.abs(premul_a - premul_b)
    white = np.abs((premul_a + 1 - aa) - (premul_b + 1 - ba))
    # The union prevents transparent margins from hiding missing/excess content.
    support = np.maximum(aa[..., 0], ba[..., 0]) > 0.02
    if not support.any():
        support = np.ones(a.shape[:2], dtype=bool)
    worst = np.maximum(black, white)
    visible = worst[support]
    corners = ((0, 0), (0, -1), (-1, 0), (-1, -1))
    opaque_background_added = all(
        aa[y, x, 0] == 0 and ba[y, x, 0] > 0.8 for y, x in corners)
    return {
        "mae": float(visible.mean()),
        "rmse": float(np.sqrt(np.mean(visible ** 2))),
        "alpha_mae": float(np.abs(aa[..., 0] - ba[..., 0])[support].mean()),
        "changed_fraction": float(np.mean(np.max(visible, axis=-1) > pixel_threshold)),
        "full_canvas_mae": float(worst.mean()),
        "black_mae": float(black[support].mean()),
        "white_mae": float(white[support].mean()),
        "max_error": float(worst.max()),
        "support_pixels": int(support.sum()),
        "opaque_background_added": bool(opaque_background_added),
    }


def difference(source: Image.Image, candidate: Image.Image) -> Image.Image:
    a = np.asarray(source, dtype=float) / 255
    b = np.asarray(candidate, dtype=float) / 255
    aa, ba = a[..., 3:4], b[..., 3:4]
    black = np.abs(a[..., :3] * aa - b[..., :3] * ba)
    white = np.abs(a[..., :3] * aa + 1 - aa - b[..., :3] * ba - 1 + ba)
    return Image.fromarray(np.uint8(np.clip(np.maximum(black, white) * 4 * 255, 0, 255)))


def preview(source, native, large, diff, label, enlargement=4):
    """Native pair plus enlarged PNG/SVG pair on dark and light backgrounds."""
    w, h = source.size
    scale = min(enlargement, 400 / max(w, h))
    size = (max(1, round(w * scale)), max(1, round(h * scale)))
    sw, sh = size
    canvas = Image.new("RGB", (max(660, 3 * (sw + 20)), 100 + 2 * (sh + 35)), "#e9edf1")
    draw = ImageDraw.Draw(canvas)
    draw.text((10, 8), label, fill="black")
    draw.text((10, 28), "Native PNG / SVG", fill="black")
    if w <= 270 and h <= 40:
        canvas.paste(source, (160, 23), source)
        canvas.paste(native, (180 + w, 23), native)
    for row, background in enumerate(("#242424", "#ffffff")):
        for col, (name, img) in enumerate((
            ("PNG enlarged", source.resize(size, Image.Resampling.NEAREST)),
            ("SVG enlarged", large.resize(size, Image.Resampling.LANCZOS)),
            ("Difference x4 (native)", diff.resize(size, Image.Resampling.NEAREST)),
        )):
            x, y = 10 + col * (sw + 20), 65 + row * (sh + 35)
            draw.text((x, y - 15), name, fill="black")
            tile = Image.new("RGBA", size, background)
            tile.alpha_composite(img.convert("RGBA"))
            canvas.paste(tile.convert("RGB"), (x, y))
    return canvas


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_gallery(output_dir, rows):
    """A local, searchable visual index; no server or external assets required."""
    cards = []
    for row in rows:
        name = html.escape(Path(row["source"]).name)
        status = html.escape(row["status"])
        preview_path = Path(row["preview"]) if row["preview"] else None
        relative = preview_path.relative_to(output_dir).as_posix() if preview_path else ""
        picture = (f'<a href="{html.escape(relative, quote=True)}"><img loading="lazy" '
                   f'src="{html.escape(relative, quote=True)}" alt="{name} comparison"></a>') if relative else ""
        metrics = f'Mean error {row["mae"]:.2%}, RMS {row["rmse"]:.2%}' if "mae" in row else ""
        hand_authored = str(row.get("hand_authored", False)).lower()
        vector_link = ""
        if row.get("hand_authored") and Path(row["svg"]).exists():
            relative_svg = os.path.relpath(row["svg"], output_dir)
            vector_link = f'<p><a href="{html.escape(relative_svg, quote=True)}">Open actual SVG</a></p>'
        cards.append(f'<article data-status="{status}" data-hand-authored="{hand_authored}"><h2>{name}</h2><p>{status}: '
                     f'{html.escape(row["reason"])}<br>{metrics}</p>{picture}{vector_link}</article>')
    page = '''<!doctype html><html lang="en"><meta charset="utf-8">
<title>Default theme SVG audit</title><style>
body{font:16px system-ui;margin:24px;background:#edf0f4;color:#243044}
header{position:sticky;top:0;background:#edf0f4;padding:12px 0}h1{margin:0}
input,select{font:inherit;margin:10px 10px 0 0;padding:8px}
main{display:grid;grid-template-columns:repeat(auto-fit,minmax(550px,1fr));gap:18px}
article{background:white;padding:16px;border-radius:8px}h2{font-size:18px}
img{width:100%;height:auto}article[hidden]{display:none}
</style><header><h1>Default theme SVG audit</h1>
<p>Native-size similarity only. Review enlarged SVG edges and fine detail before use.<br>
Difference images amplify errors 4 times. Previews show dark and light backgrounds.</p>
<input id="search" aria-label="Image name" placeholder="Find an image">
<select id="status" aria-label="Result"><option value="">All results</option>
<option>pass</option><option>poor_match</option><option>missing</option><option>invalid</option><option>error</option></select>
<span id="count"></span></header><main>'''
    page += "\n".join(cards)
    page += '''</main><script>
const search=document.querySelector('#search'),status=document.querySelector('#status');
function filter(){let count=0;document.querySelectorAll('article').forEach(a=>{
a.hidden=!(a.querySelector('h2').textContent.toLowerCase().includes(search.value.toLowerCase())&&(!status.value||(status.value==='completed'?a.dataset.handAuthored==='true':a.dataset.status===status.value)));
if(!a.hidden)count++;});document.querySelector('#count').textContent=count+' images';}
search.addEventListener('input',filter);status.addEventListener('change',filter);filter();
</script></html>'''
    (output_dir / "index.html").write_text(page)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-dir", type=Path, default=Path("data/themes/default"))
    p.add_argument("--svg-dir", type=Path, default=Path("data/themes/default-svg"))
    p.add_argument("--output-dir", type=Path, default=Path("outputs/theme-svg-audit"))
    p.add_argument("--renderer", choices=("cairosvg", "inkscape"), default="cairosvg")
    p.add_argument("--max-mae", type=float, default=0.03)
    p.add_argument("--max-rmse", type=float, default=0.08)
    p.add_argument("--max-alpha-mae", type=float, default=0.03)
    p.add_argument("--max-changed-fraction", type=float, default=0.10)
    p.add_argument("--pixel-threshold", type=float, default=32 / 255)
    p.add_argument("--no-previews", action="store_true")
    p.add_argument("--preview-scale", type=int, default=4, help="SVG enlargement for visual inspection (1-16)")
    p.add_argument("--require-hand-authored", action="store_true",
                   help="Only audit SVGs with an individual hand-authored completion note; other assets remain pending")
    args = p.parse_args()
    if not 1 <= args.preview_scale <= 16:
        p.error("preview-scale must lie in [1, 16]")
    for key, value in vars(args).items():
        if isinstance(value, float) and not 0 <= value <= 1:
            p.error(f"{key} must lie in [0, 1]")
    if not args.source_dir.is_dir():
        p.error("Source directory does not exist")
    if not shutil.which(args.renderer) and not (args.renderer == "cairosvg" and cairosvg is not None):
        p.error(f"Renderer command not found: {args.renderer}")
    sources = sorted(args.source_dir.rglob("*.png"))
    if not sources:
        p.error("No source PNG images found")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    notes_file = args.svg_dir / "conversion-notes.json"
    notes = json.loads(notes_file.read_text()) if notes_file.exists() else {}
    if isinstance(notes, list):
        notes = {x.get("source", x.get("file", "")): x for x in notes}
    rows = []
    for source in sources:
        relative = source.relative_to(args.source_dir)
        svg = args.svg_dir / relative.with_suffix(".svg")
        row = dict(source=str(source), svg=str(svg), status="missing", reason="No SVG candidate",
                   width=None, height=None, source_bytes=source.stat().st_size,
                   svg_bytes=None, source_sha256=sha256(source), svg_sha256="",
                   method="", notes="", preview="")
        note = notes.get(str(relative), notes.get(source.name, {}))
        if isinstance(note, dict):
            row.update(method=note.get("method", ""), notes=str(note.get("notes", note.get("limitations", ""))))
        if args.require_hand_authored:
            row.update(hand_authored=note.get("hand_authored") is True,
                       agent_model=note.get("agent_model", ""),
                       reasoning_effort=note.get("reasoning_effort", ""),
                       geometry=note.get("geometry", ""),
                       reason="Pending individual redraw and completion note")
        try:
            with Image.open(source) as original:
                original.load()
                image = original.convert("RGBA")
            row.update(width=image.width, height=image.height)
            if svg.exists() and (not args.require_hand_authored or note.get("hand_authored") is True):
                row.update(svg_bytes=svg.stat().st_size, svg_sha256=sha256(svg))
                issues = vector_issues(svg)
                if issues:
                    row.update(status="invalid", reason="; ".join(issues))
                else:
                    native = render_svg(svg, image.width, image.height, args.renderer)
                    row.update(compare_images(image, native, args.pixel_threshold))
                    failures = [metric for metric, limit in (
                        ("mae", args.max_mae), ("rmse", args.max_rmse),
                        ("alpha_mae", args.max_alpha_mae),
                        ("changed_fraction", args.max_changed_fraction)) if row[metric] > limit]
                    if row["opaque_background_added"]:
                        failures.append("opaque background added over transparent source corners")
                    row.update(status="poor_match" if failures else "pass",
                               reason="Exceeded: " + ", ".join(failures) if failures else "Within configured similarity thresholds")
                    for subdir, output in (("rendered", native), ("diffs", difference(image, native))):
                        dest = args.output_dir / subdir / relative
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        output.save(dest)
                    if not args.no_previews:
                        large = render_svg(svg, image.width * args.preview_scale, image.height * args.preview_scale, args.renderer)
                        dest = args.output_dir / "previews" / relative
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        preview(image, native, large, difference(image, native), str(relative) + " : " + row["status"], args.preview_scale).save(dest)
                        row["preview"] = str(dest)
        except Exception as error:
            row.update(status="error", reason=f"{type(error).__name__}: {error}")
        rows.append(row)
        if len(rows) % 25 == 0:
            print(f"Compared {len(rows)}/{len(sources)} images", flush=True)
    thresholds = {k: v for k, v in vars(args).items() if k.startswith("max_") or k == "pixel_threshold"}
    summary = {status: sum(r["status"] == status for r in rows)
               for status in ("pass", "poor_match", "missing", "invalid", "error")}
    report = dict(source_dir=str(args.source_dir), svg_dir=str(args.svg_dir),
                  renderer=args.renderer, thresholds=thresholds, summary=summary, images=rows)
    (args.output_dir / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    columns = list(dict.fromkeys(k for row in rows for k in row))
    with (args.output_dir / "results.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, columns)
        writer.writeheader()
        writer.writerows(rows)
    write_gallery(args.output_dir, rows)
    print(json.dumps(summary))
    # Missing/poor candidates are expected during a one-pass conversion, not a CLI failure.
    return 1 if summary["error"] or summary["invalid"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
