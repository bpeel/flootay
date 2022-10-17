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

#include "flt-renderer.h"

#include <math.h>

#include "flt-util.h"
#include "flt-buffer.h"
#include "flt-map-renderer.h"

#define SCORE_LABEL "SCORE "
#define SCORE_NAME "LYON"
#define ELEVATION_LABEL "ELEVATION"
#define SCORE_SLIDE_TIME 0.5
#define MAP_POINT_SIZE 24.0

struct flt_renderer {
        struct flt_scene *scene;
        struct flt_map_renderer *map_renderer;
        cairo_pattern_t *map_point_pattern;
};

struct flt_error_domain
flt_renderer_error;

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
set_source_from_color(cairo_t *cr, uint32_t color)
{
        cairo_set_source_rgb(cr,
                             ((color >> 16) & 0xff) / 255.0,
                             ((color >> 8) & 0xff) / 255.0,
                             (color & 0xff) / 255.0);
}

static void
interpolate_and_add_rectangle(struct flt_renderer *renderer,
                              cairo_t *cr,
                              const struct flt_scene_rectangle *rectangle,
                              double i,
                              const struct flt_scene_rectangle_key_frame *s,
                              const struct flt_scene_rectangle_key_frame *e)
{
        int x1 = clamp(interpolate(i, s->x1, e->x1),
                       0,
                       renderer->scene->video_width);
        int y1 = clamp(interpolate(i, s->y1, e->y1),
                       0,
                       renderer->scene->video_height);
        int x2 = clamp(interpolate(i, s->x2, e->x2),
                       x1,
                       renderer->scene->video_width);
        int y2 = clamp(interpolate(i, s->y2, e->y2),
                       y1,
                       renderer->scene->video_height);

        set_source_from_color(cr, rectangle->color);
        cairo_rectangle(cr, x1, y1, x2 - x1, y2 - y1);
        cairo_fill(cr);
}

static void
interpolate_and_add_svg(struct flt_renderer *renderer,
                        cairo_t *cr,
                        const struct flt_scene_svg *svg,
                        double i,
                        const struct flt_scene_svg_key_frame *s,
                        const struct flt_scene_svg_key_frame *e)
{
        int x = interpolate(i, s->x, e->x);
        int y = interpolate(i, s->y, e->y);

        cairo_save(cr);
        cairo_translate(cr, x, y);
        rsvg_handle_render_cairo(svg->handle, cr);
        cairo_restore(cr);
}

static bool
interpolate_and_add_svg_viewport(struct flt_renderer *renderer,
                                 cairo_t *cr,
                                 const struct flt_scene_svg *svg,
                                 double i,
                                 const struct
                                 flt_scene_svg_viewport_key_frame *s,
                                 const struct
                                 flt_scene_svg_viewport_key_frame *e,
                                 struct flt_error **error_out)
{
        double x1 = interpolate_double(i, s->x1, e->x1);
        double y1 = interpolate_double(i, s->y1, e->y1);
        double x2 = interpolate_double(i, s->x2, e->x2);
        double y2 = interpolate_double(i, s->y2, e->y2);

        RsvgRectangle viewport = {
                .x = MIN(x1, x2),
                .y = MIN(y1, y2),
                .width = fabs(x1 - x2),
                .height = fabs(y2 - y1),
        };

        GError *error = NULL;

        if (!rsvg_handle_render_document(svg->handle,
                                         cr,
                                         &viewport,
                                         &error)) {
                flt_set_error(error_out,
                              &flt_renderer_error,
                              FLT_RENDERER_ERROR_SVG,
                              "%s",
                              error->message);
                g_error_free(error);

                return false;
        }

        return true;
}

static void
render_score_text(struct flt_renderer *renderer,
                  cairo_t *cr,
                  const char *text)
{
        double after_x, after_y;

        cairo_save(cr);
        cairo_set_line_width(cr, renderer->scene->video_height / 90.0f);
        cairo_text_path(cr, text);
        cairo_get_current_point(cr, &after_x, &after_y);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_fill(cr);
        cairo_restore(cr);

        cairo_move_to(cr, after_x, after_y);
}

static void
interpolate_and_add_score(struct flt_renderer *renderer,
                          cairo_t *cr,
                          const struct flt_scene_score *score,
                          double timestamp,
                          const struct flt_scene_score_key_frame *s,
                          const struct flt_scene_score_key_frame *e)
{
        float gap = renderer->scene->video_height / 15.0f;

        cairo_save(cr);
        cairo_set_font_size(cr, renderer->scene->video_height / 10.0f);

        cairo_font_extents_t extents;

        cairo_font_extents(cr, &extents);
        cairo_move_to(cr, gap, extents.height);

        render_score_text(renderer, cr, SCORE_LABEL);

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        if (s->value != e->value &&
            timestamp >= e->base.timestamp - SCORE_SLIDE_TIME) {
                double score_x, score_y;
                cairo_get_current_point(cr, &score_x, &score_y);

                cairo_save(cr);
                cairo_rectangle(cr,
                                0,
                                score_y - extents.ascent,
                                renderer->scene->video_width,
                                extents.ascent + extents.descent);
                cairo_clip(cr);

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

                cairo_move_to(cr,
                              score_x,
                              score_y + extents.height - offset);
                flt_buffer_append_printf(&buf, "%i", bottom_value);
                render_score_text(renderer, cr, (const char *) buf.data);

                cairo_move_to(cr, score_x, score_y - offset);
                flt_buffer_set_length(&buf, 0);
                flt_buffer_append_printf(&buf, "%i", top_value);
                render_score_text(renderer, cr, (const char *) buf.data);

                cairo_restore(cr);
        } else {
                flt_buffer_append_printf(&buf, "%i", s->value);
                render_score_text(renderer, cr, (const char *) buf.data);
        }

        cairo_text_extents_t text_extents;

        cairo_text_extents(cr, SCORE_NAME, &text_extents);
        cairo_move_to(cr,
                      renderer->scene->video_width -
                      text_extents.x_advance -
                      gap,
                      extents.height);
        render_score_text(renderer, cr, SCORE_NAME);

        cairo_restore(cr);

        flt_buffer_destroy(&buf);
}

static void
add_speed(struct flt_renderer *renderer,
          cairo_t *cr,
          double speed_ms)
{
        int speed_kmh = round(speed_ms * 3600 / 1000);

        float gap = renderer->scene->video_height / 15.0f;

        cairo_save(cr);

        cairo_set_font_size(cr, renderer->scene->video_height / 12.0f);

        cairo_move_to(cr, gap, renderer->scene->video_height - gap);

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf, "%2i", speed_kmh);

        cairo_save(cr);
        cairo_select_font_face(cr,
                               "monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);

        render_score_text(renderer, cr, (const char *) buf.data);

        cairo_restore(cr);

        flt_buffer_destroy(&buf);

        cairo_set_font_size(cr, renderer->scene->video_height / 24.0f);

        render_score_text(renderer, cr, " km/h");

        cairo_restore(cr);
}

static void
add_elevation(struct flt_renderer *renderer,
              cairo_t *cr,
              double elevation)
{
        float gap = renderer->scene->video_height / 15.0f;

        cairo_save(cr);

        cairo_set_font_size(cr, renderer->scene->video_height / 12.0f);

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf, "%2i", (int) round(elevation));

        cairo_save(cr);

        cairo_select_font_face(cr,
                               "monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);

        cairo_text_extents_t text_extents;

        cairo_text_extents(cr,
                           (const char *) buf.data,
                           &text_extents);

        cairo_move_to(cr,
                      renderer->scene->video_width -
                      gap -
                      text_extents.x_advance,
                      renderer->scene->video_height - gap);

        render_score_text(renderer, cr, (const char *) buf.data);

        cairo_restore(cr);

        flt_buffer_destroy(&buf);

        cairo_set_font_size(cr, renderer->scene->video_height / 30.0f);

        cairo_text_extents(cr, ELEVATION_LABEL, &text_extents);

        cairo_move_to(cr,
                      renderer->scene->video_width -
                      gap -
                      text_extents.x_advance,
                      renderer->scene->video_height -
                      gap +
                      text_extents.height *
                      1.3);

        render_score_text(renderer, cr, ELEVATION_LABEL);

        cairo_restore(cr);
}

static void
add_distance(struct flt_renderer *renderer,
             cairo_t *cr,
             double distance)
{
        float gap = renderer->scene->video_height / 15.0f;

        cairo_save(cr);

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;
        const char *units;

        if (distance < 1000.0) {
                flt_buffer_append_printf(&buf,
                                         "%2i",
                                         (int) distance);
                units = " m";
        } else {
                flt_buffer_append_printf(&buf,
                                         "%.2f",
                                         distance / 1000.0);
                units = " km";
        }

        cairo_set_font_size(cr, renderer->scene->video_height / 24.0f);

        cairo_text_extents_t units_extents;

        cairo_text_extents(cr, units, &units_extents);

        cairo_save(cr);

        cairo_text_extents_t text_extents;

        cairo_select_font_face(cr,
                               "monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, renderer->scene->video_height / 12.0f);

        cairo_text_extents(cr,
                           (const char *) buf.data,
                           &text_extents);

        double total_x_advance = (units_extents.x_advance +
                                  text_extents.x_advance);

        cairo_move_to(cr,
                      renderer->scene->video_width / 2.0 -
                      total_x_advance / 2.0,
                      renderer->scene->video_height - gap);

        render_score_text(renderer, cr, (const char *) buf.data);

        cairo_restore(cr);

        flt_buffer_destroy(&buf);

        cairo_move_to(cr,
                      renderer->scene->video_width / 2.0 -
                      total_x_advance / 2.0 +
                      text_extents.x_advance,
                      renderer->scene->video_height - gap);

        render_score_text(renderer, cr, units);

        cairo_restore(cr);
}

static bool
add_map(struct flt_renderer *renderer,
        cairo_t *cr,
        double lat, double lon,
        struct flt_error **error)
{
        if (renderer->map_renderer == NULL) {
                renderer->map_renderer =
                        flt_map_renderer_new(renderer->scene->map_url_base,
                                             renderer->scene->map_api_key);
        }

        if (renderer->map_point_pattern == NULL) {
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
                renderer->map_point_pattern = p;
        }

        bool ret = true;

        const float map_size_tile_units = 216.0f;
        float gap = renderer->scene->video_height / 15.0f;
        float map_size = renderer->scene->video_height * 0.3;
        float map_scale = map_size / map_size_tile_units;

        cairo_save(cr);
        cairo_translate(cr,
                        renderer->scene->video_width - gap - map_size / 2.0,
                        renderer->scene->video_height - gap - map_size / 2.0);
        cairo_scale(cr, map_scale, map_scale);

        if (!flt_map_renderer_render(renderer->map_renderer,
                                     cr,
                                     17, /* zoom */
                                     lat, lon,
                                     0.0, 0.0, /* draw_center_x/y */
                                     round(map_size_tile_units),
                                     round(map_size_tile_units),
                                     error))
                ret = false;

        cairo_set_source(cr, renderer->map_point_pattern);
        cairo_rectangle(cr,
                        -MAP_POINT_SIZE / 2.0, -MAP_POINT_SIZE / 2.0,
                        MAP_POINT_SIZE,
                        MAP_POINT_SIZE);
        cairo_fill(cr);

        cairo_restore(cr);

        return ret;
}

static bool
interpolate_and_add_gpx(struct flt_renderer *renderer,
                        cairo_t *cr,
                        const struct flt_scene_gpx *gpx,
                        double i,
                        const struct flt_scene_gpx_key_frame *s,
                        const struct flt_scene_gpx_key_frame *e,
                        struct flt_error **error)
{
        double timestamp = interpolate_double(i, s->timestamp, e->timestamp);

        struct flt_gpx_data gpx_data;

        if (!flt_gpx_find_data(gpx->file->points,
                               gpx->file->n_points,
                               timestamp,
                               &gpx_data))
                return true;

        if (gpx->show_speed)
                add_speed(renderer, cr, gpx_data.speed);
        if (gpx->show_elevation)
                add_elevation(renderer, cr, gpx_data.elevation);

        if (gpx->show_distance) {
                add_distance(renderer,
                             cr,
                             gpx_data.distance + gpx->distance_offset);
        }

        if (gpx->show_map &&
            !add_map(renderer, cr, gpx_data.lat, gpx_data.lon, error))
                return false;

        return true;
}

static void
interpolate_and_add_time(struct flt_renderer *renderer,
                         cairo_t *cr,
                         double i,
                         const struct flt_scene_time_key_frame *s,
                         const struct flt_scene_time_key_frame *e)
{
        int value = interpolate_double(i, s->value, e->value);

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        if (value < 0) {
                flt_buffer_append_c(&buf, '-');
                value = -value;
        }

        if (value >= 3600) {
                flt_buffer_append_printf(&buf,
                                         "%ih%02im%02is",
                                         value / 3600,
                                         value % 3600 / 60,
                                         value % 60);
        } else if (value >= 60) {
                flt_buffer_append_printf(&buf,
                                         "%im%02is",
                                         value / 60,
                                         value % 60);
        } else {
                flt_buffer_append_printf(&buf, "%is", value);
        }

        float gap = renderer->scene->video_height / 15.0f;

        cairo_save(cr);

        cairo_select_font_face(cr,
                               "monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, renderer->scene->video_height / 12.0f);

        cairo_text_extents_t text_extents;

        cairo_text_extents(cr,
                           (const char *) buf.data,
                           &text_extents);

        cairo_move_to(cr,
                      renderer->scene->video_width / 2.0 -
                      text_extents.x_advance / 2.0,
                      gap + text_extents.height);

        render_score_text(renderer, cr, (const char *) buf.data);

        cairo_restore(cr);

        flt_buffer_destroy(&buf);
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
interpolate_and_add_curve(struct flt_renderer *renderer,
                          cairo_t *cr,
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

        cairo_save(cr);

        cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
        set_source_from_color(cr, curve->color);
        cairo_set_line_width(cr,
                             interpolate_double(i,
                                                s->stroke_width,
                                                e->stroke_width));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

        cairo_move_to(cr, sub_x_points[0], sub_y_points[0]);
        cairo_curve_to(cr,
                       sub_x_points[1], sub_y_points[1],
                       sub_x_points[2], sub_y_points[2],
                       sub_x_points[3], sub_y_points[3]);
        cairo_stroke(cr);

        cairo_restore(cr);
}

static enum flt_renderer_result
interpolate_and_add_object(struct flt_renderer *renderer,
                           cairo_t *cr,
                           double timestamp,
                           const struct flt_scene_object *object,
                           struct flt_error **error)
{
        const struct flt_scene_key_frame *end_frame;

        flt_list_for_each(end_frame, &object->key_frames, link) {
                if (end_frame->timestamp > timestamp)
                        goto found_frame;
        }

        return FLT_RENDERER_RESULT_EMPTY;

found_frame:

        /* Ignore if the end frame is the first frame */
        if (object->key_frames.next == &end_frame->link)
                return FLT_RENDERER_RESULT_EMPTY;

        const struct flt_scene_key_frame *s =
                flt_container_of(end_frame->link.prev,
                                 struct flt_scene_key_frame,
                                 link);
        double i = ((timestamp - s->timestamp) /
                    (end_frame->timestamp - s->timestamp));

        switch (object->type) {
        case FLT_SCENE_OBJECT_TYPE_RECTANGLE:
                interpolate_and_add_rectangle(renderer,
                                              cr,
                                              (const struct
                                               flt_scene_rectangle *) object,
                                              i,
                                              (const struct
                                               flt_scene_rectangle_key_frame *)
                                              s,
                                              (const struct
                                               flt_scene_rectangle_key_frame *)
                                              end_frame);
                break;
        case FLT_SCENE_OBJECT_TYPE_SVG:
                interpolate_and_add_svg(renderer,
                                        cr,
                                        (const struct flt_scene_svg *) object,
                                        i,
                                        (const struct
                                         flt_scene_svg_key_frame *)
                                        s,
                                        (const struct
                                         flt_scene_svg_key_frame *)
                                        end_frame);
                break;
        case FLT_SCENE_OBJECT_TYPE_SVG_VIEWPORT:
                if (!interpolate_and_add_svg_viewport(renderer,
                                                      cr,
                                                      (const void *) object,
                                                      i,
                                                      (const void *) s,
                                                      (const void *) end_frame,
                                                      error))
                        return FLT_RENDERER_RESULT_ERROR;
                break;
        case FLT_SCENE_OBJECT_TYPE_SCORE:
                interpolate_and_add_score(renderer,
                                          cr,
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
                if (!interpolate_and_add_gpx(renderer,
                                             cr,
                                             (const struct flt_scene_gpx *)
                                             object,
                                             i,
                                             (const struct
                                              flt_scene_gpx_key_frame *) s,
                                             (const struct
                                              flt_scene_gpx_key_frame *)
                                             end_frame,
                                             error))
                        return FLT_RENDERER_RESULT_ERROR;
                break;
        case FLT_SCENE_OBJECT_TYPE_TIME:
                interpolate_and_add_time(renderer,
                                         cr,
                                         i,
                                         (const struct
                                          flt_scene_time_key_frame *)
                                         s,
                                         (const struct
                                          flt_scene_time_key_frame *)
                                         end_frame);
                break;
        case FLT_SCENE_OBJECT_TYPE_CURVE:
                interpolate_and_add_curve(renderer,
                                          cr,
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

        return FLT_RENDERER_RESULT_OK;
}

struct flt_renderer *
flt_renderer_new(struct flt_scene *scene)
{
        struct flt_renderer *renderer = flt_calloc(sizeof *renderer);

        renderer->scene = scene;

        return renderer;
}

enum flt_renderer_result
flt_renderer_render(struct flt_renderer *renderer,
                    cairo_t *cr,
                    double timestamp,
                    struct flt_error **error)
{
        const struct flt_scene_object *object;

        enum flt_renderer_result ret = FLT_RENDERER_RESULT_EMPTY;

        flt_list_for_each(object, &renderer->scene->objects, link) {
                switch (interpolate_and_add_object(renderer,
                                                   cr,
                                                   timestamp,
                                                   object,
                                                   error)) {
                case FLT_RENDERER_RESULT_ERROR:
                        return FLT_RENDERER_RESULT_ERROR;

                case FLT_RENDERER_RESULT_EMPTY:
                        break;

                case FLT_RENDERER_RESULT_OK:
                        ret = FLT_RENDERER_RESULT_OK;
                        break;
                }
        }

        return ret;
}

void
flt_renderer_free(struct flt_renderer *renderer)
{
        if (renderer->map_point_pattern)
                cairo_pattern_destroy(renderer->map_point_pattern);
        if (renderer->map_renderer)
                flt_map_renderer_free(renderer->map_renderer);

        flt_free(renderer);
}
