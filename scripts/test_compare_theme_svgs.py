"""Small independent fixtures for the image audit's scoring and CLI."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

from compare_theme_svgs import compare_images, render_svg, vector_issues


class ComparisonTest(unittest.TestCase):
    def test_unreviewed_svg_does_not_count_as_completed_redraw(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, svg, output = (root / x for x in ("png", "svg", "audit"))
            source.mkdir()
            svg.mkdir()
            for name in ("done", "pending"):
                Image.new("RGBA", (8, 8), "red").save(source / f"{name}.png")
                (svg / f"{name}.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 8 8"><rect width="8" height="8" fill="red"/></svg>')
            (svg / "conversion-notes.json").write_text(json.dumps([
                {"source": "done.png", "hand_authored": True, "agent_model": "gpt-6.1-terra", "reasoning_effort": "low"}]))
            completed = subprocess.run([sys.executable, str(Path(__file__).with_name("compare_theme_svgs.py")),
                "--source-dir", str(source), "--svg-dir", str(svg), "--output-dir", str(output),
                "--require-hand-authored", "--preview-scale", "8"], capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            report = json.loads((output / "results.json").read_text())
            self.assertEqual(report["summary"]["pass"], 1)
            self.assertEqual(report["summary"]["missing"], 1)
            self.assertFalse(report["images"][1]["hand_authored"])

    def test_exact_and_hidden_transparent_rgb(self):
        a = Image.new("RGBA", (10, 10), (255, 0, 0, 0))
        b = Image.new("RGBA", (10, 10), (0, 255, 0, 0))
        result = compare_images(a, b)
        for key in ("mae", "rmse", "alpha_mae", "changed_fraction"):
            self.assertEqual(result[key], 0)

    def test_transparency_must_match_on_both_backgrounds(self):
        black = Image.new("RGBA", (2, 2), (0, 0, 0, 255))
        transparent = Image.new("RGBA", (2, 2), (0, 0, 0, 0))
        result = compare_images(black, transparent)
        self.assertEqual(result["black_mae"], 0)
        self.assertEqual(result["white_mae"], 1)
        self.assertEqual(result["alpha_mae"], 1)

    def test_hidden_rgb_cannot_become_an_opaque_background(self):
        transparent = Image.new("RGBA", (16, 16), (24, 30, 36, 0))
        transparent.putpixel((8, 8), (255, 255, 255, 255))
        opaque = Image.new("RGBA", (16, 16), (24, 30, 36, 255))
        opaque.putpixel((8, 8), (255, 255, 255, 255))
        self.assertTrue(compare_images(transparent, opaque)["opaque_background_added"])
        self.assertFalse(compare_images(opaque, opaque)["opaque_background_added"])
        self.assertFalse(compare_images(transparent, transparent)["opaque_background_added"])

    def test_margins_cannot_hide_missing_shape(self):
        original = Image.new("RGBA", (100, 100))
        original.putpixel((50, 50), (255, 255, 255, 255))
        missing = Image.new("RGBA", (100, 100))
        result = compare_images(original, missing)
        self.assertEqual(result["mae"], 1)
        self.assertEqual(result["support_pixels"], 1)
        self.assertLess(result["full_canvas_mae"], 0.001)

    def test_renderer_and_cli_with_missing_poor_invalid_and_exact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, svg, output = (root / x for x in ("png", "svg", "audit"))
            source.mkdir()
            svg.mkdir()
            for name in ("exact", "poor", "missing", "invalid"):
                Image.new("RGBA", (12, 8), "red").save(source / f"{name}.png")
            template = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 3 2"><rect width="3" height="2" fill="{}"/></svg>'
            (svg / "exact.svg").write_text(template.format("red"))
            (svg / "poor.svg").write_text(template.format("blue"))
            (svg / "invalid.svg").write_text('<svg viewBox="0 0 12 8"><image href="data:image/png;base64,AA=="/></svg>')
            self.assertTrue(vector_issues(svg / "invalid.svg"))
            rendered = render_svg(svg / "exact.svg", 12, 8, "cairosvg")
            self.assertEqual(rendered.size, (12, 8))
            self.assertEqual(rendered.getpixel((6, 4)), (255, 0, 0, 255))
            completed = subprocess.run([sys.executable, str(Path(__file__).with_name("compare_theme_svgs.py")),
                "--source-dir", str(source), "--svg-dir", str(svg), "--output-dir", str(output)],
                capture_output=True, text=True)
            self.assertEqual(completed.returncode, 1, completed.stderr)
            report = json.loads((output / "results.json").read_text())
            self.assertEqual(report["summary"], {"pass": 1, "poor_match": 1, "missing": 1, "invalid": 1, "error": 0})
            self.assertTrue((output / "previews/exact.png").exists())


if __name__ == "__main__":
    unittest.main()
