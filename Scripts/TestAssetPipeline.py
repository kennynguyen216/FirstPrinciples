import argparse
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
PARSER = argparse.ArgumentParser()
PARSER.add_argument("--binaries", type=pathlib.Path, default=ROOT / "Projects/Sample/Binaries/Debug")
OPTIONS = PARSER.parse_args()
SUFFIX = ".exe" if sys.platform == "win32" else ""


class AssetPipelineTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="FPAssetPipeline-")
        self.addCleanup(self.temporary.cleanup)
        self.project = pathlib.Path(self.temporary.name) / "Project"
        self.content = self.project / "Content"
        self.content.mkdir(parents=True)
        (self.project / "Test.fpproject").write_text("Name Test\nStartScene main.scene\n", encoding="utf-8")
        (self.content / "main.scene").write_text(
            "version 2\nentity 11111111-1111-4111-8111-111111111111\n"
            "component Sprite\n  Texture image.png\n", encoding="utf-8")
        (self.content / "image.png").write_bytes(b"source")

    def run_tool(self, name, *arguments):
        return subprocess.run(
            [str(OPTIONS.binaries.resolve() / (name + SUFFIX)), *map(str, arguments)],
            cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)

    def test_import_preserves_metadata_and_lists_dependencies(self):
        imported = self.run_tool("AssetTool", "--project", self.project, "--import")
        self.assertEqual(imported.returncode, 0, imported.stdout)
        metadata = self.content / "image.png.fpmeta"
        before = metadata.read_bytes()
        repeated = self.run_tool("AssetTool", "--project", self.project, "--import")
        self.assertEqual(repeated.returncode, 0, repeated.stdout)
        self.assertEqual(metadata.read_bytes(), before)
        listed = self.run_tool("AssetTool", "--project", self.project, "--dependencies", "main.scene")
        self.assertEqual(listed.returncode, 0, listed.stdout)
        self.assertIn("image.png", listed.stdout)

    def test_missing_reference_blocks_packaging_before_staging(self):
        (self.content / "image.png").unlink()
        validated = self.run_tool("AssetTool", "--project", self.project, "--validate")
        self.assertEqual(validated.returncode, 1, validated.stdout)
        self.assertIn("Missing dependency image.png", validated.stdout)
        output = pathlib.Path(self.temporary.name) / ("Game.app" if sys.platform == "darwin" else "Game")
        packaged = self.run_tool("PackageTool", ROOT, self.project, output)
        self.assertEqual(packaged.returncode, 1, packaged.stdout)
        self.assertIn("Asset validation failed", packaged.stdout)
        self.assertFalse(output.exists())

    def test_duplicate_ids_and_bad_metadata_are_rejected(self):
        imported = self.run_tool("AssetTool", "--project", self.project, "--import")
        self.assertEqual(imported.returncode, 0, imported.stdout)
        metadata = self.content / "image.png.fpmeta"
        (self.content / "copy.png").write_bytes(b"copy")
        (self.content / "copy.png.fpmeta").write_bytes(metadata.read_bytes())
        duplicated = self.run_tool("AssetTool", "--project", self.project, "--validate")
        self.assertEqual(duplicated.returncode, 1, duplicated.stdout)
        self.assertIn("Duplicate asset ID", duplicated.stdout)
        metadata.write_text("version 99\nid invalid\n", encoding="utf-8")
        damaged = self.run_tool("AssetTool", "--project", self.project, "--import")
        self.assertEqual(damaged.returncode, 1, damaged.stdout)
        self.assertIn("Invalid asset metadata", damaged.stdout)
        self.assertEqual(metadata.read_text(encoding="utf-8"), "version 99\nid invalid\n")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
