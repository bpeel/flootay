/*
 * Flootay – a video overlay generator
 * Copyright (C) 2023  Neil Roberts
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
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <string.h>

#include "flt-gpx.h"
#include "flt-list.h"
#include "flt-get-video-length.h"

#define PHOTO_DISTANCE 3.0

struct video {
        struct flt_list link;
        const char *filename;
        double length;
};

struct config {
        const char *gpx_filename;
        double gpx_offset;
        struct flt_list videos;
};

static void
destroy_config(struct config *config)
{
        struct video *video, *tmp;

        flt_list_for_each_safe(video, tmp, &config->videos, link) {
                flt_free(video);
        }
}

static bool
find_video(const struct config *config,
           double timestamp,
           const struct video **video_out,
           double *offset_out)
{
        timestamp += config->gpx_offset;

        if (timestamp < 0.0)
                return false;

        double video_offset = 0.0;
        const struct video *video;

        flt_list_for_each(video, &config->videos, link) {
                if (timestamp - video_offset < video->length) {
                        *offset_out = timestamp - video_offset;
                        *video_out = video;
                        return true;
                }

                video_offset += video->length;
        }

        return false;
}

static void
print_timestamp(double timestamp)
{
        time_t t = (time_t) timestamp;
        struct tm *tm = gmtime(&t);
        char buf[1024];

        if (!strftime(buf, sizeof buf, "%Y:%m:%d %H:%M:%S", tm))
                return;

        fputs(buf, stdout);

        double integer_part;
        double frac_part = modf(timestamp, &integer_part);

        if (frac_part != 0.0) {
                snprintf(buf, sizeof buf, "%f", frac_part);

                const char *frac_str = strchr(buf, '.');

                if (frac_str)
                        fputs(frac_str, stdout);
        }

        fputc('Z', stdout);
}

static bool
print_photos(const struct config *config,
             const struct flt_gpx_point *points,
             size_t n_points)
{
        if (n_points < 1)
                return true;

        printf("set -eux\n");

        float last_distance = points[0].distance;

        for (size_t i = 0; i < n_points; i++) {
                const struct flt_gpx_point *point = points + i;

                if (point->distance - last_distance >= PHOTO_DISTANCE) {
                        const struct video *video;
                        double offset;

                        if (!find_video(config, point->time, &video, &offset)) {
                                fprintf(stderr,
                                        "couldn’t find video for offset %f\n",
                                        point->time);
                                return false;
                        }

                        printf("ffmpeg -ss %f -i \"%s\" "
                               "-frames 1 "
                               "photo-%03zu.jpg\n",
                               offset,
                               video->filename,
                               i);

                        printf("exiftool -alldates=\"");

                        print_timestamp(point->time);

                        printf("\" "
                               "-GPSLatitude=%f "
                               "-GPSLongitude=%f "
                               "photo-%03zu.jpg\n",
                               point->lat,
                               point->lon,
                               i);

                        last_distance = point->distance;
                }
        }

        return true;
}

static bool
parse_video(struct config *config, const char *filename)
{
        double length;

        if (!flt_get_video_length(filename, &length))
                return false;

        struct video *video = flt_alloc(sizeof *video);

        video->filename = filename;
        video->length = length;

        flt_list_insert(config->videos.prev, &video->link);

        return true;
}

static bool
process_options(int argc, char **argv, struct config *config)
{
        char *tail;

        config->gpx_filename = NULL;
        config->gpx_offset = 0.0;
        flt_list_init(&config->videos);

        while (true) {
                switch (getopt(argc, argv, "-g:o:")) {
                case 1:
                        if (!parse_video(config, optarg))
                                goto error;
                        break;

                case 'g':
                        config->gpx_filename = optarg;
                        break;

                case 'o':
                        errno = 0;

                        config->gpx_offset = strtod(optarg, &tail);

                        if (errno ||
                            !isnormal(config->gpx_offset) ||
                            *tail) {
                                fprintf(stderr,
                                        "invalid offset: %s\n",
                                        optarg);
                                goto error;
                        }
                        break;

                case -1:
                        goto done;

                default:
                        goto error;
                }
        }

done:
        if (config->gpx_filename == NULL ||
            flt_list_empty(&config->videos)) {
                fprintf(stderr,
                        "usage: photos "
                        "[-o <gpx_offset>] "
                        "-g <gpx_file> "
                        "<video_file>… \n");
                goto error;
        }

        return true;

error:
        destroy_config(config);
        return false;
}

int
main(int argc, char **argv)
{
        struct config config;

        if (!process_options(argc, argv, &config))
                return EXIT_FAILURE;

        int ret = EXIT_SUCCESS;
        struct flt_error *error = NULL;
        struct flt_gpx_point *points;
        size_t n_points;

        if (!flt_gpx_parse(config.gpx_filename,
                           &points,
                           &n_points,
                           &error)) {
                fprintf(stderr, "%s\n", error->message);
                flt_error_free(error);
                ret = EXIT_FAILURE;
        } else {
                if (!print_photos(&config, points, n_points))
                        ret = EXIT_FAILURE;

                flt_free(points);
        }

        destroy_config(&config);

        return ret;
}
