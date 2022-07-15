/*
 * Flootay – a video overlay generator
 * Copyright (C) 2013, 2021  Neil Roberts
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

#ifndef FLT_BUFFER_H
#define FLT_BUFFER_H

#include <stdint.h>
#include <stdarg.h>

#include "flt-util.h"

struct flt_buffer {
        uint8_t *data;
        size_t length;
        size_t size;
};

#define FLT_BUFFER_STATIC_INIT { .data = NULL, .length = 0, .size = 0 }

void
flt_buffer_init(struct flt_buffer *buffer);

void
flt_buffer_ensure_size(struct flt_buffer *buffer,
                       size_t size);

void
flt_buffer_set_length(struct flt_buffer *buffer,
                      size_t length);

FLT_PRINTF_FORMAT(2, 3) void
flt_buffer_append_printf(struct flt_buffer *buffer,
                         const char *format,
                         ...);

void
flt_buffer_append_vprintf(struct flt_buffer *buffer,
                          const char *format,
                          va_list ap);

void
flt_buffer_append(struct flt_buffer *buffer,
                  const void *data,
                  size_t length);

static inline void
flt_buffer_append_c(struct flt_buffer *buffer,
                    char c)
{
        if (buffer->size > buffer->length)
                buffer->data[buffer->length++] = c;
        else
                flt_buffer_append(buffer, &c, 1);
}

void
flt_buffer_append_string(struct flt_buffer *buffer,
                         const char *str);

void
flt_buffer_destroy(struct flt_buffer *buffer);

#endif /* FLT_BUFFER_H */
