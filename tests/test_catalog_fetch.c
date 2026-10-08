/* SPDX-License-Identifier: MIT */
/* Each fetch scenario runs in a child because catalog_prefetch is process-wide and runs once. */
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "catalog.h"
#include "env.h"
#include "files.h"
#include "harness.h"
#include "loopback.h"
#include "model_meta.h"
#include "process.h"
#include "provider.h"
#include "system/clock.h"
#include "system/socket.h"

/* Parent-made temp root; children carve their own XDG_CACHE_HOME under it. */
static char *g_root;

/* ---------------- child-side helpers ---------------- */

/* Point the module at a private cache dir and the scenario's server. catalog.refresh=1ms makes any
 * existing snapshot count as stale, so the fetch always spawns. */
static void child_env(const char *name, int port)
{
    char dir[512], url[64];
    snprintf(dir, sizeof(dir), "%s/%s", g_root, name);
    t_mkdir(dir, 0755);
    t_env_set("XDG_CACHE_HOME", dir);
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/api.json", port);
    t_env_set("HAX_CATALOG_URL", url);
    t_env_set("HAX_CATALOG_REFRESH", "1ms");
}

static void write_snapshot(const char *json)
{
    char path[600];
    snprintf(path, sizeof(path), "%s/hax", getenv("XDG_CACHE_HOME"));
    t_mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/hax/catalog.json", getenv("XDG_CACHE_HOME"));
    FILE *f = fopen(path, "w");
    if (!f)
        FAIL("fopen %s: %s", path, strerror(errno));
    fputs(json, f);
    fclose(f);
}

static void backdate_snapshot_days(long days)
{
    char path[600];
    snprintf(path, sizeof(path), "%s/hax/catalog.json", getenv("XDG_CACHE_HOME"));
    EXPECT(t_file_set_mtime(path, (int64_t)time(NULL) - days * 24 * 60 * 60) == 0);
}

/* Poll the asynchronous refresh for at most three seconds. */
static int wait_for_rate(const char *provider_id, const char *model, double expected_rate)
{
    for (int attempt = 0; attempt < 300; attempt++) {
        struct catalog_entry entry;
        if (catalog_lookup(NULL, provider_id, model, &entry) == 0 &&
            entry.cost_input == expected_rate)
            return 1;
        clock_sleep_ms(10);
    }
    return 0;
}

/* ---------------- scenarios (each runs in its own child) ---------------- */

static void scenario_cold_start(void)
{
    /* The generation bump must invalidate the miss memoized while the first fetch runs. */
    struct loopback server = {0};
    loopback_reply_ok(&server, 0,
                      "{\"openai\": {\"models\": {"
                      "\"m1\": {\"cost\": {\"input\": 7, \"output\": 1}}}}}");
    int port = loopback_listen(&server);
    EXPECT(port > 0);
    child_env("cold", port);
    EXPECT(loopback_serve(&server) == 0);

    catalog_prefetch();
    EXPECT(catalog_stale_days() == 0); /* no snapshot yet ⇒ nothing to be stale */
    EXPECT(wait_for_rate("openai", "m1", 7));

    loopback_stop(&server);
    catalog_shutdown();
}

static void scenario_refresh_invalidates_memo(void)
{
    /* A stale snapshot answers (and is memoized) first; the refresh must replace the file and the
     * generation bump must invalidate the memoized old value — the "estimates self-heal when a
     * refresh lands mid-session" contract. */
    struct loopback server = {0};
    loopback_reply_ok(&server, 0,
                      "{\"openai\": {\"models\": {"
                      "\"m2\": {\"cost\": {\"input\": 9, \"output\": 1}}}}}");
    int port = loopback_listen(&server);
    EXPECT(port > 0);
    child_env("refresh", port);
    write_snapshot("{\"openai\": {\"models\": {"
                   "\"m2\": {\"cost\": {\"input\": 2, \"output\": 1}}}}}");

    struct catalog_entry entry;
    EXPECT(catalog_lookup(NULL, "openai", "m2", &entry) == 0);
    EXPECT(entry.cost_input == 2); /* old snapshot, now memoized */

    EXPECT(loopback_serve(&server) == 0);
    catalog_prefetch();
    EXPECT(catalog_stale_days() == 0); /* stale for the TTL, not for the alarm */
    EXPECT(wait_for_rate("openai", "m2", 9));

    loopback_stop(&server);
    catalog_shutdown();
}

static void run_bad_payload_scenario(const char *name, const char *bad_body)
{
    struct loopback server = {0};
    loopback_reply_ok(&server, 0, bad_body);
    int port = loopback_listen(&server);
    EXPECT(port > 0);
    child_env(name, port);
    write_snapshot("{\"openai\": {\"models\": {"
                   "\"m3\": {\"cost\": {\"input\": 2, \"output\": 1}}}}}");

    struct catalog_entry entry;
    EXPECT(catalog_lookup(NULL, "openai", "m3", &entry) == 0);
    EXPECT(entry.cost_input == 2);

    EXPECT(loopback_serve(&server) == 0);
    catalog_prefetch();
    /* catalog_shutdown joins validation after the server has delivered the full response. */
    for (int i = 0; i < 300 && !atomic_load(&server.served); i++) {
        clock_sleep_ms(10);
    }
    EXPECT(atomic_load(&server.served));
    loopback_stop(&server);
    catalog_shutdown();

    /* Shutdown clears the memo, forcing this lookup to read the snapshot on disk. */
    EXPECT(catalog_lookup(NULL, "openai", "m3", &entry) == 0);
    EXPECT(entry.cost_input == 2);
}

static void scenario_garbage_keeps_snapshot(void)
{
    /* A 200 response that isn't JSON at all (an HTML error page behind a broken proxy) must never
     * replace a working snapshot. */
    run_bad_payload_scenario("garbage", "<html>bad gateway</html>");
}

static void scenario_json_error_keeps_snapshot(void)
{
    /* A JSON-shaped error payload parses fine but lacks the catalog shape (no provider entry
     * carrying a models object) — it must be rejected too, or its fresh mtime would suppress a
     * recovering re-fetch for a whole refresh interval. */
    run_bad_payload_scenario("json-error", "{\"error\": \"rate limited\"}");
}

static void scenario_truncated_tail_keeps_snapshot(void)
{
    /* A body whose prefix validates but which is cut mid-member (a proxy truncation with a
     * happens-to-match Content-Length) must be rejected whole — accepting it would silently drop
     * every provider after the cut until the next refresh. */
    run_bad_payload_scenario("truncated-tail",
                             "{\"openai\": {\"models\": {\"m3\": {}}}, \"anthropic\":");
}

static void scenario_invalid_member_keeps_snapshot(void)
{
    /* Brace-balanced garbage after a valid member: the structural scan alone would wave it through,
     * so every member slice must survive a real parse before the snapshot is replaced. */
    run_bad_payload_scenario("invalid-member",
                             "{\"openai\": {\"models\": {\"m3\": {}}}, \"tail\": wat}");
}

static void scenario_trailing_garbage_keeps_snapshot(void)
{
    /* Bytes after the root object's closing brace (a concatenated or corrupted response) mean the
     * body isn't the artifact — reject. */
    run_bad_payload_scenario("trailing-garbage",
                             "{\"openai\": {\"models\": {\"m3\": {}}}} garbage");
}

static void scenario_drain_completes_fetch(void)
{
    /* The one-shot exit path drains the in-flight fetch (bounded) instead of letting shutdown
     * cancel it: with a server slower than the run, a post-drain lookup must already see the
     * fetched values — no polling, and no cold cache left behind. The delay only has to outlast an
     * undrained lookup, which follows prefetch at once. */
    struct loopback server = {.delay_ms = 50};
    loopback_reply_ok(&server, 0,
                      "{\"openai\": {\"models\": {"
                      "\"m5\": {\"cost\": {\"input\": 7, \"output\": 1}}}}}");
    int port = loopback_listen(&server);
    EXPECT(port > 0);
    child_env("drain", port);
    EXPECT(loopback_serve(&server) == 0);

    catalog_prefetch();
    catalog_drain(5000);
    struct catalog_entry entry;
    EXPECT(catalog_lookup(NULL, "openai", "m5", &entry) == 0);
    EXPECT(entry.cost_input == 7);

    loopback_stop(&server);
    catalog_shutdown();
}

static void scenario_stale_snapshot_warns(void)
{
    /* A snapshot that hasn't refreshed for over the alarm window (~30d) makes prefetch record its
     * age for catalog_stale_days — the frontend's cue to warn that estimates may have drifted —
     * while the refresh it spawns still recovers as usual. */
    /* The age is read while the fetch is held in flight. */
    struct loopback server = {.hold = 1};
    loopback_reply_ok(&server, 0,
                      "{\"openai\": {\"models\": {"
                      "\"m4\": {\"cost\": {\"input\": 9, \"output\": 1}}}}}");
    int port = loopback_listen(&server);
    EXPECT(port > 0);
    child_env("stale", port);
    write_snapshot("{\"openai\": {\"models\": {"
                   "\"m4\": {\"cost\": {\"input\": 2, \"output\": 1}}}}}");
    backdate_snapshot_days(40);

    EXPECT(loopback_serve(&server) == 0);
    catalog_prefetch();
    long stale_days = catalog_stale_days();
    EXPECT(stale_days >= 39 && stale_days <= 41);
    catalog_prefetch();                /* one fetch per run */
    EXPECT(catalog_stale_days() == 0); /* and one report */
    loopback_release(&server);
    EXPECT(wait_for_rate("openai", "m4", 9));

    loopback_stop(&server);
    catalog_shutdown();
}

static void scenario_wait_catalog_starts_fetch(void)
{
    /* A picker or pre-request wait on a catalog-backed provider is itself the trigger: nothing has
     * called catalog_prefetch before it, and the fetched values are visible when it returns,
     * without polling. */
    struct loopback server = {0};
    loopback_reply_ok(&server, 0,
                      "{\"openai\": {\"models\": {"
                      "\"m6\": {\"cost\": {\"input\": 7, \"output\": 1}}}}}");
    int port = loopback_listen(&server);
    EXPECT(port > 0);
    child_env("wait-starts", port);
    EXPECT(loopback_serve(&server) == 0);

    struct provider provider = {.catalog_id = "openai"};
    model_meta_wait_catalog(&provider, 5000, NULL, NULL);
    struct catalog_entry entry;
    EXPECT(catalog_lookup(NULL, "openai", "m6", &entry) == 0);
    EXPECT(entry.cost_input == 7);

    loopback_stop(&server);
    catalog_shutdown();
}

static void scenario_no_identity_never_fetches(void)
{
    /* A provider without a catalog identity (a local server) must not cause any request to the
     * catalog host, however the metadata path is exercised. */
    struct loopback server = {0};
    loopback_reply_ok(&server, 0, "{}");
    int port = loopback_listen(&server);
    EXPECT(port > 0);
    child_env("no-identity", port);

    struct provider local = {.catalog_id = NULL};
    model_meta_prefetch(&local);
    model_meta_wait_catalog(&local, 5000, NULL, NULL);
    model_meta_wait_ms(&local, 5000, NULL, NULL);
    /* Draining returns at once when nothing was fetched and otherwise waits for the fetch, so any
     * request it made has reached the listener by now. */
    catalog_drain(5000);
    uint32_t ready;
    EXPECT(socket_wait_readable(&server.listener_fd, 1, 0, &ready) == 0);
    loopback_stop(&server);
    catalog_shutdown();
}

static int always_cancel(void *user)
{
    (void)user;
    return 1;
}

static void scenario_wait_honors_cancellation(void)
{
    /* A picker's Esc must dismiss the wait at once while the fetch keeps running to completion, so
     * the cache still warms for later callers. The held reply leaves cancelling as the only way the
     * wait can end before its timeout. */
    struct loopback server = {.hold = 1};
    loopback_reply_ok(&server, 0,
                      "{\"openai\": {\"models\": {"
                      "\"m7\": {\"cost\": {\"input\": 7, \"output\": 1}}}}}");
    int port = loopback_listen(&server);
    EXPECT(port > 0);
    child_env("wait-cancel", port);
    EXPECT(loopback_serve(&server) == 0);

    long before = monotonic_ms();
    catalog_prefetch();
    catalog_wait(5000, always_cancel, NULL);
    long elapsed_ms = monotonic_ms() - before;
    EXPECT(elapsed_ms < 1000);
    loopback_release(&server);
    EXPECT(wait_for_rate("openai", "m7", 7)); /* the fetch itself was not cancelled */

    loopback_stop(&server);
    catalog_shutdown();
}

static void scenario_refresh_clears_stale_warning(void)
{
    /* When the refresh lands before the frontend reads the age — a picker waited for it — the stale
     * snapshot is gone and warning about it would be false. */
    struct loopback server = {0};
    loopback_reply_ok(&server, 0,
                      "{\"openai\": {\"models\": {"
                      "\"m8\": {\"cost\": {\"input\": 9, \"output\": 1}}}}}");
    int port = loopback_listen(&server);
    EXPECT(port > 0);
    child_env("stale-refreshed", port);
    write_snapshot("{\"openai\": {\"models\": {"
                   "\"m8\": {\"cost\": {\"input\": 2, \"output\": 1}}}}}");
    backdate_snapshot_days(40);

    EXPECT(loopback_serve(&server) == 0);
    catalog_prefetch();
    catalog_wait(5000, NULL, NULL);
    struct catalog_entry entry;
    EXPECT(catalog_lookup(NULL, "openai", "m8", &entry) == 0);
    EXPECT(entry.cost_input == 9);
    EXPECT(catalog_stale_days() == 0);

    loopback_stop(&server);
    catalog_shutdown();
}

struct scenario {
    const char *name;
    void (*run)(void);
};

static const struct scenario SCENARIOS[] = {
    {"cold-start", scenario_cold_start},
    {"refresh-invalidates-memo", scenario_refresh_invalidates_memo},
    {"garbage-keeps-snapshot", scenario_garbage_keeps_snapshot},
    {"json-error-keeps-snapshot", scenario_json_error_keeps_snapshot},
    {"truncated-tail-keeps-snapshot", scenario_truncated_tail_keeps_snapshot},
    {"invalid-member-keeps-snapshot", scenario_invalid_member_keeps_snapshot},
    {"trailing-garbage-keeps-snapshot", scenario_trailing_garbage_keeps_snapshot},
    {"drain-completes-fetch", scenario_drain_completes_fetch},
    {"stale-snapshot-warns", scenario_stale_snapshot_warns},
    {"wait-catalog-starts-fetch", scenario_wait_catalog_starts_fetch},
    {"no-identity-never-fetches", scenario_no_identity_never_fetches},
    {"wait-honors-cancellation", scenario_wait_honors_cancellation},
    {"refresh-clears-stale-warning", scenario_refresh_clears_stale_warning},
};

int main(int argc, char **argv)
{
    if (argc == 3) {
        g_root = argv[2];
        for (size_t i = 0; i < sizeof(SCENARIOS) / sizeof(*SCENARIOS); i++) {
            if (strcmp(argv[1], SCENARIOS[i].name) == 0) {
                SCENARIOS[i].run();
                T_REPORT();
            }
        }
        return 2;
    }
    char *program = t_program_path(argv[0]);
    EXPECT(program != NULL);
    if (!program)
        T_REPORT();
    g_root = t_tempdir();
    for (size_t i = 0; i < sizeof(SCENARIOS) / sizeof(*SCENARIOS); i++) {
        const char *child_argv[] = {program, SCENARIOS[i].name, g_root, NULL};
        struct t_process *child = t_process_start(child_argv);
        EXPECT(child != NULL);
        if (!child)
            continue;
        int code = t_process_wait(child, 10000);
        if (code != 0)
            FAIL("scenario '%s' failed in child (exit %d)", SCENARIOS[i].name, code);
        t_process_close(child);
    }
    free(program);
    T_REPORT();
}
