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

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <float.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include "flt-gpx.h"

struct config {
        double lat, lon;
        const char *gpx_filename;
};

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
        config->gpx_filename = NULL;

        while (true) {
                switch (getopt(argc, argv, "-g:")) {
                case 'g':
                        config->gpx_filename = optarg;
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
        if (config->lat == DBL_MAX ||
            config->lon == DBL_MAX ||
            config->gpx_filename == NULL) {
                fprintf(stderr,
                        "usage: pos-to-time -g <gpx_file> "
                        "<latitude> <longitude>\n");
                return false;
        }

        return true;
}

static int
find_best_point(const struct flt_gpx_point *points,
                size_t n_points,
                double lat,
                double lon)
{
        size_t best_point = 0;
        double best_distance = DBL_MAX;

        struct flt_gpx_point target_point = {
                .lat = lat,
                .lon = lon,
        };

        for (size_t i = 0; i < n_points; i++) {
                double distance = flt_gpx_point_distance_between(points + i,
                                                                 &target_point);

                if (distance < best_distance) {
                        best_point = i;
                        best_distance = distance;
                }
        }

        return best_point;
}

static void
print_best_point(const struct flt_gpx_point *point,
                 double lat,
                 double lon)
{
        struct flt_gpx_point target_point = {
                .lat = lat,
                .lon = lon,
        };

        double distance = flt_gpx_point_distance_between(point, &target_point);

        printf("best point at %f,%f. distance = %f\n",
               point->lat, point->lon,
               distance);

        time_t seconds_offset = (time_t) point->time;
        struct tm *tm = gmtime(&seconds_offset);

        /* 20221102T09:27:00Z */
        printf("%04i%02i%02iT%02i:%02i:%02i",
               tm->tm_year + 1900,
               tm->tm_mon + 1,
               tm->tm_mday,
               tm->tm_hour,
               tm->tm_min,
               tm->tm_sec);

        double seconds;
        double fraction = modf(point->time, &seconds);

        if (fraction != 0.0)
                printf(".%03i", (int) (fraction * 100));

        printf("Z\n");
}

int
main(int argc, char **argv)
{
        struct config config;

        if (!process_options(argc, argv, &config))
                return EXIT_FAILURE;

        struct flt_error *error = NULL;
        struct flt_gpx_point *points;
        size_t n_points;

        if (!flt_gpx_parse(config.gpx_filename,
                           &points,
                           &n_points,
                           &error)) {
                fprintf(stderr, "%s\n", error->message);
                flt_error_free(error);
                return EXIT_FAILURE;
        }

        int ret = EXIT_SUCCESS;

        if (n_points <= 0) {
                fprintf(stderr,
                        "%s: GPX file contains no track points",
                        config.gpx_filename);
                ret = EXIT_FAILURE;
        } else {
                int point = find_best_point(points, n_points,
                                            config.lat, config.lon);
                print_best_point(points + point, config.lat, config.lon);
        }

        return ret;
}
