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

#include <math.h>
#include <stdio.h>
#include <expat.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <unistd.h>

#define TILE_SIZE 256

struct config {
        double lat, lon;
        int width, height;
        int zoom;
        const char *output_filename;
};

struct data {
        struct config config;
        FILE *output_file;
        int left_x;
        int top_y;
        bool is_first;
        bool is_segment_start;
};

static void
lon_to_x(double lon, int zoom,
         int *tile_x_out,
         int *pixel_x_out)
{
        double x = (lon + 180.0) / 360.0 * (1 << zoom);

        double tile_x;
        double frac_x = modf(x, &tile_x);

        *tile_x_out = tile_x;
        *pixel_x_out = round(frac_x * TILE_SIZE);
}

static void
lat_to_y(double lat, int zoom,
         int *tile_y_out,
         int *pixel_y_out)
{
        double lat_rad = lat * M_PI / 180.0;
        double y = (1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * (1 << zoom);

        double tile_y;
        double frac_y = modf(y, &tile_y);

        *tile_y_out = tile_y;
        *pixel_y_out = round(frac_y * TILE_SIZE);
}

static int
lon_to_pixel_x(double lon, int zoom)
{
        int tile, pixel;
        lon_to_x(lon, zoom, &tile, &pixel);
        return tile * TILE_SIZE + pixel;
}

static int
lat_to_pixel_y(double lat, int zoom)
{
        int tile, pixel;
        lat_to_y(lat, zoom, &tile, &pixel);
        return tile * TILE_SIZE + pixel;
}

static void XMLCALL
start_element_cb(void *user_data, const XML_Char *name, const XML_Char **atts)
{
        struct data *data = user_data;

        if (!strcmp(name, "trkseg")) {
                data->is_segment_start = true;
        } else if (!strcmp(name, "trkpt")) {
                double lat = 0.0, lon = 0.0;

                for (const char **att = atts; *att; att += 2) {
                        if (!strcmp(*att, "lat"))
                                lat = strtod(att[1], NULL);
                        else if (!strcmp(*att, "lon"))
                                lon = strtod(att[1], NULL);
                }

                int x = lon_to_pixel_x(lon, data->config.zoom) - data->left_x;
                int y = lat_to_pixel_y(lat, data->config.zoom) - data->top_y;

                if (data->is_first)
                        data->is_first = false;
                else
                        fputc(' ', data->output_file);

                fprintf(data->output_file,
                        "%c %i %i",
                        data->is_segment_start ? 'M' : 'L',
                        x,
                        y);

                data->is_segment_start = false;
        }
}

static void XMLCALL
end_element_cb(void *user_data, const XML_Char *name)
{
}

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
        config->width = 1920;
        config->height = 1080;
        config->zoom = 17;

        while (true) {
                switch (getopt(argc, argv, "-w:h:z:o:")) {
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
        struct data data = {
                .is_first = true,
                .is_segment_start = true,
        };

        if (!process_options(argc, argv, &data.config))
                return EXIT_FAILURE;

        data.left_x = (lon_to_pixel_x(data.config.lon, data.config.zoom) -
                       data.config.width / 2);
        data.top_y = (lat_to_pixel_y(data.config.lat, data.config.zoom) -
                      data.config.height / 2);

        if (data.config.output_filename) {
                data.output_file = fopen(data.config.output_filename, "w");

                if (data.output_file == NULL) {
                        fprintf(stderr,
                                "%s: %s\n",
                                data.config.output_filename,
                                strerror(errno));
                        return EXIT_FAILURE;
                }
        } else {
                data.output_file = stdout;
        }

        char buf[512];
        XML_Parser parser = XML_ParserCreate(NULL);
        int ret = EXIT_SUCCESS;

        XML_SetUserData(parser, &data);
        XML_SetElementHandler(parser, start_element_cb, end_element_cb);

        bool done;

        do {
                size_t len = fread(buf, 1, sizeof(buf), stdin);
                done = len < sizeof(buf);

                if (XML_Parse(parser,
                              buf,
                              (int) len,
                              done) == XML_STATUS_ERROR) {
                        const char *message =
                                (const char *)
                                XML_ErrorString(XML_GetErrorCode(parser));

                        fprintf(stderr,
                                "%s at line %u\n",
                                message,
                                (int) XML_GetCurrentLineNumber(parser));

                        ret = EXIT_FAILURE;

                        break;
                }
        } while (!done);

        if (!data.is_first)
                fputc('\n', data.output_file);

        XML_ParserFree(parser);

        if (data.config.output_filename)
                fclose(data.output_file);

        return ret;
}
