#!/usr/bin/env python3
"""Trace default theme PNGs into editable vector candidates with Potrace.

Requires Pillow and the potrace executable. Quantized RGBA regions become
independent paths; transparent pixels remain transparent. This is a one-pass
approximation, not a reconstruction of the original source artwork.
"""
import argparse
import json
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]

def convert(source, target, source_dir, output_dir, alphamax=0.6, upscale=1):
    im = Image.open(source).convert('RGBA')
    width, height = im.size
    # Full artwork is unusually complex, but still receives a reviewable candidate.
    colors = 48 if width * height > 10000 else 24
    q = im.quantize(colors=colors, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    # Calculate representative colors from the original, with alpha-weighted RGB.
    buckets = {}
    for index, rgba in zip(q.getdata(), im.getdata()):
        bucket = buckets.setdefault(index, [0, 0, 0, 0, 0])
        r, g, b, a = rgba
        bucket[0] += r * a; bucket[1] += g * a; bucket[2] += b * a
        bucket[3] += a; bucket[4] += 1
    masks = sorted(buckets, key=lambda x: buckets[x][4], reverse=True)
    indices = Image.frombytes('L', q.size, q.tobytes())
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<!-- Automatically traced candidate; see conversion-notes.json. -->']
    with tempfile.TemporaryDirectory(prefix='theme-trace-') as temp:
        bitmap = Path(temp) / 'mask.pbm'
        vector = Path(temp) / 'mask.svg'
        for index in masks:
            rs, gs, bs, asum, count = buckets[index]
            alpha = round(asum / count)
            if alpha < 3 or not asum:
                continue
            color = (round(rs / asum), round(gs / asum), round(bs / asum))
            mask = indices.point([0 if i == index else 255 for i in range(256)]).convert('1')
            if upscale != 1:
                mask = mask.resize((width * upscale, height * upscale), Image.Resampling.NEAREST)
            mask.save(bitmap)
            subprocess.run(['potrace', str(bitmap), '-s', '-o', str(vector), '--turdsize', '0', '--alphamax', str(alphamax), '--opttolerance', '0.15'], check=True, stdout=subprocess.DEVNULL)
            tree = ET.parse(vector)
            group = tree.getroot().find('{http://www.w3.org/2000/svg}g')
            if group is None:
                continue
            paths = group.findall('{http://www.w3.org/2000/svg}path')
            if not paths:
                continue
            parts.append(f'<g transform="scale({1 / upscale}) {group.get("transform")}" fill="#{color[0]:02x}{color[1]:02x}{color[2]:02x}" fill-opacity="{alpha / 255:.4f}" stroke="none">')
            parts.extend(f'<path d="{p.get("d")}"/>' for p in paths)
            parts.append('</g>')
    parts.append('</svg>')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('\n'.join(parts) + '\n')
    return {'source': str(source.relative_to(source_dir)), 'svg': str(target.relative_to(output_dir)), 'method': 'RGBA color quantization and Potrace vector contours', 'limitations': 'Automatically traced approximation; small details, antialiasing and smooth gradients may differ from the raster original.' + (' Complex artwork is a low-confidence candidate.' if width * height > 10000 else ''), 'width': width, 'height': height, 'color_limit': colors}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=ROOT/'data/themes/default')
    parser.add_argument('--output-dir', type=Path, default=ROOT/'data/themes/default-svg')
    args = parser.parse_args()
    notes = []
    for source in sorted(args.source_dir.rglob('*.png')):
        target = args.output_dir / source.relative_to(args.source_dir).with_suffix('.svg')
        notes.append(convert(source, target, args.source_dir, args.output_dir))
        print(f'{len(notes):3d} {source.name}', flush=True)
    (args.output_dir/'conversion-notes.json').write_text(json.dumps(notes, indent=2)+'\n')

if __name__ == '__main__':
    main()
