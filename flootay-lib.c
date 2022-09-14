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

#include "flt-util.h"
#include "flt-scene.h"
#include "flt-parser.h"
#include "flt-file-error.h"
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

struct stdio_source {
        struct flt_source base;
        FILE *infile;
};

static bool
read_stdio_cb(struct flt_source *source,
              void *ptr,
              size_t *length,
              struct flt_error **error)
{
        struct stdio_source *stdio_source = (struct stdio_source *) source;

        size_t got = fread(ptr, 1, *length, stdio_source->infile);

        if (got < *length) {
                if (ferror(stdio_source->infile)) {
                        flt_file_error_set(error,
                                           errno,
                                           "%s",
                                           strerror(errno));
                        return false;
                }

                *length = got;
        }

        return true;
}

static bool
load_file(struct flt_scene *scene,
          const char *base_dir,
          FILE *file,
          struct flt_error **error)
{
        struct stdio_source source = {
                .base = { .read_source = read_stdio_cb },
                .infile = file,
        };

        if (source.infile == NULL) {
                flt_file_error_set(error,
                                   errno,
                                   "%s",
                                   strerror(errno));
                return false;
        }

        return flt_parser_parse(scene, &source.base, base_dir, error);
}

bool
flootay_load_script(struct flootay *flootay,
                    const char *base_dir,
                    FILE *file)
{
        struct flt_scene *scene = flt_scene_new();
        struct flt_error *error = NULL;

        if (!load_file(scene, base_dir, file, &error)) {
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

bool
flootay_render(struct flootay *flootay,
               cairo_t *cr,
               double timestamp)
{
        if (flootay->renderer == NULL) {
                set_error(flootay, "render called before loading a script");
                return false;
        }

        struct flt_error *error = NULL;

        if (!flt_renderer_render(flootay->renderer,
                                 cr,
                                 timestamp,
                                 &error)) {
                set_error(flootay, error->message);
                flt_error_free(error);
                return false;
        }

        return true;
}

void
flootay_free(struct flootay *flootay)
{
        free_renderer(flootay);
        free_error(flootay);

        flt_free(flootay);
}