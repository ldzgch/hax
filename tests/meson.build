# Fixtures shared across tests, each a header with its implementation beside it.
bash_fixture_source = host_machine.system() == 'windows' ? 'tools/bash_fixtures_win.c' : 'tools/bash_fixtures_posix.c'
harness_source = host_machine.system() == 'windows' ? 'harness_win.c' : 'harness.c'
harness_test_source = host_machine.system() == 'windows' ? 'test_harness_win.c' : 'test_harness.c'
platform_test_support = []
if host_machine.system() == 'windows'
    platform_test_support += files('win_files.c', 'terminal/win_console.c')
endif
test_support = static_library('test_support',
    'env.c',
    'files.c',
    'catalog_fixture.c',
    'stream_capture.c',
    harness_source,
    'harness_counts.c',
    'harness_path.c',
    'loopback.c',
    'pipe.c',
    'process.c',
    'tools/bash_fixtures.c',
    bash_fixture_source,
    platform_test_support,
    dependencies: [hax_dep],
)
test_support_dep = declare_dependency(link_with: test_support, dependencies: [hax_dep])

# Each entry is just the test's source path; everything else is derived. The registered name is
# the path without the `test_` prefix and `.c` suffix, keeping the subdirectory:
# `tools/test_read.c` -> `tools/read`, top-level `test_buf.c` -> `buf`. The list mirrors the
# production `sources` in the top-level meson.build: grouped by subdirectory, sorted within each.
#
# A test is a single .c file linked against `test_support_dep`, which carries the fixtures,
# `hax_lib`, and the libcurl/jansson/threads flags; lazy archive linking pulls in only the objects
# the test references.
test_sources = [
    'test_agent.c',
    'test_agent_core.c',
    'test_agent_dispatch.c',
    'test_agent_env.c',
    'test_agent_loop.c',
    'test_agent_stats.c',
    'test_agent_tool.c',
    'test_agent_usage.c',
    'test_banner.c',
    'test_buf.c',
    'test_catalog.c',
    'test_catalog_fetch.c',
    'test_cli.c',
    'test_compact.c',
    'test_config.c',
    'test_cred_store.c',
    'test_diag.c',
    'test_effort.c',
    'test_file_mention.c',
    'test_history.c',
    'test_model_meta.c',
    'test_model_sort.c',
    harness_test_source,
    'test_harness_path.c',
    'test_loopback.c',
    'test_oneshot.c',
    'test_paste_image.c',
    'test_provider.c',
    'test_process.c',
    'test_select_config.c',
    'test_select_model.c',
    'test_session.c',
    'test_session_paths.c',
    'test_session_storage.c',
    'test_session_prune.c',
    'test_slash.c',
    'test_tool_schema.c',
    'test_trace.c',
    'test_transcript.c',
    'test_turn.c',
    'test_xalloc.c',

    'providers/test_anthropic_body.c',
    'providers/test_anthropic_events.c',
    'providers/test_anthropic_models.c',
    'providers/test_chat_body.c',
    'providers/test_chat_events.c',
    'providers/test_codex.c',
    'providers/test_codex_auth.c',
    'providers/test_codex_login.c',
    'providers/test_codex_settings.c',
    'providers/test_deepseek.c',
    'providers/test_provider_config.c',
    'providers/test_http_provider.c',
    'providers/test_llamacpp.c',
    'providers/test_mock.c',
    'providers/test_model_info.c',
    'providers/test_openai_models.c',
    'providers/test_opencode.c',
    'providers/test_registry.c',
    'providers/test_responses_body.c',
    'providers/test_responses_events.c',
    'providers/test_stream_retry.c',
    'providers/test_wire.c',

    'render/test_ctrl_strip.c',
    'render/test_diff_color.c',
    'render/test_disp.c',
    'render/test_markdown.c',
    'render/test_markdown_scan.c',
    'render/test_markdown_table.c',
    'render/test_markdown_wrap.c',
    'render/test_render_ctx.c',
    'render/test_spinner.c',
    'render/test_tool_render.c',

    'system/test_bg_job.c',
    'system/test_browser.c',
    'system/test_clock.c',
    'system/test_fd.c',
    'system/test_file_lock.c',
    'system/test_line_reader.c',
    'system/test_fs_directory.c',
    'system/test_fs_read.c',
    'system/test_git.c',
    'system/test_keepawake.c',
    'system/test_locale.c',
    'system/test_os.c',
    'system/test_path.c',
    'system/test_rand.c',
    'system/test_socket.c',
    'system/test_stream_capture.c',

    'terminal/test_clipboard.c',
    'terminal/test_input.c',
    'terminal/test_input_core.c',
    'terminal/test_interrupt.c',
    'terminal/test_picker_core.c',
    'terminal/test_theme.c',
    'terminal/test_vt_resolve.c',
    'terminal/test_width.c',

    'text/test_base64.c',
    'text/test_completion.c',
    'text/test_diff.c',
    'text/test_display_safe.c',
    'text/test_fmt.c',
    'text/test_frontmatter.c',
    'text/test_json_scan.c',
    'text/test_glob.c',
    'text/test_placeholder.c',
    'text/test_sha256.c',
    'text/test_shell_quote.c',
    'text/test_windows_argv.c',
    'text/test_unicode_cells.c',
    'text/test_url.c',
    'text/test_utf8.c',
    'text/test_utf8_sanitize.c',
    'text/test_width.c',

    'tools/test_bash.c',
    'tools/test_bash_cd_strip.c',
    'tools/test_bash_classify.c',
    'tools/test_bash_output.c',
    'tools/test_bash_env.c',
    'tools/test_edit.c',
    'tools/test_image_sniff.c',
    'tools/test_output_cap.c',
    'tools/test_path_preprocess.c',
    'tools/test_read.c',
    'tools/test_task.c',
    'tools/test_task_wait.c',
    'tools/test_write.c',

    'transport/test_api_error.c',
    'transport/test_ca.c',
    'transport/test_http.c',
    'transport/test_oauth.c',
    'transport/test_retry.c',
    'transport/test_sse.c',
]

if host_machine.system() == 'windows'
    test_sources += [
        'test_session_prune_win.c',
        'system/test_fs_win.c',
        'system/test_spawn_win.c',
        'system/test_tempfiles_win.c',
        'system/test_win_process.c',
        'system/test_win_utf8.c',
        'terminal/test_interrupt_win.c',
        'terminal/test_tty_win.c',
        'terminal/test_clipboard_win.c',
        'terminal/test_win_keys.c',
        'tools/test_bash_win.c',
    ]
else
    test_sources += [
        'system/test_fs.c',
        'system/test_spawn.c',
        'system/test_tempfiles.c',
    ]
endif

fs = import('fs')
# TSan sleeps at every process exit by default (atexit_sleep_ms, 1s) to catch races with exiting
# threads; tests spawn and exit processes constantly, so that sleep would dominate the suite.
# Prepended so a caller's own TSAN_OPTIONS wins.
test_env = environment()
test_env.prepend('TSAN_OPTIONS', 'atexit_sleep_ms=0', separator: ':')

foreach src : test_sources
    stem = fs.stem(src).substring(5)  # drop the leading 'test_'
    parent = fs.parent(src)
    name = parent == '.' ? stem : parent / stem
    exe = executable('test_' + name.underscorify(),
        src,
        dependencies: [test_support_dep],
    )
    test(name, exe, env: test_env)
endforeach

# End-to-end scenarios: Python scripts under e2e/, one per user-facing area, driving the built
# binary through the mock provider (see e2e/harness.py). Names mirror the C tests:
# e2e/test_oneshot.py registers as e2e/oneshot, so `meson test 'e2e/*'` selects the group.
e2e_scenarios = [
    'oneshot',
]

python = find_program('python3')
e2e_env = environment({'HAX_BIN': hax_exe.full_path()})
e2e_dependencies = [hax_exe]
if host_machine.system() == 'windows'
    terminal_driver = executable('windows_terminal_driver', 'e2e/windows_terminal.c',
        dependencies: [test_support_dep])
    e2e_env.set('HAX_WINDOWS_TERMINAL_DRIVER', terminal_driver.full_path())
    e2e_dependencies += terminal_driver
    e2e_scenarios += 'repl_windows'
else
    e2e_scenarios += [
        'repl_interrupt',
        'repl_smoke',
    ]
endif
e2e_env.prepend('TSAN_OPTIONS', 'atexit_sleep_ms=0', separator: ':')
foreach name : e2e_scenarios
    test('e2e' / name,
        python,
        args: [files('e2e/test_' + name + '.py')],
        env: e2e_env,
        depends: e2e_dependencies,
        # The slowest tests, scheduled first so they don't bound the run from its tail.
        priority: 1,
        timeout: 60,
    )
endforeach
