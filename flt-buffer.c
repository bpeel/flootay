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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "flt-buffer.h"

void
flt_buffer_init(struct flt_buffer *buffer)
{
        static const struct flt_buffer init = FLT_BUFFER_STATIC_INIT;

        *buffer = init;
}

void
flt_buffer_ensure_size(struct flt_buffer *buffer,
                       size_t size)
{
        size_t new_size = MAX(buffer->size, 1);

        while (new_size < size)
                new_size *= 2;

        if (new_size != buffer->size) {
                buffer->data = flt_realloc(buffer->data, new_size);
                buffer->size = new_size;
        }
}

void
flt_buffer_set_length(struct flt_buffer *buffer,
                      size_t length)
{
        flt_buffer_ensure_size(buffer, length);
        buffer->length = length;
}

void
flt_buffer_append_vprintf(struct flt_buffer *buffer,
                          const char *format,
                          va_list ap)
{
        va_list apcopy;
        int length;

        flt_buffer_ensure_size(buffer, buffer->length + 16);

        va_copy(apcopy, ap);
        length = vsnprintf((char *) buffer->data + buffer->length,
                           buffer->size - buffer->length,
                           format,
                           ap);

        if (length >= buffer->size - buffer->length) {
                flt_buffer_ensure_size(buffer, buffer->length + length + 1);
                vsnprintf((char *) buffer->data + buffer->length,
                          buffer->size - buffer->length,
                          format,
                          apcopy);
        }

        va_end(apcopy);

        buffer->length += length;
}

FLT_PRINTF_FORMAT(2, 3) void
flt_buffer_append_printf(struct flt_buffer *buffer,
                         const char *format,
                         ...)
{
        va_list ap;

        va_start(ap, format);
        flt_buffer_append_vprintf(buffer, format, ap);
        va_end(ap);
}

void
flt_buffer_append(struct flt_buffer *buffer,
                  const void *data,
                  size_t length)
{
        flt_buffer_ensure_size(buffer, buffer->length + length);
        memcpy(buffer->data + buffer->length, data, length);
        buffer->length += length;
}

void
flt_buffer_append_string(struct flt_buffer *buffer,
                         const char *str)
{
        flt_buffer_append(buffer, str, strlen(str) + 1);
        buffer->length--;
}

void
flt_buffer_destroy(struct flt_buffer *buffer)
{
        flt_free(buffer->data);
}
