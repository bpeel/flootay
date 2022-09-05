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

#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <cairo.h>
#include <errno.h>

#include "flt-util.h"
#include "flt-scene.h"
#include "flt-parser.h"
#include "flt-file-error.h"
#include "flt-renderer.h"

#define FPS 30

static bool
write_surface(cairo_surface_t *surface)
{
        int width = cairo_image_surface_get_width(surface);
        int height = cairo_image_surface_get_height(surface);
        int stride = cairo_image_surface_get_stride(surface);
        uint8_t *data = cairo_image_surface_get_data(surface);

        for (int y = 0; y < height; y++) {
                uint32_t *row = (uint32_t *) (data + y * stride);
                uint8_t *out_pix = data;

                for (int x = 0; x < width; x++) {
                        uint32_t value = row[x];

                        uint8_t a = value >> 24;
                        uint8_t r = (value >> 16) & 0xff;
                        uint8_t g = (value >> 8) & 0xff;
                        uint8_t b = value & 0xff;

                        if (a > 0) {
                                /* unpremultiply */
                                r = r * 255 / a;
                                g = g * 255 / a;
                                b = b * 255 / a;
                        }

                        *(out_pix++) = r;
                        *(out_pix++) = g;
                        *(out_pix++) = b;
                        *(out_pix++) = a;
                }

                size_t wrote = fwrite(data, 1, width * 4, stdout);

                if (wrote != width * 4) {
                        fprintf(stderr,
                                "error writing frame: %s\n",
                                strerror(errno));
                        return false;
                }
        }

        return true;
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
load_stdin(struct flt_scene *scene,
           struct flt_error **error)
{
        struct stdio_source source = {
                .base = { .read_source = read_stdio_cb },
                .infile = stdin,
        };

        return flt_parser_parse(scene, &source.base, NULL, error);
}

static bool
load_file(struct flt_scene *scene,
          const char *filename,
          struct flt_error **error)
{
        struct stdio_source source = {
                .base = { .read_source = read_stdio_cb },
                .infile = fopen(filename, "rb"),
        };

        if (source.infile == NULL) {
                flt_file_error_set(error,
                                   errno,
                                   "%s: %s",
                                   filename,
                                   strerror(errno));
                return false;
        }

        char *base_dir;

        const char *last_part = strrchr(filename, '/');

        if (last_part == NULL)
                base_dir = NULL;
        else
                base_dir = flt_strndup(filename, last_part - filename);

        bool ret = flt_parser_parse(scene, &source.base, base_dir, error);

        fclose(source.infile);

        flt_free(base_dir);

        return ret;
}

int
main(int argc, char **argv)
{
        struct flt_scene *scene = flt_scene_new();
        int ret = EXIT_SUCCESS;

        if (argc <= 1) {
                fprintf(stderr, "usage: <script-file>…\n");
                ret = EXIT_FAILURE;
                goto out;
        }

        for (int i = 1; i < argc; i++) {
                struct flt_error *error = NULL;
                bool load_ret;

                if (!strcmp(argv[i], "-"))
                        load_ret = load_stdin(scene, &error);
                else
                        load_ret = load_file(scene, argv[i], &error);

                if (!load_ret) {
                        fprintf(stderr, "%s\n", error->message);
                        flt_error_free(error);
                        ret = EXIT_FAILURE;
                        goto out;
                }
        }

        cairo_surface_t *surface =
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                           scene->video_width,
                                           scene->video_height);
        cairo_t *cr = cairo_create(surface);

        int n_frames = ceil(flt_scene_get_max_timestamp(scene) * FPS) + 1;

        struct flt_renderer *renderer = flt_renderer_new(scene);

        for (int frame_num = 0; frame_num < n_frames; frame_num++) {
                if (!flt_renderer_render(renderer,
                                         cr,
                                         frame_num / (double) FPS)) {
                        ret = EXIT_FAILURE;
                        goto render_out;
                }

                cairo_surface_flush(surface);

                if (!write_surface(surface)) {
                        ret = EXIT_FAILURE;
                        goto render_out;
                }
        }

render_out:
        cairo_surface_destroy(surface);
        cairo_destroy(cr);

        flt_renderer_free(renderer);

out:
        flt_scene_free(scene);

        return ret;
}
