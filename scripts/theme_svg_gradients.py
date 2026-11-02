#!/usr/bin/env python3
"""Represent a one-pixel-wide background strip as a continuous SVG gradient."""
from PIL import Image


def banded_gradient_svg(image: Image.Image, axis="y") -> str:
    """Rebuild rectangular key/bevel bands with continuous gradient fills.

    This is intended for rectangular controls whose columns (or rows) repeat,
    rather than arbitrary icon tracing. Adjacent identical bands are merged.
    """
    image = image.convert("RGBA")
    width, height = image.size
    if axis not in {"x", "y"}:
        raise ValueError("Gradient axis must be x or y")
    vertical = axis == "y"
    length, across = (height, width) if vertical else (width, height)
    bands = []
    for cross in range(across):
        values = [image.getpixel((cross, i) if vertical else (i, cross)) for i in range(length)]
        values = [rgba if rgba[3] else (0, 0, 0, 0) for rgba in values]
        if bands and bands[-1][2] == values:
            bands[-1][1] += 1
        else:
            bands.append([cross, 1, values])
    if len(bands) > 12:
        raise ValueError("Too many independent bands for a simple control")
    defs, rects = [], []
    for index, (cross, span, values) in enumerate(bands):
        if not any(v[3] for v in values):
            continue
        stops = []
        for n, (r, g, b, a) in enumerate(values):
            stops.append(f'<stop offset="{(n + .5) / length:.8f}" stop-color="#{r:02x}{g:02x}{b:02x}" stop-opacity="{a / 255:.8f}"/>')
        endpoint = f'x2="{0 if vertical else width}" y2="{height if vertical else 0}"'
        defs.append(f'<linearGradient id="band{index}" gradientUnits="userSpaceOnUse" x1="0" y1="0" {endpoint}>\n' + "\n".join(stops) + '\n</linearGradient>')
        x, y, w, h = (cross, 0, span, height) if vertical else (0, cross, width, span)
        rects.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="url(#band{index})"/>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">\n'
            '<defs>\n' + "\n".join(defs) + '\n</defs>\n' + "\n".join(rects) + '\n</svg>\n')


def gradient_svg(image: Image.Image) -> str:
    image = image.convert("RGBA")
    width, height = image.size
    if min(width, height) != 1:
        raise ValueError("Expected a single row or column")
    vertical = height > 1
    length = height if vertical else width
    # Sample positions use pixel centers. Intermediate stops preserve deliberate
    # border highlights while interpolation removes steps when the strip grows.
    values = [image.getpixel((0, n) if vertical else (n, 0)) for n in range(length)]
    stops = []
    for n, (r, g, b, a) in enumerate(values):
        offset = (n + 0.5) / length
        stops.append(f'<stop offset="{offset:.8f}" stop-color="#{r:02x}{g:02x}{b:02x}" stop-opacity="{a / 255:.8f}"/>')
    endpoint = f'x2="{0 if vertical else width}" y2="{height if vertical else 0}"'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">\n'
            f'<defs><linearGradient id="strip" gradientUnits="userSpaceOnUse" x1="0" y1="0" {endpoint}>\n'
            + "\n".join(stops) + '\n</linearGradient></defs>\n'
            f'<rect width="{width}" height="{height}" fill="url(#strip)"/>\n</svg>\n')
