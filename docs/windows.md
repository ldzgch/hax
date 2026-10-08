# Native Windows development

The native Windows port is in progress. The executable builds and runs one-shot mock-provider
requests, including Bash tools and JSON streaming. The registered native Windows build and test
suite pass; POSIX-only fixtures are selected on POSIX, with Windows-specific counterparts for
native behavior. Pseudoconsole REPL scenarios cover prompt editing, tools, pause/resume, and abort.
Broader manual verification across supported terminal hosts remains part of port validation.

The native target is Windows 10 version 1809 or later. The current toolchain is 64-bit MinGW-w64
GCC, CMake, Ninja, Python, and Meson, available on `PATH`.
Git for Windows supplies Git Bash for shell commands. hax looks beside the `git.exe` found on
`PATH`, then for Bash on `PATH`, then in standard Git installation directories. Windows'
`System32/bash.exe` is a WSL launcher and is excluded.

Run the following from PowerShell in the repository root:

```powershell
python scripts/windows_deps.py
$env:CC = (Get-Command gcc).Source
$dependencyPrefix = (Join-Path (Get-Location) 'build-windows-deps/prefix').Replace('\', '/')
meson setup build-windows-native "-Dcmake_prefix_path=$dependencyPrefix"
meson compile -C build-windows-native hax
.\build-windows-native\hax.exe --help
```

The dependency script downloads hash-pinned curl and Jansson sources and builds static libraries
under `build-windows-deps/prefix`.
It does not install system packages or change the user's `PATH`. The executable links the
toolchain's winpthreads runtime statically and imports only Windows system DLLs. Its notices are
in `LICENSES/winpthreads.txt`. curl uses Schannel for TLS and
the Windows certificate store. `--download-only` fetches and validates the sources without building.

Native helper processes use UTF-16 Windows APIs with UTF-8 arguments at hax's boundaries. Only the
specified standard handles are inherited. Owned commands belong to a Windows job object before
they start, so stopping a command or closing its owner terminates descendants as well. Detached
helpers intentionally have no owned job. Git Bash receives its standard helper directories on
the child `PATH` without changing hax's environment or starting a login shell.

Foreground Bash tools can hand their owned process and output pipe to the background-task registry.
Task completion and shutdown retain the job until output is drained and owned descendants are
terminated. Windows stop requests terminate the whole job immediately; POSIX TERM traps and the
TERM grace window are unavailable through the native Git Bash launcher. Conversation-history
paging captures output in a private temporary file and removes it after reading.

Windows-specific tests cover Unicode filenames and arguments, binary pipe bytes, private ACLs,
file locking, bounded process capture, descendant cleanup, and handle inheritance. Manual checks
across supported terminal hosts remain required before the port is ready for use.

Console input uses Unicode key events, preserving supplementary-plane characters, AltGr, and
modified cursor keys. The Escape watcher is joined before the prompt regains stdin. In headless
runs, Ctrl-C requests an abort and Ctrl-Break requests a pause; repeating Ctrl-C escalates to
termination. Console-control cleanup restores input modes, output modes, and the output code page.
Native terminal tests run in an owned Windows pseudoconsole without opening a visible window.

The shared HTTP loopback test fixture uses the native socket backend. Its standalone native tests
cover split request bodies, scripted replies, held responses, and independent server lifetimes.

The shared test-support library now builds natively. Windows Bash test gates use loopback TCP
redirection instead of POSIX FIFOs, so task tests retain deterministic handoffs and live-output
checks. Native config resolution reads UTF-16 environment values as UTF-8 and distinguishes empty
settings from missing ones. Session writers publish completed records without relying on CRT line
buffering, which Windows does not implement.

Native REPL e2e scenarios use the shared hidden pseudoconsole fixture to submit prompts, edit
Unicode input, cancel the config picker, run Bash tools, pause/resume, and abort blocked commands.
They inspect emitted output and transcript files; they do not reconstruct the whole terminal screen.
The Bash scenarios reuse the shared loopback TCP gate. Run this focused gate from PowerShell:

```powershell
meson compile -C build-windows-native hax windows_terminal_driver test_transcript test_system_fs_win
meson test -C build-windows-native --no-rebuild e2e/repl_windows transcript system/fs_win --print-errorlogs
```

Transcript and HTTP trace files use the UTF-8 native file backend instead of GNU stdio modes.
New files have private permissions and non-inheritable handles. Transcript headers, resets, and
completed turns are flushed so readers see them while the REPL remains open.

Catalog pricing, model metadata, usage, statistics, history, banner, and rendering suites also run
natively. Catalog fixtures share an owned Unicode cache directory, and rendering fixtures capture
binary output without `open_memstream` or CRT `tmpfile`. Bash preprocessing obtains the current
directory through the UTF-8 path backend and recognizes equivalent native drive, UNC, and Git Bash
drive paths. It preserves component case and leaves drive-relative paths and ambiguous shell syntax
unchanged.

Config persistence and preset symlink scenarios run natively, asserting owner-only DACLs in place
of POSIX permission bits. Shared filesystem fixtures create Unicode directories and file/directory
symlinks through native APIs; symlink scenarios skip only when the process lacks the required
privilege. Authentication tests set explicit scratch state paths because changing `HOME` alone does
not override native app-data locations. HTTP provider, stream retry, transport, and certificate
selection suites run against the shared native loopback fixture.

One-shot, CLI, session persistence, and retention suites also run natively. One-shot tests use an
owned pseudoconsole for Ctrl-Break pauses and binary scratch streams for redirected output and
broken-pipe checks. The binary-level one-shot scenario sends Ctrl-C in an owned console, verifies
the interrupted session, and resumes it through the CLI. Session listing and prompt history accept
both native separator spellings while retaining existing session directory hashes.

Read, edit, and write tool suites run natively. Read uses UTF-8 native paths and binary descriptors
for image signatures and text slices, preserving CRLF and Ctrl-Z bytes. A shared fixture supplies
POSIX FIFOs or owned Windows named pipes for special-file rejection tests. POSIX permission-bit
scenarios skip on Windows; native filesystem tests verify ACL preservation separately.

Catalog refresh scenarios and concurrent credential updates launch fresh, owned test processes
instead of requiring `fork()`. Credential tests verify private ACLs and that symlink aliases share
one transaction lock. Native harness checks cover read-only scratch trees, directory symlinks,
and hard links to files outside the tree; cleanup preserves the external file's contents and
read-only attribute. PATH fixtures preserve Unicode values and distinguish empty from unset.

The Git Bash lint runner accepts `python3` or `python` and selects the configured GCC target and
MinGW headers for clang-tidy. Run it with `BUILD_DIR=build-windows-native scripts/check.sh lint` in
Git Bash. Formatting and style checks run natively; the full clang-tidy gate still reports remaining
porting and include-cleaner findings.

Project instructions and skill discovery use native Unicode directory enumeration and file
identities. The environment prompt uses UTF-8 working and home directories, falling back to
`USERPROFILE` when `HOME` is unset. Project skill precedence remains intact when the shared home
skill directory is reached through a symlink. The shared discovery suite runs natively, including
a Unicode project and skill scenario.

Filesystem APIs accept deep drive and UNC paths through Windows extended-length path prefixes.
Working-directory changes and shell launches retain ordinary Windows paths so Git Bash and other
child processes continue to understand their current directory.

```powershell
meson compile -C build-windows-native hax windows_terminal_driver test_cli test_oneshot test_session test_session_prune
meson test -C build-windows-native --no-rebuild cli oneshot session session_prune e2e/oneshot e2e/repl_windows --print-errorlogs
```
