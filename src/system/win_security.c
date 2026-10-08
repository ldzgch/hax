/* SPDX-License-Identifier: MIT */
#include "system/win_security.h"

#include <aclapi.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"
#include "system/win_error.h"

int win_private_security_init(struct win_private_security *security)
{
    memset(security, 0, sizeof(*security));
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        win_error_set_errno(GetLastError());
        return -1;
    }
    DWORD count = 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &count);
    TOKEN_USER *user = xmalloc(count);
    security->user = user;
    int result = -1;
    DWORD error;
    if (!GetTokenInformation(token, TokenUser, user, count, &count)) {
        error = GetLastError();
        goto out;
    }
    EXPLICIT_ACCESSW access = {0};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = (wchar_t *)user->User.Sid;
    error = SetEntriesInAclW(1, &access, NULL, &security->acl);
    if (error != ERROR_SUCCESS)
        goto out;
    if (!InitializeSecurityDescriptor(&security->descriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorOwner(&security->descriptor, user->User.Sid, FALSE) ||
        !SetSecurityDescriptorDacl(&security->descriptor, TRUE, security->acl, FALSE) ||
        !SetSecurityDescriptorControl(&security->descriptor, SE_DACL_PROTECTED,
                                      SE_DACL_PROTECTED)) {
        error = GetLastError();
        goto out;
    }
    security->attributes.nLength = sizeof(security->attributes);
    security->attributes.lpSecurityDescriptor = &security->descriptor;
    result = 0;
out:
    CloseHandle(token);
    if (result < 0) {
        win_private_security_free(security);
        win_error_set_errno(error);
    }
    return result;
}

void win_private_security_free(struct win_private_security *security)
{
    if (security->acl)
        LocalFree(security->acl);
    free(security->user);
    memset(security, 0, sizeof(*security));
}
