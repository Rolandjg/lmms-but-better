#!/usr/bin/env python3
"""Search Potrace settings for flagged theme SVGs, retaining lower-error candidates.

Run after generating a baseline audit, from the repository root, for example:
  /usr/bin/python3 scripts/improve_theme_svgs.py --backup-dir /tmp/theme-before-improve
Requires Pillow, numpy, cairosvg and Potrace. Only SVGs marked poor_match in the
input audit are considered. One-dimensional strips and rectangular controls use
gradient alternatives. Reviewed geometric reconstructions are preserved unless
--include-reviewed is specified. Native scoring does not judge enlarged quality.
"""
import argparse
import io
import json
import shutil
import tempfile
from pathlib import Path

import cairosvg
from PIL import Image
from compare_theme_svgs import compare_images
from generate_theme_svgs import convert
from theme_svg_gradients import banded_gradient_svg, gradient_svg


GRADIENT_CONTROLS = {
    'black_key', 'black_key_pressed', 'white_key', 'white_key_disabled',
    'white_key_pressed', 'pr_black_key', 'pr_black_key_pressed',
    'pr_white_key_big', 'pr_white_key_big_pressed', 'pr_white_key_small',
    'pr_white_key_small_pressed', 'horizontal_slider', 'main_slider',
}


def score(metrics):
    return (metrics['mae'] + .45 * metrics['rmse'] +
            .4 * metrics['alpha_mae'] + .15 * metrics['changed_fraction'])


def render(path, size):
    raw = cairosvg.svg2png(url=str(path), output_width=size[0], output_height=size[1])
    return Image.open(io.BytesIO(raw)).convert('RGBA')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', type=Path, default=Path('outputs/theme-svg-audit/results.json'))
    parser.add_argument('--source-dir', type=Path, default=Path('data/themes/default'))
    parser.add_argument('--svg-dir', type=Path, default=Path('data/themes/default-svg'))
    parser.add_argument('--backup-dir', type=Path, required=True, help='New directory to receive a copy before changes')
    parser.add_argument('--include-reviewed', action='store_true', help='Also optimize reviewed geometry for native error; this may restore stepped pixel contours')
    args = parser.parse_args()
    shutil.copytree(args.svg_dir, args.backup_dir)
    audit = json.loads(args.audit.read_text())
    notes_path = args.svg_dir / 'conversion-notes.json'
    notes = json.loads(notes_path.read_text())
    note_by_source = {note['source']: note for note in notes}
    changed = 0
    with tempfile.TemporaryDirectory(prefix='theme-svg-search-') as work:
        work = Path(work)
        for item in audit['images']:
            if item['status'] != 'poor_match':
                continue
            relative = Path(item['source']).relative_to(args.source_dir)
            if note_by_source[str(relative)].get('geometry_reviewed') and not args.include_reviewed:
                print(f'{relative}: preserved reviewed geometry', flush=True)
                continue
            source = args.source_dir / relative
            target = args.svg_dir / relative.with_suffix('.svg')
            candidate = work / relative.with_suffix('.svg')
            original = Image.open(source).convert('RGBA')
            best = score(compare_images(original, render(target, original.size)))
            best_svg = best_note = None
            variants = []
            if min(original.size) == 1:
                variants.append((gradient_svg(original), 'Continuous SVG linear gradient sampled at source pixel centers'))
            elif source.stem in GRADIENT_CONTROLS:
                for axis in ('x', 'y'):
                    try:
                        variants.append((banded_gradient_svg(original, axis),
                                         f'Merged rectangular bands with continuous SVG linear gradients (axis {axis})'))
                    except ValueError:
                        pass
            candidate.parent.mkdir(parents=True, exist_ok=True)
            for svg_text, method in variants:
                candidate.write_text(svg_text)
                value = score(compare_images(original, render(candidate, original.size)))
                if value < best - .00001:
                    best = value
                    best_svg = svg_text
                    best_note = dict(note_by_source[str(relative)], method=method,
                                     limitations='Continuous gradient interiors preserve source colors; inspect enlarged bevel boundaries.')
            for alpha, scale in (() if min(original.size) == 1 else ((0, 1), (.25, 1), (0, 4), (.6, 4), (0, 8))):
                note = convert(source, candidate, args.source_dir, work, alpha, scale)
                value = score(compare_images(original, render(candidate, original.size)))
                if value < best - .00001:
                    best = value
                    best_svg = candidate.read_text()
                    note['method'] += f'; Potrace corner threshold {alpha}, mask scale {scale}×'
                    note['limitations'] += ' Polygonal contours preserve thin details; inspect enlarged appearance.'
                    best_note = note
            if best_svg is not None:
                target.write_text(best_svg)
                note_by_source[str(relative)] = best_note
                changed += 1
            print(f'{relative}: score {best:.5f}', flush=True)
    notes_path.write_text(json.dumps([note_by_source[n['source']] for n in notes], indent=2) + '\n')
    print(f'Updated {changed} candidates; backup: {args.backup_dir}')


if __name__ == '__main__':
    main()
