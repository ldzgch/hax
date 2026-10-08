/* SPDX-License-Identifier: MIT */
#include <stdlib.h>

#include "buf.h"
#include "xalloc.h"
#include "system/locale.h"
#include "system/spawn.h"
#include "text/shell_quote.h"

/* Everything a pinned LC_ALL was covering besides the charset. Clearing it is what lets LC_CTYPE
 * apply at all, so each category is restated at the value it had: hax is entitled to the encoding
 * its own text is in, not to the user's collation, messages or number format. */
static const char *const PINNED_CATEGORIES[] = {
    "LC_COLLATE", "LC_MONETARY", "LC_NUMERIC", "LC_TIME", "LC_MESSAGES",
};

char *spawn_shell_cmd_force_utf8(char *shell_cmd)
{
#ifdef _WIN32
    const char *ctype = shell_cmd ? "C.UTF-8" : NULL;
#else
    const char *ctype = shell_cmd ? locale_child_ctype_override() : NULL;
#endif
    if (!ctype)
        return shell_cmd;

    struct buf command;
    buf_init(&command);
    buf_append_str(&command, "export");

    const char *pinned = getenv("LC_ALL");
    char *quoted_pinned = pinned && *pinned ? shell_single_quote(pinned) : NULL;
    for (size_t i = 0; quoted_pinned && i < sizeof(PINNED_CATEGORIES) / sizeof(*PINNED_CATEGORIES);
         i++) {
        char *assignment = xasprintf(" %s=%s", PINNED_CATEGORIES[i], quoted_pinned);
        buf_append_str(&command, assignment);
        free(assignment);
    }
    free(quoted_pinned);

    /* An assignment prefix would only reach a simple command, and `sh` rejects one before the
     * `{ ...; }` a caller may well pass. Exporting first reaches any command shape. setlocale()
     * ignores an empty LC_ALL, which the pinned one would otherwise outrank. */
    char *quoted_ctype = shell_single_quote(ctype);
    char *tail = xasprintf(" LC_ALL= LC_CTYPE=%s; %s", quoted_ctype, shell_cmd);
    buf_append_str(&command, tail);
    free(tail);
    free(quoted_ctype);
    free(shell_cmd);
    return buf_steal(&command);
}
