"""Adversarial fixtures for the repository gate; no desktop or network access."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "check_repository.py"
SPEC = importlib.util.spec_from_file_location("repository_checks", SCRIPT)
checks = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checks)


class RepositoryChecksTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.files = set()

    def file(self, name, text):
        target = self.root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
        self.files.add(name)

    def links(self, text, source="README.md"):
        return checks.link_errors(self.root, self.files, source, text)

    def test_encoded_paths_parent_links_and_duplicate_headings(self):
        self.file("docs/A B.md", "# 说明\n## Repeated\n## Repeated\n")
        self.assertEqual([], self.links("[ok](docs/A%20B.md#说明) [again](<docs/A B.md#repeated-1>)"))
        self.file("README.md", "# Root\n")
        self.assertEqual([], self.links("[root](../README.md#root)", "docs/A B.md"))
        self.assertTrue(self.links("[missing heading](docs/A%20B.md#no-heading)"))

    def test_untracked_file_does_not_hide_a_broken_link(self):
        self.file("only-local.md", "# Local")
        self.files.remove("only-local.md")
        self.assertTrue(self.links("[missing in checkout](only-local.md)"))

    def test_case_mismatch_fails_on_windows_too(self):
        self.file("docs/Actual.md", "# Heading")
        self.assertTrue(self.links("[wrong case](docs/actual.md)"))
        self.assertFalse(self.links("[right case](docs/Actual.md)"))

    def test_external_urls_examples_and_reference_images(self):
        self.file("docs/guide.md", "# Guide")
        good = "[remote](https://example.invalid/no-network)\n`[example](missing.md)`\n```md\n[example](missing.md)\n```\n~~~md\n[example](missing-too.md)\n~~~\n"
        self.assertFalse(self.links(good))
        self.assertTrue(self.links("![image][preview]\n[preview]: missing.png"))
        self.assertFalse(self.links("[reference][guide]\n[guide]: docs/guide.md#guide"))

    def test_nested_image_checks_inner_and_outer_destinations(self):
        self.file("assets/preview.png", "image placeholder")
        self.file("docs/details.md", "# Details")
        nested = "[![preview](assets/preview.png)](docs/details.md#details)"
        self.assertEqual([], self.links(nested))
        self.assertTrue(self.links("[![preview](missing.png)](docs/details.md)"))
        self.assertTrue(self.links("[![preview](assets/preview.png)](missing.md)"))

    def test_escaping_and_machine_specific_links_fail(self):
        self.assertTrue(self.links("[escape](../outside.md)"))
        self.assertTrue(self.links("[private](file:///outside.md)"))
        self.assertTrue(self.links("[nonportable](docs\\guide.md)"))

    def test_native_boundaries_ignore_comments_and_strings_but_reject_code(self):
        self.assertFalse(checks.boundary_errors("src/core/model.cpp", '// HWND is opaque\nconst char* s="HWND";'))
        self.assertFalse(checks.boundary_errors("src/ui/widget.cpp", 'const char* s="#include <windows.h>";'))
        self.assertTrue(checks.boundary_errors("src/ui/widget.cpp", '#include <windows.h>\n'))
        self.assertTrue(checks.boundary_errors("src/platform/window_api_adapter.h", 'HWND window;'))
        self.assertTrue(checks.boundary_errors("src/core/model.cpp", '#include <X11/Xlib.h>'))
        self.assertFalse(checks.boundary_errors("src/platform/windows/backend.cpp", '#include <windows.h>'))
        self.assertFalse(checks.boundary_errors(
            "src/core/model.cpp", 'const char* s = R"tag(HWND // #include <windows.h>)tag";'))

    def test_missing_boundary_target_is_reported(self):
        self.file("README.md", "# Fixture\n")
        errors = checks.check_files(self.root, ["README.md"])
        self.assertTrue(any("boundary check targets missing" in error for error in errors))

    def test_artifacts_and_utf8(self):
        for name in ("build/a.exe", "build-review/CMakeCache.txt", ".vs/state", "scripts/__pycache__/a.pyc", "plans/handoff.md", ".env"):
            self.assertTrue(checks.artifact_errors(name), name)
        self.assertFalse(checks.artifact_errors("docs/notes/verification.md"))
        self.file("README.md", "")
        (self.root / "README.md").write_bytes(b"\xff\xfe")
        self.assertTrue(checks.check_files(self.root, ["README.md"]))

    def test_cli_detects_committed_whitespace_in_a_pr_range(self):
        def git(*args):
            return subprocess.check_output(["git", "-C", str(self.root), *args], text=True).strip()
        git("init", "-q")
        git("config", "user.name", "Repository fixture")
        git("config", "user.email", "fixture@example.invalid")
        git("config", "core.autocrlf", "false")
        self.file("README.md", "# Good\n")
        for name in checks.NEUTRAL_FILES:
            self.file(name, "")
        git("add", "README.md", *sorted(checks.NEUTRAL_FILES))
        git("commit", "-qm", "test: initial fixture")
        base = git("rev-parse", "HEAD")
        result = subprocess.run([sys.executable, str(SCRIPT), "--base", base], cwd=self.root, capture_output=True)
        self.assertEqual(0, result.returncode, result.stderr)
        self.file("README.md", "# Bad \n")
        git("add", "README.md")
        git("commit", "-qm", "test: trailing whitespace")
        result = subprocess.run([sys.executable, str(SCRIPT), "--base", base], cwd=self.root, capture_output=True)
        self.assertNotEqual(0, result.returncode)
        self.assertIn(b"diff --check", result.stderr)


if __name__ == "__main__":
    unittest.main()
