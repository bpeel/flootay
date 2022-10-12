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
#include <errno.h>
#include <limits.h>
#include <float.h>
#include <getopt.h>
#include <math.h>

#include "flt-map-renderer.h"

struct config {
        double lat, lon;
        int width, height;
        int zoom;
        bool clip;
        const char *url_base;
        const char *api_key;
        const char *output_filename;
};

static bool
parse_positive_int(const char *str, int *value_out)
{
        errno = 0;

        char *tail;

        long value = strtol(str, &tail, 10);

        if (value <= 0 || value > INT_MAX || errno || *tail)
                return false;

        *value_out = value;

        return true;
}

static bool
parse_coordinate(const char *arg, struct config *config)
{
        const char *part;
        double *value_out;
        double min_value, max_value;

        if (config->lat == DBL_MAX) {
                value_out = &config->lat;
                min_value = -90.0;
                max_value = 90.0;
                part = "latitude";
        } else if (config->lon == DBL_MAX) {
                value_out = &config->lon;
                min_value = -180.0;
                max_value = 180.0;
                part = "longitude";
        } else {
                fprintf(stderr, "Too many coordinates specified\n");
                return false;
        }

        errno = 0;
        char *tail;
        *value_out = strtod(optarg, &tail);

        if (errno ||
            (!isnormal(*value_out) && *value_out != 0.0) ||
            *tail ||
            *value_out < min_value ||
            *value_out > max_value) {
                fprintf(stderr,
                        "invalid %s: %s\n",
                        part,
                        optarg);
                return false;
        }

        return true;
}

static bool
process_options(int argc, char **argv, struct config *config)
{
        config->lat = DBL_MAX;
        config->lon = DBL_MAX;
        config->width = 512;
        config->height = 512;
        config->zoom = 17;
        config->clip = false;
        config->output_filename = "map.png";
        config->url_base = NULL;
        config->api_key = NULL;

        while (true) {
                switch (getopt(argc, argv, "-w:h:z:cu:a:o:")) {
                case 'w':
                        if (!parse_positive_int(optarg, &config->width)) {
                                fprintf(stderr,
                                        "invalid width: %s\n",
                                        optarg);
                                return false;
                        }
                        break;
                case 'h':
                        if (!parse_positive_int(optarg, &config->height)) {
                                fprintf(stderr,
                                        "invalid height: %s\n",
                                        optarg);
                                return false;
                        }
                        break;
                case 'z':
                        if (!parse_positive_int(optarg, &config->zoom)) {
                                fprintf(stderr,
                                        "invalid zoom: %s\n",
                                        optarg);
                                return false;
                        }
                        break;
                case 'c':
                        config->clip = true;
                        break;
                case 'u':
                        config->url_base = optarg;
                        break;
                case 'a':
                        config->api_key = optarg;
                        break;
                case 'o':
                        config->output_filename = optarg;
                        break;
                case 1:
                        if (!parse_coordinate(optarg, config))
                                return false;
                        break;

                case -1:
                        goto done;

                default:
                        return false;
                }
        }

done:
        if (config->lat == DBL_MAX) {
                config->lat = 45.767615;
                config->lon = 4.834434;
        } else if (config->lon == DBL_MAX) {
                fprintf(stderr,
                        "latitude specified without longitude\n");
                return false;
        }

        return true;
}

int
main(int argc, char **argv)
{
        struct config config;

        if (!process_options(argc, argv, &config))
                return EXIT_FAILURE;

        int ret = EXIT_SUCCESS;

        cairo_surface_t *surface =
                cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                           config.width,
                                           config.height);
        cairo_t *cr = cairo_create(surface);

        struct flt_map_renderer *renderer =
                flt_map_renderer_new(config.url_base,
                                     config.api_key);

        struct flt_error *error = NULL;

        flt_map_renderer_set_clip(renderer, config.clip);

        if (!flt_map_renderer_render(renderer,
                                     cr,
                                     config.zoom,
                                     config.lat,
                                     config.lon,
                                     config.width / 2.0,
                                     config.height / 2.0,
                                     config.width,
                                     config.height,
                                     &error)) {
                fprintf(stderr, "%s\n", error->message);
                ret = EXIT_FAILURE;
                flt_error_free(error);
        } else if (cairo_surface_write_to_png(surface,
                                              config.output_filename) !=
                   CAIRO_STATUS_SUCCESS) {
                fprintf(stderr, "error saving png\n");
                ret = EXIT_FAILURE;
        }

        flt_map_renderer_free(renderer);

        cairo_destroy(cr);
        cairo_surface_destroy(surface);

        return ret;
}
