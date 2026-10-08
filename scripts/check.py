#!/usr/bin/env python3
"""Portable build, test, lint, and install entry point with compact diagnostics."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from filter_clang_tidy import filter_output

ROOT = Path(__file__).resolve().parent.parent
PRESETS = {
    "build": [],
    "build-asan": ["-Db_sanitize=address,undefined"],
    "build-tsan": ["-Db_sanitize=thread"],
    "build-release": ["--buildtype=release"],
}


def tool(name: str) -> str:
    found = shutil.which(name)
    if not found and name == "run-clang-tidy":
        for directory in os.get_exec_path():
            for filename in (name, name + ".py"):
                candidate = Path(directory) / filename
                if candidate.is_file():
                    found = str(candidate)
                    break
            if found:
                break
    if not found and sys.platform == "darwin" and shutil.which("brew"):
        prefix = subprocess.check_output(["brew", "--prefix", "llvm"], text=True).strip()
        candidate = Path(prefix) / "bin" / name
        if candidate.is_file():
            found = str(candidate)
    if not found:
        raise RuntimeError(f"{name} not found on PATH; see README.md")
    return found


def relay(output: str, build: Path) -> str:
    output = re.sub(r"\x1b\[[0-9;]*[mK]", "", output)
    lines = []
    relative_root = os.path.relpath(ROOT, build).replace("\\", "/") + "/"
    for line in output.splitlines(keepends=True):
        if line.startswith(("HAX_NINJA_STATUS ", "ninja: Entering directory ",
                            "ninja: entering directory ", "ninja: no work to do",
                            "ninja: nothing to do")):
            continue
        if line.startswith(relative_root):
            line = line[len(relative_root):]
        lines.append(line)
    return "".join(lines)


def captured(command: list[str], build: Path, *, tidy: str | None = None,
             quiet: bool = False) -> None:
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding="utf-8", errors="replace")
    output = result.stdout
    if tidy:
        output = "".join(filter_output(output.splitlines(keepends=True), tidy))
    if result.returncode or not quiet:
        sys.stdout.write(relay(output, build))
    if result.returncode:
        raise subprocess.CalledProcessError(result.returncode, command)


def setup(build: Path, meson: list[str]) -> None:
    if (build / "meson-private" / "coredata.dat").is_file():
        return
    if build.parent != ROOT or build.name not in PRESETS:
        raise RuntimeError(f"build dir '{build}' is not configured; run meson setup first")
    options = PRESETS[build.name].copy()
    if os.name == "nt":
        os.environ.setdefault("CC", tool("gcc"))
        prefix = ROOT / "build-windows-deps" / "prefix"
        if prefix.is_dir():
            options.append("-Dcmake_prefix_path=" + str(prefix))
    captured([*meson, "setup", str(build), *options], build, quiet=True)
    print(f"setup OK ({build.name})")


def lint(build: Path, meson: list[str]) -> None:
    clang_format = tool("clang-format")
    clang_tidy = tool("clang-tidy")
    run_clang_tidy = tool("run-clang-tidy")
    if sys.platform == "darwin" and not os.environ.get("SDKROOT"):
        os.environ["SDKROOT"] = subprocess.check_output(
            ["xcrun", "--show-sdk-path"], text=True).strip()
    for directory in (ROOT / "src", ROOT / "tests"):
        sources = sorted(str(path) for path in directory.rglob("*")
                         if path.suffix in (".c", ".h"))
        # Stay below Windows' command-line length limit.
        for offset in range(0, len(sources), 40):
            captured([clang_format, "--dry-run", "--Werror", "--ferror-limit=1",
                      *sources[offset:offset + 40]], build)
    captured([sys.executable, "scripts/lint_style.py"], build)
    setup(build, meson)
    captured([tool("ninja"), "-C", str(build), "build.ninja"], build, quiet=True)
    extra = []
    if os.name == "nt":
        compilers = json.loads(subprocess.check_output(
            [*meson, "introspect", "--compilers", str(build)], text=True))
        compiler = compilers["host"]["c"]["exelist"][0]
        target = subprocess.check_output([compiler, "-dumpmachine"], text=True).strip()
        kernel32 = subprocess.check_output(
            [compiler, "-print-file-name=libkernel32.a"], text=True).strip()
        include = Path(kernel32).parent.parent / "include"
        if not include.is_dir():
            raise RuntimeError(f"MinGW headers not found beside {kernel32}")
        extra = [f"-extra-arg=--target={target}", f"-extra-arg=-isystem{include}"]
    captured([sys.executable, run_clang_tidy, "-clang-tidy-binary", clang_tidy,
              "-quiet", "-p", str(build), *extra], build, tidy=clang_tidy, quiet=True)
    print("lint OK")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("build", "test", "lint", "install"))
    parser.add_argument("names", nargs="*", help="selected test names")
    parser.add_argument("--build-dir", default=os.environ.get("BUILD_DIR", "build"))
    args = parser.parse_args()
    if args.names and args.action != "test":
        parser.error("test names are only valid with test")
    os.chdir(ROOT)
    os.environ["NINJA_STATUS"] = "HAX_NINJA_STATUS "
    build = Path(args.build_dir).resolve()
    try:
        meson = [tool("meson")]
        if args.action == "lint":
            lint(build, meson)
            return 0
        setup(build, meson)
        captured([tool("ninja"), "-C", str(build)], build)
        print("build OK" + (f" ({build.name})" if build.name != "build" else ""), flush=True)
        if args.action == "test":
            captured([*meson, "test", "-C", str(build), "--no-rebuild", "-q",
                      "--print-errorlogs", *args.names], build)
        elif args.action == "install":
            subprocess.run([*meson, "install", "-C", str(build)], check=True)
    except (RuntimeError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        return error.returncode if error.returncode > 0 else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
