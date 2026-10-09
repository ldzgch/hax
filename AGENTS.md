# AGENTS.md

Guidance for AI agents working in this repository. Keep this file high-level: commands,
architecture seams, and durable conventions belong here; module-level details belong in code or
headers.

## Build, test, lint

```sh
python3 scripts/check.py build            # quiet; sets up build/ on first run
python3 scripts/check.py test             # build + all tests (unit + e2e)
python3 scripts/check.py lint             # clang-format + style script + clang-tidy
python3 scripts/check.py test <name>...   # build + selected tests
```

Use `python` on Windows. `scripts/check.sh` is a POSIX convenience wrapper.
The Python runner invokes the C compiler and archiver directly, relays diagnostics, and prints
compact confirmations. `-j` sets parallelism; test names accept glob patterns.

`python3 scripts/check.py lint` runs clang-format, project style checks, and clang-tidy.
Run `clang-format -i` on any C source/header you touch before reporting done.

`--build-dir` (or `BUILD_DIR`) selects the build directory. Presets select debug (`build`),
release (`build-release`), address/undefined sanitizers (`build-asan`), or thread sanitizer
(`build-tsan`). Other directory names default to debug; `--mode` overrides the preset.
Sanitizers require compiler/runtime support and are unavailable in the documented MinGW toolchain.

```sh
python3 scripts/check.py test --build-dir build-asan
python3 scripts/check.py test --build-dir build-tsan <name>
python3 scripts/check.py build --build-dir build-release
```

## Manual checks and debugging

[`docs/debugging.md`](docs/debugging.md) covers the knobs in full. The ones an agent reaches for
most:

- `HAX_PROVIDER=mock` runs the scripted/mock provider. Pair with `HAX_MOCK_SCRIPT=path` or
  `scripts/stream_demo.py` for visual checks without a live LLM. Mock runs record no session by
  default: add `HAX_NO_SESSION=0` to check resume, `/session`, or anything else that reads a
  session file, and point `XDG_STATE_HOME` at a scratch directory to keep those files out of the
  user's own session list.
- `HAX_TRACE=path` logs HTTP/SSE traffic with auth redacted.
- `HAX_TRANSCRIPT=path` logs the model-facing transcript, including tools and results.

The REPL prompt and the pickers need a real tty, so they can't be checked by piping stdin.
Use tmux rather than hand-rolled pty scripts — send keys, capture the pane, read the result:

```sh
tmux new-session -d -s haxtest -x 110 -y 32 'HAX_PROVIDER=mock ./build/hax'
tmux send-keys -t haxtest '/model' Enter   # keys; Enter/C-u/Escape as named keys
tmux capture-pane -t haxtest -p            # pane text, escapes already resolved
tmux kill-session -t haxtest
```

Scope cleanup to exactly what you started. The user may be working inside tmux, and this agent
may itself be running inside hax, so anything that matches by name takes their session down
along with the one under test: no `kill-server`, and no `pkill hax` / `killall hax` /
`pkill -f hax`. Kill the session you named, or the PID you captured at launch.

## Architecture

hax is a single-binary REPL:

`input → build context → provider streams events → assemble turn → dispatch tools → loop`

The stable extension seams are `src/provider.h` and `src/tool.h`.

Terminology:

- A **turn** is one provider `stream()` round-trip producing one assistant response and optional
  tool calls.
- A **user turn** is one user prompt plus every spawned turn until the model stops requesting
  tools.
- `ITEM_TURN_BOUNDARY` separates consecutive turns inside one user turn.

Core boundaries:

- Keep shared primitives in small focused modules; there is no catch-all `util`. Extend the
  module whose contract a new helper fits, or give it a focused module of its own.
- The canonical conversation state is the flat, provider-independent `struct item` log owned by
  `struct agent_session`. Compaction appends a summary seed without deleting prior history; build
  model-visible windows with `agent_session_context()` rather than slicing the raw log.
- Provider adapters own native API protocols and wire JSON; shared transport owns HTTP/SSE
  mechanics. Adapters serialize `struct context` and emit provider-independent
  `struct stream_event`; agent behavior must not depend on native response shapes.
- `src/turn.{c,h}` is a pure state machine from borrowed stream events to owned conversation items.
  Keep I/O and presentation out of it.
- Behavior shared by the interactive and one-shot frontends belongs below `src/agent.c` and
  `src/oneshot.c`, primarily in `agent_core` and `agent_loop`. Frontends supply presentation and
  cancellation through hooks rather than duplicating the continuation loop.
- Declare user-facing settings in the config registry and consume them by canonical key. Direct
  environment reads are for startup/bootstrap decisions, conventional process environment, or
  deliberately environment-only secrets.
- Process-wide config and live provider state are foreground-thread state. Resolve config and
  prepare owned worker inputs before spawning background work. Use `system/bg_job` for ordinary
  cooperative jobs, and join every worker before destroying state it may access or tearing down
  global libcurl state.
- `model_meta` is the resolved view for live provider/model capability decisions; `catalog` is its
  lower-level metadata and pricing source. Cost estimation belongs in `agent_usage` (one response)
  and `agent_stats` (a whole conversation, derived from its items), not provider adapters.
- `transcript` renders the model-facing conversation, `history` reconstructs the user-facing
  display, and `session` is structured resumable persistence. Do not substitute one representation
  for another.
- Interactive conversation rendering flows through `render_ctx` and `disp`; live indicators are
  explicit direct-terminal owners. Use `terminal/ansi.h` for fixed controls and semantic `theme`
  roles for colors. Settle cursor-addressed output with `vt_resolve` before writing it to a pager,
  file, or other non-terminal sink.

Extension workflows:

- Every provider is one `struct provider_def` (`providers/registry.h`): shipped defs live in
  `registry.c`'s `DEFS[]` table at their autoselect priority, and config.json `providers.*`
  blocks overlay shipped defs or add data-only ones. Prefer pure data; add capability hooks
  (`parse_model`, `probe_model`, `query_usage`, ...) only for genuinely provider-specific
  behavior, and a `construct` override only when construction itself needs code. Hook sources are
  discovered automatically; a user-visible endpoint variant should be config, not C.
- A compiled-in tool needs a source under `src/`, an exported `const struct tool` declaration
  in `tool.h`, and an entry in `agent_core.c`'s `TOOLS[]`.
- Keep protocol translation and terminal-independent state machines pure and separately testable;
  do not require HTTP or a TTY to test parsing and state transitions.

## Tests

Unit tests are plain C binaries using `tests/harness.h` (`EXPECT`, `EXPECT_STR_EQ`, `T_SKIP`,
`T_REPORT`). Create scratch directories with the harness's `t_tempdir()`, which removes them
at process exit; raw `mkdtemp` in tests fails lint. Tests named `test_*.c` under `tests/` are discovered
automatically. Test
names are path-derived: `tools/test_read.c` becomes `tools/read`, and `test_buf.c` becomes
`buf`.

End-to-end scenarios follow the same conventions in Python: standalone scripts under
`tests/e2e/`, registered in `scripts/check.py`. They run the built binary
hermetically against inline mock-provider scripts via `tests/e2e/harness.py`; its docstrings are
the how-to, and `scripts/mock/` holds the fixtures for manual checks instead. REPL scenarios drive
the binary through tmux with its `Terminal`; a change to REPL layout or terminal handling comes
with one.

Where a test goes:

- A test file mirrors the production module it exercises, and behavior is tested in the module
  that owns it. When a change extends a shared module and adds a consumer of the extension, test
  the extension in the shared module's file with the smallest input that exercises it, and test
  only the consumer's own code in the consumer's file. Shipped data tables and their ordering are
  tested where the table lives.
- Use the lowest level that can observe the behavior: a pure function over an assembled object,
  an object against a fake peer (loopback socket, scratch directory, scripted stream) over the
  built binary. Reserve `tests/e2e/` for behavior only visible from the binary: CLI flags and
  exit codes, stdout and stderr shape, signal handling, terminal interaction. Drive scenarios
  with the in-process mock provider by default; stand up a fake endpoint (for example
  `scripts/mock_openai_server.py`) only when the behavior under test depends on the network
  path, such as a picker over a live listing or a retry indicator, not to inspect the request
  the binary sent.
- Do not assert the same behavior at two levels. Once a unit test pins it, an e2e scenario that
  repeats the check adds run time without adding signal.
- Before writing a fixture (loopback server, fake command on `PATH`, scripted stream, scratch
  tree), look for one in sibling test files or the `test_support` library and reuse or extract it
  rather than copying it. Shared fixtures are a header plus a `.c` beside it (`tests/harness.h`,
  `tests/loopback.h`, `tests/tools/bash_fixtures.h`), automatically built into the test-support archive.

## Code style and conventions

- C11, `-Wall -Wextra -Wpedantic`, with the platform feature defines in `scripts/build.py`.
- Linux-kernel-inspired userspace style: snake_case, no typedef'd structs, function braces on
  their own line, control-flow braces on the same line.
- Every source file starts with `/* SPDX-License-Identifier: MIT */`.
- Use plain `malloc`/`calloc`/`free`; `xmalloc`/`xstrdup`/`xasprintf` in `src/xalloc.h` abort on
  OOM. No arenas.
- Use kernel-style goto cleanup for multi-resource functions, with labels in reverse
  acquisition order.
- Always release owned resources on success and all early exits: `json_decref` jansson roots,
  `curl_easy_cleanup` handles, `free` buffers, etc.
- Avoid non-portable kernel idioms: no `likely()`/`unlikely()`, `BUG_ON`, `ERR_PTR`, or
  `kmalloc`. Use `<stdint.h>` types and plain negative-int returns plus `errno`.
- Markdown is hard-wrapped around 100 columns, same as code.
- Before changing code in any language, read [`docs/code-style.md`](docs/code-style.md) and apply
  its general naming, structure, and comment principles using that language's conventions.

## Changelog

Record notable user-facing changes in `CHANGELOG.md` under `[Unreleased]` as part of the change
itself, following the file's Keep a Changelog format.

## Git conventions

Do not create commits or perform any other git history manipulation unless the user explicitly
prompts for it. This includes commands such as `git commit`, `git commit --amend`, `git rebase`,
`git reset`, `git cherry-pick`, and `git merge`.

Do not switch or create branches and do not push anything to remote, unless explicitly prompted.

Commit messages follow these patterns:
- For the subject line, use sentence case with a present-tense verb (e.g., "Add", "Fix").
  Subject line does not end with a period.
- Prefer adding a brief explanatory body after the subject line. Describe why the change was made
  and summarize what changed, while keeping it concise. Explanatory body is free-form.
- For non-trivial commits, write the commit message to a temporary file first to check formatting
  before committing. Aim for approximately 80-90 columns in commit message prose.

## Dependencies

Dependencies are declared in `scripts/build.py`. Keep the footprint small; before adding one, read
[`docs/philosophy.md`](docs/philosophy.md#small-dependency-footprint). Every new dependency must be
in Debian main and either ship with macOS or be available via a single `brew install`. Do not add
GPL libraries.
