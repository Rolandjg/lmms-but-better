#!/usr/bin/env python3
"""Collect individual redraw notes and audit only completed hand-authored SVGs."""
import csv
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'data/themes/default'
SVG = ROOT / 'data/themes/default-svg'
OUTPUT = ROOT / 'outputs/theme-svg-clean-audit'


def main():
    notes = []
    for source in sorted(SOURCE.rglob('*.png')):
        relative = source.relative_to(SOURCE)
        note_path = SVG / 'notes' / relative.with_suffix('.json')
        if not note_path.exists():
            continue
        note = json.loads(note_path.read_text())
        if not (note.get('hand_authored') is True and
                note.get('agent_model') == 'gpt-6.1-terra' and
                note.get('reasoning_effort') == 'low' and
                note.get('source') == str(relative)):
            raise ValueError(f'Invalid completion/provenance note: {note_path}')
        if not (SVG / relative.with_suffix('.svg')).exists():
            raise ValueError(f'Completed note has no SVG: {note_path}')
        notes.append(note)
    (SVG / 'conversion-notes.json').write_text(json.dumps(notes, indent=2) + '\n')
    completed = subprocess.run([
        sys.executable, str(ROOT / 'scripts/compare_theme_svgs.py'),
        '--source-dir', str(SOURCE), '--svg-dir', str(SVG),
        '--output-dir', str(OUTPUT), '--require-hand-authored', '--preview-scale', '8'], cwd=ROOT)
    report = json.loads((OUTPUT / 'results.json').read_text())
    for row in report['images']:
        row['redraw_status'] = 'completed' if row['hand_authored'] else 'pending'
    report['redraw_summary'] = dict(
        total=len(report['images']), completed=len(notes),
        pending=len(report['images']) - len(notes),
        native_pass=report['summary']['pass'],
        native_flagged=report['summary']['poor_match'],
        opaque_background_added=sum(row.get('opaque_background_added', False)
                                    for row in report['images']))
    (OUTPUT / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    gallery = OUTPUT / 'index.html'
    page = gallery.read_text().replace('Default theme SVG audit', 'Hand-authored theme SVG redraws')
    page = page.replace('<option>missing</option>', '<option value="missing">Pending redraw</option>')
    page = page.replace('<option value="">All results</option>',
                        '<option value="completed" selected>Completed redraws</option><option value="">All images</option>')
    page = page.replace('<p>Native-size similarity only.',
                        f'<p>{len(notes)} individually hand-authored redraws; '
                        f'{len(report["images"]) - len(notes)} pending. Native-size similarity only.')
    gallery.write_text(page)
    columns = list(dict.fromkeys(key for row in report['images'] for key in row))
    with (OUTPUT / 'results.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        writer.writerows(report['images'])
    worklist = [dict(source=str(Path(row['source']).relative_to(SOURCE)),
                     status=row['redraw_status'], native_similarity=row['status'])
                for row in report['images']]
    (SVG / 'worklist.json').write_text(json.dumps(worklist, indent=2) + '\n')
    print(json.dumps(report['redraw_summary']))
    return completed.returncode


if __name__ == '__main__':
    raise SystemExit(main())
