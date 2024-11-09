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

#include "flt-trace.h"

#include <stdio.h>
#include <string.h>
#include <json_object.h>
#include <errno.h>
#include <json_tokener.h>

#include "flt-util.h"
#include "flt-buffer.h"
#include "flt-file-error.h"

/* This file parses the cycle path trace files used by Cyclopolis:
 * https://github.com/benoitdemaegdt/voieslyonnaises/tree/main/content/voies-cyclables
 */

static const char * const
status_names[] = {
        [FLT_TRACE_SEGMENT_STATUS_DONE] = "done",
        [FLT_TRACE_SEGMENT_STATUS_WIP] = "wip",
        [FLT_TRACE_SEGMENT_STATUS_PLANNED] = "planned",
        [FLT_TRACE_SEGMENT_STATUS_TESTED] = "tested",
        [FLT_TRACE_SEGMENT_STATUS_POSTPONED] = "postponed",
        [FLT_TRACE_SEGMENT_STATUS_UNKNOWN] = "unknown",
        [FLT_TRACE_SEGMENT_STATUS_VARIANT] = "variante",
        [FLT_TRACE_SEGMENT_STATUS_VARIANT_POSTPONED] = "variante-postponed",
};

struct parser {
        struct flt_error *error;
        const char *filename;
        struct flt_buffer segments;
        struct flt_buffer points;
};

struct flt_error_domain
flt_trace_error;

static void
destroy_segments(struct flt_trace_segment *segments,
                 size_t n_segments)
{
        for (size_t i = 0; i < n_segments; i++)
                flt_free(segments[i].points);
}

static bool
check_only_spaces_in_buffer(const char *filename,
                            const char *buf,
                            size_t buf_length,
                            struct flt_error **error)
{
        for (size_t i = 0; i < buf_length; i++) {
                if (!strchr(" \t\r\n", buf[i])) {
                        flt_set_error(error,
                                      &flt_trace_error,
                                      FLT_TRACE_ERROR_INVALID,
                                      "%s: extra data at end of file",
                                      filename);
                        return false;
                }
        }

        return true;
}

static bool
check_only_spaces_after_parse_end(const char *filename,
                                  struct json_tokener *tokener,
                                  const char *buf,
                                  size_t buf_size,
                                  struct flt_error **error)
{
        size_t used = json_tokener_get_parse_end(tokener);

        return check_only_spaces_in_buffer(filename,
                                           buf + used,
                                           buf_size - used,
                                           error);
}

static ssize_t
fill_buffer(const char *filename,
            char *buf,
            size_t buf_size,
            FILE *input,
            struct flt_error **error)
{
        size_t got = fread(buf, 1, sizeof buf, input);

        if (ferror(input)) {
                flt_file_error_set(error,
                                   errno,
                                   "%s: %s",
                                   filename,
                                   strerror(errno));
                return -1;
        }

        return got;
}

static bool
check_only_spaces_in_file(const char *filename,
                          char *buf,
                          size_t buf_size,
                          FILE *input,
                          struct flt_error **error)
{
        while (true) {
                ssize_t got = fill_buffer(filename,
                                          buf,
                                          buf_size,
                                          input,
                                          error);

                if (got < 0)
                        return false;

                if (got == 0)
                        return true;

                if (!check_only_spaces_in_buffer(filename, buf, got, error))
                        return false;
        }
}

static struct json_object *
load_json_from_file(const char *filename,
                    struct flt_error **error)
{
        FILE *input = fopen(filename, "r");

        if (input == NULL) {
                flt_file_error_set(error,
                                   errno,
                                   "%s: %s",
                                   filename,
                                   strerror(errno));
                return NULL;
        }

        struct json_tokener *tokener = json_tokener_new();
        char buf[1024];
        struct json_object *obj = NULL;

        while (true) {
                ssize_t got = fill_buffer(filename,
                                          buf,
                                          sizeof buf,
                                          input,
                                          error);

                if (got < 0)
                        break;

                if (got == 0) {
                        flt_set_error(error,
                                      &flt_trace_error,
                                      FLT_TRACE_ERROR_INVALID,
                                      "%s: %s",
                                      filename,
                                      "unexpected EOF");
                        break;
                }

                obj = json_tokener_parse_ex(tokener, buf, got);

                if (obj) {
                        if (!check_only_spaces_after_parse_end(filename,
                                                               tokener,
                                                               buf,
                                                               got,
                                                               error) ||
                            !check_only_spaces_in_file(filename,
                                                       buf,
                                                       sizeof buf,
                                                       input,
                                                       error)) {
                                json_object_put(obj);
                                obj = NULL;
                        }

                        break;
                } else {
                        enum json_tokener_error jerr =
                                json_tokener_get_error(tokener);

                        if (jerr != json_tokener_continue) {
                                flt_set_error(error,
                                              &flt_trace_error,
                                              FLT_TRACE_ERROR_INVALID,
                                              "%s: %s",
                                              filename,
                                              json_tokener_error_desc(jerr));
                                break;
                        }
                }
        }

        fclose(input);
        json_tokener_free(tokener);

        return obj;
}

static struct json_object *
get_field(struct parser *parser,
          struct json_object *obj,
          const char *field,
          struct flt_error **error)
{
        if (!json_object_is_type(obj, json_type_object)) {
                flt_set_error(error,
                              &flt_trace_error,
                              FLT_TRACE_ERROR_INVALID,
                              "%s: object expected but a different type "
                              "was found",
                              parser->filename);
                return NULL;
        }

        struct json_object *value;

        if (!json_object_object_get_ex(obj, field, &value)) {
                flt_set_error(error,
                              &flt_trace_error,
                              FLT_TRACE_ERROR_INVALID,
                              "%s: missing property “%s”",
                              parser->filename,
                              field);
                return NULL;
        }

        return value;
}

static bool
check_array(struct parser *parser,
            struct json_object *obj,
            struct flt_error **error)
{
        if (!json_object_is_type(obj, json_type_array)) {
                flt_set_error(error,
                              &flt_trace_error,
                              FLT_TRACE_ERROR_INVALID,
                              "%s: array expected but a different type "
                              "was found",
                              parser->filename);
                return false;
        } else {
                return true;
        }
}

enum check_string_result {
        CHECK_STRING_RESULT_ERROR,
        CHECK_STRING_RESULT_MATCHED,
        CHECK_STRING_RESULT_NOT_MATCHED,
};

static enum check_string_result
compare_string(struct parser *parser,
               struct json_object *value,
               const char *expected_value,
               struct flt_error **error)
{
       if (!json_object_is_type(value, json_type_string)) {
                flt_set_error(error,
                              &flt_trace_error,
                              FLT_TRACE_ERROR_INVALID,
                              "%s: string expected but a different type "
                              "was found",
                              parser->filename);
                return CHECK_STRING_RESULT_ERROR;
        }

        size_t len = json_object_get_string_len(value);

        if (len == strlen(expected_value) &&
            !memcmp(expected_value, json_object_get_string(value), len))
                return CHECK_STRING_RESULT_MATCHED;
        else
                return CHECK_STRING_RESULT_NOT_MATCHED;
}

static enum check_string_result
check_string_field(struct parser *parser,
                   struct json_object *obj,
                   const char *field,
                   const char *expected_value,
                   struct flt_error **error)
{
        struct json_object *value =
                get_field(parser, obj, field, error);

        if (value == NULL)
                return CHECK_STRING_RESULT_ERROR;

        return compare_string(parser, value, expected_value, error);
}

static bool
extract_status(struct parser *parser,
               struct json_object *feature,
               enum flt_trace_segment_status *status_ret,
               struct flt_error **error)
{
        struct json_object *properties =
                get_field(parser, feature, "properties", error);

        if (properties == NULL)
                return false;

        struct json_object *status_obj =
                get_field(parser, properties, "status", error);

        if (status_obj == NULL)
                return false;

        for (unsigned i = 0; i < FLT_N_ELEMENTS(status_names); i++) {
                switch (compare_string(parser,
                                       status_obj,
                                       status_names[i],
                                       error)) {
                case CHECK_STRING_RESULT_ERROR:
                        return false;
                case CHECK_STRING_RESULT_NOT_MATCHED:
                        continue;
                case CHECK_STRING_RESULT_MATCHED:
                        *status_ret = i;
                        return true;
                }
        }

        flt_set_error(error,
                      &flt_trace_error,
                      FLT_TRACE_ERROR_INVALID,
                      "%s: unexpected feature status: %s",
                      parser->filename,
                      json_object_get_string(status_obj));
        return false;
}

static void
add_point(struct parser *parser,
          const double *parts)
{
        flt_buffer_set_length(&parser->points,
                              parser->points.length +
                              sizeof (struct flt_trace_point));

        struct flt_trace_point *point =
                (struct flt_trace_point *)
                (parser->points.data +
                 parser->points.length -
                 sizeof (struct flt_trace_point));

        point->lon = parts[0];
        point->lat = parts[1];
}

static bool
parse_coordinate(struct parser *parser,
                 struct json_object *coord,
                 struct flt_error **error)
{
        if (!check_array(parser, coord, error))
                return false;

        size_t len = json_object_array_length(coord);

        if (len != 2) {
                flt_set_error(error,
                              &flt_trace_error,
                              FLT_TRACE_ERROR_INVALID,
                              "%s: encountered coordinate with %zu elements",
                              parser->filename,
                              len);
                return false;
        }

        double parts[2];

        for (int i = 0; i < 2; i++) {
                struct json_object *part = json_object_array_get_idx(coord, i);

                if (!json_object_is_type(part, json_type_double)) {
                        flt_set_error(error,
                                      &flt_trace_error,
                                      FLT_TRACE_ERROR_INVALID,
                                      "%s: double expected but a different "
                                      "type was found",
                                      parser->filename);
                        return false;
                }

                parts[i] = json_object_get_double(part);
        }

        add_point(parser, parts);

        return true;
}

static bool
parse_coordinates(struct parser *parser,
                  struct json_object *coordinates,
                  struct flt_error **error)
{
        if (!check_array(parser, coordinates, error))
                return false;

        flt_buffer_set_length(&parser->points, 0);

        size_t len = json_object_array_length(coordinates);

        for (size_t i = 0; i < len; i++) {
                struct json_object *coord =
                        json_object_array_get_idx(coordinates, i);
                if (!parse_coordinate(parser, coord, error))
                        return false;
        }

        return true;
}

static void
store_segment(struct parser *parser,
              enum flt_trace_segment_status status)
{
        flt_buffer_set_length(&parser->segments,
                              parser->segments.length +
                              sizeof (struct flt_trace_segment));

        struct flt_trace_segment *segment =
                (struct flt_trace_segment *)
                (parser->segments.data +
                 parser->segments.length -
                 sizeof (struct flt_trace_segment));

        segment->status = status;
        segment->points = (struct flt_trace_point *) parser->points.data;
        segment->n_points = parser->points.length /
                sizeof (struct flt_trace_point);

        flt_buffer_init(&parser->points);
}

static bool
parse_feature(struct parser *parser,
              struct json_object *feature,
              struct flt_error **error)
{
        struct json_object *geometry =
                get_field(parser, feature, "geometry", error);

        if (geometry == NULL)
                return false;

        switch (check_string_field(parser,
                                   geometry,
                                   "type",
                                   "LineString",
                                   error)) {
        case CHECK_STRING_RESULT_ERROR:
                return false;
        case CHECK_STRING_RESULT_NOT_MATCHED:
                return true;
        case CHECK_STRING_RESULT_MATCHED:
                break;
        }

        enum flt_trace_segment_status status;

        if (!extract_status(parser, feature, &status, error))
                return false;

        struct json_object *coordinates =
                get_field(parser, geometry, "coordinates", error);

        if (coordinates == NULL)
                return false;

        if (!parse_coordinates(parser, coordinates, error))
                return false;

        store_segment(parser, status);

        return true;
}

static bool
parse_object(struct parser *parser,
             struct json_object *obj,
             struct flt_error **error)
{
        struct json_object *features =
                get_field(parser, obj, "features", error);

        if (features == NULL)
                return false;

        if (!check_array(parser, features, error))
                return false;

        size_t n_features = json_object_array_length(features);

        for (size_t i = 0; i < n_features; i++) {
                if (!parse_feature(parser,
                                   json_object_array_get_idx(features, i),
                                   error))
                        return false;
        }

        return true;
}

struct flt_trace *
flt_trace_parse(const char *filename,
                struct flt_error **error)
{
        struct json_object *obj = load_json_from_file(filename, error);

        if (obj == NULL)
                return NULL;

        struct parser parser = {
                .filename = filename,
                .segments = FLT_BUFFER_STATIC_INIT,
                .points = FLT_BUFFER_STATIC_INIT,
        };

        struct flt_trace *ret = NULL;

        bool parse_ret = parse_object(&parser, obj, error);

        size_t n_segments = parser.segments.length /
                sizeof (struct flt_trace_segment);

        if (parse_ret) {
                ret = flt_alloc(sizeof *ret);

                ret->n_segments = n_segments;
                ret->segments =
                        (struct flt_trace_segment *) parser.segments.data;
        } else {
                destroy_segments((struct flt_trace_segment *)
                                 parser.segments.data,
                                 n_segments);
                flt_buffer_destroy(&parser.segments);
        }

        flt_buffer_destroy(&parser.points);
        json_object_put(obj);

        return ret;
}

void
flt_trace_free(struct flt_trace *trace)
{
        destroy_segments(trace->segments, trace->n_segments);
        flt_free(trace->segments);
        flt_free(trace);
}
