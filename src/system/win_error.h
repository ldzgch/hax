/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_WIN_ERROR_H
#define HAX_SYSTEM_WIN_ERROR_H

/* Translate a saved Win32 error into errno for platform-independent callers. */
void win_error_set_errno(unsigned long error);

#endif /* HAX_SYSTEM_WIN_ERROR_H */
