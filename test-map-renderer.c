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
#include <stdlib.h>

#include "flt-map-renderer.h"

#define IMAGE_SIZE 512

int
main(int argc, char **argv)
{
        double lat = 45.767615, lon = 4.834434;

        if (argc >= 2) {
                lat = strtod(argv[1], NULL);

                if (argc >= 3)
                        lon = strtod(argv[2], NULL);
        }

        int ret = EXIT_SUCCESS;

        cairo_surface_t *surface =
                cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                           IMAGE_SIZE, IMAGE_SIZE);
        cairo_t *cr = cairo_create(surface);

        struct flt_map_renderer *renderer =
                flt_map_renderer_new(NULL, /* map_url_base */
                                     NULL /* api_key */);

        struct flt_error *error = NULL;

        if (!flt_map_renderer_render(renderer,
                                     cr,
                                     17, /* zoom */
                                     lat, lon,
                                     IMAGE_SIZE / 2.0,
                                     IMAGE_SIZE / 2.0,
                                     IMAGE_SIZE, IMAGE_SIZE,
                                     &error)) {
                fprintf(stderr, "%s\n", error->message);
                ret = EXIT_FAILURE;
                flt_error_free(error);
        } else {
                cairo_surface_write_to_png(surface, "map.png");
        }

        flt_map_renderer_free(renderer);

        cairo_destroy(cr);
        cairo_surface_destroy(surface);

        return ret;
}
