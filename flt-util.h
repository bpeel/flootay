/*
 * Flootay – a video overlay generator
 * Copyright (C) 2019, 2022  Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FLT_UTIL_H
#define FLT_UTIL_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __GNUC__
#define FLT_NO_RETURN __attribute__((noreturn))
#define FLT_PRINTF_FORMAT(string_index, first_to_check) \
  __attribute__((format(printf, string_index, first_to_check)))
#define FLT_NULL_TERMINATED __attribute__((sentinel))
#else
#define FLT_PRINTF_FORMAT(string_index, first_to_check)
#define FLT_NULL_TERMINATED
#ifdef _MSC_VER
#define FLT_NO_RETURN __declspec(noreturn)
#else
#define FLT_NO_RETURN
#endif
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define FLT_STRINGIFY(macro_or_string) FLT_STRINGIFY_ARG(macro_or_string)
#define FLT_STRINGIFY_ARG(contents) #contents

#define FLT_N_ELEMENTS(array) \
  (sizeof (array) / sizeof ((array)[0]))

void *
flt_alloc(size_t size);

void *
flt_calloc(size_t size);

void *
flt_realloc(void *ptr, size_t size);

void
flt_free(void *ptr);

FLT_NULL_TERMINATED char *
flt_strconcat(const char *string1, ...);

char *
flt_strdup(const char *str);

char *
flt_strndup(const char *str, size_t size);

void *
flt_memdup(const void *data, size_t size);

FLT_NO_RETURN
FLT_PRINTF_FORMAT(1, 2)
void
flt_fatal(const char *format, ...);

FLT_PRINTF_FORMAT(1, 2) void
flt_warning(const char *format, ...);

#endif /* FLT_UTIL_H */
