/* SPDX-License-Identifier: MIT */
#include "win_files.h"

#include <windows.h>
#include <aclapi.h>
#include <stdlib.h>

#include "harness.h"
#include "xalloc.h"
#include "system/win_utf8.h"

void t_win_expect_file_bytes(const char *path, const void *expected, size_t length)
{
    wchar_t *wide = win_utf8_to_wide(path);
    EXPECT(wide != NULL);
    if (!wide)
        return;
    HANDLE file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    free(wide);
    EXPECT(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE)
        return;
    char *bytes = xmalloc(length + 1);
    DWORD count = 0;
    EXPECT(ReadFile(file, bytes, (DWORD)length + 1, &count, NULL));
    EXPECT_MEM_EQ(bytes, count, expected, length);
    free(bytes);
    CloseHandle(file);
}

void t_win_expect_private_acl(const char *path)
{
    wchar_t *wide = win_utf8_to_wide(path);
    PSID owner = NULL;
    PACL acl = NULL;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    DWORD result = GetNamedSecurityInfoW(wide, SE_FILE_OBJECT,
                                         OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                         &owner, NULL, &acl, NULL, &descriptor);
    EXPECT(result == ERROR_SUCCESS);
    if (result == ERROR_SUCCESS) {
        EXPECT(acl != NULL && acl->AceCount == 1);
        if (acl && acl->AceCount == 1) {
            ACCESS_ALLOWED_ACE *ace = NULL;
            EXPECT(GetAce(acl, 0, (void **)&ace));
            if (ace) {
                EXPECT(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE);
                EXPECT(EqualSid(owner, &ace->SidStart));
                EXPECT(!(ace->Header.AceFlags & INHERITED_ACE));
            }
        }
        SECURITY_DESCRIPTOR_CONTROL control;
        DWORD revision;
        EXPECT(GetSecurityDescriptorControl(descriptor, &control, &revision));
        EXPECT(control & SE_DACL_PROTECTED);
    }
    LocalFree(descriptor);
    free(wide);
}
