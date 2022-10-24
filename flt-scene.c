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
        struct flt_scene_key_frame *key_frame, *tmp;

        flt_list_for_each_safe(key_frame, tmp, key_frames, link) {
                flt_free(key_frame);
        }
}

static void
destroy_svg(struct flt_scene_svg *svg)
{
        if (svg->handle)
                g_object_unref(svg->handle);
}

static void
destroy_gpx(struct flt_scene_gpx *gpx)
{
        struct flt_scene_gpx_object *object, *tmp;

        flt_list_for_each_safe(object, tmp, &gpx->objects, link) {
                flt_free(object);
        }
}

static void
destroy_object(struct flt_scene_object *object)
{
        destroy_key_frames(&object->key_frames);

        switch (object->type) {
        case FLT_SCENE_OBJECT_TYPE_RECTANGLE:
        case FLT_SCENE_OBJECT_TYPE_SCORE:
        case FLT_SCENE_OBJECT_TYPE_CURVE:
        case FLT_SCENE_OBJECT_TYPE_TIME:
                break;
        case FLT_SCENE_OBJECT_TYPE_GPX:
                destroy_gpx((struct flt_scene_gpx *) object);
                break;
        case FLT_SCENE_OBJECT_TYPE_SVG:
                destroy_svg((struct flt_scene_svg *) object);
                break;
        }

        flt_free(object);
}

static void
destroy_gpx_files(struct flt_scene *scene)
{
        struct flt_scene_gpx_file *file, *tmp;

        flt_list_for_each_safe(file, tmp, &scene->gpx_files, link) {
                flt_free(file->filename);
                flt_free(file->points);
                flt_free(file);
        }
}

static void
destroy_objects(struct flt_scene *scene)
{
        struct flt_scene_object *object, *tmp;

        flt_list_for_each_safe(object, tmp, &scene->objects, link) {
                destroy_object(object);
        }
}

struct flt_scene *
flt_scene_new(void)
{
        struct flt_scene *scene = flt_calloc(sizeof *scene);

        scene->video_width = 1920;
        scene->video_height = 1080;

        flt_list_init(&scene->objects);
        flt_list_init(&scene->gpx_files);

        return scene;
}

double
flt_scene_get_max_timestamp(const struct flt_scene *scene)
{
        double max_timestamp = 0.0;

        const struct flt_scene_object *rect;

        flt_list_for_each(rect, &scene->objects, link) {
                const struct flt_scene_key_frame *last_frame =
                        flt_container_of(rect->key_frames.prev,
                                         struct flt_scene_key_frame,
                                         link);

                if (last_frame->timestamp > max_timestamp)
                        max_timestamp = last_frame->timestamp;
        }

        return max_timestamp;
}

void
flt_scene_free(struct flt_scene *scene)
{
        destroy_gpx_files(scene);
        destroy_objects(scene);

        flt_free(scene->map_url_base);
        flt_free(scene->map_api_key);

        flt_free(scene);
}
