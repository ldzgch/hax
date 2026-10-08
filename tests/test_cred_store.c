/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cred_store.h"
#include "env.h"
#include "files.h"
#include "harness.h"
#include "process.h"
#include "xalloc.h"
#include "system/fs.h"

/* Point the store at a scratch state directory the test controls. */
static void scratch_state_home(void)
{
    char *dir = t_tempdir();
    t_env_set("XDG_STATE_HOME", dir);
}

static void test_missing_store(void)
{
    scratch_state_home();
    EXPECT(cred_store_get("codex") == NULL);
    EXPECT(cred_store_delete("codex") == 0);
}

static void test_set_get_delete_roundtrip(void)
{
    scratch_state_home();

    json_t *entry = json_pack("{s:s, s:s}", "access_token", "at", "refresh_token", "rt");
    EXPECT(cred_store_set("codex", entry) == 0);
    json_decref(entry);

    json_t *loaded = cred_store_get("codex");
    EXPECT(loaded != NULL);
    if (loaded) {
        EXPECT_STR_EQ(json_string_value(json_object_get(loaded, "access_token")), "at");
        EXPECT_STR_EQ(json_string_value(json_object_get(loaded, "refresh_token")), "rt");
        json_decref(loaded);
    }

    EXPECT(cred_store_delete("codex") == 1);
    EXPECT(cred_store_get("codex") == NULL);
    EXPECT(cred_store_delete("codex") == 0);
}

/* Credentials must never be world- or group-readable, including right after creation. */
static void test_store_mode_0600(void)
{
    scratch_state_home();

    json_t *entry = json_pack("{s:s}", "access_token", "at");
    EXPECT(cred_store_set("codex", entry) == 0);
    json_decref(entry);

    char *path = cred_store_file_path();
    EXPECT(path != NULL);
    if (path) {
        t_expect_private_file(path);
        free(path);
    }
}

static void test_entries_are_independent(void)
{
    scratch_state_home();

    json_t *codex = json_pack("{s:s}", "access_token", "codex-token");
    json_t *other = json_pack("{s:s}", "api_key", "other-key");
    EXPECT(cred_store_set("codex", codex) == 0);
    EXPECT(cred_store_set("other", other) == 0);
    json_decref(codex);
    json_decref(other);

    EXPECT(cred_store_delete("codex") == 1);
    json_t *loaded = cred_store_get("other");
    EXPECT(loaded != NULL);
    if (loaded) {
        EXPECT_STR_EQ(json_string_value(json_object_get(loaded, "api_key")), "other-key");
        json_decref(loaded);
    }
}

static void write_store_file(const char *contents)
{
    char *path = cred_store_file_path();
    EXPECT(path != NULL);
    if (!path)
        return;
    FILE *file = fopen(path, "w");
    EXPECT(file != NULL);
    if (file) {
        fputs(contents, file);
        fclose(file);
    }
    free(path);
}

static void test_corrupt_store(void)
{
    scratch_state_home();

    json_t *entry = json_pack("{s:s}", "access_token", "at");
    EXPECT(cred_store_set("codex", entry) == 0);
    write_store_file("{not json");

    /* Reads and deletes refuse to guess; a new login replaces the unreadable file. */
    EXPECT(cred_store_get("codex") == NULL);
    EXPECT(cred_store_delete("codex") == -1);
    EXPECT(cred_store_set("codex", entry) == 0);
    json_decref(entry);

    json_t *loaded = cred_store_get("codex");
    EXPECT(loaded != NULL);
    json_decref(loaded);
}

static enum cred_store_verdict bump_counter(json_t *entry, json_t **replacement, void *ctx)
{
    (void)ctx;
    if (!entry)
        return CRED_STORE_KEEP;
    json_int_t count = json_integer_value(json_object_get(entry, "count"));
    json_incref(entry);
    json_object_set_new(entry, "count", json_integer(count + 1));
    *replacement = entry;
    return CRED_STORE_WRITE;
}

static enum cred_store_verdict decline_update(json_t *entry, json_t **replacement, void *ctx)
{
    (void)replacement;
    *(json_t **)ctx = entry;
    return CRED_STORE_KEEP;
}

static enum cred_store_verdict create_entry(json_t *entry, json_t **replacement, void *ctx)
{
    (void)entry;
    (void)ctx;
    *replacement = json_pack("{s:s}", "access_token", "created");
    return CRED_STORE_WRITE;
}

static enum cred_store_verdict remove_entry(json_t *entry, json_t **replacement, void *ctx)
{
    (void)entry;
    (void)replacement;
    (void)ctx;
    return CRED_STORE_REMOVE;
}

static void test_update_transaction(void)
{
    scratch_state_home();

    /* Declining against an absent entry writes nothing and observes NULL. */
    json_t *seen = (json_t *)0x1;
    EXPECT(cred_store_update("codex", decline_update, &seen) == 0);
    EXPECT(seen == NULL);
    EXPECT(cred_store_get("codex") == NULL);

    /* A returned entry is written; mutating the current entry in place is the typical shape. */
    json_t *entry = json_pack("{s:i}", "count", 1);
    EXPECT(cred_store_set("codex", entry) == 0);
    json_decref(entry);
    EXPECT(cred_store_update("codex", bump_counter, NULL) == 1);

    json_t *loaded = cred_store_get("codex");
    EXPECT(loaded != NULL);
    if (loaded) {
        EXPECT(json_integer_value(json_object_get(loaded, "count")) == 2);
        json_decref(loaded);
    }

    /* Declining leaves the stored entry untouched. */
    seen = NULL;
    EXPECT(cred_store_update("codex", decline_update, &seen) == 0);
    EXPECT(seen != NULL);
    loaded = cred_store_get("codex");
    EXPECT(loaded != NULL);
    if (loaded) {
        EXPECT(json_integer_value(json_object_get(loaded, "count")) == 2);
        json_decref(loaded);
    }

    /* An update may also create the entry. */
    EXPECT(cred_store_delete("codex") == 1);
    EXPECT(cred_store_update("codex", create_entry, NULL) == 1);
    loaded = cred_store_get("codex");
    EXPECT(loaded != NULL);
    if (loaded) {
        EXPECT_STR_EQ(json_string_value(json_object_get(loaded, "access_token")), "created");
        json_decref(loaded);
    }

    /* An update may remove the entry; removing an absent one changes nothing. */
    EXPECT(cred_store_update("codex", remove_entry, NULL) == 1);
    EXPECT(cred_store_get("codex") == NULL);
    EXPECT(cred_store_update("codex", remove_entry, NULL) == 0);
}

static void test_take_returns_removed_entry(void)
{
    scratch_state_home();

    json_t *taken = (json_t *)0x1;
    EXPECT(cred_store_take("codex", &taken) == 0);
    EXPECT(taken == NULL);

    json_t *entry = json_pack("{s:s}", "refresh_token", "rt");
    EXPECT(cred_store_set("codex", entry) == 0);
    json_decref(entry);

    EXPECT(cred_store_take("codex", &taken) == 1);
    EXPECT(taken != NULL);
    if (taken) {
        EXPECT_STR_EQ(json_string_value(json_object_get(taken, "refresh_token")), "rt");
        json_decref(taken);
    }
    EXPECT(cred_store_get("codex") == NULL);
}

static void test_symlink_aliases_share_transaction_lock(const char *program)
{
    scratch_state_home();
    json_t *entry = json_pack("{s:i}", "count", 0);
    EXPECT(cred_store_set("codex", entry) == 0);
    json_decref(entry);
    char *destination = cred_store_file_path();
    const char *homes[] = {t_tempdir(), t_tempdir()};
    struct t_process *children[2] = {0};
    enum { UPDATES = 64 };

    for (size_t i = 0; i < 2; i++) {
        char *directory = xasprintf("%s/hax", homes[i]);
        EXPECT(fs_mkdir_p(directory) == 0);
        char *alias = xasprintf("%s/auth.json", directory);
        int result = t_symlink(destination, alias, 0);
        int error = errno;
        free(alias);
        free(directory);
        if (result < 0) {
            free(destination);
            if (error == EPERM)
                T_SKIP("symlink creation requires Developer Mode or symlink privilege");
            FAIL("cannot create credential alias: %s", strerror(error));
            return;
        }
    }
    for (size_t i = 0; i < 2; i++) {
        const char *child_argv[] = {program, "--update-child", homes[i], NULL};
        children[i] = t_process_start(child_argv);
        EXPECT(children[i] != NULL);
    }
    for (size_t i = 0; i < 2; i++) {
        if (!children[i])
            continue;
        EXPECT(t_process_wait(children[i], 15000) == 0);
        t_process_close(children[i]);

        char *alias_lock = xasprintf("%s/hax/auth.json.lock", homes[i]);
        EXPECT(fs_entry_exists(alias_lock) == 0);
        free(alias_lock);
        char *alias = xasprintf("%s/hax/auth.json", homes[i]);
        EXPECT(t_file_is_symlink(alias));
        free(alias);
    }
    json_t *loaded = cred_store_get("codex");
    EXPECT(loaded != NULL);
    EXPECT(json_integer_value(json_object_get(loaded, "count")) == 2 * UPDATES);
    json_decref(loaded);
    free(destination);
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--update-child") == 0) {
        t_env_set("XDG_STATE_HOME", argv[2]);
        for (int update = 0; update < 64; update++)
            EXPECT(cred_store_update("codex", bump_counter, NULL) == 1);
        T_REPORT();
    }
    char *program = t_program_path(argv[0]);
    EXPECT(program != NULL);
    if (program)
        test_symlink_aliases_share_transaction_lock(program);
    free(program);
    test_missing_store();
    test_set_get_delete_roundtrip();
    test_store_mode_0600();
    test_entries_are_independent();
    test_corrupt_store();
    test_update_transaction();
    test_take_returns_removed_entry();
    T_REPORT();
}
