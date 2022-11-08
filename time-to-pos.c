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
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <unistd.h>
#include <stdlib.h>

#include "flt-child-proc.h"
#include "flt-buffer.h"
#include "flt-parse-time.h"
#include "flt-get-video-length.h"
#include "flt-gpx.h"
#include "flt-lexer.h"
#include "flt-list.h"

struct config {
        const char *gpx_filename;
        int video_num, video_part;
        double timestamp;
};

static int
extract_digits(const char *digits, int n_digits)
{
        int value = 0;

        for (int i = 0; i < n_digits; i++)
                value = (value * 10) + digits[i] - '0';

        return value;
}

static bool
parse_video_filename(struct config *config, const char *filename)
{
        const char *p = filename;

        if (p[0] != 'G' || p[1] != 'H')
                goto error;

        p += 2;

        for (int i = 0; i < 6; i++) {
                if (*p < '0' || *p > '9')
                        goto error;
                p++;
        }

        if (strcmp(p, ".MP4"))
                goto error;

        config->video_part = extract_digits(filename + 2, 2);
        config->video_num = extract_digits(filename + 4, 4);

        return true;

error:
        fprintf(stderr,
                "invalid video filename (must be like GH010001.MP4): %s\n",
                filename);
        return false;
}

struct parse_timestamp_data {
        struct flt_source source;
        const char *data;
        size_t pos, size;
};

static bool
parse_timestamp_source_cb(struct flt_source *source,
                          void *ptr,
                          size_t *length,
                          struct flt_error **error)
{
        struct parse_timestamp_data *data =
                flt_container_of(source, struct parse_timestamp_data, source);

        if (*length + data->pos > data->size)
                *length = data->size - data->pos;

        memcpy(ptr, data->data + data->pos, *length);

        data->pos += *length;

        return true;
}

static bool
parse_timestamp(const char *str, double *timestamp_out)
{
        struct parse_timestamp_data data = {
                .source = {
                        .read_source = parse_timestamp_source_cb,
                },
                .data = str,
                .pos = 0,
                .size = strlen(str),
        };

        bool ret = true;

        struct flt_lexer *lexer = flt_lexer_new(&data.source);
        struct flt_error *error = NULL;

        const struct flt_lexer_token *token =
                flt_lexer_get_token(lexer, &error);

        if (token == NULL) {
                fprintf(stderr,
                        "invalid timestamp: %s\n",
                        error->message);
                ret = false;
                flt_error_free(error);
        } else {
                switch (token->type) {
                case FLT_LEXER_TOKEN_TYPE_NUMBER:
                        *timestamp_out = token->number_value;
                        break;
                case FLT_LEXER_TOKEN_TYPE_FLOAT:
                        *timestamp_out = (token->number_value +
                                          token->fraction /
                                          (double) FLT_LEXER_FRACTION_RANGE);
                        break;
                default:
                        fprintf(stderr,
                                "invalid timestamp: %s\n",
                                str);
                        ret = false;
                        break;
                }
        }

        flt_lexer_free(lexer);

        return ret;
}

static bool
process_options(int argc, char **argv, struct config *config)
{
        config->gpx_filename = "speed.gpx";

        config->video_num = -1;
        config->video_part = -1;
        config->timestamp = DBL_MAX;

        while (true) {
                switch (getopt(argc, argv, "-g:")) {
                case 'g':
                        config->gpx_filename = optarg;
                        break;
                case 1:
                        if (config->video_num < 0) {
                                if (!parse_video_filename(config, optarg))
                                        return false;
                        } else if (config->timestamp != DBL_MAX) {
                                fprintf(stderr, "extra argument: %s\n", optarg);
                                return false;
                        } else if (!parse_timestamp(optarg,
                                                    &config->timestamp)) {
                                return false;
                        }
                        break;

                case -1:
                        goto done;

                default:
                        return false;
                }
        }

done:
        if (config->timestamp == DBL_MAX ||
            config->video_part < 0) {
                fprintf(stderr,
                        "usage: time-to-pos "
                        "[-g <gpx_file>] "
                        "<video_file> "
                        "<timestamp>\n");
                return false;
        }

        return true;
}

static bool
parse_video_offset_output(char *output,
                          int *part_out,
                          double *offset_out)
{
        errno = 0;
        char *tail;

        unsigned long part = strtoul(output, &tail, 10);

        if (errno || *tail != ' ' || part >= 100)
                return false;

        double offset = strtod(tail, &tail);

        if (errno ||
            (!isnormal(offset) && offset != 0.0) ||
            offset < 0.0)
                return false;

        while (*tail == ' ')
                tail++;

        int time_len = strlen(tail);

        if (time_len > 0 && tail[time_len - 1] == '\n')
                tail[time_len - 1] = '\0';

        double timestamp;
        struct flt_error *error = NULL;

        if (!flt_parse_time(tail, &timestamp, &error)) {
                flt_error_free(error);
                return false;
        }

        *part_out = part;
        *offset_out = timestamp - offset;

        return true;
}

static bool
get_video_offset(int video_num,
                 int *part_out,
                 double *offset_out)
{
        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf,
                                 "sed -rn -e "
                                 "'s/^gpx_offset +GH([0-9]{2})%04d\\.MP4 +"
                                 "([0-9]+(\\.[0-9]+)?) +"
                                 "([^ ]+)"
                                 ".*/\\1 \\2 \\4/p' "
                                 "*.script | "
                                 "head -n 1",
                                 video_num);

        const char *const args[] = {
                "-c", (const char *) buf.data, NULL
        };

        char *output = flt_child_proc_get_output(NULL, /* source_dir */
                                                 "bash",
                                                 args);

        flt_buffer_destroy(&buf);

        if (output == NULL)
                return false;

        bool ret = true;

        if (*output == '\0') {
                fprintf(stderr,
                        "no output received when trying to get gpx offset\n");
                ret = false;
        } else if (!parse_video_offset_output(output, part_out, offset_out)) {
                fprintf(stderr,
                        "invalid output received when trying to get gpx "
                        "offset:\n"
                        "%s",
                        output);

                int output_len = strlen(output);

                if (output_len > 0 && output[output_len - 1] != '\n')
                        fputc('\n', stdout);

                ret = false;
        }

        flt_free(output);

        return ret;
}

static bool
get_part_lengths(int video_num,
                 int first_part,
                 int n_parts,
                 double *part_lengths_out)
{
        double part_lengths = 0.0;
        bool ret = true;

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        for (int i = 0; i < n_parts; i++) {
                flt_buffer_set_length(&buf, 0);

                flt_buffer_append_printf(&buf,
                                         "GH%02i%04i.MP4",
                                         first_part + i,
                                         video_num);

                double video_length;

                if (!flt_get_video_length((const char *) buf.data,
                                          &video_length)) {
                        ret = false;
                        break;
                }

                part_lengths += video_length;
        }

        flt_buffer_destroy(&buf);

        if (ret)
                *part_lengths_out = part_lengths;

        return ret;
}

static bool
get_pos_from_gpx(const char *gpx_filename,
                 double timestamp,
                 double *lat_out, double *lon_out)
{
        struct flt_error *error = NULL;
        struct flt_gpx_point *points;
        size_t n_points;

        if (!flt_gpx_parse(gpx_filename,
                           &points,
                           &n_points,
                           &error)) {
                fprintf(stderr, "%s\n", error->message);
                flt_error_free(error);
                return false;
        }

        bool ret = true;
        struct flt_gpx_data data;

        if (!flt_gpx_find_data(points, n_points, timestamp, &data)) {
                fprintf(stderr,
                        "couldn’t find data for timestamp %f\n",
                        timestamp);
                ret = false;
        } else {
                *lat_out = data.lat;
                *lon_out = data.lon;
        }

        flt_free(points);

        return ret;
}

static void
encode_coords(double lat,
              double lon,
              char *digits,
              size_t n_digits)
{
        /* https://wiki.openstreetmap.org/wiki/Shortlink */

        static const char codes[] = ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                     "abcdefghijklmnopqrstuvwxyz"
                                     "0123456789"
                                     "_~");

        uint32_t lat_bits = (lat + 90.0) * (UINT32_MAX + 1.0) / 180.0;
        uint32_t lon_bits = (lon + 180.0) * (UINT32_MAX + 1.0) / 360.0;
        uint64_t combined_bits = 0;

        for (int i = 0; i < sizeof (lat_bits) * 8; i++) {
                combined_bits = ((combined_bits << 2) |
                                 ((lon_bits >> 30) & 0x2) |
                                 (lat_bits >> 31));
                lat_bits <<= 1;
                lon_bits <<= 1;
        }

        for (unsigned i = 0; i < n_digits; i++) {
                digits[i] = codes[combined_bits >> 58];
                combined_bits <<= 6;
        }
}

static void
print_url(double lat, double lon)
{
        char code[11];

        encode_coords(lat, lon, code, sizeof code - 1);
        code[sizeof code - 1] = '\0';

        printf("https://osm.org/go/%s?layers=C&m\n", code);
}

int
main(int argc, char **argv)
{
        struct config config;

        if (!process_options(argc, argv, &config))
                return EXIT_FAILURE;

        int video_offset_part;
        double timestamp;

        if (!get_video_offset(config.video_num,
                              &video_offset_part,
                              &timestamp))
                return EXIT_FAILURE;

        if (video_offset_part > config.video_part) {
                fprintf(stderr,
                        "gpx_offset video part (%i) "
                        "is less than chosen video (%i)\n",
                        video_offset_part,
                        config.video_part);
                return EXIT_FAILURE;
        }

        double part_lengths;

        if (!get_part_lengths(config.video_num,
                              video_offset_part,
                              config.video_part - video_offset_part,
                              &part_lengths))
                return EXIT_FAILURE;

        timestamp += part_lengths;

        double lat, lon;

        if (!get_pos_from_gpx(config.gpx_filename,
                              timestamp + config.timestamp,
                              &lat, &lon))
                return EXIT_FAILURE;

        printf("%f,%f\n",
               lat, lon);

        print_url(lat, lon);

        return EXIT_SUCCESS;
}
