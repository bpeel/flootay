/*
 * Flootay – a video overlay generator
 * Copyright (C) 2013, 2019, 2022  Neil Roberts
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef FLT_ERROR_H
#define FLT_ERROR_H

#include <stdarg.h>

#include "flt-util.h"

/* Exception handling mechanism inspired by glib's GError */

struct flt_error_domain {
        int stub;
};

struct flt_error {
        struct flt_error_domain *domain;
        int code;
        char message[1];
};

void
flt_set_error_va_list(struct flt_error **error_out,
                      struct flt_error_domain *domain,
                      int code,
                      const char *format,
                      va_list ap);

FLT_PRINTF_FORMAT(4, 5) void
flt_set_error(struct flt_error **error,
              struct flt_error_domain *domain,
              int code,
              const char *format,
              ...);

void
flt_error_free(struct flt_error *error);

void
flt_error_clear(struct flt_error **error);

void
flt_error_propagate(struct flt_error **error,
                    struct flt_error *other);

#endif /* FLT_ERROR_H */
