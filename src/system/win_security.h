/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_WIN_SECURITY_H
#define HAX_SYSTEM_WIN_SECURITY_H

#include <windows.h>

/* Security attributes for new private files/directories: a protected DACL grants full access only
 * to the current process user. The ACL is owned; free after the creating API returns. */
struct win_private_security {
    SECURITY_DESCRIPTOR descriptor;
    SECURITY_ATTRIBUTES attributes;
    ACL *acl;
    TOKEN_USER *user;
};

/* Return 0 on success, or -1 with errno set. A failed initialization needs no cleanup. */
int win_private_security_init(struct win_private_security *security);
void win_private_security_free(struct win_private_security *security);

#endif /* HAX_SYSTEM_WIN_SECURITY_H */
