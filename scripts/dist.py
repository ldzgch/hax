#!/usr/bin/env python3
"""Archive tracked sources and test the extracted distribution."""

import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

from build import ROOT, VERSION


def distribution(build: Path, jobs: int):
    build.mkdir(parents=True, exist_ok=True)
    paths = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=ROOT).decode().split("\0")
    archive = build / f"hax-{VERSION}.tar.xz"
    with tarfile.open(archive, "w:xz") as package:
        for name in paths:
            path = ROOT / name
            if name and path.is_file():
                package.add(path, arcname=f"hax-{VERSION}/{name}", recursive=False)
    with tempfile.TemporaryDirectory() as temporary:
        with tarfile.open(archive) as package:
            package.extractall(temporary)
        env = os.environ.copy()
        if os.name == "nt":
            env.setdefault("HAX_DEPS_PREFIX", str(ROOT / "build-windows-deps/prefix"))
        subprocess.run([sys.executable, "scripts/check.py", "test", "-j", str(jobs)],
                       cwd=Path(temporary) / f"hax-{VERSION}", check=True, env=env)
    print(f"dist OK ({archive})")
