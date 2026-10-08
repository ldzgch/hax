/* SPDX-License-Identifier: MIT */
#ifndef HAX_TEXT_GLOB_H
#define HAX_TEXT_GLOB_H

/* Case-sensitive whole-string shell glob matching, with *, ?, bracket sets/ranges, and backslash
 * escapes. Separators and leading dots are ordinary characters. Returns 1 for a match, 0 otherwise.
 * Native Windows named character classes use the C locale's ASCII definitions. */
int glob_match(const char *pattern, const char *text);

#endif /* HAX_TEXT_GLOB_H */
