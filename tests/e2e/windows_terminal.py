"""Native REPL driver using the shared C pseudoconsole fixture."""

from __future__ import annotations

import json
import os
import subprocess
import time
from pathlib import Path

import harness


class WindowsTerminal:
    def __init__(
        self,
        script: str,
        extra_env: dict[str, str] | None = None,
        args: list[str] | None = None,
        home: Path | None = None,
        workdir: Path | None = None,
    ):
        driver = os.environ.get("HAX_WINDOWS_TERMINAL_DRIVER")
        if not driver:
            raise RuntimeError("HAX_WINDOWS_TERMINAL_DRIVER must name the native fixture executable")
        if home is None and workdir is None:
            home, workdir = harness.make_home()
        if home is None or workdir is None:
            raise ValueError("home and workdir must be provided together")
        self.home, self.workdir = home, workdir
        self.transcript_path = self.home / "transcript.txt"
        env = harness.mock_env(self.home, script, extra_env)
        env["HAX_TRANSCRIPT"] = str(self.transcript_path)
        self.proc = subprocess.Popen(
            [str(Path(driver).resolve()), str(harness.hax_binary()), *(args or [])],
            cwd=self.workdir,
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            encoding="utf-8",
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        try:
            ready = self._read_reply()
            if not ready.get("ok"):
                raise RuntimeError("could not start native pseudoconsole")
            Path(env["HAX_MOCK_SCRIPT"]).write_text(
                script.replace("{{GATE}}", ready["gate"]), encoding="utf-8"
            )
        except Exception:
            self.close()
            raise

    def _read_reply(self) -> dict:
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError("native pseudoconsole driver exited without a reply")
        return json.loads(line)

    def _request(self, op: str, **values) -> dict:
        self.proc.stdin.write(json.dumps({"op": op, **values}) + "\n")
        self.proc.stdin.flush()
        result = self._read_reply()
        harness.expect(result.get("ok", False), f"native terminal operation {op}: {values}")
        return result

    def release_gate(self) -> None:
        self._request("release")

    def mark(self) -> None:
        self._request("mark")

    def type(self, text: str) -> None:
        encoded = text.encode("utf-16-le")
        for offset in range(0, len(encoded), 2):
            unit = int.from_bytes(encoded[offset : offset + 2], "little")
            self._request("key", key=0xE7, utf16=unit)

    def send(self, name: str) -> None:
        keys = {
            "Enter": (0x0D, 0x0D, 0),
            "Escape": (0x1B, 0x1B, 0),
            "Left": (0x25, 0, 0),
            "Right": (0x27, 0, 0),
            "Backspace": (0x08, 0x08, 0),
            "C-d": (ord("D"), 4, 8),
            "C-c": (ord("C"), 3, 8),
        }
        key, utf16, controls = keys[name]
        self._request("key", key=key, utf16=utf16, controls=controls)

    def expect(self, text: str, timeout_ms: int = 3000) -> None:
        self._request("expect", text=text, timeout_ms=timeout_ms)

    def wait_for_prompt_after(self, text: str) -> None:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            output = self._request("output")["text"]
            position = output.rfind(text)
            if position >= 0 and output.rfind("\u276f") >= position + len(text):
                return
            time.sleep(0.01)
        harness.expect(False, f"native prompt did not return after {text!r}: {output}")

    def wait_exit(self) -> int:
        return self._request("wait")["exit_code"]

    def close(self) -> None:
        if self.proc.stdin and not self.proc.stdin.closed:
            self.proc.stdin.close()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
        self.proc.stdout.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()
