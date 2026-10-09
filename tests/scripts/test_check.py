#!/usr/bin/env python3
"""Unit tests for the platform-independent build runner."""

from contextlib import redirect_stdout
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import check


class CheckTests(unittest.TestCase):
    def test_relay_preserves_diagnostics(self):
        output = "\x1b[31m../src/main.c:2: warning: example\x1b[0m\n"
        self.assertEqual(check.relay(output, check.ROOT / "build"),
                         "src/main.c:2: warning: example\n")
        self.assertEqual(check.relay("../../src/main.c:2: error\n",
                                    check.ROOT / "nested" / "build"),
                         "src/main.c:2: error\n")

    def test_glob_selection_and_unknown_names(self):
        commands = {"tools/read": ["read"], "tools/write": ["write"], "buf": ["buf"]}
        with patch.object(check, "captured") as run, redirect_stdout(io.StringIO()):
            check.run_tests(commands, ["tools/*", "tools/read"], 1, check.ROOT / "build")
            self.assertEqual(run.call_count, 2)
        with self.assertRaisesRegex(RuntimeError, "no tests match"):
            check.run_tests(commands, ["missing"], 1, check.ROOT / "build")

    def test_incremental_inputs_and_command_changes(self):
        from build import Build
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "object.o"
            source = root / "source.c"
            source.write_text("source")
            builder = Build.__new__(Build)
            self.assertTrue(builder.stale(output, [source], ["cc"]))
            output.write_text("object")
            output.with_suffix(".o.cmd").write_text('["cc"]')
            self.assertFalse(builder.stale(output, [source], ["cc"]))
            self.assertTrue(builder.stale(output, [source], ["clang"]))
            source.unlink()
            self.assertTrue(builder.stale(output, [source], ["cc"]))

    def test_platform_source_selection(self):
        import build
        with patch.object(build, "WINDOWS", True):
            paths = {path.name for path in build.sources("src")}
            self.assertIn("spawn_win.c", paths)
            self.assertNotIn("spawn.c", paths)
        with patch.object(build, "WINDOWS", False):
            paths = {path.name for path in build.sources("src")}
            self.assertIn("spawn.c", paths)
            self.assertNotIn("spawn_win.c", paths)

    def test_captured_relays_utf8_and_failure_status(self):
        output = io.StringIO()
        with redirect_stdout(output), self.assertRaises(subprocess.CalledProcessError) as caught:
            check.captured([sys.executable, "-c",
                            "import sys; sys.stdout.buffer.write(b'warning: \\xc3\\xa9\\n'); "
                            "sys.exit(7)"], check.ROOT / "build", quiet=True)
        self.assertEqual(caught.exception.returncode, 7)
        self.assertEqual(output.getvalue(), "warning: \u00e9\n")


if __name__ == "__main__":
    unittest.main()
