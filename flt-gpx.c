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

#include "flt-util.h"
#include "flt-buffer.h"
#include "flt-file-error.h"

struct flt_error_domain
flt_gpx_error;

struct flt_gpx_parser {
        XML_Parser parser;

        const char *filename;

        struct flt_buffer points;

        /* Temporary buffer used for collecting element text */
        struct flt_buffer buf;

        bool in_time;
        bool in_speed;

        /* The current node depth if we are in a trkpt, or -1 otherwise */
        int trkpt_depth;
        /* The unix time that we found in this trkpt, or -1 if not
         * found yet.
         */
        int64_t time;
        /* The speed that we found, or a negative number if not found yet */
        float speed;

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

        parser->time = t;

        return true;

fail:
        report_error(parser, "invalid time");
        return false;
}

static bool
parse_speed(struct flt_gpx_parser *parser)
{
        char *tail;

        errno = 0;
        float f = strtof((const char *) parser->buf.data, &tail);

        while (is_space(*tail))
                tail++;

        if (*tail != '\0' || errno || f < 0) {
                report_error(parser, "invalid speed");
                return false;
        }

        parser->speed = f;

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

        point->time = parser->time;
        point->speed = parser->speed;
}

static void
start_element_cb(void *user_data,
                 const XML_Char *name,
                 const XML_Char **atts)
{
        struct flt_gpx_parser *parser = user_data;

        if (parser->in_time || parser->in_speed) {
                report_error(parser, "unexpected element start");
                return;
        }

        if (parser->trkpt_depth < 0) {
                if (!strcmp(name, "trkpt"))
                        parser->trkpt_depth = 0;
                return;
        }

        parser->trkpt_depth++;

        if (parser->trkpt_depth == 0) {
                parser->time = -1;
                parser->speed = -1.0f;
                return;
        }

        if (parser->trkpt_depth != 1)
                return;

        flt_buffer_set_length(&parser->buf, 0);

        if (!strcmp(name, "time"))
                parser->in_time = true;
        else if (!strcmp(name, "speed"))
                parser->in_speed = true;
}

static void
end_element_cb(void *user_data,
               const XML_Char *name)
{
        struct flt_gpx_parser *parser = user_data;

        if (parser->in_time) {
                if (!parse_time(parser))
                        return;
                parser->in_time = false;
        }

        if (parser->in_speed) {
                if (!parse_speed(parser))
                        return;
                parser->in_speed = false;
        }

        if (parser->trkpt_depth < 0)
                return;

        if (parser->trkpt_depth == 0 &&
            parser->time >= 0 &&
            parser->speed >= 0.0f)
                add_point(parser);

        parser->trkpt_depth--;
}

static void
character_data_cb(void *user_data,
                  const XML_Char *s,
                  int len)
{
        struct flt_gpx_parser *parser = user_data;

        if (parser->in_time || parser->in_speed) {
                flt_buffer_append(&parser->buf, s, len);
                flt_buffer_append_c(&parser->buf, '\0');
                parser->buf.length--;
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
                .parser = XML_ParserCreate(NULL),
                .filename = filename,
                .points = FLT_BUFFER_STATIC_INIT,
                .buf = FLT_BUFFER_STATIC_INIT,
                .trkpt_depth = -1,
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

        *n_points_out = parser.points.length / sizeof (struct flt_gpx_point);
        *points_out = (struct flt_gpx_point *) parser.points.data;

        qsort(*points_out,
              *n_points_out,
              sizeof **points_out,
              compare_point_time_cb);

        return true;
}