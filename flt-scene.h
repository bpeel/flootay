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

enum flt_scene_object_type {
        FLT_SCENE_OBJECT_TYPE_RECTANGLE,
        FLT_SCENE_OBJECT_TYPE_SVG,
        FLT_SCENE_OBJECT_TYPE_SCORE,
};

struct flt_scene_object {
        struct flt_list link;

        enum flt_scene_object_type type;

        struct flt_list key_frames;
};

struct flt_scene_key_frame {
        struct flt_list link;
        int num;
};

struct flt_scene_rectangle {
        struct flt_scene_object base;
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

        int x, y;
};

struct flt_scene_score {
        struct flt_scene_object base;
};

struct flt_scene_score_key_frame {
        struct flt_scene_key_frame base;
        int value;
};

struct flt_scene {
        int video_width, video_height;

        struct flt_list objects;
};

struct flt_scene *
flt_scene_new(void);

int
flt_scene_get_n_frames(const struct flt_scene *scene);

void
flt_scene_free(struct flt_scene *scene);

#endif /* FLT_SCENE */
