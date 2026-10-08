/* SPDX-License-Identifier: MIT */
#ifndef HAX_FORMAT_ATTR_H
#define HAX_FORMAT_ATTR_H

/* Match the C99 formatter selected by the native MinGW build, including %zu and %td. */
#ifdef __MINGW32__
#define HAX_PRINTF_FORMAT gnu_printf
#else
#define HAX_PRINTF_FORMAT printf
#endif

#endif /* HAX_FORMAT_ATTR_H */
