/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"
#include "terminal/ansi.h"
#include "terminal/clipboard.h"
#include "text/base64.h"

#define OSC52_PREFIX      ANSI_ESC "]52;c;"
#define OSC52_SUFFIX      ANSI_BEL
#define TMUX_OSC52_PREFIX ANSI_TMUX_PASSTHROUGH_BEGIN OSC52_PREFIX
#define TMUX_OSC52_SUFFIX OSC52_SUFFIX ANSI_TMUX_PASSTHROUGH_END

char *clipboard_osc52_sequence(const char *text, size_t text_len, int tmux_wrap, size_t *out_len)
{
    if (text_len > CLIPBOARD_OSC52_MAX_BYTES)
        return NULL;

    size_t encoded_len;
    char *encoded = base64_encode(text, text_len, &encoded_len);
    const char *prefix = tmux_wrap ? TMUX_OSC52_PREFIX : OSC52_PREFIX;
    const char *suffix = tmux_wrap ? TMUX_OSC52_SUFFIX : OSC52_SUFFIX;
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t sequence_len = prefix_len + encoded_len + suffix_len;
    char *sequence = xmalloc(sequence_len + 1);

    memcpy(sequence, prefix, prefix_len);
    memcpy(sequence + prefix_len, encoded, encoded_len);
    memcpy(sequence + prefix_len + encoded_len, suffix, suffix_len + 1);
    free(encoded);

    if (out_len)
        *out_len = sequence_len;
    return sequence;
}
