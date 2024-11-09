/*
 * Flootay – a video overlay generator
 * Copyright (C) 2024  Neil Roberts
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

#ifndef FLT_TRACE
#define FLT_TRACE

#include "flt-error.h"

extern struct flt_error_domain
flt_trace_error;

enum flt_trace_error {
        FLT_TRACE_ERROR_INVALID,
};

struct flt_trace_point {
        float lat, lon;
};

enum flt_trace_segment_status {
        FLT_TRACE_SEGMENT_STATUS_DONE,
        FLT_TRACE_SEGMENT_STATUS_WIP,
        FLT_TRACE_SEGMENT_STATUS_PLANNED,
        FLT_TRACE_SEGMENT_STATUS_TESTED,
        FLT_TRACE_SEGMENT_STATUS_POSTPONED,
        FLT_TRACE_SEGMENT_STATUS_UNKNOWN,
        FLT_TRACE_SEGMENT_STATUS_VARIANT,
        FLT_TRACE_SEGMENT_STATUS_VARIANT_POSTPONED,
};

struct flt_trace_segment {
        enum flt_trace_segment_status status;
        size_t n_points;
        struct flt_trace_point *points;
};

struct flt_trace {
        size_t n_segments;
        struct flt_trace_segment *segments;
};

struct flt_trace *
flt_trace_parse(const char *filename,
                struct flt_error **error);

void
flt_trace_free(struct flt_trace *trace);

#endif /* FLT_TRACE */
