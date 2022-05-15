#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <SDL_image.h>
#include <SDL.h>

struct data {
        bool should_quit;

        bool sdl_inited;
        SDL_Window *window;
        SDL_Renderer *renderer;

        int n_images;
        const char * const *image_names;

        int current_image_num;
        SDL_Surface *current_image;
        SDL_Texture *current_texture;

        int fb_width, fb_height;
        int tex_width, tex_height;
        SDL_Rect tex_draw_rect;
        bool layout_dirty;

        bool has_box;
        SDL_Rect box;

        bool redraw_queued;
};

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
                                        1080, 788, /* width/height */
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

        data->current_image = IMG_Load(data->image_names[image_num]);

        if (data->current_image == NULL) {
                fprintf(stderr,
                        "%s: %s\n",
                        data->image_names[image_num],
                        IMG_GetError());
                free_image(data);
                return;
        }

        data->current_texture =
                SDL_CreateTextureFromSurface(data->renderer,
                                             data->current_image);

        if (data->current_texture == NULL) {
                fprintf(stderr,
                        "Error creating texture: %s: %s\n",
                        data->image_names[image_num],
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
              data->tex_draw_rect.w);
        *y = ((*y - data->tex_draw_rect.y) *
              data->tex_height /
              data->tex_draw_rect.h);
}

static void
copy_box_to_clipboard(struct data *data)
{
        int x1 = data->box.x;
        int y1 = data->box.y;
        int x2 = data->box.x + data->box.w;
        int y2 = data->box.y + data->box.h;

        map_coords(data, &x1, &y1);
        map_coords(data, &x2, &y2);

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

        char buf[800];

        snprintf(buf, sizeof buf,
                 "%i, %i, %i, %i",
                 x1, y1,
                 x2, y2);

        buf[sizeof buf - 1] = '\0';

        printf("%s\n", buf);

        SDL_SetClipboardText(buf);
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
        }
}

static void
handle_mouse_button(struct data *data,
                    const SDL_MouseButtonEvent *event)
{
        if (event->button != 1)
                return;

        if (event->state == SDL_PRESSED) {
                if (data->has_box || data->current_texture == NULL)
                        return;

                data->has_box = true;
                data->box.x = event->x;
                data->box.y = event->y;
                data->box.w = 0;
                data->box.h = 0;
        } else {
                if (!data->has_box)
                        return;

                if (data->current_texture)
                        copy_box_to_clipboard(data);

                data->has_box = false;
                data->redraw_queued = true;
        }
}

static void
handle_mouse_motion(struct data *data,
                    const SDL_MouseMotionEvent *event)
{
        if (!data->has_box)
                return;

        data->box.w = event->x - data->box.x;
        data->box.h = event->y - data->box.y;

        data->redraw_queued = true;
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
paint_box(struct data *data)
{
        SDL_SetRenderDrawColor(data->renderer, 0, 0, 128, 128);
        SDL_SetRenderDrawBlendMode(data->renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRects(data->renderer, &data->box, 1);
        SDL_SetRenderDrawBlendMode(data->renderer, SDL_BLENDMODE_NONE);
}

static void
paint(struct data *data)
{
        data->redraw_queued = false;

        SDL_SetRenderDrawColor(data->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(data->renderer);

        if (data->current_texture)
                paint_texture(data);

        if (data->has_box)
                paint_box(data);

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

int
main(int argc, char **argv)
{
        struct data data = {
                .n_images = argc - 1,
                .image_names = (const char *const *) argv + 1,
                .current_image_num = -1,
                .redraw_queued = true,
                .layout_dirty = true,
        };

        int ret = EXIT_SUCCESS;

        if (data.n_images <= 0) {
                fprintf(stderr, "Usage: make-key-frames <image>…\n");
                ret = EXIT_FAILURE;
                goto out;
        }

        if (!init_sdl(&data)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        set_image(&data, 0);

        run_main_loop(&data);

out:
        free_image(&data);
        destroy_sdl(&data);

        return ret;
}
