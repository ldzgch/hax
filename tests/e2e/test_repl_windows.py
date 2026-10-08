#!/usr/bin/env python3
"""Native Windows REPL scenarios against the built binary in an owned pseudoconsole."""

import os
import time

import harness
from windows_terminal import WindowsTerminal


def test_prompt_response_exit():
    with WindowsTerminal("text Hello from native mock\nend-turn\n") as term:
        term.expect("mock-model")
        term.expect("try /help")
        term.mark()
        term.type("hi")
        term.send("Enter")
        term.expect("Hello from native mock")
        term.wait_for_prompt_after("Hello from native mock")
        term.send("C-d")
        harness.expect(term.wait_exit() == 0, "Ctrl-D at an empty native prompt exits 0")


def test_unicode_editing():
    with WindowsTerminal("text Edited input received\nend-turn\n") as term:
        term.expect("try /help")
        term.mark()
        term.type("edit-\u00e9\U0001f600x")
        term.send("Left")
        term.send("Backspace")
        term.type("\u754c")
        term.send("Enter")
        term.wait_for_prompt_after("Edited input received")
        term.send("C-d")
        harness.expect(term.wait_exit() == 0, "Unicode prompt exits normally")
        transcript = term.transcript_path.read_text(encoding="utf-8")
        harness.expect("edit-\u00e9\u754cx" in transcript, "cursor and backspace preserve Unicode input")
        harness.expect("edit-\u00e9\U0001f600x" not in transcript, "only the edited prompt reaches the model")


def test_bash_tool_continuation():
    script = (
        'tool bash {"command":"printf native-tool > repl-output.txt; pwd"}\nend-turn\n'
        "text Tool finished in the REPL\nend-turn\n"
    )
    with WindowsTerminal(script) as term:
        term.expect("try /help")
        term.mark()
        term.type("run the tool")
        term.send("Enter")
        term.wait_for_prompt_after("Tool finished in the REPL")
        harness.expect(
            (term.workdir / "repl-output.txt").read_text() == "native-tool",
            "interactive Bash runs in the REPL work directory",
        )
        term.send("C-d")
        harness.expect(term.wait_exit() == 0, "REPL exits after tool continuation")


def test_picker_cancel_restores_input():
    with WindowsTerminal("text Input after picker\nend-turn\n") as term:
        term.expect("try /help")
        term.mark()
        term.type("/config")
        term.send("Enter")
        term.expect("configuration")
        term.mark()
        term.send("Escape")
        term.expect("\u276f")
        term.mark()
        term.type("after picker")
        term.send("Enter")
        term.wait_for_prompt_after("Input after picker")
        term.send("C-d")
        harness.expect(term.wait_exit() == 0, "picker cancellation returns to editable prompt")


def test_escape_pauses_and_enter_continues():
    script = (
        'tool bash {"command":"printf gate-ready; read -r _ <{{GATE}}"}\nend-turn\n'
        "text Resumed native turn\nend-turn\n"
    )
    with WindowsTerminal(script) as term:
        term.expect("try /help")
        term.mark()
        term.type("go")
        term.send("Enter")
        term.expect("gate-ready")
        term.send("Escape")
        # The tool stays blocked while the asynchronous watcher consumes Escape.
        time.sleep(0.3)
        term.release_gate()
        term.expect("paused \u2014 enter to continue")
        term.mark()
        term.send("Enter")
        term.wait_for_prompt_after("Resumed native turn")
        term.send("C-d")
        harness.expect(term.wait_exit() == 0, "paused native tool turn resumes and exits")


def test_double_escape_aborts_tool():
    script = 'tool bash {"command":"printf gate-ready; read -r _ <{{GATE}}"}\nend-turn\n'
    with WindowsTerminal(script) as term:
        term.expect("try /help")
        term.mark()
        term.type("go")
        term.send("Enter")
        term.expect("gate-ready")
        term.mark()
        term.send("Escape")
        term.send("Escape")
        term.wait_for_prompt_after("[interrupted]")
        term.send("C-d")
        harness.expect(term.wait_exit() == 0, "aborting a blocked Bash job restores the prompt")


if os.name != "nt":
    harness.skip("native Windows pseudoconsole scenarios")
harness.main(globals())
