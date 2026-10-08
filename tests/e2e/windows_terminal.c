/* SPDX-License-Identifier: MIT */
#include <jansson.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system/line_reader.h"
#include "terminal/win_console.h"
#include "text/utf8_sanitize.h"
#include "tools/bash_fixtures.h"

static void reply(json_t *result)
{
    char *line = json_dumps(result, JSON_COMPACT);
    if (line) {
        puts(line);
        fflush(stdout);
    }
    free(line);
    json_decref(result);
}

static int bounded_integer(const json_t *request, const char *key, int fallback)
{
    json_t *value = json_object_get(request, key);
    if (!json_is_integer(value))
        return fallback;
    json_int_t number = json_integer_value(value);
    return number >= 0 && number <= INT_MAX ? (int)number : fallback;
}

static json_t *perform(struct t_win_console *console, const json_t *request, size_t *mark,
                       const char *gate)
{
    const char *op = json_string_value(json_object_get(request, "op"));
    int ok = 0;
    json_t *result = json_object();
    if (!op)
        goto out;
    if (strcmp(op, "mark") == 0) {
        *mark = t_win_console_mark(console);
        ok = 1;
    } else if (strcmp(op, "release") == 0) {
        gate_release(gate);
        ok = 1;
    } else if (strcmp(op, "send") == 0) {
        json_t *value = json_object_get(request, "bytes");
        const char *bytes = json_string_value(value);
        size_t length = bytes ? json_string_length(value) : 0;
        if (bytes && length <= UINT_MAX)
            ok = t_win_console_send(console, bytes, (unsigned)length);
    } else if (strcmp(op, "key") == 0) {
        ok = t_win_console_key(console, bounded_integer(request, "key", 0),
                               bounded_integer(request, "utf16", 0),
                               bounded_integer(request, "controls", 0));
    } else if (strcmp(op, "expect") == 0) {
        const char *text = json_string_value(json_object_get(request, "text"));
        if (text)
            ok = t_win_console_expect_since(console, text,
                                            bounded_integer(request, "timeout_ms", 3000), *mark);
    } else if (strcmp(op, "output") == 0) {
        char *plain = t_win_console_output(console, *mark);
        char *clean = utf8_sanitize(plain, strlen(plain));
        json_object_set_new(result, "text", json_string(clean));
        free(clean);
        free(plain);
        ok = 1;
    } else if (strcmp(op, "wait") == 0) {
        unsigned long code = 0;
        int waited =
            t_win_console_wait(console, bounded_integer(request, "timeout_ms", 3000), &code);
        json_object_set_new(result, "result", json_integer(waited));
        json_object_set_new(result, "exit_code", json_integer(code));
        ok = waited == 1;
    }
out:
    json_object_set_new(result, "ok", json_boolean(ok));
    return result;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    struct t_win_console *console = t_win_console_start((const char *const *)(argv + 1));
    if (!console) {
        reply(json_pack("{s:b}", "ok", 0));
        return 1;
    }
    char *gate = gate_create();
    reply(json_pack("{s:b,s:s}", "ok", 1, "gate", gate));
    char *line = NULL;
    size_t capacity = 0;
    size_t mark = 0;
    while (line_reader_get(stdin, &line, &capacity) >= 0) {
        json_t *request = json_loads(line, 0, NULL);
        reply(perform(console, request, &mark, gate));
        json_decref(request);
    }
    free(line);
    free(gate);
    t_win_console_close(console);
    return 0;
}
