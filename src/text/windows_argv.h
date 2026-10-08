/* SPDX-License-Identifier: MIT */
#ifndef HAX_TEXT_WINDOWS_ARGV_H
#define HAX_TEXT_WINDOWS_ARGV_H

/* Encode a NULL-terminated UTF-8 argv for the Windows C runtime's argument parser. Returns an
 * allocated command line, or NULL with errno EINVAL for an absent/empty program name. */
char *windows_argv_encode(const char *const *argv);

#endif /* HAX_TEXT_WINDOWS_ARGV_H */
