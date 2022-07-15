/*
 * Flootay – a video overlay generator
 * Copyright (C) 2021, 2022  Neil Roberts
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

#ifndef FLT_SOURCE_H
#define FLT_SOURCE_H

#include <stdbool.h>
#include <stdlib.h>

#include "flt-error.h"

struct flt_source {
        /* Read *length bytes from the source. If an error is
         * encountered then false is returned and *error is set.
         * Otherwise true is returned. If EOF is encountered before
         * reading the full amount of data then true is still returned
         * and *length is modified to contain the actual number of
         * bytes read.
         */
        bool (* read_source)(struct flt_source *source,
                             void *ptr,
                             size_t *length,
                             struct flt_error **error);
};

#endif /* FLT_SOURCE_H */
