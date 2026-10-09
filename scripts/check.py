#!/usr/bin/env python3
"""Portable build, test, lint, and install entry point with compact diagnostics."""

from concurrent.futures import ThreadPoolExecutor
import fnmatch
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from filter_clang_tidy import filter_output
from build import Build, WINDOWS

ROOT = Path(__file__).resolve().parent.parent
PRESETS = {"build": "debug", "build-asan": "asan", "build-tsan": "tsan",
           "build-release": "release"}


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
        if line.startswith(relative_root):
            line = line[len(relative_root):]
        lines.append(line)
    return "".join(lines)


def captured(command: list[str], build: Path, *, tidy: str | None = None,
             quiet: bool = False, timeout: int | None = None) -> None:
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding="utf-8", errors="replace", timeout=timeout)
    output = result.stdout
    if tidy:
        output = "".join(filter_output(output.splitlines(keepends=True), tidy))
    if result.returncode or not quiet:
        sys.stdout.write(relay(output, build))
    if result.returncode:
        raise subprocess.CalledProcessError(result.returncode, command)


def lint(build: Path, builder: Build) -> None:
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
    builder.build(tests=True)
    extra = []
    if os.name == "nt":
        compiler = builder.cc[0]
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


def test_commands(build, binary, registered):
    commands = {name: [str(path)] for name, path in registered.items()}
    for path in sorted((ROOT / "tests/scripts").glob("test_*.py")):
        commands["scripts/" + path.stem[5:]] = [sys.executable, str(path)]
    scenarios = ["oneshot", "repl_windows"] if WINDOWS else [
        "oneshot", "repl_interrupt", "repl_smoke"]
    for name in scenarios:
        commands["e2e/" + name] = [sys.executable, str(ROOT / "tests/e2e" / ("test_" + name + ".py"))]
    return commands


def run_tests(commands, names, jobs, build):
    selected = set()
    for pattern in names or ["*"]:
        matches = {name for name in commands if fnmatch.fnmatchcase(name, pattern)}
        if not matches:
            raise RuntimeError(f"no tests match '{pattern}'")
        selected.update(matches)
    def run(name):
        try:
            captured(commands[name], build, timeout=60)
            return True
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
            print(f"FAIL: {name}", flush=True)
            return False
    with ThreadPoolExecutor(jobs) as pool:
        results = list(pool.map(run, sorted(selected)))
    if not all(results):
        raise RuntimeError(f"{results.count(False)} of {len(results)} tests failed")
    print(f"test OK ({len(results)} tests)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("build", "test", "lint", "install", "symlink", "clean", "dist"))
    parser.add_argument("names", nargs="*", help="test names or glob patterns")
    parser.add_argument("--build-dir", default=os.environ.get("BUILD_DIR", "build"))
    parser.add_argument("--mode", choices=("debug", "release", "asan", "tsan"))
    parser.add_argument("--static", action="store_true")
    parser.add_argument("-j", "--jobs", type=int, default=min(os.cpu_count() or 1, 8))
    parser.add_argument("--prefix", default=os.environ.get("PREFIX", "/usr/local"))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    if args.names and args.action != "test":
        parser.error("test names are only valid with test")
    os.chdir(ROOT)
    build = Path(args.build_dir).resolve()
    try:
        if args.action == "clean":
            if build == ROOT or ROOT.is_relative_to(build) or not (build / "version.h").is_file():
                raise RuntimeError("refusing to clean a directory not created by the build runner")
            shutil.rmtree(build)
            return 0
        if args.action == "dist":
            from dist import distribution
            distribution(build, args.jobs)
            return 0
        builder = Build(build, args.jobs, args.mode or PRESETS.get(build.name, "debug"), args.static)
        if args.action == "lint":
            lint(build, builder)
            return 0
        binary, registered = builder.build(tests=args.action == "test")
        print("build OK", flush=True)
        if args.action == "test":
            os.environ["HAX_BIN"] = str(binary)
            os.environ["HAX_WINDOWS_TERMINAL_DRIVER"] = str(build / "windows_terminal_driver.exe")
            os.environ["TSAN_OPTIONS"] = "atexit_sleep_ms=0:" + os.environ.get("TSAN_OPTIONS", "")
            run_tests(test_commands(build, binary, registered), args.names, args.jobs, build)
        elif args.action in ("install", "symlink"):
            destination = (Path.home() / ".local" if args.action == "symlink" else
                           Path(os.environ.get("DESTDIR", "")) / args.prefix.lstrip("/"))
            if args.action == "install" and not os.environ.get("DESTDIR"):
                destination = Path(args.prefix)
            destination = destination / "bin" / binary.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            if args.action == "symlink":
                if destination.exists() and not destination.is_symlink():
                    raise RuntimeError(f"refusing to replace {destination}")
                destination.unlink(missing_ok=True)
                destination.symlink_to(binary)
            else:
                shutil.copy2(binary, destination)
            print(destination)
    except (RuntimeError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        return error.returncode if error.returncode > 0 else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
