#!/usr/bin/env python3
"""Prepare build dependencies on Windows or delegate to the platform package manager."""

import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("extras", nargs="*", choices=("ci", "tests", "lint"))
    args = parser.parse_args()
    os.chdir(ROOT)
    if os.name == "nt":
        command = [sys.executable, "scripts/windows_deps.py"]
    else:
        command = ["sh", "scripts/install_deps.sh", *args.extras]
    try:
        subprocess.run(command, check=True)
        if os.name == "nt" and "lint" in args.extras:
            print("Lint also requires LLVM's clang-format, clang-tidy, and run-clang-tidy on PATH.")
    except OSError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        return error.returncode if error.returncode > 0 else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
