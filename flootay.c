#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define IMAGE_WIDTH 1000
#define IMAGE_HEIGHT 550

#define N_ELEMENTS(x) (sizeof (x) / sizeof ((x)[0]))

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
        { N_ELEMENTS(gcum_rectangle), gcum_rectangle },
        { N_ELEMENTS(portiere_rectangle), portiere_rectangle },
};

static int
get_n_frames(void)
{
        int max_frame = 0;

        for (int i = 0; i < N_ELEMENTS(rectangles); i++) {
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
fill_rectangle(uint8_t *buf,
               int x1, int y1,
               int x2, int y2)
{
        for (int y = y1; y < y2; y++) {
                uint8_t *p = buf + y * IMAGE_WIDTH * 4 + x1 * 4;

                for (int x = x1; x < x2; x++) {
                        memset(p, 0, 3);
                        p[3] = 255;
                        p += 4;
                }
        }
}

static void
interpolate_and_add_rectangle(uint8_t *buf,
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

        fill_rectangle(buf, x1, y1, x2, y2);
}

int
main(int argc, char **argv)
{
        size_t buf_size = IMAGE_WIDTH * IMAGE_HEIGHT * 4;
        uint8_t *buf = malloc(buf_size);

        int n_frames = get_n_frames();

        for (int frame_num = 0; frame_num < n_frames; frame_num++) {
                memset(buf, 0, buf_size);

                for (int i = 0; i < N_ELEMENTS(rectangles); i++) {
                        interpolate_and_add_rectangle(buf,
                                                      frame_num,
                                                      rectangles + i);
                }

                fwrite(buf, 1, buf_size, stdout);
        }

        free(buf);

        return EXIT_SUCCESS;
}
