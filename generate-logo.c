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
#include <librsvg/rsvg.h>
#include <errno.h>
#include <expat.h>

#include "flt-util.h"

#define VIDEO_WIDTH 1920
#define VIDEO_HEIGHT 1080
#define FPS 30

/* Total time of the animation in seconds */
#define TOTAL_TIME 3

#define N_FRAMES (TOTAL_TIME * FPS)

#define LOGO_FILENAME "biclou-lyon-logo.svg"

#define BICLOU_DISPLACEMENT 718
#define BICLOU_TIME 2

#define WHEEL_X 490
#define WHEEL_Y 307
#define WHEEL_RADIUS (561 - WHEEL_X)
#define WHEEL_DIAMETER (2.0f * M_PI * WHEEL_RADIUS)

#define Y_START 158
#define Y_END 1025
#define Y_TIME 1

#define LON_START_TIME 1
#define LON_TIME 1

enum {
        LABEL_BACKGROUND,
        LABEL_BICLU,
        LABEL_WHEEL,
        LABEL_LON,
        LABEL_Y,
};

static const char * const
labels[] = {
        [LABEL_BACKGROUND] = "background",
        [LABEL_BICLU] = "biclu",
        [LABEL_WHEEL] = "wheel",
        [LABEL_LON] = "lon",
        [LABEL_Y] = "y",
};

#define N_LABELS FLT_N_ELEMENTS(labels)

struct painter {
        char *ids[N_LABELS];
        RsvgHandle *svg;
        cairo_surface_t *lon_surface;
};

static void
start_element_cb(void *user_data,
                 const XML_Char *name,
                 const XML_Char **atts)
{
        char **ids = user_data;
        const char *id = NULL, *label = NULL;

        if (strcmp(name, "g"))
                return;

        for (const XML_Char **att = atts; *att; att += 2) {
                if (!strcmp(*att, "inkscape:label"))
                        label = att[1];
                else if (!strcmp(*att, "id"))
                        id = att[1];
        }

        if (id == NULL || label == NULL)
                return;

        for (int i = 0; i < N_LABELS; i++) {
                if (strcmp(labels[i], label))
                        continue;

                if (ids[i] == NULL)
                        ids[i] = flt_strconcat("#", id, NULL);

                break;
        }
}

static void
end_element_cb(void *user_data,
               const XML_Char *name)
{
}

static void
free_ids(char *ids[N_LABELS])
{
        for (int i = 0; i < N_LABELS; i++)
                flt_free(ids[i]);
}

static bool
read_labels(const char *filename,
            char *ids[N_LABELS])
{
        memset(ids, 0, sizeof (char *) * N_LABELS);

        FILE *f = fopen(filename, "r");

        if (f == NULL) {
                fprintf(stderr, "%s: %s\n", filename, strerror(errno));
                return false;
        }

        XML_Parser parser = XML_ParserCreate(NULL);

        XML_SetUserData(parser, ids);
        XML_SetElementHandler(parser, start_element_cb, end_element_cb);

        bool ret = true;

        while (true) {
                char buf[128];

                size_t got = fread(buf, 1, sizeof buf, f);

                if (XML_Parse(parser, buf, got, got < sizeof buf) ==
                    XML_STATUS_ERROR) {
                        fprintf(stderr,
                                "%s:%u: %s\n",
                                filename,
                                (int) XML_GetCurrentLineNumber(parser),
                                XML_ErrorString(XML_GetErrorCode(parser)));
                        ret = false;
                        break;
                }

                if (got < sizeof buf)
                        break;
        }

        if (ret) {
                for (int i = 0; i < N_LABELS; i++) {
                        if (ids[i] == NULL) {
                                fprintf(stderr,
                                        "%s: missing label “%s”\n",
                                        filename,
                                        labels[i]);
                                ret = false;
                                break;
                        }
                }
        }

        if (!ret)
                free_ids(ids);

        fclose(f);
        XML_ParserFree(parser);

        return ret;
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
                fwrite(row, sizeof (uint32_t), width, stdout);
        }
}

static void
render_biclou(cairo_t *cr, int frame_num, struct painter *painter)
{
        int total_frames = BICLOU_TIME * FPS;

        if (frame_num > total_frames)
                frame_num = total_frames;

        float t = 1.0f - frame_num / (float) total_frames;

        /* cubic easing */
        float distance = BICLOU_DISPLACEMENT * t * t * t;

        cairo_save(cr);
        cairo_translate(cr, -distance, 0.0f);
        rsvg_handle_render_cairo_sub(painter->svg,
                                     cr,
                                     painter->ids[LABEL_BICLU]);
        cairo_restore(cr);

        float angle = -distance * 2.0f * M_PI / WHEEL_DIAMETER;

        cairo_save(cr);
        cairo_translate(cr, WHEEL_X - distance, WHEEL_Y);
        cairo_rotate(cr, angle);
        cairo_translate(cr, -WHEEL_X, -WHEEL_Y);
        rsvg_handle_render_cairo_sub(painter->svg,
                                     cr,
                                     painter->ids[LABEL_WHEEL]);
        cairo_restore(cr);
}

static void
render_y(cairo_t *cr, int frame_num, struct painter *painter)
{
        int total_frames = Y_TIME * FPS;

        if (frame_num > total_frames)
                frame_num = total_frames;

        cairo_save(cr);
        cairo_rectangle(cr,
                        0,
                        Y_START,
                        VIDEO_WIDTH,
                        frame_num * (Y_END - Y_START) / total_frames);
        cairo_clip(cr);
        rsvg_handle_render_cairo_sub(painter->svg,
                                     cr,
                                     painter->ids[LABEL_Y]);
        cairo_restore(cr);
}

static void
render_lon(cairo_t *cr, int frame_num, struct painter *painter)
{
        frame_num -= LON_START_TIME * FPS;

        if (frame_num < 0)
                return;

        float alpha;

        if (frame_num >= LON_TIME * FPS) {
                alpha = 1.0f;
        } else {
                float t = 1.0f - frame_num / ((float) LON_TIME * FPS);
                /* quadratic easing */
                alpha = 1.0f - t * t;
        }

        cairo_save(cr);
        cairo_set_source_surface(cr, painter->lon_surface, 0.0, 0.0);
        cairo_paint_with_alpha(cr, alpha);
        cairo_restore(cr);
}

static void
paint_frame(cairo_t *cr, int frame_num, struct painter *painter)
{
        rsvg_handle_render_cairo_sub(painter->svg,
                                     cr,
                                     painter->ids[LABEL_BACKGROUND]);

        render_biclou(cr, frame_num, painter);

        render_y(cr, frame_num, painter);

        render_lon(cr, frame_num, painter);
}

static void
render_animation(struct painter *painter)
{
        cairo_surface_t *surface =
                cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                           VIDEO_WIDTH,
                                           VIDEO_HEIGHT);
        cairo_t *cr = cairo_create(surface);

        for (int i = 0; i < N_FRAMES; i++) {
                paint_frame(cr, i, painter);
                cairo_surface_flush(surface);
                write_surface(surface);
        }

        cairo_destroy(cr);
        cairo_surface_destroy(surface);
}

static void
create_lon_surface(struct painter *painter)
{
        painter->lon_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                          VIDEO_WIDTH,
                                                          VIDEO_HEIGHT);
        cairo_t *cr = cairo_create(painter->lon_surface);

        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_restore(cr);

        rsvg_handle_render_cairo_sub(painter->svg,
                                     cr,
                                     painter->ids[LABEL_LON]);

        cairo_destroy(cr);
}

static char *
get_source_dir(const char *exe)
{
        const char *end = strrchr(exe, '/');

        if (end == NULL)
                return flt_strdup(".");

        static const char build_end[] = "/build";

        if (end - exe >= sizeof build_end &&
            !memcmp(build_end,
                    end - (sizeof build_end) + 1,
                    (sizeof build_end) - 1))
                end -= (sizeof build_end) - 1;

        return flt_strndup(exe, end - exe);
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        struct painter painter;

        char *source_dir = get_source_dir(argv[0]);
        char *logo_filename =
                flt_strconcat(source_dir, "/" LOGO_FILENAME, NULL);

        if (!read_labels(logo_filename, painter.ids)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        GError *error = NULL;

        painter.svg = rsvg_handle_new_from_file(logo_filename, &error);

        if (painter.svg == NULL) {
                fprintf(stderr,
                        "%s\n",
                        error->message);
                g_error_free(error);
                ret = EXIT_FAILURE;
        } else {
                create_lon_surface(&painter);
                render_animation(&painter);
                cairo_surface_destroy(painter.lon_surface);
                g_object_unref(painter.svg);
        }

        free_ids(painter.ids);

out:
        flt_free(source_dir);
        flt_free(logo_filename);

        return ret;
}
