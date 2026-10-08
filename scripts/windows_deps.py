#!/usr/bin/env python3
"""Build the existing libcurl/Jansson dependencies with native MinGW and Windows TLS.

Requires Python, CMake, Ninja, and gcc on PATH. Everything is installed beneath the repository's
build-windows-deps directory. System installations and PATH are left untouched.
"""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parent.parent
DEPS = ROOT / "build-windows-deps"
PACKAGES = [
    (
        "curl-8.22.0",
        "https://curl.se/download/curl-8.22.0.zip",
        "f9ec970e52124e494606209e6bc0e985c623b428796641742f60c5b56931aaee",
        [
            "-DBUILD_SHARED_LIBS=OFF",
            "-DBUILD_CURL_EXE=OFF",
            "-DBUILD_TESTING=OFF",
            "-DCURL_USE_SCHANNEL=ON",
            "-DCURL_USE_OPENSSL=OFF",
            "-DCURL_USE_LIBPSL=OFF",
            "-DCURL_ZLIB=OFF",
            "-DCURL_BROTLI=OFF",
            "-DCURL_ZSTD=OFF",
            "-DUSE_NGHTTP2=OFF",
            "-DCURL_DISABLE_LDAP=ON",
            "-DCURL_DISABLE_LDAPS=ON",
        ],
    ),
    (
        "jansson-2.14.1",
        "https://github.com/akheron/jansson/archive/refs/tags/v2.14.1.zip",
        "eaf704f7563cfa47bf78616e2a4dd0ff46000937f5178cfceba2d17a88efea4e",
        [
            "-DJANSSON_BUILD_SHARED_LIBS=OFF",
            "-DJANSSON_BUILD_DOCS=OFF",
            "-DJANSSON_WITHOUT_TESTS=ON",
            "-DJANSSON_EXAMPLES=OFF",
        ],
    ),
]


def source(name: str, url: str, expected_digest: str) -> Path:
    downloads = DEPS / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / (name + ".zip")
    if not archive.exists():
        temporary = archive.with_suffix(".part")
        with urllib.request.urlopen(url, timeout=30) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        temporary.replace(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != expected_digest:
        raise RuntimeError(f"archive checksum mismatch: {archive}")
    target = DEPS / "sources"
    target.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as package:
        roots = {Path(entry.filename).parts[0] for entry in package.infolist()}
        if roots != {name}:
            raise RuntimeError(f"unexpected archive root: {roots}")
        for entry in package.infolist():
            destination = (target / entry.filename).resolve()
            if not destination.is_relative_to(target.resolve()):
                raise RuntimeError(f"archive entry escapes source directory: {entry.filename}")
        marker = target / name / ".extracted.sha256"
        if not marker.exists() or marker.read_text().strip() != digest:
            package.extractall(target)
            marker.write_text(digest + "\n", encoding="ascii")
    print(f"{name} source SHA256: {digest}", flush=True)
    return target / name


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download-only", action="store_true")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("this dependency setup is for native Windows")
    tools = {name: shutil.which(name) for name in ("gcc", "cmake", "ninja")}
    tools = {name: str(Path(path).resolve()) if path else None for name, path in tools.items()}
    if not args.download_only and not all(tools.values()):
        parser.error("gcc, cmake, and ninja must be on PATH")
    for name, url, digest, options in PACKAGES:
        directory = source(name, url, digest)
        if args.download_only:
            continue
        build = DEPS / "build" / name
        compiler = tools["gcc"]
        fresh = []
        cache = build / "CMakeCache.txt"
        if cache.is_file():
            for line in cache.read_text(encoding="utf-8").splitlines():
                if line.startswith("CMAKE_C_COMPILER:"):
                    cached = line.partition("=")[2]
                    if Path(cached).is_file() and os.path.samefile(cached, compiler):
                        # CMake compares spellings and resets all options on a compiler change.
                        compiler = cached
                    else:
                        fresh = ["--fresh"]
        subprocess.run(
            [
                tools["cmake"], *fresh, "-S", str(directory), "-B", str(build), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
                "-DCMAKE_C_COMPILER=" + compiler,
                "-DCMAKE_MAKE_PROGRAM=" + tools["ninja"],
                "-DCMAKE_C_COMPILER_LAUNCHER=",
                "-DCMAKE_INSTALL_PREFIX=" + str(DEPS / "prefix"),
                *options,
            ],
            check=True,
        )
        subprocess.run([tools["cmake"], "--build", str(build), "--parallel", "4"], check=True)
        subprocess.run([tools["cmake"], "--install", str(build)], check=True)
    if args.download_only:
        print(f"Windows dependency sources downloaded under {DEPS / 'sources'}", flush=True)
    else:
        print(f"Windows dependencies available under {DEPS / 'prefix'}", flush=True)


if __name__ == "__main__":
    main()
