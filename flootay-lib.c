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

#include "flootay.h"

#include <stdio.h>
#include <cairo.h>
#include <errno.h>
#include <assert.h>

#include "flt-util.h"
#include "flt-scene.h"
#include "flt-parse-stdio.h"
#include "flt-renderer.h"

struct flootay {
        struct flt_scene *scene;
        struct flt_renderer *renderer;
        char *error_message;
};

struct flootay *
flootay_new(void)
{
        struct flootay *flootay = flt_calloc(sizeof *flootay);

        return flootay;
}

static void
free_error(struct flootay *flootay)
{
        flt_free(flootay->error_message);
        flootay->error_message = NULL;
}

static void
free_renderer(struct flootay *flootay)
{
        if (flootay->renderer) {
                flt_renderer_free(flootay->renderer);
                flootay->renderer = NULL;
        }

        if (flootay->scene) {
                flt_scene_free(flootay->scene);
                flootay->scene = NULL;
        }
}

static void
set_error(struct flootay *flootay,
          const char *message)
{
        free_error(flootay);
        flootay->error_message = flt_strdup(message);
}

const char *
flootay_get_error(struct flootay *flootay)
{
        return flootay->error_message;
}

bool
flootay_load_script(struct flootay *flootay,
                    const char *base_dir,
                    FILE *file)
{
        struct flt_scene *scene = flt_scene_new();
        struct flt_error *error = NULL;

        if (!flt_parse_stdio(scene, base_dir, file, &error)) {
                set_error(flootay, error->message);
                flt_error_free(error);
                flt_scene_free(scene);
                return false;
        }

        free_renderer(flootay);

        flootay->scene = scene;
        flootay->renderer = flt_renderer_new(scene);

        return true;
}

enum flootay_render_result
flootay_render(struct flootay *flootay,
               cairo_t *cr,
               double timestamp)
{
        if (flootay->renderer == NULL) {
                set_error(flootay, "render called before loading a script");
                return false;
        }

        struct flt_error *error = NULL;

        switch (flt_renderer_render(flootay->renderer,
                                    cr,
                                    timestamp,
                                    &error)) {
        case FLT_RENDERER_RESULT_ERROR:
                set_error(flootay, error->message);
                flt_error_free(error);
                return FLOOTAY_RENDER_RESULT_ERROR;

        case FLT_RENDERER_RESULT_EMPTY:
                return FLOOTAY_RENDER_RESULT_EMPTY;

        case FLT_RENDERER_RESULT_OK:
                return FLOOTAY_RENDER_RESULT_OK;
        }

        assert(!"unexpected return value from flt_renderer_render");

        return FLOOTAY_RENDER_RESULT_EMPTY;
}

void
flootay_free(struct flootay *flootay)
{
        free_renderer(flootay);
        free_error(flootay);

        flt_free(flootay);
}
