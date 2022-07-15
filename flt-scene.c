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

#include "flt-scene.h"

#include "flt-util.h"

static void
destroy_key_frames(struct flt_list *key_frames)
{
        struct flt_scene_rectangle_key_frame *key_frame, *tmp;

        flt_list_for_each_safe(key_frame, tmp, key_frames, link) {
                flt_free(key_frame);
        }
}

static void
destroy_rectangles(struct flt_scene *scene)
{
        struct flt_scene_rectangle *rectangle, *tmp;

        flt_list_for_each_safe(rectangle, tmp, &scene->rectangles, link) {
                destroy_key_frames(&rectangle->key_frames);

                flt_free(rectangle);
        }
}

struct flt_scene *
flt_scene_new(void)
{
        struct flt_scene *scene = flt_calloc(sizeof *scene);

        scene->video_width = 1920;
        scene->video_height = 1080;

        flt_list_init(&scene->rectangles);

        return scene;
}

int
flt_scene_get_n_frames(const struct flt_scene *scene)
{
        int max_frame = 0;

        const struct flt_scene_rectangle *rect;

        flt_list_for_each(rect, &scene->rectangles, link) {
                const struct flt_scene_rectangle_key_frame *last_frame =
                        flt_container_of(rect->key_frames.prev,
                                         struct flt_scene_rectangle_key_frame,
                                         link);

                if (last_frame->num > max_frame)
                        max_frame = last_frame->num;
        }

        return max_frame + 1;
}

void
flt_scene_free(struct flt_scene *scene)
{
        destroy_rectangles(scene);

        flt_free(scene);
}
