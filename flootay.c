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

static int
interpolate(float factor, int s, int e)
{
        return roundf(s + factor * (e - s));
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
interpolate_and_add_rectangle(const struct flt_scene *scene,
                              cairo_t *cr,
                              float i,
                              const struct flt_scene_rectangle_key_frame *s,
                              const struct flt_scene_rectangle_key_frame *e)
{
        int x1 = clamp(interpolate(i, s->x1, e->x1), 0, scene->video_width);
        int y1 = clamp(interpolate(i, s->y1, e->y1), 0, scene->video_height);
        int x2 = clamp(interpolate(i, s->x2, e->x2), x1, scene->video_width);
        int y2 = clamp(interpolate(i, s->y2, e->y2), y1, scene->video_height);

        fill_rectangle(cr, x1, y1, x2, y2);
}

static void
interpolate_and_add_svg(const struct flt_scene *scene,
                        const struct flt_scene_svg *svg,
                        cairo_t *cr,
                        float i,
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

static void
interpolate_and_add_object(const struct flt_scene *scene,
                           cairo_t *cr,
                           int frame_num,
                           const struct flt_scene_object *object)
{
        const struct flt_scene_key_frame *end_frame;

        flt_list_for_each(end_frame, &object->key_frames, link) {
                if (end_frame->num > frame_num)
                        goto found_frame;
        }

        return;

found_frame:

        /* Ignore if the end frame is the first frame */
        if (object->key_frames.next == &end_frame->link)
                return;

        const struct flt_scene_key_frame *s =
                flt_container_of(end_frame->link.prev,
                                 struct flt_scene_key_frame,
                                 link);
        float i = (frame_num - s->num) / (float) (end_frame->num - s->num);

        switch (object->type) {
        case FLT_SCENE_OBJECT_TYPE_RECTANGLE:
                interpolate_and_add_rectangle(scene,
                                              cr,
                                              i,
                                              (const struct
                                               flt_scene_rectangle_key_frame *)
                                              s,
                                              (const struct
                                               flt_scene_rectangle_key_frame *)
                                              end_frame);
                break;
        case FLT_SCENE_OBJECT_TYPE_SVG:
                interpolate_and_add_svg(scene,
                                        (const struct flt_scene_svg *) object,
                                        cr,
                                        i,
                                        (const struct
                                         flt_scene_svg_key_frame *)
                                        s,
                                        (const struct
                                         flt_scene_svg_key_frame *)
                                        end_frame);
                break;
        }
}

static void
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

                fwrite(data, 1, width * 4, stdout);
        }
}

static bool
read_stdin_cb(struct flt_source *source,
              void *ptr,
              size_t *length,
              struct flt_error **error)
{
        size_t got = fread(ptr, 1, *length, stdin);

        if (got < *length) {
                if (ferror(stdin)) {
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

static struct flt_scene *
load_stdin(void)
{
        struct flt_source source = {
                .read_source = read_stdin_cb,
        };

        struct flt_error *error = NULL;

        struct flt_scene *scene = flt_parser_parse(&source, ".", &error);

        if (scene == NULL) {
                fprintf(stderr, "%s\n", error->message);
                flt_error_free(error);
        }

        return scene;
}

int
main(int argc, char **argv)
{
        struct flt_scene *scene = load_stdin();

        if (scene == NULL)
                return EXIT_FAILURE;

        cairo_surface_t *surface =
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                           scene->video_width,
                                           scene->video_height);
        cairo_t *cr = cairo_create(surface);

        int n_frames = flt_scene_get_n_frames(scene);

        for (int frame_num = 0; frame_num < n_frames; frame_num++) {
                cairo_save(cr);
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(cr);
                cairo_restore(cr);

                const struct flt_scene_object *object;

                flt_list_for_each(object, &scene->objects, link) {
                        interpolate_and_add_object(scene,
                                                   cr,
                                                   frame_num,
                                                   object);
                }

                cairo_surface_flush(surface);

                write_surface(surface);
        }

        cairo_surface_destroy(surface);
        cairo_destroy(cr);

        flt_scene_free(scene);

        return EXIT_SUCCESS;
}
