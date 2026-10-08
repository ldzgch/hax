/* SPDX-License-Identifier: MIT */
#include "text/glob.h"

#ifndef _WIN32
#include <fnmatch.h>
#else
#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "text/utf8.h"

static uint32_t next_character(const char **cursor)
{
    const unsigned char *bytes = (const unsigned char *)*cursor;
    size_t count = utf8_sequence_length(*bytes);
    if (!*bytes)
        return 0;
    size_t available = 0;
    while (available < count && bytes[available])
        available++;
    if (available < count || !utf8_sequence_is_valid(*cursor, count))
        count = 1;
    uint32_t value = count == 1 ? *bytes : *bytes & (0x7fu >> count);
    for (size_t i = 1; i < count; i++)
        value = (value << 6) | (bytes[i] & 0x3f);
    *cursor += count;
    return value;
}

struct character_class {
    const char *name;
    int (*matches)(int);
};

static const struct character_class CLASSES[] = {
    {"alnum", isalnum}, {"alpha", isalpha}, {"blank", isblank}, {"cntrl", iscntrl},
    {"digit", isdigit}, {"graph", isgraph}, {"lower", islower}, {"print", isprint},
    {"punct", ispunct}, {"space", isspace}, {"upper", isupper}, {"xdigit", isxdigit},
};

static int named_class(const char *name, size_t length, uint32_t value)
{
    if (value > 127)
        return 0;
    for (size_t i = 0; i < sizeof(CLASSES) / sizeof(*CLASSES); i++) {
        if (strlen(CLASSES[i].name) == length && !memcmp(name, CLASSES[i].name, length))
            return !!CLASSES[i].matches((int)value);
    }
    return 0;
}

/* Return -1 for an unterminated set, leaving the caller to match '[' literally. */
static int match_set(const char **pattern, uint32_t value)
{
    const char *cursor = *pattern + 1;
    int negate = *cursor == '!' || *cursor == '^';
    cursor += negate;
    int matched = 0;
    int first = 1;
    while (*cursor) {
        if (*cursor == ']' && !first) {
            *pattern = cursor + 1;
            return matched != negate;
        }
        first = 0;
        if (cursor[0] == '[' && cursor[1] == ':') {
            const char *end = strstr(cursor + 2, ":]");
            if (!end)
                return -1;
            matched |= named_class(cursor + 2, (size_t)(end - cursor - 2), value);
            cursor = end + 2;
            continue;
        }
        if (*cursor == '\\' && cursor[1])
            cursor++;
        uint32_t low = next_character(&cursor);
        uint32_t high = low;
        if (*cursor == '-' && cursor[1] && cursor[1] != ']') {
            cursor++;
            if (*cursor == '\\' && cursor[1])
                cursor++;
            high = next_character(&cursor);
        }
        matched |= value >= low && value <= high;
    }
    return -1;
}

static int match_token(const char **pattern, uint32_t character)
{
    if (**pattern == '?') {
        (*pattern)++;
        return 1;
    }
    if (**pattern == '[') {
        int result = match_set(pattern, character);
        if (result >= 0)
            return result;
    }
    if (**pattern == '\\') {
        (*pattern)++;
        if (!**pattern)
            return 0;
    }
    return next_character(pattern) == character;
}
#endif

int glob_match(const char *pattern, const char *text)
{
    if (!pattern || !text)
        return 0;
#ifndef _WIN32
    return fnmatch(pattern, text, 0) == 0;
#else
    const char *star_pattern = NULL;
    const char *star_text = NULL;
    while (*text) {
        if (*pattern == '*') {
            while (*pattern == '*')
                pattern++;
            star_pattern = pattern;
            star_text = text;
            if (!*pattern)
                return 1;
        }
        const char *after_text = text;
        uint32_t character = next_character(&after_text);
        const char *after_pattern = pattern;
        if (*pattern && match_token(&after_pattern, character)) {
            pattern = after_pattern;
            text = after_text;
        } else if (star_pattern && *star_text) {
            next_character(&star_text);
            text = star_text;
            pattern = star_pattern;
        } else {
            return 0;
        }
    }
    while (*pattern == '*')
        pattern++;
    return !*pattern;
#endif
}
