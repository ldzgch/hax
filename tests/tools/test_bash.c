/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "buf.h"
#include "config.h"
#include "env.h"
#include "harness.h"
#include "tool.h"
#include "xalloc.h"
#include "system/fs.h"
#include "system/tempfiles.h"
#ifdef _WIN32
#include "system/win_bash.h"
#endif
#include "terminal/interrupt.h"
#include "tools/bash_env.h"
#include "tools/bash_fixtures.h"

static char *call_bash(const char *escaped_command)
{
    char *args = xasprintf("{\"command\":\"%s\"}", escaped_command);
    char *out = TOOL_BASH.run(args, NULL);
    free(args);
    return out;
}

/* Runs a command emitting one line of `bytes` 'x' characters, with no trailing newline. A printf
 * field width keeps the generator POSIX: `head -c` is a GNU/BSD extension that OpenBSD lacks. */
static char *call_bash_long_line(size_t bytes)
{
    char *command = xasprintf("printf '%%%zus' '' | tr ' ' x", bytes);
    char *out = call_bash(command);
    free(command);
    return out;
}

static char *call_bash_streamed(const char *escaped_command, struct display_capture *capture)
{
    char *args = xasprintf("{\"command\":\"%s\"}", escaped_command);
    struct tool_run_ctx ctx = {.display = append_display, .display_data = capture};
    char *out = TOOL_BASH.run(args, &ctx);
    free(args);
    return out;
}

/* A latched abort kills the command and reports the cut through both the output marker and the run
 * context's interrupted provenance. */
static void test_bash_interrupt_sets_provenance(void)
{
    interrupt_install_request_signal_handlers();
    raise(SIGINT); /* the request handler only latches; the run's next poll observes it */

    struct tool_run_ctx ctx = {0};
    char *out = TOOL_BASH.run("{\"command\":\"sleep 5\"}", &ctx);
    interrupt_clear_requests();

    EXPECT(ctx.interrupted);
    EXPECT(strstr(out, "[interrupted]") != NULL);
    free(out);
}

static void test_bash_invalid_json(void)
{
    char *out = TOOL_BASH.run("not json", NULL);
    EXPECT(strstr(out, "invalid arguments") != NULL);
    free(out);
}

static void test_bash_missing_command(void)
{
    char *out = TOOL_BASH.run("{}", NULL);
    EXPECT(strstr(out, "missing 'command'") != NULL);
    free(out);
}

static void test_bash_stdout(void)
{
    char *out = call_bash("echo hello");
    EXPECT_STR_EQ(out, "hello\n");
    free(out);
}

static void test_bash_stderr_merged(void)
{
    char *out = call_bash("echo err 1>&2");
    EXPECT_STR_EQ(out, "err\n");
    free(out);
}

static void test_bash_exit_code(void)
{
    char *out = call_bash("false");
    EXPECT(strstr(out, "[exit 1]") != NULL);
    free(out);
}

static void test_bash_signal(void)
{
    char *out = call_bash("kill -TERM $$");
#ifdef _WIN32
    /* Git Bash exposes its wait status as the native process exit code. */
    EXPECT(strstr(out, "[exit 3840]") != NULL);
#else
    EXPECT(strstr(out, "[signal 15]") != NULL);
#endif
    free(out);
}

static void test_bash_no_output(void)
{
    char *out = call_bash("true");
    EXPECT_STR_EQ(out, "(no output)");
    free(out);
}

static void test_bash_stdin_detached(void)
{
    time_t t0 = time(NULL);
    char *out = call_bash("cat");
    time_t elapsed = time(NULL) - t0;
    EXPECT(elapsed < 3);
    EXPECT_STR_EQ(out, "(no output)");
    free(out);
}

static void test_bash_clean_exit_after_closing_output(void)
{
    /* Pipe EOF arrives while the shell still runs; the exit must not be reported as a kill. */
    char *out = call_bash("exec >&- 2>&-; sleep 0.05");
    EXPECT_STR_EQ(out, "(no output)");
    free(out);
}

static void test_bash_dev_tty_does_not_hang(void)
{
    /* The child has no controlling terminal, so direct /dev/tty reads must fail promptly. */
    time_t t0 = time(NULL);
    char *out = call_bash("stty sane </dev/tty 2>&1; cat /dev/tty 2>&1; echo done");
    time_t elapsed = time(NULL) - t0;
    EXPECT(elapsed < 3);
    EXPECT(strstr(out, "done") != NULL);
    free(out);
}

static void test_bash_background_job_does_not_hang(void)
{
    time_t t0 = time(NULL);
    char *out = call_bash("echo spawned; sleep 30 &");
    time_t elapsed = time(NULL) - t0;
    EXPECT(elapsed < 3);
    EXPECT(strstr(out, "spawned") != NULL);
    free(out);
}

static void test_bash_foreground_infinite_writer_caps(void)
{
    time_t t0 = time(NULL);
    char *out = call_bash("yes foo");
    time_t elapsed = time(NULL) - t0;
    EXPECT(elapsed < 3);
    EXPECT(strstr(out, "[output truncated") != NULL);
    EXPECT(strstr(out, "saved to") != NULL);
#ifdef _WIN32
    EXPECT(strstr(out, "[exit 1]") != NULL);
#else
    EXPECT(strstr(out, "[signal 9]") != NULL);
#endif
    free(out);
}

static void test_bash_timeout_kills_process_tree(void)
{
    t_env_set("HAX_BASH_TIMEOUT", "30ms");
    time_t t0 = time(NULL);
    char *out = call_bash("sleep 30");
    time_t elapsed = time(NULL) - t0;
    EXPECT(elapsed < 2);
    EXPECT(strstr(out, "[timed out after 30ms]") != NULL);
    /* No bare [signal N] — the timeout supersedes it. */
    EXPECT(strstr(out, "[signal ") == NULL);
    free(out);
    t_env_unset("HAX_BASH_TIMEOUT");
}

static void test_bash_per_call_timeout_overrides_env(void)
{
    t_env_set("HAX_BASH_TIMEOUT", "10ms");
    char *out = TOOL_BASH.run("{\"command\":\"sleep 0.05\",\"timeout_seconds\":60}", NULL);
    EXPECT(strstr(out, "[timed out") == NULL);
    free(out);
    t_env_unset("HAX_BASH_TIMEOUT");
}

static void test_bash_per_call_timeout_clamped_to_max(void)
{
    t_env_set("HAX_BASH_TIMEOUT_MAX", "30ms");
    time_t t0 = time(NULL);
    char *out = TOOL_BASH.run("{\"command\":\"sleep 30\",\"timeout_seconds\":9999}", NULL);
    time_t elapsed = time(NULL) - t0;
    EXPECT(elapsed < 2);
    EXPECT(strstr(out, "[timed out after 30ms]") != NULL);
    free(out);
    t_env_unset("HAX_BASH_TIMEOUT_MAX");
}

static void test_bash_timeout_graceful_sigterm(void)
{
    t_env_set("HAX_BASH_TIMEOUT", "30ms");
    time_t t0 = time(NULL);
    char *out = call_bash("sleep 30");
    time_t elapsed = time(NULL) - t0;
    EXPECT(elapsed < 2);
    EXPECT(strstr(out, "[timed out after 30ms]") != NULL);
    free(out);
    t_env_unset("HAX_BASH_TIMEOUT");
}

static void test_bash_timeout_escalates_to_sigkill(void)
{
    t_env_set("HAX_BASH_TIMEOUT", "30ms");
    t_env_set("HAX_BASH_TIMEOUT_GRACE", "30ms");
    time_t t0 = time(NULL);
    char *out = call_bash("trap '' TERM; while :; do :; done");
    time_t elapsed = time(NULL) - t0;
    EXPECT(elapsed < 2);
    EXPECT(strstr(out, "[timed out after 30ms]") != NULL);
    free(out);
    t_env_unset("HAX_BASH_TIMEOUT");
    t_env_unset("HAX_BASH_TIMEOUT_GRACE");
}

static void test_bash_timeout_grace_allows_cleanup(void)
{
#ifdef _WIN32
    T_SKIP("native Windows jobs terminate immediately without a TERM grace window");
#endif
#if (defined(T_ASAN) || defined(T_TSAN)) && defined(__APPLE__)
    /* Reproducible with a sanitized parent and a plain pipe alone, no hax code involved. */
    T_SKIP("bash sporadically drops its TERM trap when spawned from a sanitized macOS parent");
#endif
    /* An armed trap must flush cleanup output during the grace period. "armed" prints only after
     * the trap is installed and only from the exec'd foreground child: a TERM landing in that
     * child's fork-to-exec window is consumed by the inherited trap disposition, leaving a
     * TERM-proof sleep the shell then defers the trap to for its full length. The trap's exit ends
     * the run at pipe EOF, so the grace never elapses on the happy path; it only needs to outlast
     * the trap's pause. */
    t_env_set("HAX_BASH_TIMEOUT", "50ms");
    t_env_set("HAX_BASH_TIMEOUT_GRACE", "500ms");
    t_env_set("HAX_BASH_TRANSITION_MIN_BYTES", "6"); /* "armed\n" */
    char *out =
        call_bash("trap 'sleep 0.05; echo cleaned; exit' TERM; sh -c 'echo armed; exec sleep 30'");
    EXPECT(strstr(out, "cleaned") != NULL);
    EXPECT(strstr(out, "[timed out") != NULL);
    free(out);
    t_env_unset("HAX_BASH_TRANSITION_MIN_BYTES");
    t_env_unset("HAX_BASH_TIMEOUT");
    t_env_unset("HAX_BASH_TIMEOUT_GRACE");
}

static void test_bash_timeout_grace_disabled(void)
{
    t_env_set("HAX_BASH_TIMEOUT", "30ms");
    t_env_set("HAX_BASH_TIMEOUT_GRACE", "0");
    time_t t0 = time(NULL);
    char *out = call_bash("trap '' TERM; while :; do :; done");
    time_t elapsed = time(NULL) - t0;
    EXPECT(elapsed < 2);
    EXPECT(strstr(out, "[timed out after 30ms]") != NULL);
    free(out);
    t_env_unset("HAX_BASH_TIMEOUT");
    t_env_unset("HAX_BASH_TIMEOUT_GRACE");
}

static void test_bash_timeout_no_grace_short_timeout(void)
{
    /* A direct SIGKILL must work even if the parent signals before the child completes setsid(). */
    t_env_set("HAX_BASH_TIMEOUT", "1ms");
    t_env_set("HAX_BASH_TIMEOUT_GRACE", "0");
    time_t t0 = time(NULL);
    char *out = call_bash("sleep 30");
    time_t elapsed = time(NULL) - t0;
    EXPECT(elapsed < 2);
    EXPECT(strstr(out, "[timed out after 1ms]") != NULL);
    free(out);
    t_env_unset("HAX_BASH_TIMEOUT");
    t_env_unset("HAX_BASH_TIMEOUT_GRACE");
}

static void test_bash_timeout_grace_no_escape_via_pipe_close(void)
{
    /* The descendant closes the output pipe during its TERM handler; its recorded process group
     * must still be gone when the grace period expires. "armed" prints only after the pgid write
     * and the trap, so holding the timeout on it orders SIGTERM after both. */
    t_env_set("HAX_BASH_TIMEOUT", "80ms");
    t_env_set("HAX_BASH_TIMEOUT_GRACE", "20ms");
    t_env_set("HAX_BASH_TRANSITION_MIN_BYTES", "6"); /* "armed\n" */

    char *path = xasprintf("%s/pid", t_tempdir());

    char *cmd = xasprintf("(echo $$ > %s; "
                          "trap 'exec >/dev/null 2>&1; while :; do :; done' TERM; "
                          "echo armed; sleep 30) & wait",
                          path);
    char *args = xasprintf("{\"command\":\"%s\"}", cmd);
    char *out = TOOL_BASH.run(args, NULL);
    free(args);
    free(cmd);
    EXPECT(strstr(out, "[timed out") != NULL);
    free(out);

    int pgid = -1;
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d", &pgid) != 1)
            pgid = -1;
        fclose(f);
    }
    free(path);
    EXPECT(pgid > 0);

#ifdef _WIN32
    EXPECT(process_is_gone(pgid));
#else
    /* ESRCH on Linux or EPERM on Darwin means the group is gone; clean up before failing. The
     * killed group lingers as an unreaped zombie until init collects it, which a loaded machine may
     * delay well past the kill itself. */
    int alive = 1;
    for (int i = 0; i < 2000; i++) {
        if (kill(-pgid, 0) < 0 && (errno == ESRCH || errno == EPERM)) {
            alive = 0;
            break;
        }
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 5 * 1000000L};
        nanosleep(&ts, NULL);
    }
    if (alive)
        kill(-pgid, SIGKILL);
    EXPECT(!alive);

#endif

    t_env_unset("HAX_BASH_TRANSITION_MIN_BYTES");
    t_env_unset("HAX_BASH_TIMEOUT");
    t_env_unset("HAX_BASH_TIMEOUT_GRACE");
}

static void test_bash_redirected_background_job_does_not_leak(void)
{
    /* A redirected background process does not hold the output pipe open, so verify it is still
     * killed when the shell exits. */
    char *path = xasprintf("%s/pid", t_tempdir());

    char *cmd = xasprintf("nohup sleep 30 >/dev/null 2>&1 & echo $! > %s", path);
    char *args = xasprintf("{\"command\":\"%s\"}", cmd);
    char *out = TOOL_BASH.run(args, NULL);
    free(args);
    free(cmd);
    free(out);

    int pid = -1;
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d", &pid) != 1)
            pid = -1;
        fclose(f);
    }
    free(path);
    EXPECT(pid > 0);

    EXPECT(process_is_gone(pid));
}

static void test_bash_timeout_huge_does_not_overflow(void)
{
    t_env_set("HAX_BASH_TIMEOUT", "9223372036854775000ms");
    char *out = call_bash("echo hi");
    EXPECT_STR_EQ(out, "hi\n");
    free(out);
    t_env_unset("HAX_BASH_TIMEOUT");
}

static void test_bash_per_call_timeout_invalid(void)
{
    char *out = TOOL_BASH.run("{\"command\":\"true\",\"timeout_seconds\":0}", NULL);
    EXPECT(strstr(out, "'timeout_seconds' must be >= 1") != NULL);
    free(out);

    out = TOOL_BASH.run("{\"command\":\"true\",\"timeout_seconds\":-5}", NULL);
    EXPECT(strstr(out, "'timeout_seconds' must be >= 1") != NULL);
    free(out);

    out = TOOL_BASH.run("{\"command\":\"true\",\"timeout_seconds\":\"30\"}", NULL);
    EXPECT(strstr(out, "'timeout_seconds' must be an integer") != NULL);
    free(out);
}

static void test_bash_head_tail_truncation(void)
{
    char *out = call_bash("echo HEAD; seq 1 20000; echo TAIL");
    char *head = strstr(out, "HEAD");
    char *marker = strstr(out, "[output truncated");
    char *tail = strstr(out, "TAIL");
    EXPECT(head != NULL);
    EXPECT(marker != NULL);
    EXPECT(tail != NULL);
    /* Order: HEAD slice, then the gap marker, then the TAIL slice. */
    EXPECT(head < marker && marker < tail);
    EXPECT(strstr(out, "omitted ") != NULL);
    EXPECT(strstr(out, "kept first ") != NULL);
    EXPECT(strstr(out, "saved to ") != NULL);
    /* Hint mentions the file under the hax-XXXXXX container dir. */
    EXPECT(strstr(out, "/hax-") != NULL);
    EXPECT(strstr(out, "/bash-") != NULL);
    free(out);
}

static void test_bash_head_kept_when_long_line_spans_gap(void)
{
    /* A byte gap within one long line has no omitted whole lines, but the leading summary must
     * still survive. */
    char *out = call_bash("echo SUMMARY; printf 'x%.0s' $(seq 1 60000)");
    char *head = strstr(out, "SUMMARY");
    char *marker = strstr(out, "[output truncated");
    EXPECT(head != NULL);
    EXPECT(marker != NULL);
    EXPECT(head < marker);
    EXPECT(strstr(out, "mid-line") != NULL);
    EXPECT(strstr(out, "0 lines") == NULL);
    EXPECT(strstr(out, "saved to ") != NULL);
    free(out);
}

static void test_bash_tail_only_fallback_uses_full_budget(void)
{
    /* Without a complete head line, tail-only output should reclaim the full 2000-line budget. */
    char *out = call_bash("printf 'X%.0s' $(seq 1 8000); echo; seq 1 5000");
    EXPECT(strstr(out, "[output truncated") != NULL);
    EXPECT(strstr(out, "last 2000 of 5001 lines") != NULL);
    EXPECT(strstr(out, "last 1750 of") == NULL);
    EXPECT(strstr(out, "saved to ") != NULL);
    free(out);
}

/* Return the owned saved-output path from a truncation marker, or NULL. */
static char *extract_saved_path(const char *result)
{
    const char *needle = "saved to ";
    const char *path_start = strstr(result, needle);
    if (!path_start)
        return NULL;
    path_start += strlen(needle);
    const char *path_end = strchr(path_start, ']');
    if (!path_end)
        return NULL;
    size_t path_len = (size_t)(path_end - path_start);
    char *path = xmalloc(path_len + 1);
    memcpy(path, path_start, path_len);
    path[path_len] = '\0';
    return path;
}

static void test_bash_saved_path_holds_full_output(void)
{
    char *out = call_bash("echo HEAD; seq 1 20000; echo TAIL");
    char *path = extract_saved_path(out);
    EXPECT(path != NULL);
    if (path) {
        struct stat st;
        EXPECT(stat(path, &st) == 0);
        /* `echo HEAD` + seq 1..20000 + `echo TAIL` = ~108 KiB. */
        EXPECT(st.st_size > 100 * 1024);
        /* Contents start with "HEAD\n" — the saved file holds the full, untruncated output,
         * head+tail preview notwithstanding. */
        FILE *f = fopen(path, "r");
        EXPECT(f != NULL);
        if (f) {
            char first[16] = {0};
            EXPECT(fgets(first, sizeof(first), f) != NULL);
            EXPECT(strcmp(first, "HEAD\n") == 0);
            fclose(f);
        }
        free(path);
    }
    free(out);
}

static void test_bash_single_line_over_cap_keeps_body(void)
{
    char *out = call_bash("printf 'x%.0s' $(seq 1 60000)");
    EXPECT(strstr(out, "[output truncated") != NULL);
    EXPECT(strstr(out, "saved to ") != NULL);
    /* Body must contain the line content, not just the marker. */
    EXPECT(strstr(out, "xxxx") != NULL);
    /* Per-line cap (500) means the kept line is far smaller than the 60 KiB original. */
    EXPECT(strlen(out) < 4 * 1024);
    free(out);
}

static void test_bash_line_cap_triggers_spill(void)
{
    char *out = call_bash("yes x | head -n 5000");
    EXPECT(strstr(out, "[output truncated") != NULL);
    EXPECT(strstr(out, "saved to ") != NULL);
    /* Total-line count in the marker reflects the producer's 5000. */
    EXPECT(strstr(out, "of 5000 lines") != NULL);
    free(out);
}

static void test_bash_unterminated_final_line_triggers_spill(void)
{
    /* The unterminated final line is the 2001st logical line and must trigger truncation. */
    char *out = call_bash("for i in $(seq 1 2000); do echo line$i; done; printf 'partial-no-nl'");
    EXPECT(strstr(out, "[output truncated") != NULL);
    EXPECT(strstr(out, "saved to ") != NULL);
    free(out);
}

static void test_bash_mkstemp_failure_falls_back_to_mem(void)
{
    /* If the spill file cannot be created, preserve the in-memory prefix and report that the
     * remainder is unavailable. */
    t_env_set("TMPDIR", "/no/such/hax-test-dir/");
    char *out = call_bash("seq 1 20000");
    EXPECT(strstr(out, "1\n") != NULL);
    EXPECT(strstr(out, "[output truncated") != NULL);
    EXPECT(strstr(out, "unavailable") != NULL);
    EXPECT(strstr(out, "saved to") == NULL);
    free(out);
    t_env_unset("TMPDIR");
}

static void test_bash_tail_keeps_line_at_window_boundary(void)
{
    /* The tail window starts exactly at line 600 and must retain that complete boundary line. */
    char *out = call_bash("for i in $(seq 0 1999); do printf '%-31s\\n' line$i; done");
    EXPECT(strstr(out, "[output truncated") != NULL);
    /* The head slice comes first: its first line is line0. */
    const char *first = strstr(out, "line");
    EXPECT(first != NULL);
    if (first)
        EXPECT(strncmp(first, "line0 ", 6) == 0);
    /* The tail slice follows the gap marker; its first line is line600, kept whole (no mid-line
     * truncation at the boundary). */
    const char *marker = strstr(out, "[output truncated");
    EXPECT(marker != NULL);
    if (marker) {
        const char *tail = strstr(marker, "] ...\n");
        EXPECT(tail != NULL);
        if (tail) {
            tail += strlen("] ...\n");
            EXPECT(strncmp(tail, "line600 ", 8) == 0);
        }
    }
    free(out);
}

static void test_bash_spill_tmpdir_encoding(void)
{
    /* An invalid UTF-8 TMPDIR cannot be advertised to the model; the spill must use a valid
     * fallback path. */
#ifdef _WIN32
    char *tmpdir = xasprintf("%s/\xe6\x96\x87-\xf0\x9f\x90\xb1", t_tempdir());
    EXPECT(fs_mkdir_p(tmpdir) == 0);
    t_env_set("TMPDIR", tmpdir);
#else
    t_env_set("TMPDIR", "/tmp/hax-test-bad-\xff-XXXXXX-NOTREAL");
#endif
    char *out = call_bash("seq 1 20000");
    EXPECT(strstr(out, "[output truncated") != NULL);
    /* No raw 0xff in the result — both the validator (rejecting the env) and utf8_sanitize (defense
     * in depth) help here. */
    EXPECT(strchr(out, (char)0xff) == NULL);
    /* The advertised path must point at a real file under the /tmp fallback: extract it, stat it,
     * confirm it exists. */
    const char *p = strstr(out, "saved to ");
    EXPECT(p != NULL);
    if (p) {
        p += strlen("saved to ");
        const char *end = strchr(p, ']');
        EXPECT(end != NULL);
        if (end) {
            size_t len = (size_t)(end - p);
            char path[256];
            if (len < sizeof(path)) {
                memcpy(path, p, len);
                path[len] = '\0';
#ifndef _WIN32
                EXPECT(strncmp(path, "/tmp/hax-", 9) == 0);
#endif
                EXPECT(strstr(path, "/bash-") != NULL);
                EXPECT(fs_check_regular(path) == 0);
            }
        }
    }
    free(out);
    tempfiles_cleanup();
    t_env_unset("TMPDIR");
#ifdef _WIN32
    free(tmpdir);
#endif
}

static void test_bash_long_line_with_trailing_newline_keeps_body(void)
{
    /* Alignment must not erase a long line when its only newline is the final byte. */
    char *out = call_bash("printf 'x%.0s' $(seq 1 60000); printf '\\n'");
    EXPECT(strstr(out, "[output truncated") != NULL);
    /* Body must contain at least the line content tail, not be empty. 'xxxx' is a uniquely
     * identifiable run that's nowhere in the truncation marker. */
    EXPECT(strstr(out, "xxxx") != NULL);
    free(out);
}

static void test_bash_cleanup_unlinks_kept_files(void)
{
    char *out = call_bash("seq 1 20000");
    char *path = extract_saved_path(out);
    EXPECT(path != NULL);
    if (path) {
        struct stat st;
        EXPECT(stat(path, &st) == 0);
        tempfiles_cleanup();
        EXPECT(stat(path, &st) < 0);
        free(path);
    }
    free(out);
}

static void test_bash_short_output_no_elision(void)
{
    char *out = call_bash("for i in 1 2 3 4 5; do echo line $i; done");
    EXPECT(strstr(out, "bytes elided") == NULL);
    EXPECT(strstr(out, "[output truncated]") == NULL);
    EXPECT_STR_EQ(out, "line 1\nline 2\nline 3\nline 4\nline 5\n");
    free(out);
}

static void test_bash_caps_long_line(void)
{
    char *out = call_bash_long_line(5000);
    EXPECT(strstr(out, "bytes elided") != NULL);
    EXPECT(strlen(out) < 2500);
    free(out);
}

static void test_bash_sanitizes_non_utf8(void)
{
    /* printf \377 produces an invalid UTF-8 byte which must be replaced. Octal, not \xff: POSIX
     * printf only mandates octal escapes and dash (Debian's /bin/sh) emits hex escapes literally.
     * Quadruple-backslash: C-literal → JSON → shell each eat one layer. */
    char *out = call_bash("printf '\\\\377'");
    EXPECT(strstr(out, "\xEF\xBF\xBD") != NULL);
    free(out);
}

static void test_bash_binary_output_suppressed(void)
{
    char *out = call_bash("printf 'BEFORE\\\\0AFTER'");
    EXPECT(strstr(out, "[binary output suppressed: ") != NULL);
    /* 12 bytes ("BEFORE\0AFTER") formats via format_byte_size as "12B". */
    EXPECT(strstr(out, "12B]") != NULL);
    EXPECT(strstr(out, "BEFORE") == NULL);
    EXPECT(strstr(out, "AFTER") == NULL);
    EXPECT(strstr(out, "\xEF\xBF\xBD") == NULL);
    free(out);
}

static void test_bash_binary_output_keeps_exit_footer(void)
{
    char *out = call_bash("printf 'BEFORE\\\\0AFTER'; exit 7");
    EXPECT(strstr(out, "[binary output suppressed:") != NULL);
    EXPECT(strstr(out, "[exit 7]") != NULL);
    free(out);
}

static void test_bash_streamed_basic(void)
{
    struct display_capture display_output = {0};
    buf_init(&display_output.buf);
    char *out = call_bash_streamed("echo hello", &display_output);
    EXPECT_STR_EQ(out, "hello\n");
    EXPECT(display_output.buf.len > 0);
    EXPECT(strstr(display_output.buf.data, "hello") != NULL);
    free(out);
    buf_free(&display_output.buf);
}

static void test_bash_streamed_binary_history_clean(void)
{
    /* Once any NUL is seen, canonical history contains only the binary marker and status. */
    struct display_capture display_output = {0};
    buf_init(&display_output.buf);
    char *out = call_bash_streamed("printf 'BEFORE\\\\0AFTER'; exit 0", &display_output);
    EXPECT(strstr(out, "[binary output suppressed:") != NULL);
    EXPECT(strstr(out, "BEFORE") == NULL);
    EXPECT(strstr(out, "AFTER") == NULL);
    EXPECT(strstr(display_output.buf.data, "[binary output suppressed:") != NULL);
    free(out);
    buf_free(&display_output.buf);
}

static void test_bash_streamed_binary_marker_isolated_from_escape(void)
{
    /* A newline must abort an unterminated escape before the binary marker's opening bracket. */
    struct display_capture display_output = {0};
    buf_init(&display_output.buf);
    /* The delay separates reads so the escape reaches display before the later NUL. JSON supplies
     * the raw ESC byte portably. */
    char *out = call_bash_streamed(
        "printf '\\u001b['; sleep 0.03; printf 'pad pad pad pad\\\\0bin'", &display_output);
    EXPECT(out != NULL);
    EXPECT(display_output.buf.data != NULL);
    const char *marker = strstr(display_output.buf.data, "[binary output suppressed:");
    EXPECT(marker != NULL);
    EXPECT(marker != NULL && marker > display_output.buf.data && marker[-1] == '\n');
    free(out);
    buf_free(&display_output.buf);
}

static void test_bash_stdout_is_not_a_tty(void)
{
    char *out = call_bash("[ -t 1 ] && echo TTY || echo NOTTY");
    EXPECT_STR_EQ(out, "NOTTY\n");
    free(out);
}

static void test_bash_stderr_is_not_a_tty(void)
{
    char *out = call_bash("[ -t 2 ] && echo TTY 1>&2 || echo NOTTY 1>&2");
    EXPECT_STR_EQ(out, "NOTTY\n");
    free(out);
}

static void test_bash_lf_not_crlf(void)
{
    char *out = call_bash("printf 'a\\nb\\n'");
    EXPECT_STR_EQ(out, "a\nb\n");
    free(out);
}

static void test_bash_env_overrides(void)
{
    /* Contradicting parent values verify replacements; NO_COLOR, FORCE_COLOR, and MAKEFLAGS verify
     * passthrough. */
    t_env_set("PAGER", "less");
    t_env_set("GIT_PAGER", "less");
    t_env_set("MANPAGER", "less");
    t_env_set("SYSTEMD_PAGER", "less");
    t_env_set("GH_PAGER", "less");
    t_env_set("GIT_EDITOR", "vim");
    t_env_set("GIT_SEQUENCE_EDITOR", "vim");
    t_env_set("VISUAL", "vim");
    t_env_set("EDITOR", "vim");
    t_env_set("TERM", "xterm-256color");
    t_env_set("NO_COLOR", "0");
    t_env_set("COLORTERM", "truecolor");
    t_env_set("AI_AGENT", "other");
    t_env_set("GIT_TERMINAL_PROMPT", "1");
    t_env_set("PYTHONUNBUFFERED", "0");
    t_env_set("TQDM_DISABLE", "0");
    t_env_set("FORCE_COLOR", "1");
    t_env_set("MAKEFLAGS", "-j8");
    char *out = call_bash("echo PAGER=$PAGER; echo GIT_PAGER=$GIT_PAGER; "
                          "echo MANPAGER=$MANPAGER; echo SYSTEMD_PAGER=$SYSTEMD_PAGER; "
                          "echo GH_PAGER=$GH_PAGER; "
                          "echo GIT_EDITOR=$GIT_EDITOR; "
                          "echo GIT_SEQUENCE_EDITOR=$GIT_SEQUENCE_EDITOR; "
                          "echo VISUAL=$VISUAL; echo EDITOR=$EDITOR; "
                          "echo TERM=$TERM; echo NO_COLOR=$NO_COLOR; echo COLORTERM=$COLORTERM; "
                          "echo AI_AGENT=$AI_AGENT; echo GIT_TERMINAL_PROMPT=$GIT_TERMINAL_PROMPT; "
                          "echo PYTHONUNBUFFERED=$PYTHONUNBUFFERED; "
                          "echo TQDM_DISABLE=$TQDM_DISABLE; "
                          "echo FORCE_COLOR=$FORCE_COLOR; echo MAKEFLAGS=$MAKEFLAGS");
    EXPECT_STR_EQ(out, "PAGER=cat\n"
                       "GIT_PAGER=cat\n"
                       "MANPAGER=cat\n"
                       "SYSTEMD_PAGER=cat\n"
                       "GH_PAGER=cat\n"
                       "GIT_EDITOR=false\n"
                       "GIT_SEQUENCE_EDITOR=false\n"
                       "VISUAL=false\n"
                       "EDITOR=false\n"
                       "TERM=dumb\n"
                       "NO_COLOR=0\n"
                       "COLORTERM=\n"
                       "AI_AGENT=hax\n"
                       "GIT_TERMINAL_PROMPT=0\n"
                       "PYTHONUNBUFFERED=1\n"
                       "TQDM_DISABLE=1\n"
                       "FORCE_COLOR=1\n"
                       "MAKEFLAGS=-j8\n");
    free(out);
    t_env_unset("PAGER");
    t_env_unset("GIT_PAGER");
    t_env_unset("MANPAGER");
    t_env_unset("SYSTEMD_PAGER");
    t_env_unset("GH_PAGER");
    t_env_unset("GIT_EDITOR");
    t_env_unset("GIT_SEQUENCE_EDITOR");
    t_env_unset("VISUAL");
    t_env_unset("EDITOR");
    t_env_unset("TERM");
    t_env_unset("NO_COLOR");
    t_env_unset("COLORTERM");
    t_env_unset("AI_AGENT");
    t_env_unset("GIT_TERMINAL_PROMPT");
    t_env_unset("PYTHONUNBUFFERED");
    t_env_unset("TQDM_DISABLE");
    t_env_unset("FORCE_COLOR");
    t_env_unset("MAKEFLAGS");
}

static void test_bash_subagent_env(void)
{
    /* Contradicting parent values verify selection replacement, preset clearing, and depth
     * stamping. */
    const char *d = getenv("HAX_SUBAGENT_DEPTH");
    char depth_expect[32];
    snprintf(depth_expect, sizeof(depth_expect), "d=%d\n", (d ? atoi(d) : 0) + 1);

    t_env_set("HAX_PROVIDER", "parent-provider");
    t_env_set("HAX_MODEL", "parent-model");
    t_env_set("HAX_EFFORT", "parent-effort");
    t_env_set("HAX_PRESET", "parent-preset");
    t_env_set("HAX_TRACE", "/tmp/parent.trace");
    t_env_set("HAX_TRANSCRIPT", "/tmp/parent.transcript");
    bash_env_set_selection("mock", "m-1", NULL);
    char *out = call_bash("echo p=$HAX_PROVIDER; echo m=$HAX_MODEL; "
                          "echo e=$HAX_EFFORT; echo ps=$HAX_PRESET; "
                          "echo d=$HAX_SUBAGENT_DEPTH; echo tr=$HAX_TRACE; "
                          "echo tl=$HAX_TRANSCRIPT");
    char *want = xasprintf("p=mock\nm=m-1\ne=\nps=\n%str=\ntl=\n", depth_expect);
    EXPECT_STR_EQ(out, want);
    free(want);
    free(out);

    /* Clearing the export reverts the selection vars to raw passthrough; the depth marker and the
     * trace/transcript clearing are unconditional (a nested hax truncates those paths at startup —
     * inheriting them would destroy this process's live logs). */
    bash_env_set_selection(NULL, NULL, NULL);
    out = call_bash("echo p=$HAX_PROVIDER; echo ps=$HAX_PRESET; echo d=$HAX_SUBAGENT_DEPTH; "
                    "echo tr=$HAX_TRACE; echo tl=$HAX_TRANSCRIPT");
    want = xasprintf("p=parent-provider\nps=parent-preset\n%str=\ntl=\n", depth_expect);
    EXPECT_STR_EQ(out, want);
    free(want);
    free(out);

    t_env_unset("HAX_PROVIDER");
    t_env_unset("HAX_MODEL");
    t_env_unset("HAX_EFFORT");
    t_env_unset("HAX_PRESET");
    t_env_unset("HAX_TRACE");
    t_env_unset("HAX_TRANSCRIPT");
}

static void test_bash_shell_prefers_bash(void)
{
    /* Pin the built-in resolution chain so user configuration cannot affect the assertion. */
    t_env_set("HAX_BASH_SHELL", CONFIG_VALUE_DEFAULT);
#ifdef _WIN32
    char *bash = win_bash_path();
    EXPECT(bash != NULL);
#else
    char *bash = fs_which("bash");
#endif
    char *out = call_bash("basename $0");
    EXPECT_STR_EQ(out, bash ? "bash\n" : "sh\n");
    free(out);
    free(bash);
    t_env_unset("HAX_BASH_SHELL");
}

static void test_bash_shell_override(void)
{
    char *shell = fs_which("sh");
    EXPECT(shell != NULL);
    if (!shell)
        return;
    t_env_set("HAX_BASH_SHELL", shell);
    free(shell);
    char *out = call_bash("basename $0");
    EXPECT_STR_EQ(out, "sh\n");
    free(out);
    t_env_unset("HAX_BASH_SHELL");
}

static void test_bash_shell_override_bad_value_falls_back(void)
{
    t_env_set("HAX_BASH_SHELL", "hax-definitely-not-a-shell");
    char *out = call_bash("echo still-works");
    EXPECT_STR_EQ(out, "still-works\n");
    free(out);
    t_env_unset("HAX_BASH_SHELL");
}

static void test_bash_streamed_history_truncated(void)
{
    /* Live display receives raw output, while canonical model history remains bounded and contains
     * the saved-output marker. */
    struct display_capture display_output = {0};
    buf_init(&display_output.buf);
    char *out = call_bash_streamed("yes hi | head -n 50000", &display_output);
    EXPECT(strstr(out, "[output truncated") != NULL);
    EXPECT(strstr(out, "saved to ") != NULL);
    EXPECT(strlen(out) < 100 * 1024);
    /* The renderer applies display-row elision, so the model-facing marker is not streamed. */
    EXPECT(strstr(display_output.buf.data, "hi") != NULL);
    EXPECT(strstr(display_output.buf.data, "[output truncated") == NULL);
    free(out);
    buf_free(&display_output.buf);
}

int main(void)
{
    /* Pin the cap so inherited configuration cannot invalidate truncation fixtures. */
    t_env_set("HAX_TOOL_OUTPUT_CAP", "50k");
    /* This suite covers the synchronous kill-on-timeout path; task detachment is covered by
     * tools/test_task.c. */
    t_env_set("HAX_NO_TASKS", "1");

    test_bash_interrupt_sets_provenance();
    test_bash_invalid_json();
    test_bash_missing_command();
    test_bash_stdout();
    test_bash_stderr_merged();
    test_bash_exit_code();
    test_bash_signal();
    test_bash_no_output();
    test_bash_stdin_detached();
    test_bash_clean_exit_after_closing_output();
    test_bash_dev_tty_does_not_hang();
    test_bash_background_job_does_not_hang();
    test_bash_foreground_infinite_writer_caps();
    test_bash_timeout_kills_process_tree();
    test_bash_per_call_timeout_overrides_env();
    test_bash_per_call_timeout_clamped_to_max();
    test_bash_timeout_graceful_sigterm();
    test_bash_timeout_escalates_to_sigkill();
    test_bash_timeout_grace_allows_cleanup();
    test_bash_timeout_grace_no_escape_via_pipe_close();
    test_bash_timeout_grace_disabled();
    test_bash_timeout_no_grace_short_timeout();
    test_bash_redirected_background_job_does_not_leak();
    test_bash_timeout_huge_does_not_overflow();
    test_bash_per_call_timeout_invalid();
    test_bash_sanitizes_non_utf8();
    test_bash_binary_output_suppressed();
    test_bash_binary_output_keeps_exit_footer();
    test_bash_streamed_basic();
    test_bash_streamed_binary_history_clean();
    test_bash_streamed_binary_marker_isolated_from_escape();
    test_bash_streamed_history_truncated();
    test_bash_stdout_is_not_a_tty();
    test_bash_stderr_is_not_a_tty();
    test_bash_env_overrides();
    test_bash_subagent_env();
    test_bash_shell_prefers_bash();
    test_bash_shell_override();
    test_bash_shell_override_bad_value_falls_back();
    test_bash_lf_not_crlf();
    test_bash_head_tail_truncation();
    test_bash_head_kept_when_long_line_spans_gap();
    test_bash_tail_only_fallback_uses_full_budget();
    test_bash_saved_path_holds_full_output();
    test_bash_single_line_over_cap_keeps_body();
    test_bash_line_cap_triggers_spill();
    test_bash_unterminated_final_line_triggers_spill();
    test_bash_tail_keeps_line_at_window_boundary();
    test_bash_long_line_with_trailing_newline_keeps_body();
    test_bash_spill_tmpdir_encoding();
    test_bash_mkstemp_failure_falls_back_to_mem();
    test_bash_cleanup_unlinks_kept_files();
    test_bash_short_output_no_elision();
    test_bash_caps_long_line();
    T_REPORT();
}
