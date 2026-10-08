/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"
#include "system/fs.h"

char *fs_shell_head(const char *shell_cmd)
{
    if (!shell_cmd)
        return NULL;
    shell_cmd += strspn(shell_cmd, " \t");
    size_t head_len = strcspn(shell_cmd, " \t");
    if (head_len == 0)
        return NULL;
    if (strcspn(shell_cmd, "\"'`\\$;|&<>()=~*?[#") < head_len)
        return NULL;

    char *head = xmalloc(head_len + 1);
    memcpy(head, shell_cmd, head_len);
    head[head_len] = '\0';
    return head;
}

int fs_shell_head_resolves(const char *shell_cmd)
{
    if (!shell_cmd)
        return 0;
    shell_cmd += strspn(shell_cmd, " \t");
    if (*shell_cmd == '\0')
        return 0;

    char *head = fs_shell_head(shell_cmd);
    /* Quoting, expansion, assignments, redirection, or globs in the head defeat a plain PATH
     * lookup, so assume the shell can start such a command. */
    if (!head)
        return 1;
    char *path = fs_which(head);
    int resolves = path != NULL;
    free(path);
    free(head);
    return resolves;
}
