<div align="center">

# hax

**A minimalist, terminal-native coding agent written in C.**

<img src="./docs/screenshot.png" width="720"
     alt="hax answering a question about its own source using a local llama.cpp model">

</div>

## Key features

- **Lightweight by design** — A single native C binary with a small dependency set.
  Starts instantly and uses only a few MB of memory, leaving more RAM for local models.
- **Local models are first-class** — Start `llama-server -m [model].gguf`, then
  `hax --provider llama.cpp`, and hax auto-discovers the model and runtime capabilities.
  No custom provider config block is needed for the default setup.
- **Respects your terminal** — Streaming Markdown and live tool output, reflowed for display
  in the terminal. Only redraws the current streaming line or the input area, native scrollback
  is preserved. Does not take over or mess with your terminal.
- **Inspectable** — See exactly what was sent to the model and what it replied in a usable
  transcript view (Ctrl+T). Optionally collect a detailed wire protocol trace.
- **Broad provider support** — OpenAI (+compatible), Anthropic (+compatible), Codex (via a ChatGPT
  subscription), OpenRouter, OpenCode Zen/Go, DeepSeek, llama.cpp, Ollama, and custom endpoints.
- **Well-behaved Unix tool** — XDG paths, clean stdout in `-p` one-shot mode with resume hints on
  stderr, plain-text config and session files, composition via subprocesses instead of plugins.

## Target audience

Developers who live in the terminal, run local models, audit what their tools do, package
software for distros, or run agents where resources are scarce. If you want MCP marketplaces, a
plugin runtime, IDE panels, or per-command permission prompts, other agents build exactly that —
hax deliberately doesn't, and [docs/philosophy.md](./docs/philosophy.md) explains each omission
and the pattern that covers the need.

If "fancy new AI tech in an old-school minimalist package" sounds like your vibe, you might
like this.

## Install

hax runs on Linux, macOS, FreeBSD, OpenBSD, and native Windows 10 (1809+) / Windows 11.
The BSDs and Windows build from source; WSL is also supported.

With [Homebrew](https://brew.sh) (macOS or Linux):

```sh
brew install oleksandrchekhovskyi/hax/hax
```

On Arch Linux, hax is in the [AUR](https://aur.archlinux.org/packages/hax) as `hax`.

On any Linux distribution, download the prebuilt static binary for your architecture (x86_64 or
aarch64) from the [latest release](https://github.com/OleksandrChekhovskyi/hax/releases/latest),
then unpack the `hax` binary into any directory on your `PATH`.

### From source

The build and test commands are the same in POSIX shells, PowerShell, and cmd. Use Python 3.10+
(`python3` below; on Windows, use `python`):

```sh
git clone https://github.com/OleksandrChekhovskyi/hax.git
cd hax
python3 scripts/install_deps.py
python3 scripts/check.py build
python3 scripts/check.py test
python3 scripts/check.py install   # optional; uses Meson's install prefix
```

On Unix, the dependency installer uses the system package manager for a C compiler, `libcurl`,
`jansson`, Meson, Ninja, and `pkg-config`, plus optional `fzf` for `@file` completion. It supports
Debian/Ubuntu, Fedora, Arch, openSUSE, Alpine, macOS, FreeBSD, and OpenBSD. Source builds link
against system libraries. `python3 scripts/install_deps.py tests` also installs `tmux` for the
interactive tests; `lint` installs LLVM where supported.

On Windows, first install [Git for Windows](https://git-scm.com/download/win), native Python,
Meson (`python -m pip install meson`), CMake 3.24+, Ninja, and a **MinGW-w64 GCC toolchain with POSIX
threads**. Put `gcc`, `cmake`, and `ninja` on `PATH` in the shell you build from; MSVC is not
supported. For example, MSYS2's UCRT64 toolchain supplies these with
`pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`;
then add `C:\msys64\ucrt64\bin` to your native shell's `PATH`. Use native Python, not MSYS Python.
The dependency installer downloads checksum-verified libcurl/Jansson sources and builds them under
`build-windows-deps/`; the build runner finds this prefix automatically. Windows builds use static
libraries and Windows TLS/certificate roots, and tests use a native pseudoconsole instead of tmux.
Run `build/hax.exe` from Windows Terminal; Bash tools use Git Bash, not WSL or PowerShell.

On Unix, `make`, `make tests`, `make lint`, and `make install` remain shortcuts for the shared
runner. `make symlink` links the development binary into `~/.local/bin`. For selected tests or
another build preset on any platform:

```sh
python3 scripts/check.py test tools/read tools/write
python3 scripts/check.py build --build-dir build-release
python3 scripts/check.py test --build-dir build-asan
python3 scripts/check.py lint
```

Sanitizer presets require compiler/runtime support (the documented MinGW build does not supply
ASan/TSan). Lint requires `clang-format`, `clang-tidy`, and `run-clang-tidy` on `PATH`; the Windows
lint gate currently reports MinGW header-attribution and Windows-specific findings and is not clean.

The examples below use `hax` as if it is on `PATH`; after a plain build, use `./build/hax`
(`./build/hax.exe` on Windows).

## Connect a provider

The easiest first run is interactive: start `hax`, then use `/provider` to see available providers
and choose a model. hax remembers interactive provider, model, and effort selections.

| Provider | Setup |
| --- | --- |
| `codex` | Run `/login` inside hax. |
| `openai` | Set `OPENAI_API_KEY`. |
| `anthropic` | Set `ANTHROPIC_API_KEY`. |
| `openrouter` | Set `OPENROUTER_API_KEY`. |
| `opencode-zen` / `opencode-go` | Set `OPENCODE_API_KEY`. |
| `deepseek` | Set `DEEPSEEK_API_KEY`. |
| `llama.cpp` | Run `llama-server`. |
| `ollama` | Run `ollama serve`. |
| Compatible or custom endpoint | See [docs/providers.md](./docs/providers.md). |

## Quick start

Run hax from the project directory you want it to work in:

```sh
hax                         # interactive REPL
hax -p "list TODOs"         # run one prompt and print the final answer
printf "explain x" | hax -p # read the prompt from stdin
hax -c                      # continue the latest session for this directory
hax --resume                # pick a past session for this directory
hax --resume=ID -p "next"   # resume a specific session in one-shot mode
```

Run `hax --help` for CLI usage. In the REPL, type `/help` for slash commands and shortcuts.
See [docs/usage.md](./docs/usage.md) for more detailed usage documentation.

## Configuration

Configuration is optional. Interactive selections are remembered separately from your config file;
CLI flags apply only to the current run, and environment variables are useful for shells and
scripts. Resumed conversations restore their own provider, model, effort, and preset unless a
CLI selection flag overrides them.

See [docs/configuration.md](./docs/configuration.md) for the file format, resolution order, presets,
and the setting reference.

## More docs

- [docs/sessions.md](./docs/sessions.md) — the session-file format and the `hax --json` stream.
- [docs/debugging.md](./docs/debugging.md) — trace/transcript logs, mock provider, and demo scripts.
- [CONTRIBUTING.md](./CONTRIBUTING.md) — how to propose, prepare, and submit a change.

## License

MIT.
