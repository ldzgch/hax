/* SPDX-License-Identifier: MIT */
#ifndef HAX_TEXT_UNICODE_CELLS_H
#define HAX_TEXT_UNICODE_CELLS_H

#include <stdint.h>

/* Locale-independent width of one Unicode scalar value: -1 for controls/unassigned/invalid,
 * 0 for combining/format characters, 2 for wide/fullwidth, and 1 otherwise. Ambiguous width is
 * narrow. Measures individual codepoints; joined emoji and grapheme clusters are not combined. */
int unicode_codepoint_cells(uint32_t codepoint);

#endif /* HAX_TEXT_UNICODE_CELLS_H */
