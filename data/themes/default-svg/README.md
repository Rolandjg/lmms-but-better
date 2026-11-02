# Hand-authored default theme SVG redraws

This is the replacement effort for the rejected automatic traces. Each image
is assigned individually to GPT-6.1 Terra on low thinking. Agents inspect the
source and write SVG geometry directly, then render and inspect it at 8x.
Automatic tracing, pixel contours, raster embedding and pixel mosaics are excluded.

Completed SVGs and their individual `notes/*.json` files live here. `worklist.json`
records completed and pending images across all 285 PNG originals. Pending images
are not filled using the rejected traces. The original PNG theme is unchanged.

Native-size fidelity and smooth enlarged geometry are separate review criteria.
The comparison keeps the original similarity thresholds and honestly flags
redraws that differ too much; passing native metrics does not certify design
quality. Individual notes describe geometry and limitations. Poor matches are
retained for review, following the requested one-pass workflow.

Flagged redraws have since received a GPT-6.1 Terra / low correction review.
`../../../outputs/theme-svg-clean-audit/correction-review.json` records the
before/after metrics and remaining limitations for each reviewed image. The
native similarity thresholds are unchanged, and unresolved flags stay visible.

Refresh the completion ledger, exact-size comparison, enlarged previews and
searchable gallery with:

```sh
/usr/bin/python3 scripts/audit_hand_authored_theme_svgs.py
```

Results are in `../../../outputs/theme-svg-clean-audit/`. Its JSON/CSV reports
include the entire source inventory, hand-authored provenance and pending work.
The workbook reports completion separately from native similarity. Retain the
original artwork attribution and repository licensing for these derivative assets.

The older rejected tracing experiment (formerly `../default-svg/`) has been removed;
its scores are not evidence of completion or clean geometry for this effort.
