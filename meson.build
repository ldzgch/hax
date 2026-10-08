project('hax', 'c',
    version: '0.5.0',
    license: 'MIT',
    default_options: [
        'c_std=c11',
        'warning_level=3',
        'buildtype=debugoptimized',
    ],
)

# glibc hides POSIX 2008 interfaces until asked for them, while the BSD libcs expose POSIX by
# default and read these macros as a request to *hide* their own extensions, flock(2)'s LOCK_*
# constants among them. So ask only where the request is needed.
#
# _XOPEN_SOURCE=700 (SUSv4: POSIX 2008 + XSI) is what exposes realpath() on glibc; _POSIX_C_SOURCE
# alone doesn't.
if host_machine.system() == 'linux'
    add_project_arguments(
        '-D_POSIX_C_SOURCE=200809L',
        '-D_XOPEN_SOURCE=700',
        language: 'c',
    )
endif

if host_machine.system() == 'windows'
    add_project_arguments('-D_WIN32_WINNT=0x0a00', '-DNTDDI_VERSION=0x0a000006',
        '-D__USE_MINGW_ANSI_STDIO=1', language: 'c')
endif

add_project_arguments(
    '-D_DARWIN_C_SOURCE',
    # Project headers go through -iquote, not -I, so `#include <foo.h>` always reaches a system
    # header rather than one of ours (under -I, a `src/util.h` would shadow macOS's <util.h>). The
    # `tests/` entry does the same for the test fixtures.
    '-iquote', meson.project_source_root() / 'src',
    '-iquote', meson.project_source_root() / 'tests',
    language: 'c',
)

# include_type system turns the dependencies' -I flags into -isystem, so clang-tidy doesn't report
# findings inside third-party headers (Homebrew installs them outside the default include path).
if host_machine.system() == 'windows'
    # Native CMake installs expose their imported targets, including static-link system libraries.
    libcurl = dependency('libcurl', method: 'pkg-config', required: false, include_type: 'system')
    if not libcurl.found()
        libcurl = dependency('CURL', method: 'cmake', modules: ['CURL::libcurl'], include_type: 'system')
    endif
    jansson = dependency('jansson', version: '>=2.13', method: 'pkg-config', required: false,
        include_type: 'system')
    if not jansson.found()
        jansson = dependency('jansson', version: '>=2.13', method: 'cmake',
            modules: ['jansson::jansson'], include_type: 'system')
    endif
else
    libcurl = dependency('libcurl', include_type: 'system')
    # >=2.13 for json_object_update_recursive.
    jansson = dependency('jansson', version: '>=2.13', include_type: 'system')
endif
if host_machine.system() == 'windows'
    # Keep the native executable runnable without the toolchain's DLL directory on PATH.
    threads = meson.get_compiler('c').find_library('winpthread', static: true)
else
    threads = dependency('threads')
endif
platform_deps = []
if host_machine.system() == 'windows'
    platform_deps += meson.get_compiler('c').find_library('bcrypt')
    platform_deps += meson.get_compiler('c').find_library('shell32')
    platform_deps += meson.get_compiler('c').find_library('advapi32')
    platform_deps += meson.get_compiler('c').find_library('user32')
    platform_deps += meson.get_compiler('c').find_library('gdi32')
    platform_deps += meson.get_compiler('c').find_library('ole32')
    platform_deps += meson.get_compiler('c').find_library('windowscodecs')
    platform_deps += meson.get_compiler('c').find_library('uuid')
    # Static libcurl's Schannel and socket dependencies are not propagated by older CMake parsers.
    platform_deps += meson.get_compiler('c').find_library('crypt32')
    platform_deps += meson.get_compiler('c').find_library('secur32')
    platform_deps += meson.get_compiler('c').find_library('ws2_32')
    platform_deps += meson.get_compiler('c').find_library('iphlpapi')
endif

# FreeBSD resolves isfinite() to a libm symbol rather than a compiler builtin, so the math library
# must be linked there. Optional because platforms that fold libm into libc ship none.
libm = meson.get_compiler('c').find_library('m', required: false)

fs = import('fs')

# The version baked into the binary. `--always` keeps pre-tag checkouts building (bare commit
# hash); the fallback covers builds without git, such as dist tarballs, and must match the tag
# scheme's leading "v".
#
# Describe only our own repository: otherwise git walks up and describes whatever encloses an
# extracted tarball. vcs_tag falls back when the command fails, not when it succeeds against the
# wrong repository, so a missing repository must select the fallback by failing. Test existence
# rather than is_dir(): `.git` is a file in a worktree or submodule.
if fs.exists('.git')
    describe = ['git', 'describe', '--tags', '--always', '--dirty=+']
else
    describe = ['false']
endif

version_h = vcs_tag(
    command: describe,
    input: 'src/version.h.in',
    output: 'version.h',
    fallback: 'v' + meson.project_version(),
)

# Production sources, grouped by subdirectory and sorted within each group. `main.c` is left out on
# purpose: it lives only in the `hax` executable, so test binaries, which bring their own `main`,
# can link the same objects through `hax_lib` without colliding.
fs_source = host_machine.system() == 'windows' ? 'src/system/fs_win.c' : 'src/system/fs.c'
spawn_source = host_machine.system() == 'windows' ? 'src/system/spawn_win.c' : 'src/system/spawn.c'
interrupt_source = host_machine.system() == 'windows' ? 'src/terminal/interrupt_win.c' : 'src/terminal/interrupt.c'
clipboard_source = host_machine.system() == 'windows' ? 'src/terminal/clipboard_win.c' : 'src/terminal/clipboard.c'
sources = files(
    'src/agent.c',
    'src/agent_core.c',
    'src/agent_dispatch.c',
    'src/agent_env.c',
    'src/agent_loop.c',
    'src/agent_stats.c',
    'src/agent_tool.c',
    'src/agent_usage.c',
    'src/banner.c',
    'src/buf.c',
    'src/busy.c',
    'src/catalog.c',
    'src/cli.c',
    'src/compact.c',
    'src/config.c',
    'src/cred_store.c',
    'src/diag.c',
    'src/effort.c',
    'src/file_mention.c',
    'src/history.c',
    'src/login.c',
    'src/model_meta.c',
    'src/model_sort.c',
    'src/oneshot.c',
    'src/paste_image.c',
    'src/provider.c',
    'src/select.c',
    'src/session.c',
    'src/session_paths.c',
    'src/session_storage.c',
    'src/session_picker.c',
    'src/session_prune.c',
    'src/slash.c',
    'src/tool_schema.c',
    'src/trace.c',
    'src/transcript.c',
    'src/turn.c',
    'src/xalloc.c',

    'src/providers/anthropic_body.c',
    'src/providers/anthropic_events.c',
    'src/providers/anthropic_models.c',
    'src/providers/chat_body.c',
    'src/providers/chat_events.c',
    'src/providers/codex.c',
    'src/providers/codex_auth.c',
    'src/providers/codex_login.c',
    'src/providers/codex_settings.c',
    'src/providers/deepseek.c',
    'src/providers/provider_config.c',
    'src/providers/http_provider.c',
    'src/providers/llamacpp.c',
    'src/providers/mock.c',
    'src/providers/openai_models.c',
    'src/providers/opencode.c',
    'src/providers/openrouter.c',
    'src/providers/registry.c',
    'src/providers/responses_body.c',
    'src/providers/responses_events.c',
    'src/providers/stream_retry.c',
    'src/providers/usage_fetch.c',
    'src/providers/usage_render.c',
    'src/providers/wire.c',

    'src/render/ctrl_strip.c',
    'src/render/diff_color.c',
    'src/render/disp.c',
    'src/render/markdown.c',
    'src/render/markdown_scan.c',
    'src/render/markdown_table.c',
    'src/render/markdown_wrap.c',
    'src/render/progress.c',
    'src/render/render_ctx.c',
    'src/render/spinner.c',
    'src/render/tool_render.c',

    'src/system/bg_job.c',
    'src/system/browser.c',
    'src/system/clock.c',
    'src/system/fd.c',
    'src/system/file_lock.c',
    'src/system/line_reader.c',
    fs_source,
    'src/system/fs_append.c',
    'src/system/fs_private.c',
    'src/system/fs_read.c',
    'src/system/fs_shell.c',
    'src/system/git.c',
    'src/system/keepawake.c',
    'src/system/locale.c',
    'src/system/os.c',
    'src/system/path.c',
    'src/system/rand.c',
    'src/system/socket.c',
    spawn_source,
    'src/system/spawn_locale.c',
    'src/system/stream_capture.c',
    'src/system/tempfiles.c',

    clipboard_source,
    'src/terminal/clipboard_osc52.c',
    'src/terminal/input.c',
    'src/terminal/input_core.c',
    interrupt_source,
    'src/terminal/interrupt_classifier.c',
    'src/terminal/notify.c',
    'src/terminal/picker.c',
    'src/terminal/picker_core.c',
    'src/terminal/theme.c',
    'src/terminal/tty.c',
    'src/terminal/ui.c',
    'src/terminal/vt_resolve.c',
    'src/terminal/width.c',

    'src/text/base64.c',
    'src/text/completion.c',
    'src/text/diff.c',
    'src/text/display_safe.c',
    'src/text/fmt.c',
    'src/text/frontmatter.c',
    'src/text/json_scan.c',
    'src/text/glob.c',
    'src/text/placeholder.c',
    'src/text/sha256.c',
    'src/text/shell_quote.c',
    'src/text/windows_argv.c',
    'src/text/unicode_cells.c',
    'src/text/url.c',
    'src/text/utf8.c',
    'src/text/utf8_sanitize.c',
    'src/text/width.c',

    'src/tools/bash.c',
    'src/tools/bash_cd_strip.c',
    'src/tools/bash_classify.c',
    'src/tools/bash_env.c',
    'src/tools/bash_output.c',
    'src/tools/bash_process.c',
    'src/tools/bash_shell.c',
    'src/tools/edit.c',
    'src/tools/image_sniff.c',
    'src/tools/output_cap.c',
    'src/tools/path_preprocess.c',
    'src/tools/read.c',
    'src/tools/task_registry.c',
    'src/tools/task_wait.c',
    'src/tools/write.c',

    'src/transport/api_error.c',
    'src/transport/ca.c',
    'src/transport/http.c',
    'src/transport/oauth.c',
    'src/transport/retry.c',
    'src/transport/sse.c',
)

if host_machine.system() == 'windows'
    sources += files(
        'src/session_prune_win.c',
        'src/system/win_bash.c',
        'src/system/win_error.c',
        'src/system/win_process.c',
        'src/tools/bash_process_win.c',
        'src/system/win_security.c',
        'src/system/win_utf8.c',
        'src/terminal/win_keys.c',
    )
endif

# One archive shared by the executable and every test. Static-archive linking is lazy: each
# consumer pulls only the objects it transitively references, so tests get exactly the closure
# they need without hand-listing sources. Named apart from the `hax` executable so
# `meson compile -C build hax` stays unambiguous.
hax_lib = static_library('hax_lib',
    sources,
    version_h,
    dependencies: [libcurl, jansson, threads, libm] + platform_deps,
)

# Consumers need the link against `hax_lib` and, for their own translation units (`main.c`, each
# test), the compile flags of libcurl, jansson, and threads, which `link_with` alone doesn't carry.
# Only the compile side comes through partial_dependency(): the static library already propagates
# its link dependencies, and adding them again makes every link warn about duplicate libraries.
hax_dep = declare_dependency(
    link_with: hax_lib,
    dependencies: [
        libcurl.partial_dependency(compile_args: true, includes: true),
        jansson.partial_dependency(compile_args: true, includes: true),
        threads.partial_dependency(compile_args: true, includes: true),
    ],
)

hax_exe = executable('hax',
    'src/main.c',
    dependencies: [hax_dep],
    install: true,
)

subdir('tests')
