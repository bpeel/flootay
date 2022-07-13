#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define IMAGE_WIDTH 1900
#define IMAGE_HEIGHT 1046

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
plate_rectangle[] = {
        { 269, 881, 379, 925, 404, },
        { 277, 810, 410, 870, 443, },
        { 292, 744, 455, 809, 483, },
        { 307, 759, 464, 834, 488, },
        { 322, 842, 453, 918, 482, },
        { 329, 824, 444, 896, 474, },
        { 337, 816, 446, 899, 464, },
        { 352, 776, 446, 864, 474, },
        { 367, 700, 467, 809, 562, },
        { 382, 650, 500, 762, 528, },
        { 389, 617, 494, 768, 554, },
        { 397, 590, 512, 717, 564, },
        { 412, 472, 522, 624, 561, },
        { 427, 434, 530, 597, 569, },
        { 442, 342, 558, 534, 614, },
        { 449, 273, 581, 546, 682, },
        { 457, 234, 627, 462, 693, },
        { 472, 144, 702, 432, 770, },
        { 487, 120, 743, 482, 825, },
        { 502, 468, 698, 845, 815, },
        { 509, 306, 782, 735, 935, },
        { 517, 164, 863, 582, 977, },
        { 532, 162, 848, 543, 1046, },
        { 547, 135, 878, 516, 1046, },
        { 562, 212, 989, 759, 1046, },
        { 569, 254, 1034, 692, 1046, },
};

static const struct key_frame
face_rectangle[] = {
        { 652, 701, 455, 887, 605, },
        { 667, 621, 444, 836, 615, },
        { 682, 455, 429, 732, 603, },
        { 689, 524, 365, 802, 642, },
        { 749, 3, 529, 630, 1045, },
        { 809, 0, 398, 596, 1045, },
        { 869, 0, 398, 0, 1045, },
};

static const struct rectangle
rectangles[] = {
        { N_ELEMENTS(plate_rectangle), plate_rectangle },
        { N_ELEMENTS(face_rectangle), face_rectangle },
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
