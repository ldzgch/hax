#!/usr/bin/env python3
"""Windows dependency configuration retains options when reusing or changing compilers."""

from contextlib import redirect_stdout
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import windows_deps


@unittest.skipUnless(os.name == "nt", "native Windows dependency setup")
class WindowsDepsTests(unittest.TestCase):
    def configure(self, change_compiler):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler = root / "gcc.exe"
            compiler.touch()
            cached = root / "other-gcc.exe" if change_compiler else compiler
            cached.touch()
            cached_spelling = str(cached).swapcase()
            build = root / "build" / "curl-test"
            build.mkdir(parents=True)
            (build / "CMakeCache.txt").write_text(
                "CMAKE_C_COMPILER:FILEPATH=" + cached_spelling + "\n", encoding="utf-8"
            )
            packages = [("curl-test", "unused", "unused", ["-DCURL_USE_SCHANNEL=ON"])]
            with patch.object(windows_deps, "DEPS", root), \
                    patch.object(windows_deps, "PACKAGES", packages), \
                    patch.object(windows_deps, "source", return_value=root / "source"), \
                    patch.object(windows_deps.shutil, "which", return_value=str(compiler)), \
                    patch.object(windows_deps.subprocess, "run") as run, \
                    patch.object(sys, "argv", ["windows_deps.py"]), \
                    redirect_stdout(io.StringIO()):
                windows_deps.main()
            return run.call_args_list[0].args[0], cached_spelling

    def test_same_compiler_preserves_cached_spelling(self):
        command, cached = self.configure(False)
        self.assertIn("-DCMAKE_C_COMPILER=" + cached, command)
        self.assertNotIn("--fresh", command)
        self.assertIn("-DCURL_USE_SCHANNEL=ON", command)

    def test_changed_compiler_starts_fresh_with_all_options(self):
        command, cached = self.configure(True)
        self.assertNotIn("-DCMAKE_C_COMPILER=" + cached, command)
        self.assertIn("--fresh", command)
        self.assertIn("-DCURL_USE_SCHANNEL=ON", command)
        self.assertIn("-DCMAKE_POLICY_VERSION_MINIMUM=3.5", command)


if __name__ == "__main__":
    unittest.main()
