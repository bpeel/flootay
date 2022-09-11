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

#include "flt-gpx.h"

#include <expat.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#include "flt-util.h"
#include "flt-buffer.h"
#include "flt-file-error.h"

/* Don’t use the point if the timestamp is more than 5 seconds from
 * what we are looking for.
 */
#define MAX_TIME_GAP 5

#define MAKE_ELEMENT(ns, tag) ns "\xff" tag

#define TPX_NAMESPACE "http://www.garmin.com/xmlschemas/TrackPointExtension/v2"
#define TPX_ELEMENT(tag) MAKE_ELEMENT(TPX_NAMESPACE, tag)

struct flt_error_domain
flt_gpx_error;

enum parse_state {
        PARSE_STATE_LOOKING_FOR_TRKPT,
        PARSE_STATE_IN_TRKPT,
        PARSE_STATE_IN_TIME,
        PARSE_STATE_IN_SPEED,
        PARSE_STATE_IN_ELE,
        PARSE_STATE_IN_EXTENSIONS,
        PARSE_STATE_IN_TRACK_POINT_EXTENSION,
        PARSE_STATE_IN_EXTENSION_SPEED,
};

struct flt_gpx_parser {
        XML_Parser parser;

        const char *filename;

        struct flt_buffer points;

        /* Temporary buffer used for collecting element text */
        struct flt_buffer buf;

        enum parse_state parse_state;

        /* If we encounter nodes that we don’t understand while
         * parsing an interesting node, will ignore nodes until this
         * depth gets back to zero.
         */
        int skip_depth;
        /* The unix time that we found in this trkpt, or -1 if not
         * found yet.
         */
        double time;
        /* The speed that we found, or a negative number if not found yet */
        float speed;
        /* The elevation that we found, or a negative number if not found yet */
        float elevation;
        /* The coordinates that we found. This is always valid when we
         * are in a trkpt because it is parsed immediately from the
         * attributes.
         */
        float lat, lon;

        struct flt_error *error;
};

static void
report_error(struct flt_gpx_parser *parser,
             const char *note)
{
        if (parser->error)
                return;

        unsigned line = XML_GetCurrentLineNumber(parser->parser);

        flt_set_error(&parser->error,
                      &flt_gpx_error,
                      FLT_GPX_ERROR_INVALID,
                      "%s:%u: %s",
                      parser->filename,
                      line,
                      note);

        XML_StopParser(parser->parser, XML_FALSE /* resumable */);
}

static void
report_xml_error(struct flt_gpx_parser *parser)
{
        enum XML_Error code = XML_GetErrorCode(parser->parser);
        const char *str = XML_ErrorString(code);

        report_error(parser, str);
}

static int
parse_digits(const char *digits,
             int n_digits)
{
        int value = 0;

        for (int i = 0; i < n_digits; i++) {
                if (digits[i] < '0' || digits[i] > '9')
                        return -1;

                value = (value * 10) + digits[i] - '0';
        }

        return value;
}

static bool
is_space(char ch)
{
        return ch && strchr(" \n\r\t", ch) != NULL;
}

static bool
parse_time(struct flt_gpx_parser *parser)
{
        const char *time_str = (const char *) parser->buf.data;

        while (is_space(*time_str))
                time_str++;

        int year = parse_digits(time_str, 4);
        time_str += 4;
        if (year == -1 || *(time_str++) != '-')
                goto fail;

        int month = parse_digits(time_str, 2);
        time_str += 2;
        if (month == -1 || *(time_str++) != '-')
                goto fail;

        int day = parse_digits(time_str, 2);
        time_str += 2;
        if (day == -1 || *(time_str++) != 'T')
                goto fail;

        int hour = parse_digits(time_str, 2);
        time_str += 2;
        if (hour == -1 || *(time_str++) != ':')
                goto fail;

        int minute = parse_digits(time_str, 2);
        time_str += 2;
        if (minute == -1 || *(time_str++) != ':')
                goto fail;

        int second = parse_digits(time_str, 2);
        time_str += 2;
        if (second == -1)
                goto fail;

        int sub_second_divisor = 1;
        int sub_second_dividend = 0;

        if (*time_str == '.') {
                for (time_str++;
                     *time_str >= '0' && *time_str <= '9';
                     time_str++) {
                        sub_second_dividend = (sub_second_dividend * 10 +
                                               *time_str - '0');
                        sub_second_divisor *= 10;
                }
        }

        if (*time_str != 'Z') {
                report_error(parser, "timezone is not Z");
                return false;
        }

        for (time_str++; is_space(*time_str); time_str++);

        if (*time_str != '\0')
                goto fail;

        struct tm tm = {
                .tm_sec = second,
                .tm_min = minute,
                .tm_hour = hour,
                .tm_mday = day,
                .tm_mon = month - 1,
                .tm_year = year - 1900,
                .tm_isdst = 0,
        };

        time_t t = timegm(&tm);

        if (t == (time_t) -1)
                goto fail;

        parser->time = t + sub_second_dividend / (double) sub_second_divisor;

        return true;

fail:
        report_error(parser, "invalid time");
        return false;
}

static bool
parse_positive_float(struct flt_gpx_parser *parser,
                     float *out)
{
        char *tail;

        errno = 0;
        float f = strtof((const char *) parser->buf.data, &tail);

        while (is_space(*tail))
                tail++;

        if (*tail != '\0' || errno || f < 0)
                return false;

        *out = f;

        return true;
}

static bool
parse_float_range(const char *str,
                  float min, float max,
                  float *out)
{
        char *tail;

        errno = 0;
        float f = strtof(str, &tail);

        while (is_space(*tail))
                tail++;

        if (*tail != '\0' ||
            errno ||
            f < min ||
            f > max ||
            (f != 0.0f && !isnormal(f)))
                return false;

        *out = f;

        return true;
}

static bool
parse_speed(struct flt_gpx_parser *parser)
{
        if (!parse_positive_float(parser, &parser->speed)) {
                report_error(parser, "invalid speed");
                return false;
        }

        return true;
}

static bool
parse_ele(struct flt_gpx_parser *parser)
{
        if (!parse_positive_float(parser, &parser->elevation)) {
                report_error(parser, "invalid elevation");
                return false;
        }

        return true;
}

static void
add_point(struct flt_gpx_parser *parser)
{
        flt_buffer_set_length(&parser->points,
                              parser->points.length +
                              sizeof (struct flt_gpx_point));
        struct flt_gpx_point *point =
                (struct flt_gpx_point *)
                (parser->points.data + parser->points.length) -
                1;

        point->lat = parser->lat;
        point->lon = parser->lon;
        point->time = parser->time;
        point->speed = parser->speed;
        point->elevation = parser->elevation;
}

static bool
parse_lat_lon(struct flt_gpx_parser *parser,
              const XML_Char **atts)
{
        bool found_lat = false, found_lon = false;

        for (const XML_Char **a = atts; a[0]; a += 2) {
                if (!strcmp(a[0], "lat")) {
                        if (!parse_float_range(a[1],
                                               -90.0f, /* min */
                                               90.0f, /* max */
                                               &parser->lat)) {
                                report_error(parser, "invalid lat");
                                return false;
                        }
                        found_lat = true;
                } else if (!strcmp(a[0], "lon")) {
                        if (!parse_float_range(a[1],
                                               -180.0f, /* min */
                                               180.0f, /* max */
                                               &parser->lon)) {
                                report_error(parser, "invalid lon");
                                return false;
                        }
                        found_lon = true;
                }
       }

        if (!found_lat) {
                report_error(parser, "missing lat attribute");
                return false;
        }

        if (!found_lon) {
                report_error(parser, "missing lon attribute");
                return false;
        }

        return true;
}

static bool
is_gpx_element(const char *name, const char *element_name)
{
        static const char namespace[] = "http://www.topografix.com/GPX/1/";

        if (strncmp(name, namespace, sizeof namespace - 1))
                return false;

        name += sizeof namespace - 1;

        if (*name != '0' && *name != '1')
                return false;

        name++;

        if (*name != '\xff')
                return false;

        name++;

        return !strcmp(name, element_name);
}

static void
start_element_cb(void *user_data,
                 const XML_Char *name,
                 const XML_Char **atts)
{
        struct flt_gpx_parser *parser = user_data;

        if (parser->skip_depth > 0) {
                parser->skip_depth++;
                return;
        }

        switch (parser->parse_state) {
        case PARSE_STATE_LOOKING_FOR_TRKPT:
                if (is_gpx_element(name, "trkpt")) {
                        if (!parse_lat_lon(parser, atts))
                                return;

                        parser->time = -1.0;
                        parser->speed = -1.0f;
                        parser->elevation = -1.0f;
                        parser->parse_state = PARSE_STATE_IN_TRKPT;
                }
                return;

        case PARSE_STATE_IN_TRKPT:
                flt_buffer_set_length(&parser->buf, 0);

                if (is_gpx_element(name, "time"))
                        parser->parse_state = PARSE_STATE_IN_TIME;
                else if (is_gpx_element(name, "speed"))
                        parser->parse_state = PARSE_STATE_IN_SPEED;
                else if (is_gpx_element(name, "ele"))
                        parser->parse_state = PARSE_STATE_IN_ELE;
                else if (is_gpx_element(name, "ele"))
                        parser->parse_state = PARSE_STATE_IN_ELE;
                else if (is_gpx_element(name, "extensions"))
                        parser->parse_state = PARSE_STATE_IN_EXTENSIONS;
                else
                        parser->skip_depth++;
                return;

        case PARSE_STATE_IN_TIME:
        case PARSE_STATE_IN_SPEED:
        case PARSE_STATE_IN_ELE:
        case PARSE_STATE_IN_EXTENSION_SPEED:
                report_error(parser, "unexpected element start");
                return;

        case PARSE_STATE_IN_EXTENSIONS:
                if (strcmp(name, TPX_ELEMENT("TrackPointExtension")))
                        parser->skip_depth++;
                else
                        parser->parse_state =
                                PARSE_STATE_IN_TRACK_POINT_EXTENSION;
                return;

        case PARSE_STATE_IN_TRACK_POINT_EXTENSION:
                if (strcmp(name, TPX_ELEMENT("speed")))
                        parser->skip_depth++;
                else
                        parser->parse_state =
                                PARSE_STATE_IN_EXTENSION_SPEED;
                return;
        }

        assert(!"Invalid parse_state");
}

static void
end_element_cb(void *user_data,
               const XML_Char *name)
{
        struct flt_gpx_parser *parser = user_data;

        if (parser->skip_depth > 0) {
                parser->skip_depth--;
                return;
        }

        switch (parser->parse_state) {
        case PARSE_STATE_LOOKING_FOR_TRKPT:
                return;

        case PARSE_STATE_IN_TRKPT:
                if (parser->time >= 0.0 &&
                    parser->speed >= 0.0f &&
                    parser->elevation >= 0.0f)
                        add_point(parser);

                parser->parse_state = PARSE_STATE_LOOKING_FOR_TRKPT;

                return;

        case PARSE_STATE_IN_TIME:
                if (!parse_time(parser))
                        return;
                parser->parse_state = PARSE_STATE_IN_TRKPT;
                return;

        case PARSE_STATE_IN_SPEED:
                if (!parse_speed(parser))
                        return;
                parser->parse_state = PARSE_STATE_IN_TRKPT;
                return;

        case PARSE_STATE_IN_ELE:
                if (!parse_ele(parser))
                        return;
                parser->parse_state = PARSE_STATE_IN_TRKPT;
                return;

        case PARSE_STATE_IN_EXTENSIONS:
                parser->parse_state = PARSE_STATE_IN_TRKPT;
                return;

        case PARSE_STATE_IN_TRACK_POINT_EXTENSION:
                parser->parse_state = PARSE_STATE_IN_EXTENSIONS;
                return;

        case PARSE_STATE_IN_EXTENSION_SPEED:
                if (!parse_speed(parser))
                        return;
                parser->parse_state = PARSE_STATE_IN_TRACK_POINT_EXTENSION;
                return;
        }

        assert(!"Invalid parse_state");
}

static void
character_data_cb(void *user_data,
                  const XML_Char *s,
                  int len)
{
        struct flt_gpx_parser *parser = user_data;

        switch (parser->parse_state) {
        case PARSE_STATE_LOOKING_FOR_TRKPT:
        case PARSE_STATE_IN_EXTENSIONS:
        case PARSE_STATE_IN_TRACK_POINT_EXTENSION:
                break;

        case PARSE_STATE_IN_TRKPT:
        case PARSE_STATE_IN_TIME:
        case PARSE_STATE_IN_SPEED:
        case PARSE_STATE_IN_ELE:
        case PARSE_STATE_IN_EXTENSION_SPEED:
                flt_buffer_append(&parser->buf, s, len);
                flt_buffer_append_c(&parser->buf, '\0');
                parser->buf.length--;
                break;
        }
}

static void
run_parser(struct flt_gpx_parser *parser,
           FILE *f)
{
        while (true) {
                char buf[128];

                size_t got = fread(buf, 1, sizeof buf, f);

                if (XML_Parse(parser->parser, buf, got, got < sizeof buf) ==
                    XML_STATUS_ERROR) {
                        report_xml_error(parser);
                        break;
                }

                if (got < sizeof buf)
                        break;
        }
}

static int
compare_point_time_cb(const void *pa,
                      const void *pb)
{
        const struct flt_gpx_point *a = pa;
        const struct flt_gpx_point *b = pb;

        if (a->time < b->time)
                return -1;
        if (a->time > b->time)
                return 1;
        return 0;
}

static size_t
remove_duplicate_points(struct flt_gpx_point *points, size_t n_points)
{
        struct flt_gpx_point *dst = points;
        const struct flt_gpx_point *end = points + n_points;

        for (const struct flt_gpx_point *src = points; src < end; src++) {
                *(dst++) = *src;

                while (src + 1 < end && src[1].time == src->time)
                        src++;
        }

        return dst - points;
}

bool
flt_gpx_parse(const char *filename,
              struct flt_gpx_point **points_out,
              size_t *n_points_out,
              struct flt_error **error)
{
        FILE *f = fopen(filename, "r");

        if (f == NULL) {
                flt_file_error_set(error,
                                   errno,
                                   "%s: %s",
                                   filename,
                                   strerror(errno));
                return false;
        }

        struct flt_gpx_parser parser = {
                .parser = XML_ParserCreateNS(NULL, /* encoding */
                                             '\xff'),
                .filename = filename,
                .points = FLT_BUFFER_STATIC_INIT,
                .buf = FLT_BUFFER_STATIC_INIT,
                .parse_state = PARSE_STATE_LOOKING_FOR_TRKPT,
                .skip_depth = 0,
                .error = NULL,
        };

        XML_SetUserData(parser.parser, &parser);
        XML_SetElementHandler(parser.parser, start_element_cb, end_element_cb);
        XML_SetCharacterDataHandler(parser.parser, character_data_cb);

        run_parser(&parser, f);

        fclose(f);

        flt_buffer_destroy(&parser.buf);

        XML_ParserFree(parser.parser);

        if (parser.error) {
                flt_buffer_destroy(&parser.points);
                flt_error_propagate(error, parser.error);
                return false;
        }

        if (parser.points.length == 0) {
                flt_buffer_destroy(&parser.points);
                flt_set_error(error,
                              &flt_gpx_error,
                              FLT_GPX_ERROR_INVALID,
                              "%s: no track points found in GPX file",
                              filename);
                return false;
        }

        struct flt_gpx_point *points =
                (struct flt_gpx_point *) parser.points.data;
        size_t n_points = parser.points.length / sizeof (struct flt_gpx_point);

        qsort(points,
              n_points,
              sizeof *points,
              compare_point_time_cb);

        *points_out = points;
        *n_points_out = remove_duplicate_points(*points_out, n_points);

        return true;
}

static void
set_data_from_point(struct flt_gpx_data *data,
                    const struct flt_gpx_point *point)
{
        data->lat = point->lat;
        data->lon = point->lon;
        data->speed = point->speed;
        data->elevation = point->elevation;
}

bool
flt_gpx_find_data(const struct flt_gpx_point *points,
                  size_t n_points,
                  double timestamp,
                  struct flt_gpx_data *data)
{
        int min = 0, max = n_points;

        while (max > min) {
                int mid = (min + max) / 2;

                if (points[mid].time < timestamp) {
                        min = mid + 1;
                } else {
                        max = mid;
                }
        }

        if (min >= n_points || points[min].time != timestamp)
                min--;

        if (min <= 0 && timestamp <= points[0].time) {
                if (points[0].time - timestamp <= MAX_TIME_GAP) {
                        set_data_from_point(data, points + 0);
                        return true;
                }

                return false;
        }

        if (min >= n_points - 1) {
                if (timestamp - points[n_points - 1].time <= MAX_TIME_GAP) {
                        set_data_from_point(data, points + n_points - 1);
                        return true;
                }

                return false;
        }

        if (timestamp - points[min].time > MAX_TIME_GAP) {
                if (points[min + 1].time - timestamp <= MAX_TIME_GAP) {
                        set_data_from_point(data, points + min + 1);
                        return true;
                }

                return false;
        }

        if (points[min + 1].time - timestamp > MAX_TIME_GAP) {
                set_data_from_point(data, points + min);
                return true;
        }

        /* Both points are in range so interpolate them */

        double t = ((timestamp - points[min].time) /
                    (double) (points[min + 1].time - points[min].time));

        data->lat = (t *
                     (points[min + 1].lat - points[min].lat) +
                     points[min].lat);

        data->lon = (t *
                     (points[min + 1].lon - points[min].lon) +
                     points[min].lon);

        data->speed = (t *
                       (points[min + 1].speed - points[min].speed) +
                       points[min].speed);

        data->elevation = (t *
                           (points[min + 1].elevation - points[min].elevation) +
                           points[min].elevation);

        return true;
}
