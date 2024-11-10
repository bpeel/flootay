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
#include <stdarg.h>
#include <assert.h>

#include "flt-util.h"
#include "flt-buffer.h"
#include "flt-map-renderer.h"
#include "flt-source-color.h"

#define ELEVATION_LABEL "ELEVATION"
#define SCORE_SLIDE_TIME 0.5
#define MAP_POINT_SIZE 24.0

struct font_with_size {
        cairo_font_face_t *face;
        double size;
};

struct flt_renderer {
        struct flt_scene *scene;
        struct flt_map_renderer *map_renderer;
        cairo_pattern_t *map_point_pattern;
        double position_offsets[FLT_SCENE_N_POSITIONS];
        float gap;

        struct font_with_size digits_font;
        struct font_with_size units_font;
        struct font_with_size label_font;
        struct font_with_size score_font;
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
set_font(cairo_t *cr, const struct font_with_size *font)
{
        cairo_set_font_face(cr, font->face);
        cairo_set_font_size(cr, font->size);
}

static void
get_position(struct flt_renderer *renderer,
             enum flt_scene_position position,
             double width,
             double height,
             double *x_out,
             double *y_out)
{
        assert(position >= 0 && position < FLT_SCENE_N_POSITIONS);

        *x_out = 0.0;
        *y_out = 0.0;

        switch (FLT_SCENE_GET_HORIZONTAL_POSITION(position)) {
        case FLT_SCENE_HORIZONTAL_POSITION_LEFT:
                *x_out = renderer->gap;
                break;
        case FLT_SCENE_HORIZONTAL_POSITION_MIDDLE:
                *x_out = renderer->scene->video_width / 2.0 - width / 2.0;
                break;
        case FLT_SCENE_HORIZONTAL_POSITION_RIGHT:
                *x_out = renderer->scene->video_width - renderer->gap - width;
                break;
        }

        double offset = renderer->position_offsets[position];

        switch (FLT_SCENE_GET_VERTICAL_POSITION(position)) {
        case FLT_SCENE_VERTICAL_POSITION_TOP:
                *y_out = renderer->gap + offset;
                break;
        case FLT_SCENE_VERTICAL_POSITION_BOTTOM:
                *y_out = (renderer->scene->video_height -
                          renderer->gap -
                          offset -
                          height);
                break;
        }

        renderer->position_offsets[position] += height;
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

        flt_source_color_set(cr, rectangle->color, 1.0);
        cairo_rectangle(cr, x1, y1, x2 - x1, y2 - y1);
        cairo_fill(cr);
}

static bool
render_svg(RsvgHandle *handle,
           cairo_t *cr,
           const RsvgRectangle *viewport,
           struct flt_error **error_out)
{
        GError *error = NULL;

        if (!rsvg_handle_render_document(handle,
                                         cr,
                                         viewport,
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

static bool
interpolate_and_add_svg(struct flt_renderer *renderer,
                        cairo_t *cr,
                        const struct flt_scene_svg *svg,
                        double i,
                        const struct
                        flt_scene_svg_key_frame *s,
                        const struct
                        flt_scene_svg_key_frame *e,
                        struct flt_error **error)
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

        return render_svg(svg->handle, cr, &viewport, error);
}

static void
render_text(struct flt_renderer *renderer,
            cairo_t *cr,
            uint32_t color,
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
        flt_source_color_set(cr, color, 1.0);
        cairo_fill(cr);
        cairo_restore(cr);

        cairo_move_to(cr, after_x, after_y);
}

static FLT_NULL_TERMINATED void
render_text_parts(struct flt_renderer *renderer,
                  cairo_t *cr,
                  enum flt_scene_position position,
                  uint32_t color,
                  ...)
{
        cairo_save(cr);

        va_list ap, copy;

        va_start(ap, color);
        va_copy(copy, ap);

        double ascent = 0.0, height = 0.0, x_advance = 0.0;

        while (true) {
                const struct font_with_size *font =
                        va_arg(ap, const struct font_with_size *);

                if (font == NULL)
                        break;

                set_font(cr, font);

                const char *text = va_arg(ap, const char *);

                cairo_text_extents_t text_extents;
                cairo_text_extents(cr, text, &text_extents);
                x_advance += text_extents.x_advance;

                cairo_font_extents_t font_extents;
                cairo_font_extents(cr, &font_extents);
                if (font_extents.ascent > ascent)
                        ascent = font_extents.ascent;
                if (font_extents.height > height)
                        height = font_extents.height;
        }

        va_end(ap);

        double x, y;

        get_position(renderer, position, x_advance, height, &x, &y);

        cairo_move_to(cr, x, y + ascent);

        while (true) {
                const struct font_with_size *font =
                        va_arg(copy, const struct font_with_size *);

                if (font == NULL)
                        break;

                set_font(cr, font);
                render_text(renderer, cr, color, va_arg(copy, const char *));
        }

        va_end(copy);

        cairo_restore(cr);
}

static void
interpolate_and_add_score(struct flt_renderer *renderer,
                          cairo_t *cr,
                          const struct flt_scene_score *score,
                          double timestamp,
                          const struct flt_scene_score_key_frame *s,
                          const struct flt_scene_score_key_frame *e)
{
        cairo_save(cr);
        set_font(cr, &renderer->score_font);

        const char *score_label = score->label ? score->label : "SCORE";

        cairo_font_extents_t font_extents;
        cairo_font_extents(cr, &font_extents);

        cairo_text_extents_t label_extents;
        cairo_text_extents(cr, score_label, &label_extents);

        cairo_text_extents_t space_extents;
        cairo_text_extents(cr, " ", &space_extents);

        cairo_text_extents_t template_extents;
        cairo_text_extents(cr, "00", &template_extents);

        double base_x, base_y;
        get_position(renderer,
                     score->position,
                     label_extents.x_advance +
                     space_extents.x_advance +
                     template_extents.x_advance,
                     font_extents.height,
                     &base_x, &base_y);

        cairo_move_to(cr, base_x, base_y + font_extents.ascent);

        render_text(renderer, cr, score->color, score_label);
        cairo_rel_move_to(cr, space_extents.x_advance, 0.0);

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        if (s->value != e->value &&
            timestamp >= e->base.timestamp - SCORE_SLIDE_TIME) {
                double score_x, score_y;
                cairo_get_current_point(cr, &score_x, &score_y);

                cairo_save(cr);
                cairo_rectangle(cr,
                                0,
                                base_y,
                                renderer->scene->video_width,
                                font_extents.height);
                cairo_clip(cr);

                double offset = ((e->base.timestamp - timestamp) *
                                 font_extents.height /
                                 SCORE_SLIDE_TIME);
                int top_value, bottom_value;

                if (e->value > s->value) {
                        top_value = s->value;
                        bottom_value = e->value;
                        offset = font_extents.height - offset;
                } else {
                        top_value = e->value;
                        bottom_value = s->value;
                }

                cairo_move_to(cr,
                              score_x,
                              score_y + font_extents.height - offset);
                flt_buffer_append_printf(&buf, "%i", bottom_value);
                render_text(renderer,
                            cr,
                            score->color,
                            (const char *) buf.data);

                cairo_move_to(cr, score_x, score_y - offset);
                flt_buffer_set_length(&buf, 0);
                flt_buffer_append_printf(&buf, "%i", top_value);
                render_text(renderer,
                            cr,
                            score->color,
                            (const char *) buf.data);

                cairo_restore(cr);
        } else {
                flt_buffer_append_printf(&buf, "%i", s->value);
                render_text(renderer,
                            cr,
                            score->color,
                            (const char *) buf.data);
        }

        cairo_restore(cr);

        flt_buffer_destroy(&buf);
}

static bool
add_speed_dial(struct flt_renderer *renderer,
               cairo_t *cr,
               const struct flt_scene_gpx_speed *speed,
               double speed_ms,
               struct flt_error **error)
{
        RsvgRectangle viewport = {
                .width = speed->width,
                .height = speed->height,
        };

        get_position(renderer,
                     speed->base.position,
                     viewport.width,
                     viewport.height,
                     &viewport.x,
                     &viewport.y);

        if (!render_svg(speed->dial, cr, &viewport, error))
                return false;

        cairo_save(cr);

        double rotation_x = viewport.x + viewport.width / 2.0;
        double rotation_y = viewport.y + viewport.height / 2.0;

        cairo_translate(cr, rotation_x, rotation_y);
        cairo_rotate(cr, speed_ms * 2.0 * M_PI / speed->full_speed);
        cairo_translate(cr, -rotation_x, -rotation_y);

        bool ret = render_svg(speed->needle, cr, &viewport, error);

        cairo_restore(cr);

        return ret;
}

static void
add_speed_digits(struct flt_renderer *renderer,
                 cairo_t *cr,
                 const struct flt_scene_gpx_speed *speed,
                 double speed_ms)
{
        int speed_kmh = round(speed_ms * 3600 / 1000);

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf, "%2i", speed_kmh);

        render_text_parts(renderer,
                          cr,
                          speed->base.position,
                          speed->color,
                          &renderer->digits_font,
                          (const char *) buf.data,
                          &renderer->units_font,
                          " km/h",
                          NULL);

        flt_buffer_destroy(&buf);
}

static bool
add_speed(struct flt_renderer *renderer,
          cairo_t *cr,
          const struct flt_scene_gpx_speed *speed,
          double speed_ms,
          struct flt_error **error)
{
        if (speed->dial) {
                return add_speed_dial(renderer, cr, speed, speed_ms, error);
        } else {
                add_speed_digits(renderer, cr, speed, speed_ms);
                return true;
        }
}

static void
add_elevation(struct flt_renderer *renderer,
              cairo_t *cr,
              const struct flt_scene_gpx_elevation *elevation_obj,
              double elevation)
{
        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf, "%2i", (int) round(elevation));

        render_text_parts(renderer,
                          cr,
                          elevation_obj->base.position,
                          elevation_obj->color,
                          &renderer->digits_font,
                          (const char  *) buf.data,
                          NULL);

        flt_buffer_destroy(&buf);

        render_text_parts(renderer,
                          cr,
                          elevation_obj->base.position,
                          elevation_obj->color,
                          &renderer->label_font,
                          ELEVATION_LABEL,
                          NULL);
}

static void
add_distance(struct flt_renderer *renderer,
             cairo_t *cr,
             const struct flt_scene_gpx_distance *distance_obj,
             double distance)
{
        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;
        const char *units;

        distance += distance_obj->offset;

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

        render_text_parts(renderer,
                          cr,
                          distance_obj->base.position,
                          distance_obj->color,
                          &renderer->digits_font,
                          (const char *) buf.data,
                          &renderer->units_font,
                          units,
                          NULL);

        flt_buffer_destroy(&buf);
}

static bool
add_map(struct flt_renderer *renderer,
        cairo_t *cr,
        const struct flt_scene_gpx_map *map,
        double lat, double lon,
        double video_timestamp,
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
        float map_size = renderer->scene->video_height * 0.3;
        float map_scale = map_size / map_size_tile_units;

        double map_x, map_y;

        get_position(renderer,
                     map->base.position,
                     map_size, map_size,
                     &map_x, &map_y);

        cairo_save(cr);
        cairo_translate(cr,
                        map_x + map_size / 2.0,
                        map_y + map_size / 2.0);
        cairo_scale(cr, map_scale, map_scale);

        struct flt_map_renderer_params params = FLT_MAP_RENDERER_DEFAULT_PARAMS;

        params.lat = lat;
        params.lon = lon;
        params.map_width = round(map_size_tile_units);
        params.map_height = params.map_width;
        params.trace = map->trace ? map->trace->trace : NULL;
        params.trace_color = map->trace_color;
        params.video_timestamp = video_timestamp;

        if (!flt_map_renderer_render(renderer->map_renderer,
                                     cr,
                                     &params,
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
                        double video_timestamp,
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

        const struct flt_scene_gpx_object *object;

        flt_list_for_each(object, &gpx->objects, link) {
                switch (object->type) {
                case FLT_SCENE_GPX_OBJECT_TYPE_SPEED:
                        if (!add_speed(renderer,
                                       cr,
                                       (const struct flt_scene_gpx_speed *)
                                       object,
                                       gpx_data.speed,
                                       error))
                                return false;
                        break;
                case FLT_SCENE_GPX_OBJECT_TYPE_ELEVATION:
                        add_elevation(renderer,
                                      cr,
                                      (const struct flt_scene_gpx_elevation *)
                                      object,
                                      gpx_data.elevation);
                        break;
                case FLT_SCENE_GPX_OBJECT_TYPE_DISTANCE:
                        add_distance(renderer,
                                     cr,
                                     (const struct flt_scene_gpx_distance *)
                                     object,
                                     gpx_data.distance);
                        break;
                case FLT_SCENE_GPX_OBJECT_TYPE_MAP:
                        if (!add_map(renderer,
                                     cr,
                                     (const struct flt_scene_gpx_map *)
                                     object,
                                     gpx_data.lat, gpx_data.lon,
                                     video_timestamp,
                                     error))
                                return false;
                        break;
                }
        }

        return true;
}

static void
interpolate_and_add_time(struct flt_renderer *renderer,
                         cairo_t *cr,
                         const struct flt_scene_time *time,
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

        render_text_parts(renderer,
                          cr,
                          time->position,
                          time->color,
                          &renderer->digits_font,
                          (const char *) buf.data,
                          NULL);

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
        flt_source_color_set(cr, curve->color, 1.0);
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

static void
add_text(struct flt_renderer *renderer,
         cairo_t *cr,
         const struct flt_scene_text *text)
{
        render_text_parts(renderer,
                          cr,
                          text->position,
                          text->color,
                          &renderer->score_font,
                          text->text,
                          NULL);
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
                if (!interpolate_and_add_svg(renderer,
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
                                             timestamp,
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
                                         (const struct flt_scene_time *)
                                         object,
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
        case FLT_SCENE_OBJECT_TYPE_TEXT:
                add_text(renderer,
                         cr,
                         (const struct flt_scene_text *) object);
                break;
        }

        return FLT_RENDERER_RESULT_OK;
}

struct flt_renderer *
flt_renderer_new(struct flt_scene *scene)
{
        struct flt_renderer *renderer = flt_calloc(sizeof *renderer);

        renderer->scene = scene;

        renderer->gap = scene->video_height / 15.0f;

        renderer->digits_font.face =
                cairo_toy_font_face_create("monospace",
                                           CAIRO_FONT_SLANT_NORMAL,
                                           CAIRO_FONT_WEIGHT_NORMAL);
        renderer->digits_font.size = scene->video_height / 12.0f;

        renderer->units_font.face =
                cairo_toy_font_face_create("",
                                           CAIRO_FONT_SLANT_NORMAL,
                                           CAIRO_FONT_WEIGHT_NORMAL);
        renderer->units_font.size = scene->video_height / 24.0f;

        renderer->label_font.face =
                cairo_font_face_reference(renderer->units_font.face);
        renderer->label_font.size = scene->video_height / 30.0f;

        renderer->score_font.face =
                cairo_font_face_reference(renderer->units_font.face);
        renderer->score_font.size = scene->video_height / 10.0f;

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

        /* Reset all of the position offsets to 0.0 */
        memset(renderer->position_offsets,
               0,
               sizeof renderer->position_offsets);

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

static void
destroy_font_with_size(struct font_with_size *font)
{
        if (font->face) {
                cairo_font_face_destroy(font->face);
                font->face = NULL;
        }
}

void
flt_renderer_free(struct flt_renderer *renderer)
{
        destroy_font_with_size(&renderer->digits_font);
        destroy_font_with_size(&renderer->units_font);
        destroy_font_with_size(&renderer->label_font);
        destroy_font_with_size(&renderer->score_font);

        if (renderer->map_point_pattern)
                cairo_pattern_destroy(renderer->map_point_pattern);
        if (renderer->map_renderer)
                flt_map_renderer_free(renderer->map_renderer);

        flt_free(renderer);
}
