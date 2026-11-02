# One-image SVG redraw assignment

Work in `/home/roland/project/lmms-but-better`. The assignment names ONE PNG in
`data/themes/default`. Reconstruct that image by writing SVG geometry by hand.
The user rejected automatic bitmap tracing and wants smooth scalable designs.

1. Inspect the original PNG with `view_image`. You may enlarge it for inspection
   and measure layout/colors from pixels. Understand its visual design.
   Check the alpha channel before describing a background as opaque. Dark RGB
   stored beneath zero alpha is transparent and must not become a visible fill.
2. Write `data/themes/default-svg-clean/NAME.svg` using native dimensions and
   viewBox. Use simple semantic shapes, deliberate straight diagonals, circles,
   ellipses, strokes, smooth curves and gradients. Keep the original style,
   proportions, details and transparency as closely as practical.
   Measure the main extents and stroke thickness. Preserve square caps and corners
   where the design uses them; do not restyle everything with rounded ends.
   Replace diagonal staircases with deliberate lines following the intended edge,
   rather than changing the icon's ink coverage, weight, or proportions.
   Curved forms need continuous arcs or Bezier curves. A series of short ledges
   with rounded corners still reproduces a pixel staircase and is unsuitable.
3. Render native and 8x with `/usr/bin/python3` and `cairosvg`, then inspect the
   enlarged rendering. Optionally compare native appearance with
   `scripts/compare_theme_svgs.py`'s `compare_images` function. Fidelity is useful,
   but a lower error must never be obtained by reproducing pixel staircases.
   One design pass is enough; flag a poor match honestly and move on.
4. Write `data/themes/default-svg-clean/notes/NAME.json` containing `source`
   (PNG basename), `svg` (SVG basename), `method: "Hand-authored SVG geometry"`,
   `hand_authored: true`, `agent_model: "gpt-6.1-terra"`,
   `reasoning_effort: "low"`, a `geometry` description and `limitations`.
   Include observed native metrics if measured. Do not claim visual QA unless
   you have inspected the enlarged SVG rendering.

Do not read or reuse the rejected files in `data/themes/default-svg`. Do not use
Potrace, automatic tracing, raster masks converted to paths, row/run rectangles,
pixel mosaics, embedded/linked raster images, font-dependent text or imagegen.
Do not convert raster outlines to polygons using a script. Reading the existing
hand-authored files in `default-svg-clean` for consistency within a family is
allowed; each assigned source still needs inspection and deliberate geometry.
Letters and numerals should use explicit vector shapes or outlined glyphs.

Touch ONLY your assigned SVG and its note. Put inspection images in `/tmp`.
Do not edit shared indexes, scripts, reports, or other assets. Do not delegate.
Complete this one image and stop. Keep the final response to two sentences with
completion and any material visual limitation. Other agents own other images.
