/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_LINE_READER_H
#define HAX_SYSTEM_LINE_READER_H

#include <stddef.h>
#include <stdio.h>

/* Read a line, including its newline, into a reusable owned buffer. Allocate or grow *line as
 * needed, updating *capacity. Return the byte count excluding the added NUL, or -1 on EOF/error.
 * Embedded NULs remain in the buffer; a final unterminated line is returned before EOF. */
ptrdiff_t line_reader_get(FILE *stream, char **line, size_t *capacity);

#endif /* HAX_SYSTEM_LINE_READER_H */
