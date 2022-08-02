/*
 * Flootay – a video overlay generator
 * Copyright (C) 2022  Neil Roberts
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

#ifndef FLT_GPX
#define FLT_GPX

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "flt-error.h"

extern struct flt_error_domain
flt_gpx_error;

enum flt_gpx_error {
        FLT_GPX_ERROR_INVALID,
};

struct flt_gpx_point {
        /* Time since Unix epoch in seconds */
        int64_t time;
        /* Velocity in metres per second at that time */
        float speed;
        /* Elevation in metres above sea level */
        float elevation;
};

struct flt_gpx_data {
        double speed, elevation;
};

bool
flt_gpx_parse(const char *filename,
              struct flt_gpx_point **points_out,
              size_t *n_points_out,
              struct flt_error **error);

bool
flt_gpx_find_data(const struct flt_gpx_point *points,
                  size_t n_points,
                  double timestamp,
                  struct flt_gpx_data *data);

#endif /* FLT_GPX */
