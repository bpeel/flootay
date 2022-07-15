#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <cairo.h>

#include "flt-util.h"

#define IMAGE_WIDTH 1000
#define IMAGE_HEIGHT 550

struct key_frame {
        int num;
        int x1, y1, x2, y2;
};

struct rectangle {
        int n_key_frames;
        const struct key_frame *key_frames;
};

static const struct key_frame
gcum_rectangle[] = {
        { 536, 636, 141, 662, 154 },
        { 543, 610, 155, 637, 163 },
        { 551, 586, 168, 615, 178 },
        { 558, 588, 183, 618, 192 },
        { 566, 607, 192, 637, 202 },
        { 573, 586, 207, 612, 218 },
        { 581, 527, 225, 562, 237 },
        { 588, 503, 229, 545, 243 },
        { 596, 508, 225, 548, 241 },
        { 603, 509, 227, 545, 240 },
        { 611, 495, 237, 546, 254 },
        { 618, 503, 239, 553, 259 },
        { 626, 549, 231, 600, 251 },
        { 633, 574, 219, 635, 239 },
        { 641, 594, 214, 661, 243 },
        { 648, 625, 221, 703, 250 },
        { 656, 656, 235, 745, 263 },
        { 663, 656, 234, 784, 272 },
        { 671, 708, 235, 869, 289 },
        { 678, 850, 240, 999, 312 },
};

static const struct key_frame
portiere_rectangle[] = {
        { 851, 0, 302, 27, 337 },
        { 858, 0, 292, 51, 333 },
        { 866, 12, 275, 87, 320 },
        { 873, 45, 257, 107, 294 },
        { 881, 63, 262, 129, 300 },
        { 888, 91, 267, 156, 309 },
        { 896, 119, 270, 178, 304 },
        { 903, 137, 265, 193, 295 },
        { 911, 152, 258, 210, 285 },
        { 918, 183, 243, 244, 275 },
        { 926, 220, 225, 290, 255 },
        { 933, 245, 225, 308, 250 },
        { 941, 279, 222, 343, 250 },
        { 948, 312, 224, 372, 246 },
        { 956, 335, 225, 401, 254 },
        { 963, 349, 232, 402, 258 },
        { 971, 342, 232, 407, 260 },
        { 978, 341, 233, 394, 261 },
        { 986, 330, 232, 380, 265 },
        { 993, 342, 236, 408, 264 },
        { 1001, 375, 234, 439, 262 },
        { 1008, 423, 228, 492, 256 },
        { 1016, 550, 208, 614, 232 },
        { 1023, 624, 202, 694, 225 },
        { 1031, 624, 219, 693, 246 },
        { 1038, 584, 239, 657, 266 },
        { 1046, 558, 259, 646, 294 },
        { 1053, 555, 267, 647, 306 },
        { 1061, 564, 274, 665, 313 },
        { 1068, 600, 275, 723, 331 },
        { 1076, 665, 300, 804, 348 },
        { 1083, 686, 292, 875, 362 },
        { 1091, 703, 306, 970, 397 },
        { 1098, 799, 321, 999, 439 },
        { 1106, 854, 318, 999, 450 },
};

static const struct rectangle
rectangles[] = {
        { FLT_N_ELEMENTS(gcum_rectangle), gcum_rectangle },
        { FLT_N_ELEMENTS(portiere_rectangle), portiere_rectangle },
};

static int
get_n_frames(void)
{
        int max_frame = 0;

        for (int i = 0; i < FLT_N_ELEMENTS(rectangles); i++) {
                const struct rectangle *rect = rectangles + i;
                const struct key_frame *last_frame =
                        rect->key_frames + rect->n_key_frames - 1;

                if (last_frame->num > max_frame)
                        max_frame = last_frame->num;
        }

        return max_frame + 1;
}

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
interpolate_and_add_rectangle(cairo_t *cr,
                              int frame_num,
                              const struct rectangle *rect)
{
        int end_num = 0;

        while (true) {
                if (end_num >= rect->n_key_frames)
                        return;

                if (rect->key_frames[end_num].num > frame_num)
                        break;

                end_num++;
        }

        if (end_num == 0)
                return;

        const struct key_frame *s = rect->key_frames + end_num - 1;
        const struct key_frame *e = rect->key_frames + end_num;
        float i = (frame_num - s->num) / (float) (e->num - s->num);

        int x1 = clamp(interpolate(i, s->x1, e->x1), 0, IMAGE_WIDTH);
        int y1 = clamp(interpolate(i, s->y1, e->y1), 0, IMAGE_HEIGHT);
        int x2 = clamp(interpolate(i, s->x2, e->x2), x1, IMAGE_WIDTH);
        int y2 = clamp(interpolate(i, s->y2, e->y2), y1, IMAGE_HEIGHT);

        fill_rectangle(cr, x1, y1, x2, y2);
}

static void
write_surface(cairo_surface_t *surface)
{
        int stride = cairo_image_surface_get_stride(surface);
        uint8_t *data = cairo_image_surface_get_data(surface);

        for (int y = 0; y < IMAGE_HEIGHT; y++) {
                uint32_t *row = (uint32_t *) (data + y * stride);
                uint8_t *out_pix = data;

                for (int x = 0; x < IMAGE_WIDTH; x++) {
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

                fwrite(data, 1, IMAGE_WIDTH * 4, stdout);
        }
}

int
main(int argc, char **argv)
{
        cairo_surface_t *surface =
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                           IMAGE_WIDTH,
                                           IMAGE_HEIGHT);
        cairo_t *cr = cairo_create(surface);

        int n_frames = get_n_frames();

        for (int frame_num = 0; frame_num < n_frames; frame_num++) {
                cairo_save(cr);
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(cr);
                cairo_restore(cr);

                for (int i = 0; i < FLT_N_ELEMENTS(rectangles); i++) {
                        interpolate_and_add_rectangle(cr,
                                                      frame_num,
                                                      rectangles + i);
                }

                cairo_surface_flush(surface);

                write_surface(surface);
        }

        cairo_surface_destroy(surface);
        cairo_destroy(cr);

        return EXIT_SUCCESS;
}
