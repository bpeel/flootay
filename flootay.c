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
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <cairo.h>
#include <errno.h>

#include "flt-util.h"
#include "flt-scene.h"
#include "flt-parser.h"
#include "flt-file-error.h"
#include "flt-buffer.h"
#include "flt-map-renderer.h"

#define SCORE_LABEL "SCORE "
#define SCORE_NAME "LYON"
#define ELEVATION_LABEL "ELEVATION"
#define SCORE_SLIDE_TIME 0.5
#define MAP_POINT_SIZE 24.0
#define FPS 30

struct render_data {
        struct flt_scene *scene;
        cairo_t *cr;
        struct flt_map_renderer *map_renderer;
        cairo_pattern_t *map_point_pattern;
};

static int
interpolate(double factor, int s, int e)
{
        return roundf(s + factor * (e - s));
}

static double
interpolate_double(double factor, double s, double e)
{
        return s + factor * (e - s);
}

static int
clamp(int value, int min, int max)
{
        if (value <= min)
                return min;
        if (value >= max)
                return max;
        return value;
}

static void
fill_rectangle(cairo_t *cr,
               int x1, int y1,
               int x2, int y2)
{
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_rectangle(cr, x1, y1, x2 - x1, y2 - y1);
        cairo_fill(cr);
}

static void
interpolate_and_add_rectangle(struct render_data *data,
                              double i,
                              const struct flt_scene_rectangle_key_frame *s,
                              const struct flt_scene_rectangle_key_frame *e)
{
        int x1 = clamp(interpolate(i, s->x1, e->x1),
                       0,
                       data->scene->video_width);
        int y1 = clamp(interpolate(i, s->y1, e->y1),
                       0,
                       data->scene->video_height);
        int x2 = clamp(interpolate(i, s->x2, e->x2),
                       x1,
                       data->scene->video_width);
        int y2 = clamp(interpolate(i, s->y2, e->y2),
                       y1,
                       data->scene->video_height);

        fill_rectangle(data->cr, x1, y1, x2, y2);
}

static void
interpolate_and_add_svg(struct render_data *data,
                        const struct flt_scene_svg *svg,
                        double i,
                        const struct flt_scene_svg_key_frame *s,
                        const struct flt_scene_svg_key_frame *e)
{
        int x = interpolate(i, s->x, e->x);
        int y = interpolate(i, s->y, e->y);

        cairo_save(data->cr);
        cairo_translate(data->cr, x, y);
        rsvg_handle_render_cairo(svg->handle, data->cr);
        cairo_restore(data->cr);
}

static void
render_score_text(struct render_data *data,
                  const char *text)
{
        double after_x, after_y;

        cairo_save(data->cr);
        cairo_set_line_width(data->cr, data->scene->video_height / 90.0f);
        cairo_text_path(data->cr, text);
        cairo_get_current_point(data->cr, &after_x, &after_y);
        cairo_set_source_rgb(data->cr, 0.0, 0.0, 0.0);
        cairo_set_line_join(data->cr, CAIRO_LINE_JOIN_ROUND);
        cairo_stroke_preserve(data->cr);
        cairo_set_source_rgb(data->cr, 1.0, 1.0, 1.0);
        cairo_fill(data->cr);
        cairo_restore(data->cr);

        cairo_move_to(data->cr, after_x, after_y);
}

static void
interpolate_and_add_score(struct render_data *data,
                          const struct flt_scene_score *score,
                          double timestamp,
                          const struct flt_scene_score_key_frame *s,
                          const struct flt_scene_score_key_frame *e)
{
        float gap = data->scene->video_height / 15.0f;

        cairo_save(data->cr);
        cairo_set_font_size(data->cr, data->scene->video_height / 10.0f);

        cairo_font_extents_t extents;

        cairo_font_extents(data->cr, &extents);
        cairo_move_to(data->cr, gap, extents.height);

        render_score_text(data, SCORE_LABEL);

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        if (s->value != e->value &&
            timestamp >= e->base.timestamp - SCORE_SLIDE_TIME) {
                double score_x, score_y;
                cairo_get_current_point(data->cr, &score_x, &score_y);

                cairo_save(data->cr);
                cairo_rectangle(data->cr,
                                0,
                                score_y - extents.ascent,
                                data->scene->video_width,
                                extents.ascent + extents.descent);
                cairo_clip(data->cr);

                double offset = ((e->base.timestamp - timestamp) *
                                 extents.height /
                                 SCORE_SLIDE_TIME);
                int top_value, bottom_value;

                if (e->value > s->value) {
                        top_value = s->value;
                        bottom_value = e->value;
                        offset = extents.height - offset;
                } else {
                        top_value = e->value;
                        bottom_value = s->value;
                }

                cairo_move_to(data->cr,
                              score_x,
                              score_y + extents.height - offset);
                flt_buffer_append_printf(&buf, "%i", bottom_value);
                render_score_text(data, (const char *) buf.data);

                cairo_move_to(data->cr, score_x, score_y - offset);
                flt_buffer_set_length(&buf, 0);
                flt_buffer_append_printf(&buf, "%i", top_value);
                render_score_text(data, (const char *) buf.data);

                cairo_restore(data->cr);
        } else {
                flt_buffer_append_printf(&buf, "%i", s->value);
                render_score_text(data, (const char *) buf.data);
        }

        cairo_text_extents_t text_extents;

        cairo_text_extents(data->cr, SCORE_NAME, &text_extents);
        cairo_move_to(data->cr,
                      data->scene->video_width - text_extents.x_advance - gap,
                      extents.height);
        render_score_text(data, SCORE_NAME);

        cairo_restore(data->cr);

        flt_buffer_destroy(&buf);
}

static void
add_speed(struct render_data *data,
          double speed_ms)
{
        int speed_kmh = round(speed_ms * 3600 / 1000);

        float gap = data->scene->video_height / 15.0f;

        cairo_save(data->cr);

        cairo_set_font_size(data->cr, data->scene->video_height / 12.0f);

        cairo_move_to(data->cr, gap, data->scene->video_height - gap);

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf, "%2i", speed_kmh);

        cairo_save(data->cr);
        cairo_select_font_face(data->cr,
                               "monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);

        render_score_text(data, (const char *) buf.data);

        cairo_restore(data->cr);

        flt_buffer_destroy(&buf);

        cairo_set_font_size(data->cr, data->scene->video_height / 24.0f);

        render_score_text(data, " km/h");

        cairo_restore(data->cr);
}

static void
add_elevation(struct render_data *data,
              double elevation)
{
        float gap = data->scene->video_height / 15.0f;

        cairo_save(data->cr);

        cairo_set_font_size(data->cr, data->scene->video_height / 12.0f);

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf, "%2i", (int) round(elevation));

        cairo_save(data->cr);

        cairo_select_font_face(data->cr,
                               "monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);

        cairo_text_extents_t text_extents;

        cairo_text_extents(data->cr, (const char *) buf.data, &text_extents);

        cairo_move_to(data->cr,
                      data->scene->video_width - gap - text_extents.x_advance,
                      data->scene->video_height - gap);

        render_score_text(data, (const char *) buf.data);

        cairo_restore(data->cr);

        flt_buffer_destroy(&buf);

        cairo_set_font_size(data->cr, data->scene->video_height / 30.0f);

        cairo_text_extents(data->cr, ELEVATION_LABEL, &text_extents);

        cairo_move_to(data->cr,
                      data->scene->video_width - gap - text_extents.x_advance,
                      data->scene->video_height -
                      gap +
                      text_extents.height *
                      1.3);

        render_score_text(data, ELEVATION_LABEL);

        cairo_restore(data->cr);
}

static bool
add_map(struct render_data *data,
        double lat, double lon)
{
        if (data->map_renderer == NULL) {
                data->map_renderer =
                        flt_map_renderer_new(data->scene->map_url_base,
                                             data->scene->map_api_key);
        }

        if (data->map_point_pattern == NULL) {
                cairo_pattern_t *p =
                        cairo_pattern_create_radial(0.0, 0.0, /* cx0/cy0 */
                                                    0.0, /* radius0 */
                                                    0.0, 0.0, /* cx1/cy1 */
                                                    MAP_POINT_SIZE / 2.0);
                cairo_pattern_add_color_stop_rgba(p,
                                                  0.0, /* offset */
                                                  0.043, 0.0, 1.0, /* rgb */
                                                  1.0 /* a */);
                cairo_pattern_add_color_stop_rgba(p,
                                                  0.6, /* offset */
                                                  0.043, 0.0, 1.0, /* rgb */
                                                  1.0 /* a */);
                cairo_pattern_add_color_stop_rgba(p,
                                                  1.0, /* offset */
                                                  0.043, 0.0, 1.0, /* rgb */
                                                  0.0 /* a */);
                data->map_point_pattern = p;
        }

        struct flt_error *error = NULL;

        bool ret = true;

        const float map_size_tile_units = 216.0f;
        float gap = data->scene->video_height / 15.0f;
        float map_size = data->scene->video_height * 0.3;
        float map_scale = map_size / map_size_tile_units;

        cairo_save(data->cr);
        cairo_translate(data->cr,
                        data->scene->video_width - gap - map_size / 2.0,
                        data->scene->video_height - gap - map_size / 2.0);
        cairo_scale(data->cr, map_scale, map_scale);

        if (!flt_map_renderer_render(data->map_renderer,
                                     data->cr,
                                     17, /* zoom */
                                     lat, lon,
                                     0.0, 0.0, /* draw_center_x/y */
                                     round(map_size_tile_units),
                                     round(map_size_tile_units),
                                     &error)) {
                fprintf(stderr, "%s\n", error->message);
                flt_error_free(error);
                ret = false;
        }

        cairo_set_source(data->cr, data->map_point_pattern);
        cairo_rectangle(data->cr,
                        -MAP_POINT_SIZE / 2.0, -MAP_POINT_SIZE / 2.0,
                        MAP_POINT_SIZE,
                        MAP_POINT_SIZE);
        cairo_fill(data->cr);

        cairo_restore(data->cr);

        return ret;
}

static bool
interpolate_and_add_gpx(struct render_data *data,
                        const struct flt_scene_gpx *gpx,
                        double i,
                        const struct flt_scene_gpx_key_frame *s,
                        const struct flt_scene_gpx_key_frame *e)
{
        double timestamp = interpolate_double(i, s->timestamp, e->timestamp);

        struct flt_gpx_data gpx_data;

        if (!flt_gpx_find_data(gpx->points,
                               gpx->n_points,
                               timestamp,
                               &gpx_data))
                return true;

        if (gpx->show_speed)
                add_speed(data, gpx_data.speed);
        if (gpx->show_elevation)
                add_elevation(data, gpx_data.elevation);

        if (gpx->show_map && !add_map(data, gpx_data.lat, gpx_data.lon))
                return false;

        return true;
}

static void
clip_curve_axis(double t,
                const double points[4],
                double sub_points[4])
{
        double t2 = t * t;
        double t3 = t2 * t;
        double rt = 1.0 - t;
        double rt2 = rt * rt;
        double rt3 = rt2 * rt;

        /* One control point */
        sub_points[0] = points[0];
        /* Two control points */
        sub_points[1] = rt * points[0] + t * points[1];
        /* Three control points */
        sub_points[2] = (rt2 * points[0] +
                         2.0 * rt * t * points[1] +
                         t2 * points[2]);
        /* Four control points */
        sub_points[3] = (rt3 * points[0] +
                         3.0 * rt2 * t * points[1] +
                         3.0 * rt * t2 * points[2] +
                         t3 * points[3]);
}

static void
interpolate_and_add_curve(struct render_data *data,
                          const struct flt_scene_curve *curve,
                          double i,
                          const struct flt_scene_curve_key_frame *s,
                          const struct flt_scene_curve_key_frame *e)
{
        double t = interpolate_double(i, s->t, e->t);

        if (t <= 0.0)
                return;

        double x_points[4], y_points[4];

        for (int p = 0; p < FLT_N_ELEMENTS(x_points); p++) {
                x_points[p] = interpolate_double(i,
                                                 s->points[p].x,
                                                 e->points[p].x);
                y_points[p] = interpolate_double(i,
                                                 s->points[p].y,
                                                 e->points[p].y);
        }

        double sub_x_points[4], sub_y_points[4];

        if (t >= 1.0) {
                memcpy(sub_x_points, x_points, sizeof x_points);
                memcpy(sub_y_points, y_points, sizeof y_points);
        } else {
                clip_curve_axis(t, x_points, sub_x_points);
                clip_curve_axis(t, y_points, sub_y_points);
        }

        cairo_save(data->cr);

        cairo_set_antialias(data->cr, CAIRO_ANTIALIAS_BEST);
        cairo_set_source_rgb(data->cr,
                             curve->r,
                             curve->g,
                             curve->b);
        cairo_set_line_width(data->cr,
                             interpolate_double(i,
                                                s->stroke_width,
                                                e->stroke_width));
        cairo_set_line_cap(data->cr, CAIRO_LINE_CAP_ROUND);

        cairo_move_to(data->cr, sub_x_points[0], sub_y_points[0]);
        cairo_curve_to(data->cr,
                       sub_x_points[1], sub_y_points[1],
                       sub_x_points[2], sub_y_points[2],
                       sub_x_points[3], sub_y_points[3]);
        cairo_stroke(data->cr);

        cairo_restore(data->cr);
}

static bool
interpolate_and_add_object(struct render_data *data,
                           double timestamp,
                           const struct flt_scene_object *object)
{
        const struct flt_scene_key_frame *end_frame;

        flt_list_for_each(end_frame, &object->key_frames, link) {
                if (end_frame->timestamp > timestamp)
                        goto found_frame;
        }

        return true;

found_frame:

        /* Ignore if the end frame is the first frame */
        if (object->key_frames.next == &end_frame->link)
                return true;

        const struct flt_scene_key_frame *s =
                flt_container_of(end_frame->link.prev,
                                 struct flt_scene_key_frame,
                                 link);
        double i = ((timestamp - s->timestamp) /
                    (end_frame->timestamp - s->timestamp));

        switch (object->type) {
        case FLT_SCENE_OBJECT_TYPE_RECTANGLE:
                interpolate_and_add_rectangle(data,
                                              i,
                                              (const struct
                                               flt_scene_rectangle_key_frame *)
                                              s,
                                              (const struct
                                               flt_scene_rectangle_key_frame *)
                                              end_frame);
                break;
        case FLT_SCENE_OBJECT_TYPE_SVG:
                interpolate_and_add_svg(data,
                                        (const struct flt_scene_svg *) object,
                                        i,
                                        (const struct
                                         flt_scene_svg_key_frame *)
                                        s,
                                        (const struct
                                         flt_scene_svg_key_frame *)
                                        end_frame);
                break;
        case FLT_SCENE_OBJECT_TYPE_SCORE:
                interpolate_and_add_score(data,
                                          (const struct flt_scene_score *)
                                          object,
                                          timestamp,
                                          (const struct
                                           flt_scene_score_key_frame *)
                                          s,
                                          (const struct
                                           flt_scene_score_key_frame *)
                                          end_frame);
                break;
        case FLT_SCENE_OBJECT_TYPE_GPX:
                if (!interpolate_and_add_gpx(data,
                                             (const struct flt_scene_gpx *)
                                             object,
                                             i,
                                             (const struct
                                              flt_scene_gpx_key_frame *) s,
                                             (const struct
                                              flt_scene_gpx_key_frame *)
                                             end_frame))
                        return false;
                break;
        case FLT_SCENE_OBJECT_TYPE_CURVE:
                interpolate_and_add_curve(data,
                                          (const struct flt_scene_curve *)
                                          object,
                                          i,
                                          (const struct
                                           flt_scene_curve_key_frame *)
                                          s,
                                          (const struct
                                           flt_scene_curve_key_frame *)
                                          end_frame);
                break;
        }

        return true;
}

static bool
write_surface(cairo_surface_t *surface)
{
        int width = cairo_image_surface_get_width(surface);
        int height = cairo_image_surface_get_height(surface);
        int stride = cairo_image_surface_get_stride(surface);
        uint8_t *data = cairo_image_surface_get_data(surface);

        for (int y = 0; y < height; y++) {
                uint32_t *row = (uint32_t *) (data + y * stride);
                uint8_t *out_pix = data;

                for (int x = 0; x < width; x++) {
                        uint32_t value = row[x];

                        uint8_t a = value >> 24;
                        uint8_t r = (value >> 16) & 0xff;
                        uint8_t g = (value >> 8) & 0xff;
                        uint8_t b = value & 0xff;

                        if (a > 0) {
                                /* unpremultiply */
                                r = r * 255 / a;
                                g = g * 255 / a;
                                b = b * 255 / a;
                        }

                        *(out_pix++) = r;
                        *(out_pix++) = g;
                        *(out_pix++) = b;
                        *(out_pix++) = a;
                }

                size_t wrote = fwrite(data, 1, width * 4, stdout);

                if (wrote != width * 4) {
                        fprintf(stderr,
                                "error writing frame: %s\n",
                                strerror(errno));
                        return false;
                }
        }

        return true;
}

struct stdio_source {
        struct flt_source base;
        FILE *infile;
};

static bool
read_stdio_cb(struct flt_source *source,
              void *ptr,
              size_t *length,
              struct flt_error **error)
{
        struct stdio_source *stdio_source = (struct stdio_source *) source;

        size_t got = fread(ptr, 1, *length, stdio_source->infile);

        if (got < *length) {
                if (ferror(stdio_source->infile)) {
                        flt_file_error_set(error,
                                           errno,
                                           "%s",
                                           strerror(errno));
                        return false;
                }

                *length = got;
        }

        return true;
}

static bool
load_stdin(struct flt_scene *scene,
           struct flt_error **error)
{
        struct stdio_source source = {
                .base = { .read_source = read_stdio_cb },
                .infile = stdin,
        };

        return flt_parser_parse(scene, &source.base, NULL, error);
}

static bool
load_file(struct flt_scene *scene,
          const char *filename,
          struct flt_error **error)
{
        struct stdio_source source = {
                .base = { .read_source = read_stdio_cb },
                .infile = fopen(filename, "rb"),
        };

        if (source.infile == NULL) {
                flt_file_error_set(error,
                                   errno,
                                   "%s: %s",
                                   filename,
                                   strerror(errno));
                return false;
        }

        char *base_dir;

        const char *last_part = strrchr(filename, '/');

        if (last_part == NULL)
                base_dir = NULL;
        else
                base_dir = flt_strndup(filename, last_part - filename);

        bool ret = flt_parser_parse(scene, &source.base, base_dir, error);

        fclose(source.infile);

        flt_free(base_dir);

        return ret;
}

int
main(int argc, char **argv)
{
        struct flt_scene *scene = flt_scene_new();
        int ret = EXIT_SUCCESS;

        if (argc <= 1) {
                fprintf(stderr, "usage: <script-file>…\n");
                ret = EXIT_FAILURE;
                goto out;
        }

        for (int i = 1; i < argc; i++) {
                struct flt_error *error = NULL;
                bool load_ret;

                if (!strcmp(argv[i], "-"))
                        load_ret = load_stdin(scene, &error);
                else
                        load_ret = load_file(scene, argv[i], &error);

                if (!load_ret) {
                        fprintf(stderr, "%s\n", error->message);
                        flt_error_free(error);
                        ret = EXIT_FAILURE;
                        goto out;
                }
        }

        cairo_surface_t *surface =
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                           scene->video_width,
                                           scene->video_height);
        cairo_t *cr = cairo_create(surface);

        int n_frames = ceil(flt_scene_get_max_timestamp(scene) * FPS) + 1;

        struct render_data data = {
                .scene = scene,
                .cr = cr,
        };

        for (int frame_num = 0; frame_num < n_frames; frame_num++) {
                cairo_save(cr);
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(cr);
                cairo_restore(cr);

                const struct flt_scene_object *object;

                flt_list_for_each(object, &scene->objects, link) {
                        if (!interpolate_and_add_object(&data,
                                                        frame_num /
                                                        (double) FPS,
                                                        object)) {
                                ret = EXIT_FAILURE;
                                goto render_out;
                        }
                }

                cairo_surface_flush(surface);

                if (!write_surface(surface)) {
                        ret = EXIT_FAILURE;
                        goto render_out;
                }
        }

render_out:
        cairo_surface_destroy(surface);
        cairo_destroy(cr);

        if (data.map_point_pattern)
                cairo_pattern_destroy(data.map_point_pattern);
        if (data.map_renderer)
                flt_map_renderer_free(data.map_renderer);

out:
        flt_scene_free(scene);

        return ret;
}
