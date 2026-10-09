#!/usr/bin/env python3
"""Direct, incremental C builds using only Python and the compiler toolchain."""

from concurrent.futures import ThreadPoolExecutor
import json
import re
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
VERSION = "0.5.0"
WINDOWS = os.name == "nt"


def sources(directory):
    paths = sorted((ROOT / directory).rglob("*.c"))
    pairs = {"fs", "spawn", "interrupt", "clipboard"}
    result = []
    for path in paths:
        stem = path.stem
        if not WINDOWS and (stem.endswith("_win") or stem.startswith("win_")):
            continue
        if WINDOWS and (stem in pairs or stem in {"test_fs", "test_spawn", "test_tempfiles"}
                        or stem.endswith("_posix")):
            continue
        result.append(path)
    return result


class Build:
    def __init__(self, directory, jobs, mode="debug", static=False):
        self.directory = directory
        self.jobs = jobs
        self.static = static
        directory.mkdir(parents=True, exist_ok=True)
        self.cc = shlex.split(os.environ.get("CC", "gcc" if WINDOWS else "cc"))
        self.flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-g",
                      "-O3" if mode == "release" else "-O2", "-D_DARWIN_C_SOURCE",
                      "-iquote", str(ROOT / "src"), "-iquote", str(ROOT / "tests"),
                      "-I", str(directory)]
        self.libs = []
        if sys.platform.startswith("linux"):
            self.flags += ["-D_POSIX_C_SOURCE=200809L", "-D_XOPEN_SOURCE=700"]
        if WINDOWS:
            self.flags += ["-D_WIN32_WINNT=0x0a00", "-DNTDDI_VERSION=0x0a000006",
                           "-D__USE_MINGW_ANSI_STDIO=1"]
            prefix = Path(os.environ.get("HAX_DEPS_PREFIX", ROOT / "build-windows-deps/prefix"))
            self.flags += ["-isystem", str(prefix / "include"), "-DCURL_STATICLIB"]
            self.libs += [str(prefix / "lib/libcurl.a"), str(prefix / "lib/libjansson.a"),
                          "-Wl,-Bstatic", "-lwinpthread", "-Wl,-Bdynamic"]
            self.libs += ["-l" + name for name in (
                "bcrypt", "shell32", "advapi32", "user32", "gdi32", "ole32",
                "windowscodecs", "uuid", "crypt32", "secur32", "ws2_32", "iphlpapi")]
        else:
            pkg = os.environ.get("PKG_CONFIG", "pkg-config")
            if shutil.which(pkg):
                for option, target in (("--cflags", self.flags), ("--libs", self.libs)):
                    command = [pkg, option, *(["--static"] if static else []), "libcurl", "jansson"]
                    target.extend(shlex.split(subprocess.check_output(command, text=True)))
            elif static:
                raise RuntimeError("static dependency discovery requires pkg-config")
            else:
                self.libs += ["-lcurl", "-ljansson"]
                prefix = os.environ.get("HAX_DEPS_PREFIX")
                if prefix:
                    self.flags += ["-isystem", str(Path(prefix) / "include")]
                    self.libs += ["-L" + str(Path(prefix) / "lib")]
            self.flags += ["-pthread"]
            self.libs += ["-pthread", "-lm"]
        self.flags += shlex.split(os.environ.get("CFLAGS", ""))
        self.libs += shlex.split(os.environ.get("LDFLAGS", ""))
        if mode in ("asan", "tsan"):
            sanitizer = "-fsanitize=" + ("address,undefined" if mode == "asan" else "thread")
            self.flags += [sanitizer, "-fno-omit-frame-pointer"]
            self.libs += [sanitizer]
        if static:
            self.libs += ["-static", "-flto=2"]
        version = "v" + VERSION
        if (ROOT / ".git").exists():
            result = subprocess.run(["git", "describe", "--tags", "--always", "--dirty=+"],
                                    cwd=ROOT, capture_output=True, text=True)
            if result.returncode == 0:
                version = result.stdout.strip()
        self.update(directory / "version.h", (ROOT / "src/version.h.in").read_text().replace(
            "@VCS_TAG@", version))
        self.commands = []

    @staticmethod
    def update(path, text):
        if not path.exists() or path.read_text() != text:
            path.write_text(text, encoding="utf-8")

    def run(self, command):
        result = subprocess.run(command, capture_output=True, text=True, errors="replace")
        if result.stdout or result.stderr:
            print(result.stdout + result.stderr, end="", flush=True)
        if result.returncode:
            raise subprocess.CalledProcessError(result.returncode, command)

    def stale(self, output, inputs, command):
        stamp = output.with_suffix(output.suffix + ".cmd")
        if not output.exists() or not stamp.exists() or stamp.read_text() != json.dumps(command):
            return True
        return any(not path.exists() or path.stat().st_mtime_ns > output.stat().st_mtime_ns
                   for path in inputs)

    def compile(self, source):
        output = self.directory / "obj" / source.relative_to(ROOT).with_suffix(".o")
        output.parent.mkdir(parents=True, exist_ok=True)
        command = [*self.cc, *self.flags, "-MMD", "-MF", str(output.with_suffix(".d")),
                   "-c", str(source), "-o", str(output)]
        self.commands.append({"directory": str(ROOT), "file": str(source), "arguments": command})
        # A project header change invalidates all objects; compiler depfiles also cover external
        # headers. This avoids parsing drive-letter colons as Make target separators.
        inputs = [source, *self.headers]
        depfile = output.with_suffix(".d")
        if depfile.exists():
            text = depfile.read_text().replace("\\\n", " ")
            dependencies = text.split(": ", 1)[-1]
            inputs += [Path(word.replace("\\ ", " ")) for word in
                       re.findall(r"(?:\\ |[^\s])+", dependencies)]
        if self.stale(output, inputs, command):
            self.run(command)
            output.with_suffix(".o.cmd").write_text(json.dumps(command))
        return output

    def archive(self, name, objects):
        output = self.directory / name
        command = [os.environ.get("AR", "ar"), "rcs", str(output), *map(str, objects)]
        if self.stale(output, objects, command):
            output.unlink(missing_ok=True)
            self.run(command)
            output.with_suffix(output.suffix + ".cmd").write_text(json.dumps(command))
        return output

    def link(self, name, source, libraries):
        obj = self.compile(source)
        output = self.directory / (name + (".exe" if WINDOWS else ""))
        command = [*self.cc, str(obj), *map(str, libraries), *self.libs, "-o", str(output)]
        if self.stale(output, [obj, *libraries], command):
            self.run(command)
            output.with_suffix(output.suffix + ".cmd").write_text(json.dumps(command))
        return output

    def build(self, tests=False):
        self.headers = [*ROOT.glob("src/**/*.h"), *ROOT.glob("tests/**/*.h"),
                        self.directory / "version.h"]
        with ThreadPoolExecutor(self.jobs) as pool:
            objects = list(pool.map(self.compile, [p for p in sources("src") if p.name != "main.c"]))
            library = self.archive("libhax.a", objects)
            binary = self.link("hax", ROOT / "src/main.c", [library])
            registered = {}
            if tests:
                paths = [p for p in sources("tests") if "e2e" not in p.parts]
                support = [p for p in paths if not p.name.startswith("test_")]
                if WINDOWS:
                    support = [p for p in support if p.name != "harness.c"]
                fixtures = self.archive("libtest_support.a", list(pool.map(self.compile, support)))
                for path in paths:
                    if not path.name.startswith("test_"):
                        continue
                    if WINDOWS and path.name == "test_harness.c":
                        continue
                    relative = path.relative_to(ROOT / "tests")
                    name = str(relative.parent / relative.stem[5:]).replace("\\", "/")
                    registered[name] = self.link("test_" + name.replace("/", "_"), path,
                                                 [fixtures, library])
                if WINDOWS:
                    self.link("windows_terminal_driver", ROOT / "tests/e2e/windows_terminal.c",
                              [fixtures, library])
        self.update(self.directory / "compile_commands.json", json.dumps(self.commands, indent=2))
        return binary, registered
