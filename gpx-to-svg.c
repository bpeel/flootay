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
#include <string.h>

#define TILE_SIZE 256
#define CENTER_LAT 45.77358932670189
#define CENTER_LON 4.892918591894951
#define ZOOM 13
#define IMAGE_WIDTH 1920
#define IMAGE_HEIGHT 1080

struct data {
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

                int x = lon_to_pixel_x(lon, ZOOM) - data->left_x;
                int y = lat_to_pixel_y(lat, ZOOM) - data->top_y;

                if (data->is_first)
                        data->is_first = false;
                else
                        fputc(' ', stdout);

                printf("%c %i %i",
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

int
main(int argc, char **argv)
{
        struct data data = {
                .left_x = lon_to_pixel_x(CENTER_LON, ZOOM) - IMAGE_WIDTH / 2,
                .top_y = lat_to_pixel_y(CENTER_LAT, ZOOM) - IMAGE_HEIGHT / 2,
                .is_first = true,
                .is_segment_start = true,
        };

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
                fputc('\n', stdout);

        XML_ParserFree(parser);

        return ret;
}
