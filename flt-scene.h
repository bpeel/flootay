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

#ifndef FLT_SCENE
#define FLT_SCENE

#include <librsvg/rsvg.h>

#include "flt-list.h"
#include "flt-gpx.h"

enum flt_scene_vertical_position {
        FLT_SCENE_VERTICAL_POSITION_TOP = 0,
        FLT_SCENE_VERTICAL_POSITION_BOTTOM = 1,
};

enum flt_scene_horizontal_position {
        FLT_SCENE_HORIZONTAL_POSITION_LEFT = (0 << 1),
        FLT_SCENE_HORIZONTAL_POSITION_MIDDLE = (1 << 1),
        FLT_SCENE_HORIZONTAL_POSITION_RIGHT = (2 << 1),
};

enum flt_scene_position {
        FLT_SCENE_POSITION_TOP_LEFT =
        (FLT_SCENE_HORIZONTAL_POSITION_LEFT |
         FLT_SCENE_VERTICAL_POSITION_TOP),
        FLT_SCENE_POSITION_TOP_MIDDLE =
        (FLT_SCENE_HORIZONTAL_POSITION_MIDDLE |
         FLT_SCENE_VERTICAL_POSITION_TOP),
        FLT_SCENE_POSITION_TOP_RIGHT =
        (FLT_SCENE_HORIZONTAL_POSITION_RIGHT |
         FLT_SCENE_VERTICAL_POSITION_TOP),
        FLT_SCENE_POSITION_BOTTOM_LEFT =
        (FLT_SCENE_HORIZONTAL_POSITION_LEFT |
         FLT_SCENE_VERTICAL_POSITION_BOTTOM),
        FLT_SCENE_POSITION_BOTTOM_MIDDLE =
        (FLT_SCENE_HORIZONTAL_POSITION_MIDDLE |
         FLT_SCENE_VERTICAL_POSITION_BOTTOM),
        FLT_SCENE_POSITION_BOTTOM_RIGHT =
        (FLT_SCENE_HORIZONTAL_POSITION_RIGHT |
         FLT_SCENE_VERTICAL_POSITION_BOTTOM),
};

#define FLT_SCENE_GET_VERTICAL_POSITION(pos) \
        ((enum flt_scene_vertical_position) ((pos) & 1))
#define FLT_SCENE_GET_HORIZONTAL_POSITION(pos) \
        ((enum flt_scene_horizontal_position) ((pos) & ~1))

#define FLT_SCENE_N_POSITIONS (2 * 3)

enum flt_scene_object_type {
        FLT_SCENE_OBJECT_TYPE_RECTANGLE,
        FLT_SCENE_OBJECT_TYPE_SVG,
        FLT_SCENE_OBJECT_TYPE_SCORE,
        FLT_SCENE_OBJECT_TYPE_GPX,
        FLT_SCENE_OBJECT_TYPE_TIME,
        FLT_SCENE_OBJECT_TYPE_CURVE,
};

struct flt_scene_gpx_file {
        struct flt_list link;
        char *filename;
        size_t n_points;
        struct flt_gpx_point *points;
};

struct flt_scene_object {
        struct flt_list link;

        enum flt_scene_object_type type;

        struct flt_list key_frames;
};

struct flt_scene_key_frame {
        struct flt_list link;
        double timestamp;
};

struct flt_scene_rectangle {
        struct flt_scene_object base;
        uint32_t color;
};

struct flt_scene_rectangle_key_frame {
        struct flt_scene_key_frame base;

        int x1, y1, x2, y2;
};

struct flt_scene_svg {
        struct flt_scene_object base;

        RsvgHandle *handle;
};

struct flt_scene_svg_key_frame {
        struct flt_scene_key_frame base;

        int x1, y1, x2, y2;
};

struct flt_scene_score {
        struct flt_scene_object base;
};

struct flt_scene_score_key_frame {
        struct flt_scene_key_frame base;
        int value;
};

struct flt_scene_time {
        struct flt_scene_object base;
        enum flt_scene_position position;
};

struct flt_scene_time_key_frame {
        struct flt_scene_key_frame base;
        double value;
};

enum flt_scene_gpx_object_type {
        FLT_SCENE_GPX_OBJECT_TYPE_SPEED,
        FLT_SCENE_GPX_OBJECT_TYPE_ELEVATION,
        FLT_SCENE_GPX_OBJECT_TYPE_DISTANCE,
        FLT_SCENE_GPX_OBJECT_TYPE_MAP,
};

struct flt_scene_gpx_object {
        struct flt_list link;
        enum flt_scene_gpx_object_type type;
        enum flt_scene_position position;
};

struct flt_scene_gpx_distance {
        struct flt_scene_gpx_object base;
        double offset;
};

struct flt_scene_gpx {
        struct flt_scene_object base;
        const struct flt_scene_gpx_file *file;
        struct flt_list objects;
};

struct flt_scene_gpx_key_frame {
        struct flt_scene_key_frame base;
        double timestamp;
};

struct flt_scene_point {
        double x, y;
};

struct flt_scene_curve {
        struct flt_scene_object base;
        uint32_t color;
};

struct flt_scene_curve_key_frame {
        struct flt_scene_key_frame base;
        double t;
        struct flt_scene_point points[4];
        double stroke_width;
};

struct flt_scene {
        int video_width, video_height;

        struct flt_list objects;
        struct flt_list gpx_files;

        char *map_url_base;
        char *map_api_key;
};

struct flt_scene *
flt_scene_new(void);

double
flt_scene_get_max_timestamp(const struct flt_scene *scene);

void
flt_scene_free(struct flt_scene *scene);

#endif /* FLT_SCENE */
