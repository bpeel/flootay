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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>
#include <SDL_image.h>
#include <SDL.h>
#include <librsvg/rsvg.h>
#include <assert.h>

#include "flt-buffer.h"
#include "flt-parse-stdio.h"

/* Tolerance for the timestamp when loading key frames from a scene */
#define LOAD_KEY_FRAME_TOLERANCE 0.0005

struct config {
        const char *video_filename;
        double start_time, end_time;
        int fps;
        int default_box_width, default_box_height;
        const char *script_to_load;
        const char *svg_to_load;
};

struct frame_data {
        bool has_box;
        SDL_Rect box;
};

struct data {
        bool should_quit;

        bool sdl_inited;
        SDL_Window *window;
        SDL_Renderer *renderer;

        int n_images;

        struct config config;

        int current_image_num;
        SDL_Surface *current_image;
        SDL_Texture *current_texture;

        SDL_Texture *svg_texture;

        int default_box_width, default_box_height;

        bool drawing_box;
        /* The aspect ratio of the box when drawing was started so
         * that if shift is held down we can retain the same
         * ratio.
         */
        int original_width, original_height;

        RsvgHandle *svg_handle;

        struct frame_data *frame_data;

        int fb_width, fb_height;
        int tex_width, tex_height;
        SDL_Rect tex_draw_rect;
        bool layout_dirty;

        bool redraw_queued;
};

#define IMAGES_DIR "key-frames-tmp"
#define VIDEO_WIDTH 1920
#define VIDEO_HEIGHT 1080
#define IMAGE_SCALE 2
#define DISPLAY_WIDTH (VIDEO_WIDTH / IMAGE_SCALE)
#define DISPLAY_HEIGHT (VIDEO_HEIGHT / IMAGE_SCALE)
/* Number of previous boxes to show */
#define N_PREVIOUS_BOXES 5
#define MIN_ALPHA 10
#define MAX_ALPHA 128

static bool
init_sdl(struct data *data)
{
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
                fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
                return false;
        }

        data->sdl_inited = true;

        data->window = SDL_CreateWindow("make-key-frames",
                                        SDL_WINDOWPOS_UNDEFINED,
                                        SDL_WINDOWPOS_UNDEFINED,
                                        DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                        SDL_WINDOW_RESIZABLE);

        if (data->window == NULL) {
                fprintf(stderr,
                        "Failed to create SDL window: %s\n",
                        SDL_GetError());
                return false;
        }

        data->renderer = SDL_CreateRenderer(data->window,
                                            -1, /* driver index */
                                            0 /* flags */);

        if (data->renderer == NULL) {
                fprintf(stderr,
                        "Failed to create SDL renderer: %s\n",
                        SDL_GetError());
                return false;
        }

        return true;
}

static void
destroy_sdl(struct data *data)
{
        if (data->renderer) {
                SDL_DestroyRenderer(data->renderer);
                data->renderer = NULL;
        }

        if (data->window) {
                SDL_DestroyWindow(data->window);
                data->window = NULL;
        }

        if (data->sdl_inited) {
                SDL_Quit();
                data->sdl_inited = false;
        }
}

static void
free_image(struct data *data)
{
        if (data->current_texture) {
                SDL_DestroyTexture(data->current_texture);
                data->current_texture = NULL;
        }

        if (data->current_image) {
                SDL_FreeSurface(data->current_image);
                data->current_image = NULL;
        }

        data->current_image_num = -1;
}

static SDL_Surface *
load_image(int image_num)
{
        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf,
                                 "%s/%03i.png",
                                 IMAGES_DIR,
                                 image_num + 1);

        SDL_Surface *surface = IMG_Load((const char *) buf.data);

        if (surface == NULL) {
                fprintf(stderr,
                        "%s: %s\n",
                        (const char *) buf.data,
                        IMG_GetError());
        }

        flt_buffer_destroy(&buf);

        return surface;
}

static void
set_image(struct data *data, int image_num)
{
        if (image_num == data->current_image_num)
                return;

        data->redraw_queued = true;
        data->layout_dirty = true;

        free_image(data);

        if (image_num == -1)
                return;

        data->current_image = load_image(image_num);

        if (data->current_image == NULL)
                return;

        data->current_texture =
                SDL_CreateTextureFromSurface(data->renderer,
                                             data->current_image);

        if (data->current_texture == NULL) {
                fprintf(stderr,
                        "Error creating texture: %s\n",
                        SDL_GetError());
                free_image(data);
                return;
        }

        data->current_image_num = image_num;
}

static void
ensure_layout(struct data *data)
{
        if (!data->layout_dirty)
                return;

        data->layout_dirty = false;

        SDL_GetWindowSize(data->window,
                          &data->fb_width,
                          &data->fb_height);

        if (data->current_texture == NULL) {
                memset(&data->tex_draw_rect, 0, sizeof data->tex_draw_rect);
                data->tex_width = 0;
                data->tex_height = 0;
                return;
        }

        SDL_QueryTexture(data->current_texture,
                         NULL, /* format */
                         NULL, /* access */
                         &data->tex_width, &data->tex_height);

        if (data->tex_width / (float) data->tex_height >
            data->fb_width / (float) data->fb_height) {
                /* Fit the width */
                data->tex_draw_rect.x = 0;
                data->tex_draw_rect.w = data->fb_width;

                data->tex_draw_rect.h =
                        data->fb_width * data->tex_height / data->tex_width;
                data->tex_draw_rect.y =
                        data->fb_height / 2 - data->tex_draw_rect.h / 2;
        } else {
                /* Fit the height */
                data->tex_draw_rect.y = 0;
                data->tex_draw_rect.h = data->fb_height;

                data->tex_draw_rect.w =
                        data->fb_height * data->tex_width / data->tex_height;
                data->tex_draw_rect.x =
                        data->fb_width / 2 - data->tex_draw_rect.w / 2;
        }
}

static void
map_coords(struct data *data,
           int *x, int *y)
{
        ensure_layout(data);

        *x = ((*x - data->tex_draw_rect.x) *
              data->tex_width /
              data->tex_draw_rect.w *
              IMAGE_SCALE);
        *y = ((*y - data->tex_draw_rect.y) *
              data->tex_height /
              data->tex_draw_rect.h *
              IMAGE_SCALE);
}

static void
unmap_coords(struct data *data,
             int *x, int *y)
{
        ensure_layout(data);

        *x = *x * data->tex_draw_rect.w / VIDEO_WIDTH + data->tex_draw_rect.x;
        *y = *y * data->tex_draw_rect.h / VIDEO_HEIGHT + data->tex_draw_rect.y;
}

static void
unmap_box(struct data *data, SDL_Rect *box)
{
        int x2 = box->x + box->w;
        int y2 = box->y + box->h;

        unmap_coords(data, &box->x, &box->y);
        unmap_coords(data, &x2, &y2);
        box->w = x2 - box->x;
        box->h = y2 - box->y;
}

struct size_change {
        int frame_num;
        int w, h;
};

static void
smooth_size_changes(struct data *data)
{
        struct size_change *size_changes =
                flt_alloc(sizeof *size_changes * data->n_images);
        int n_size_changes = 0;

        /* Get a list of size changes */
        for (int i = 0; i < data->n_images; i++) {
                if (data->frame_data[i].has_box &&
                    (n_size_changes == 0 ||
                     data->frame_data[i].box.w !=
                     size_changes[n_size_changes - 1].w ||
                     data->frame_data[i].box.h !=
                     size_changes[n_size_changes - 1].h)) {
                        size_changes[n_size_changes].frame_num = i;
                        size_changes[n_size_changes].w =
                                data->frame_data[i].box.w;
                        size_changes[n_size_changes].h =
                                data->frame_data[i].box.h;
                        n_size_changes++;
                }
        }

        if (n_size_changes < 2)
                goto out;

        int change_start = 0;

        for (int i = 0; i < data->n_images; i++) {
                if (i >= size_changes[change_start + 1].frame_num) {
                        change_start++;
                        if (change_start + 1 >= n_size_changes)
                                break;
                }

                struct frame_data *frame = data->frame_data + i;

                if (!frame->has_box)
                        continue;

                const struct size_change *s = size_changes + change_start;
                const struct size_change *e = s + 1;

                int w = ((i - s->frame_num) *
                         (e->w - s->w) /
                         (e->frame_num - s->frame_num) +
                         s->w);
                int h = ((i - s->frame_num) *
                         (e->h - s->h) /
                         (e->frame_num - s->frame_num) +
                         s->h);

                int cx = frame->box.x + frame->box.w / 2;
                int cy = frame->box.y + frame->box.h / 2;

                frame->box.x = cx - w / 2;
                frame->box.y = cy - h / 2;
                frame->box.w = w;
                frame->box.h = h;
        }

        data->redraw_queued = true;

out:
        flt_free(size_changes);
}

static double
get_frame_time(struct data *data, int frame_num)
{
        return (data->config.start_time +
                frame_num /
                (double) data->config.fps);
}

static void
get_key_frame_line(struct data *data, int frame_num, struct flt_buffer *buf)
{
        const SDL_Rect *box = &data->frame_data[frame_num].box;
        int x1 = box->x;
        int y1 = box->y;
        int x2 = box->x + box->w;
        int y2 = box->y + box->h;

        if (x1 > x2) {
                int t = x1;
                x1 = x2;
                x2 = t;
        }

        if (y1 > y2) {
                int t = y1;
                y1 = y2;
                y2 = t;
        }

        flt_buffer_append_printf(buf,
                                 "        key_frame %f { "
                                 "x1 %i "
                                 "y1 %i "
                                 "x2 %i "
                                 "y2 %i "
                                 "}",
                                 get_frame_time(data, frame_num),
                                 x1, y1, x2, y2);
}

static void
copy_box_to_clipboard(struct data *data)
{
        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        get_key_frame_line(data, data->current_image_num, &buf);

        printf("%s\n", (const char *) buf.data);

        SDL_SetClipboardText((const char *) buf.data);

        flt_buffer_destroy(&buf);
}

static void
write_key_frames(struct data *data)
{
        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_string(&buf,
                                 data->svg_handle ?
                                 "svg" :
                                 "rectangle");
        flt_buffer_append_string(&buf, " {\n");

        if (data->config.svg_to_load) {
                flt_buffer_append_string(&buf, "        file \"");
                flt_buffer_append_string(&buf, data->config.svg_to_load);
                flt_buffer_append_string(&buf, "\"\n");
        }

        for (int i = 0; i < data->n_images; i++) {
                const struct frame_data *frame = data->frame_data + i;

                if (!frame->has_box)
                        continue;

                get_key_frame_line(data, i, &buf);

                flt_buffer_append_c(&buf, '\n');
        }

        flt_buffer_append_string(&buf, "}\n");

        fputs((const char *) buf.data, stdout);
        SDL_SetClipboardText((const char *) buf.data);

        flt_buffer_destroy(&buf);
}

static void
delete_box(struct data *data)
{
        struct frame_data *frame_data =
                data->frame_data + data->current_image_num;

        if (!frame_data->has_box)
                return;

        frame_data->has_box = false;
        data->redraw_queued = true;
}

static void
ensure_box(struct data *data)
{
        struct frame_data *frame_data =
                data->frame_data + data->current_image_num;

        if (frame_data->has_box)
                return;

        /* Try to copy the box from a previous frame */
        for (int i = data->current_image_num - 1; i >= 0; i--) {
                const struct frame_data *other_frame = data->frame_data + i;

                if (!other_frame->has_box)
                        continue;

                frame_data->box = other_frame->box;
                frame_data->has_box = true;

                return;
        }

        /* Make up a box */
        frame_data->box.x = (VIDEO_WIDTH / 2 -
                             data->default_box_width / 2);
        frame_data->box.y = (VIDEO_HEIGHT / 2 -
                             data->default_box_height / 2);
        frame_data->box.w = data->default_box_width;
        frame_data->box.h = data->default_box_height;
        frame_data->has_box = true;
}

static void
move_box(struct data *data, int x, int y)
{
        ensure_box(data);

        struct frame_data *frame_data =
                data->frame_data + data->current_image_num;

        SDL_Keymod mods = SDL_GetModState();
        int offset;

        if ((mods & KMOD_SHIFT))
                offset = 100;
        else if ((mods & KMOD_ALT))
                offset = 1;
        else
                offset = 10;

        frame_data->box.x += offset * x;
        frame_data->box.y += offset * y;

        data->redraw_queued = true;
}

static void
handle_key_event(struct data *data,
                 const SDL_KeyboardEvent *event)
{
        switch (event->keysym.sym) {
        case SDLK_PAGEUP:
                if (data->current_image_num > 0)
                        set_image(data, data->current_image_num - 1);
                break;

        case SDLK_PAGEDOWN:
                if (data->current_image_num + 1 < data->n_images)
                        set_image(data, data->current_image_num + 1);
                break;

        case SDLK_d:
                delete_box(data);
                break;

        case SDLK_s:
                if ((SDL_GetModState() & KMOD_SHIFT))
                        smooth_size_changes(data);
                break;

        case SDLK_UP:
                move_box(data, 0, -1);
                break;
        case SDLK_DOWN:
                move_box(data, 0, 1);
                break;
        case SDLK_LEFT:
                move_box(data, -1, 0);
                break;
        case SDLK_RIGHT:
                move_box(data, 1, 0);
                break;

        case SDLK_w:
                write_key_frames(data);
                break;
        }
}

static void
handle_drag_button(struct data *data,
                   const SDL_MouseButtonEvent *event)
{
        if (event->state == SDL_PRESSED) {
                if (data->drawing_box || data->current_texture == NULL)
                        return;

                data->drawing_box = true;

                ensure_box(data);

                struct frame_data *frame_data =
                        data->frame_data + data->current_image_num;

                data->original_width = abs(frame_data->box.w);
                data->original_height = abs(frame_data->box.h);

                int x = event->x, y = event->y;

                map_coords(data, &x, &y);

                frame_data->has_box = true;
                frame_data->box.x = x;
                frame_data->box.y = y;
                frame_data->box.w = 0;
                frame_data->box.h = 0;
        } else {
                if (!data->drawing_box)
                        return;

                if (data->current_texture)
                        copy_box_to_clipboard(data);

                data->drawing_box = false;
                data->redraw_queued = true;
        }
}

static void
handle_center_button(struct data *data,
                     const SDL_MouseButtonEvent *event)
{
        if (event->state != SDL_PRESSED || data->current_texture == NULL)
                return;

        ensure_box(data);

        struct frame_data *frame = data->frame_data + data->current_image_num;

        int x = event->x, y = event->y;
        map_coords(data, &x, &y);

        frame->box.x = x - frame->box.w / 2;
        frame->box.y = y - frame->box.h / 2;

        data->redraw_queued = true;

        copy_box_to_clipboard(data);
}

static void
handle_mouse_button(struct data *data,
                    const SDL_MouseButtonEvent *event)
{
        switch (event->button) {
        case 1:
                handle_drag_button(data, event);
                break;
        case 3:
                handle_center_button(data, event);
                break;
        }
}

static void
handle_mouse_motion(struct data *data,
                    const SDL_MouseMotionEvent *event)
{
        if (!data->drawing_box)
                return;

        SDL_Rect *box = &data->frame_data[data->current_image_num].box;

        int x = event->x, y = event->y;

        map_coords(data, &x, &y);

        int w = x - box->x;
        int h = y - box->y;

        if ((SDL_GetModState() & KMOD_SHIFT) &&
            data->original_width > 0 &&
            data->original_height > 0) {
                if (data->original_width > data->original_height) {
                        box->w = w;
                        box->h = (abs(w) *
                                  data->original_height /
                                  data->original_width);
                        if (h < 0)
                                box->h = -box->h;
                } else {
                        box->h = h;
                        box->w = (abs(h) *
                                  data->original_width /
                                  data->original_height);
                        if (w < 0)
                                box->w = -box->w;
                }
        } else {
                box->w = w;
                box->h = h;
        }

        data->redraw_queued = true;
}

static void
handle_mouse_wheel(struct data *data,
                    const SDL_MouseWheelEvent *event)
{
        int image = data->current_image_num - event->y;

        if (image < 0)
                image = 0;
        else if (image >= data->n_images)
                image = data->n_images - 1;

        if (image == data->current_image_num)
                return;

        set_image(data, image);
}

static void
handle_event(struct data *data,
             const SDL_Event *event)
{
        switch (event->type) {
        case SDL_WINDOWEVENT:
                switch (event->window.event) {
                case SDL_WINDOWEVENT_CLOSE:
                        data->should_quit = true;
                        break;
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                        data->redraw_queued = true;
                        data->layout_dirty = true;
                        break;
                case SDL_WINDOWEVENT_EXPOSED:
                        data->redraw_queued = true;
                        break;
                }
                break;

        case SDL_KEYDOWN:
                handle_key_event(data, &event->key);
                break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
                handle_mouse_button(data, &event->button);
                break;

        case SDL_MOUSEMOTION:
                handle_mouse_motion(data, &event->motion);
                break;

        case SDL_MOUSEWHEEL:
                handle_mouse_wheel(data, &event->wheel);
                break;

        case SDL_QUIT:
                data->should_quit = true;
                break;
        }
}

static void
paint_texture(struct data *data)
{
        ensure_layout(data);

        SDL_RenderCopy(data->renderer,
                       data->current_texture,
                       NULL, /* src_rect */
                       &data->tex_draw_rect);
}

static void
paint_boxes(struct data *data)
{
        for (int i = MAX(0, data->current_image_num - N_PREVIOUS_BOXES);
             i <= data->current_image_num;
             i++) {
                const struct frame_data *frame_data = data->frame_data + i;

                if (!frame_data->has_box)
                        continue;

                int alpha = ((N_PREVIOUS_BOXES + i - data->current_image_num) *
                             (MAX_ALPHA - MIN_ALPHA) /
                             N_PREVIOUS_BOXES +
                             MIN_ALPHA);

                if (i == data->current_image_num) {
                        SDL_SetRenderDrawColor(data->renderer,
                                               128, 0, 0,
                                               alpha);
                } else {
                        SDL_SetRenderDrawColor(data->renderer,
                                               0, 0, 128,
                                               alpha);
                }

                SDL_SetRenderDrawBlendMode(data->renderer, SDL_BLENDMODE_BLEND);

                SDL_Rect box = frame_data->box;
                unmap_box(data, &box);

                SDL_RenderFillRects(data->renderer, &box, 1);

                SDL_SetRenderDrawBlendMode(data->renderer, SDL_BLENDMODE_NONE);
        }
}

static void
free_svg_texture(struct data *data)
{
        if (data->svg_texture) {
                SDL_DestroyTexture(data->svg_texture);
                data->svg_texture = NULL;
        }
}

static SDL_BlendMode
get_premultiplied_blend_mode(void)
{
        /* Get the blend mode to compensate for pre-multiplied alpha
         * in the cairo image.
         */
        return SDL_ComposeCustomBlendMode(
                /* srcColorFactor */
                SDL_BLENDFACTOR_ONE,
                /* dstColorFactor */
                SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                /* colorOperation */
                SDL_BLENDOPERATION_ADD,
                /* srcAlphaFactor */
                SDL_BLENDFACTOR_ONE,
                /* dstAlphaFactor */
                SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                /* alphaOperation */
                SDL_BLENDOPERATION_ADD);
}

static bool
get_cached_svg_texture(struct data *data,
                       int width,
                       int height)
{
        if (data->svg_handle == NULL)
                return false;

        if (data->svg_texture) {
                int tw, th;

                SDL_QueryTexture(data->svg_texture,
                                 NULL, /* format */
                                 NULL, /* access */
                                 &tw, &th);

                if (tw == width && th == height)
                        return true;
        }

        free_svg_texture(data);

        bool ret = true;

        cairo_surface_t *surface =
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                           width,
                                           height);
        cairo_t *cr = cairo_create(surface);

        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_restore(cr);

        RsvgRectangle viewport = {
                .x = 0, .y = 0,
                .width = width,
                .height = height,
        };

        if (rsvg_handle_render_document(data->svg_handle,
                                        cr,
                                        &viewport,
                                        NULL /* error */)) {
                data->svg_texture = SDL_CreateTexture(data->renderer,
                                                      SDL_PIXELFORMAT_BGRA32,
                                                      SDL_TEXTUREACCESS_STATIC,
                                                      width,
                                                      height);

                SDL_SetTextureBlendMode(data->svg_texture,
                                        get_premultiplied_blend_mode());

                if (data->svg_texture == NULL) {
                        ret = false;
                } else {
                        cairo_surface_flush(surface);

                        const void *pixels =
                                cairo_image_surface_get_data(surface);
                        int pitch =
                                cairo_image_surface_get_stride(surface);

                        SDL_UpdateTexture(data->svg_texture,
                                          NULL, /* rect */
                                          pixels,
                                          pitch);
                }
        } else {
                ret = false;
        }

        cairo_destroy(cr);
        cairo_surface_destroy(surface);

        return ret;
}

static void
paint_svg(struct data *data)
{
        const struct frame_data *frame_data =
                data->frame_data + data->current_image_num;

        if (!frame_data->has_box)
                return;

        if (!get_cached_svg_texture(data,
                                    abs(frame_data->box.w),
                                    abs(frame_data->box.h)))
                return;

        SDL_Rect box = frame_data->box;
        unmap_box(data, &box);

        if (box.w < 0) {
                box.x += box.w;
                box.w = -box.w;
        }
        if (box.h < 0) {
                box.y += box.h;
                box.h = -box.h;
        }

        SDL_RenderCopy(data->renderer,
                       data->svg_texture,
                       NULL, /* src_rect */
                       &box);
}

static void
paint(struct data *data)
{
        data->redraw_queued = false;

        SDL_SetRenderDrawColor(data->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(data->renderer);

        if (data->current_texture)
                paint_texture(data);

        paint_boxes(data);
        paint_svg(data);

        SDL_RenderPresent(data->renderer);
}

static void
run_main_loop(struct data *data)
{
        while (!data->should_quit) {
                SDL_Event event;

                if (data->redraw_queued) {
                        if (SDL_PollEvent(&event))
                                handle_event(data, &event);
                        else
                                paint(data);
                } else if (SDL_WaitEvent(&event)) {
                        handle_event(data, &event);
                }
        }
}

static bool
ensure_images_dir(void)
{
        if (mkdir(IMAGES_DIR, 0777) == -1 && errno != EEXIST) {
                fprintf(stderr,
                        "error creating %s: %s\n",
                        IMAGES_DIR,
                        strerror(errno));
                return false;
        }

        return true;
}

static bool
run_ffmpeg(const struct config *config)
{
        pid_t pid = fork();

        if (pid == -1) {
                fprintf(stderr, "fork failed: %s\n", strerror(errno));
                return false;
        }

        if (pid == 0) {
                struct flt_buffer start_time = FLT_BUFFER_STATIC_INIT;

                flt_buffer_append_printf(&start_time, "%f", config->start_time);

                struct flt_buffer end_time = FLT_BUFFER_STATIC_INIT;

                flt_buffer_append_printf(&end_time, "%f", config->end_time);

                struct flt_buffer filter = FLT_BUFFER_STATIC_INIT;

                flt_buffer_append_printf(&filter,
                                         "fps=%i,"
                                         "scale=%i:%i,"
                                         "drawtext=fontfile=Arial.ttf:"
                                         "text='%%{expr\\:t+%f}':"
                                         "fontsize=%i:"
                                         "bordercolor=white:"
                                         "borderw=%i:"
                                         "y=%i",
                                         config->fps,
                                         DISPLAY_WIDTH,
                                         DISPLAY_HEIGHT,
                                         config->start_time,
                                         DISPLAY_HEIGHT / 5,
                                         DISPLAY_HEIGHT / 180,
                                         DISPLAY_HEIGHT / 180);

                const char *args[] = {
                        "ffmpeg",
                        "-ss", (const char *) start_time.data,
                        "-to", (const char *) end_time.data,
                        "-i", config->video_filename,
                        "-vf", (const char *) filter.data,
                        IMAGES_DIR "/%03d.png",
                        NULL
                };

                execvp(args[0], (char **) args);

                fprintf(stderr,
                        "exec failed: %s: %s\n",
                        args[0],
                        strerror(errno));

                exit(EXIT_FAILURE);

                return false;
        }

        int status = EXIT_FAILURE;

        if (waitpid(pid, &status, 0 /* options */) == -1 ||
            !WIFEXITED(status) ||
            WEXITSTATUS(status) != EXIT_SUCCESS) {
                fprintf(stderr, "ffmpeg failed\n");
                return false;
        } else {
                return true;
        }
}

static bool
count_images(time_t min_time,
             int *n_images_out)
{
        DIR *d = opendir(IMAGES_DIR);

        if (d == NULL) {
                fprintf(stderr,
                        "%s: %s\n",
                        IMAGES_DIR,
                        strerror(errno));
                return false;
        }

        bool ret = true;
        int n_images = 0;
        struct dirent *e;

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        while ((e = readdir(d))) {
                if (e->d_name[0] == '.')
                        continue;

                flt_buffer_set_length(&buf, 0);
                flt_buffer_append_string(&buf, IMAGES_DIR);
                flt_buffer_append_c(&buf, '/');
                flt_buffer_append_string(&buf, e->d_name);

                struct stat statbuf;

                if (stat((const char *) buf.data, &statbuf) == -1) {
                        fprintf(stderr,
                                "%s: %s\n",
                                (const char *) buf.data,
                                strerror(errno));
                        ret = false;
                        break;
                }

                if (statbuf.st_mtime >= min_time)
                        n_images++;
        }

        flt_buffer_destroy(&buf);

        if (ret)
                *n_images_out = n_images;

        return ret;
}

static bool
generate_images(const struct config *config,
                int *n_images_out)
{
        if (!ensure_images_dir())
                return false;

        time_t ffmpeg_run_time = time(NULL);

        if (!run_ffmpeg(config))
                return false;

        if (!count_images(ffmpeg_run_time, n_images_out))
                return false;

        return true;
}

static void
set_svg_handle(struct data *data, RsvgHandle *handle)
{
        assert(data->svg_handle == NULL);

        data->svg_handle = g_object_ref(handle);

        gboolean has_width, has_height, has_viewbox;
        RsvgLength width, height;
        RsvgRectangle viewbox;

        rsvg_handle_get_intrinsic_dimensions(handle,
                                             &has_width,
                                             &width,
                                             &has_height,
                                             &height,
                                             &has_viewbox,
                                             &viewbox);

        if (has_width &&
            has_height &&
            width.unit != RSVG_UNIT_PERCENT &&
            height.unit != RSVG_UNIT_PERCENT) {
                /* We mostly only care about the proportions so it
                 * doesn’t really matter what the units are.
                 */
                data->default_box_width = width.length;
                data->default_box_height = height.length;
        } else if (has_viewbox) {
                data->default_box_width = viewbox.width;
                data->default_box_height = viewbox.height;
        }
}

static bool
load_svg(struct data *data, const char *filename)
{
        GError *error = NULL;

        RsvgHandle *handle = rsvg_handle_new_from_file(filename, &error);

        if (handle == NULL) {
                fprintf(stderr,
                        "%s: %s\n",
                        filename,
                        error->message);

                g_error_free(error);

                return false;
        }

        set_svg_handle(data, handle);

        g_object_unref(handle);

        return true;
}

static void
set_frame_data(struct data *data,
               double timestamp,
               int x1, int y1,
               int x2, int y2)
{
        int frame_num = round((timestamp - data->config.start_time) *
                              data->config.fps);

        if (frame_num < 0 ||
            frame_num >= data->n_images ||
            fabs(frame_num /
                 (float) data->config.fps +
                 data->config.start_time -
                 timestamp) > LOAD_KEY_FRAME_TOLERANCE)
                return;

        struct frame_data *frame_data =
                data->frame_data + frame_num;

        frame_data->has_box = true;
        frame_data->box.x = x1;
        frame_data->box.y = y1;
        frame_data->box.w = x2 - x1;
        frame_data->box.h = y2 - y1;
}

static void
load_frame_data_from_rectangle(struct data *data,
                               const struct flt_scene_rectangle *rect)
{
        const struct flt_scene_rectangle_key_frame *key_frame;

        flt_list_for_each(key_frame, &rect->base.key_frames, base.link) {
                set_frame_data(data,
                               key_frame->base.timestamp,
                               key_frame->x1,
                               key_frame->y1,
                               key_frame->x2,
                               key_frame->y2);
        }
}

static void
load_frame_data_from_svg(struct data *data,
                         const struct flt_scene_svg *svg)
{
        const struct flt_scene_svg_key_frame *key_frame;

        flt_list_for_each(key_frame, &svg->base.key_frames, base.link) {
                set_frame_data(data,
                               key_frame->base.timestamp,
                               key_frame->x1,
                               key_frame->y1,
                               key_frame->x2,
                               key_frame->y2);
        }
}

static void
load_data_from_scene(struct data *data,
                     const struct flt_scene *scene)
{
        struct flt_scene_object *object;

        flt_list_for_each(object, &scene->objects, link) {
                if (object->type == FLT_SCENE_OBJECT_TYPE_RECTANGLE) {
                        struct flt_scene_rectangle *rect =
                                flt_container_of(object,
                                                 struct flt_scene_rectangle,
                                                 base);
                        load_frame_data_from_rectangle(data, rect);
                        break;
                } else if (object->type == FLT_SCENE_OBJECT_TYPE_SVG) {
                        struct flt_scene_svg *svg =
                                flt_container_of(object,
                                                 struct flt_scene_svg,
                                                 base);

                        load_frame_data_from_svg(data, svg);

                        if (data->svg_handle == NULL)
                                set_svg_handle(data, svg->handle);

                        break;
                }
        }
}

static bool
load_script(struct data *data, const char *filename)
{
        struct flt_scene *scene = flt_scene_new();
        struct flt_error *error = NULL;
        bool ret = true;

        if (!flt_parse_stdio_from_file(scene,
                                       filename,
                                       &error)) {
                fprintf(stderr, "%s: %s\n", filename, error->message);
                flt_error_free(error);
                ret = false;
        } else {
                load_data_from_scene(data, scene);
        }

        flt_scene_free(scene);

        return ret;
}

static bool
parse_time(const char *time_str, double *value_out)
{
        char *tail;

        errno = 0;
        double value = strtod(time_str, &tail);

        if (errno || (!isnormal(value) && value != 0.0) || value < 0.0)
                goto error;

        if (*tail == ':') {
                double seconds = strtod(tail + 1, &tail);

                if (errno ||
                    (!isnormal(value) && value != 0.0) ||
                    value < 0.0 ||
                    *tail)
                        goto error;

                value = value * 60.0 + seconds;
        } else if (*tail != '\0') {
                goto error;
        }

        *value_out = value;

        return true;

error:
        fprintf(stderr, "invalid time value: %s\n", time_str);
        return false;
}

static bool
parse_args(int argc, char **argv, struct config *config)
{
        while (true) {
                switch (getopt(argc, argv, "-s:e:r:w:h:l:S:")) {
                case 's':
                        if (!parse_time(optarg, &config->start_time))
                                return false;
                        break;
                case 'e':
                        if (!parse_time(optarg, &config->end_time))
                                return false;
                        break;
                case 'r':
                        config->fps = strtoul(optarg, NULL, 10);

                        if (config->fps <= 0 || config->fps >= 1000) {
                                fprintf(stderr,
                                        "invalid FPS: %s\n",
                                        optarg);
                                return false;
                        }
                        break;

                case 'w':
                        config->default_box_width = strtoul(optarg, NULL, 10);
                        break;

                case 'h':
                        config->default_box_height = strtoul(optarg, NULL, 10);
                        break;

                case 'l':
                        config->script_to_load = optarg;
                        break;

                case 'S':
                        config->svg_to_load = optarg;
                        break;

                case 1:
                        config->video_filename = optarg;
                        break;

                case -1:
                        goto done;

                default:
                        return false;
                }
        }

done:
        if (config->start_time < 0.0 ||
            config->end_time < 0.0 ||
            config->video_filename == NULL) {
                fprintf(stderr,
                        "usage: make-key-frames -s <start_time> -e <end_time> "
                        "[-r <fps>] [-l <script>] <video>\n");
                return false;
        }

        return true;
}

int
main(int argc, char **argv)
{
        struct data data = {
                .current_image_num = -1,
                .redraw_queued = true,
                .layout_dirty = true,

                .config = {
                        .fps = 10,
                        .start_time = -1.0,
                        .end_time = -1.0,
                        .default_box_width = 200,
                        .default_box_height = 66,
                },
        };

        if (!parse_args(argc, argv, &data.config))
                return EXIT_FAILURE;

        data.default_box_width = data.config.default_box_width;
        data.default_box_height = data.config.default_box_height;

        if (!generate_images(&data.config, &data.n_images)) {
                return EXIT_FAILURE;
        }

        if (data.n_images <= 0) {
                fprintf(stderr, "no images were found\n");
                return EXIT_FAILURE;
        }

        data.frame_data = flt_calloc(data.n_images *
                                     sizeof (struct frame_data));

        int ret = EXIT_SUCCESS;

        if (data.config.svg_to_load &&
            !load_svg(&data, data.config.svg_to_load)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        if (data.config.script_to_load &&
            !load_script(&data, data.config.script_to_load)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        if (!init_sdl(&data)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        set_image(&data, 0);

        run_main_loop(&data);

        write_key_frames(&data);

out:
        if (data.svg_handle)
                g_object_unref(data.svg_handle);

        free_image(&data);
        free_svg_texture(&data);
        destroy_sdl(&data);

        flt_free(data.frame_data);

        return ret;
}
