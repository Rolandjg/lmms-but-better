"""Renderer checks for thin feature preservation and scalable control gradients."""
from pathlib import Path
import tempfile
import unittest

from PIL import Image, ImageDraw

from compare_theme_svgs import compare_images, render_svg, vector_issues
from generate_theme_svgs import convert
from theme_svg_gradients import banded_gradient_svg, gradient_svg


class ImprovementTest(unittest.TestCase):
    def test_one_pixel_strokes_keep_their_area(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = Image.new("RGBA", (16, 16))
            draw = ImageDraw.Draw(image)
            draw.line((2, 2, 12, 2), fill="white")
            draw.line((2, 6, 2, 13), fill="white")
            source, svg = root / "strokes.png", root / "strokes.svg"
            image.save(source)
            convert(source, svg, root, root, alphamax=0, upscale=4)
            metrics = compare_images(image, render_svg(svg, 16, 16, "cairosvg"))
            self.assertLess(metrics["mae"], .005)
            self.assertLess(metrics["alpha_mae"], .005)
            self.assertEqual(vector_issues(svg), [])

    def test_strip_retains_native_colors_and_interpolates_when_enlarged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for size in ((1, 4), (4, 1)):
                image = Image.new("RGBA", size)
                image.putdata([(v, v, v, 255) for v in (10, 50, 90, 130)])
                svg = root / "strip.svg"
                svg.write_text(gradient_svg(image))
                metrics = compare_images(image, render_svg(svg, *size, "cairosvg"))
                self.assertLess(metrics["mae"], .005)
                large = render_svg(svg, size[0] * 10, size[1] * 10, "cairosvg")
                center = large.getpixel((0, 12) if size[0] == 1 else (12, 0))[0]
                self.assertGreater(center, 10)
                self.assertLess(center, 50)
                self.assertNotIn(center, (10, 50, 90, 130))

    def test_control_bands_preserve_transparent_margin_and_gradient(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Image.new("RGBA", (8, 12))
            for y in range(12):
                for x in range(2, 6):
                    image.putpixel((x, y), (10 + y * 10, 180, 80, 255))
            svg = Path(directory) / "key.svg"
            svg.write_text(banded_gradient_svg(image, "y"))
            native = render_svg(svg, 8, 12, "cairosvg")
            self.assertLess(compare_images(image, native)["mae"], .005)
            large = render_svg(svg, 80, 120, "cairosvg")
            self.assertEqual(large.getpixel((5, 60))[3], 0)
            self.assertEqual(large.getpixel((35, 60))[3], 255)
            self.assertEqual(vector_issues(svg), [])


if __name__ == "__main__":
    unittest.main()
