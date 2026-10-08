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
        output = ("ninja: Entering directory 'build'\nHAX_NINJA_STATUS compiling\n"
                  "\x1b[31m../src/main.c:2: warning: example\x1b[0m\n"
                  "ninja: no work to do.\n")
        self.assertEqual(check.relay(output, check.ROOT / "build"),
                         "src/main.c:2: warning: example\n")
        self.assertEqual(check.relay("../../src/main.c:2: error\n",
                                    check.ROOT / "nested" / "build"),
                         "src/main.c:2: error\n")

    def test_setup_uses_coredata_not_directory_existence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            build = root / "build-release"
            build.mkdir()
            with patch.object(check, "ROOT", root), patch.object(check, "captured") as run, \
                    patch.object(check, "tool", return_value="gcc"), redirect_stdout(io.StringIO()):
                check.setup(build, ["meson"])
                self.assertIn("--buildtype=release", run.call_args.args[0])
                (build / "meson-private").mkdir()
                (build / "meson-private" / "coredata.dat").touch()
                run.reset_mock()
                check.setup(build, ["meson"])
                run.assert_not_called()

    def test_unknown_unconfigured_directory(self):
        with self.assertRaisesRegex(RuntimeError, "meson setup"):
            check.setup(check.ROOT / "unknown-unconfigured-build", ["meson"])

    @unittest.skipUnless(os.name == "nt", "native Windows dependency prefix")
    def test_windows_prefix_is_forwarded(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            prefix = root / "build-windows-deps" / "prefix"
            prefix.mkdir(parents=True)
            with patch.object(check, "ROOT", root), patch.object(check, "captured") as run, \
                    patch.object(check, "tool", return_value="gcc"), redirect_stdout(io.StringIO()):
                check.setup(root / "build", ["meson"])
                self.assertIn("-Dcmake_prefix_path=" + str(prefix), run.call_args.args[0])

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
