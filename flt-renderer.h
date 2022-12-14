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

#ifndef FLT_RENDERER
#define FLT_RENDERER

#include <cairo.h>
#include <stdbool.h>

#include "flt-scene.h"
#include "flt-error.h"

struct flt_renderer;

enum flt_renderer_result {
        FLT_RENDERER_RESULT_ERROR,
        FLT_RENDERER_RESULT_EMPTY,
        FLT_RENDERER_RESULT_OK,
};

enum flt_renderer_error {
        FLT_RENDERER_ERROR_SVG,
};

extern struct flt_error_domain
flt_renderer_error;

struct flt_renderer *
flt_renderer_new(struct flt_scene *scene);

enum flt_renderer_result
flt_renderer_render(struct flt_renderer *renderer,
                    cairo_t *cr,
                    double timestamp,
                    struct flt_error **error);

void
flt_renderer_free(struct flt_renderer *renderer);

#endif /* FLT_RENDERER */
