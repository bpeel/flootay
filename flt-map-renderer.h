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

#ifndef FLT_MAP_RENDERER
#define FLT_MAP_RENDERER

#include <cairo.h>

#include "flt-error.h"
#include "flt-trace.h"

extern struct flt_error_domain
flt_map_renderer_error;

enum flt_map_renderer_error {
        FLT_MAP_RENDERER_ERROR_LOAD_FAILED,
        FLT_MAP_RENDERER_ERROR_FETCH_FAILED,
};

struct flt_map_renderer;

/* url_base can be NULL to use the default. If api_key is NULL then no
 * key will be used.
 */
struct flt_map_renderer *
flt_map_renderer_new(const char *url_base,
                     const char *api_key);

void
flt_map_renderer_set_clip(struct flt_map_renderer *renderer,
                          bool clip);

bool
flt_map_renderer_render(struct flt_map_renderer *renderer,
                        cairo_t *cr,
                        int zoom,
                        double lat, double lon,
                        double draw_center_x, double draw_center_y,
                        int map_width, int map_height,
                        const struct flt_trace *trace,
                        double video_timestamp,
                        struct flt_error **error);

void
flt_map_renderer_free(struct flt_map_renderer *renderer);

#endif /* FLT_MAP_RENDERER */
