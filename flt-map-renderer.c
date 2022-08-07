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

#include "flt-map-renderer.h"

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "flt-util.h"
#include "flt-buffer.h"
#include "flt-list.h"
#include "flt-file-error.h"

#define N_CACHED_TILES 8

#define TILE_SIZE 256

#define TILE_CACHE_DIRECTORY "map-tiles"

#define DEFAULT_MAP_URL_BASE "http://a.tile.thunderforest.com/cycle/"

struct flt_map_renderer {
        struct flt_list tile_cache;
        int n_cached_tiles;

        char *url_base;
};

struct cached_tile {
        struct flt_list link;

        int zoom, x, y;

        cairo_surface_t *surface;
};

struct flt_error_domain
flt_map_renderer_error;

struct flt_map_renderer *
flt_map_renderer_new(const char *url_base)
{
        struct flt_map_renderer *renderer = flt_calloc(sizeof *renderer);

        flt_list_init(&renderer->tile_cache);

        if (url_base == NULL)
                url_base = DEFAULT_MAP_URL_BASE;

        size_t url_base_length = strlen(url_base);

        while (url_base_length > 0 && url_base[url_base_length - 1] == '/')
                url_base_length--;

        renderer->url_base = flt_strndup(url_base, url_base_length);

        return renderer;
}

static void
delete_cached_tile(struct cached_tile *tile)
{
        cairo_surface_destroy(tile->surface);
        flt_list_remove(&tile->link);
        flt_free(tile);
}

static struct cached_tile *
get_cached_tile(struct flt_map_renderer *renderer,
                int zoom,
                int x, int y)
{
        struct cached_tile *tile;

        flt_list_for_each(tile, &renderer->tile_cache, link) {
                if (tile->zoom == zoom &&
                    tile->x == x &&
                    tile->y == y) {
                        /* Move the tile to the front of the list to
                         * mark it as recently used.
                         */
                        flt_list_remove(&tile->link);
                        flt_list_insert(renderer->tile_cache.prev,
                                        &tile->link);
                        return tile;
                }
        }

        return NULL;
}

static char *
get_tile_filename(int zoom, int x, int y)
{
        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf,
                                 "%s/%i-%i-%i.png",
                                 TILE_CACHE_DIRECTORY,
                                 zoom,
                                 x, y);

        return (char *) buf.data;
}

static struct cached_tile *
add_tile(struct flt_map_renderer *renderer,
         int zoom,
         int x, int y,
         cairo_surface_t *surface)
{
        if (renderer->n_cached_tiles >= N_CACHED_TILES) {
                struct cached_tile *last_tile =
                        flt_container_of(renderer->tile_cache.prev,
                                         struct cached_tile,
                                         link);

                delete_cached_tile(last_tile);

                renderer->n_cached_tiles--;
        }

        struct cached_tile *tile = flt_alloc(sizeof *tile);

        tile->zoom = zoom;
        tile->x = x;
        tile->y = y;
        tile->surface = surface;

        flt_list_insert(renderer->tile_cache.prev, &tile->link);

        renderer->n_cached_tiles++;

        return tile;
}

static struct cached_tile *
load_tile(struct flt_map_renderer *renderer,
          int zoom,
          int x, int y,
          struct flt_error **error)
{
        char *filename = get_tile_filename(zoom, x, y);

        cairo_surface_t *surface =
                cairo_image_surface_create_from_png(filename);

        cairo_status_t status = cairo_surface_status(surface);

        struct cached_tile *tile = NULL;

        switch (status) {
        case CAIRO_STATUS_SUCCESS:
                tile = add_tile(renderer, zoom, x, y, surface);
                break;
        case CAIRO_STATUS_FILE_NOT_FOUND:
                flt_set_error(error,
                              &flt_file_error,
                              FLT_FILE_ERROR_NOENT,
                              "no such file: %s",
                              filename);
                cairo_surface_destroy(surface);
                break;
        default:
                flt_set_error(error,
                              &flt_map_renderer_error,
                              FLT_MAP_RENDERER_ERROR_LOAD_FAILED,
                              "error loading %s: %s",
                              filename,
                              cairo_status_to_string(status));
                cairo_surface_destroy(surface);
                break;
        }

        flt_free(filename);

        return tile;
}

static bool
ensure_tile_cache_directory(struct flt_error **error)
{
        if (mkdir(TILE_CACHE_DIRECTORY, 0777) == -1 &&
            errno != EEXIST) {
                flt_file_error_set(error,
                                   errno,
                                   "%s: %s",
                                   TILE_CACHE_DIRECTORY,
                                   strerror(errno));
                return false;
        }

        return true;
}

static bool
download_tile(struct flt_map_renderer *renderer,
              int zoom,
              int x, int y,
              struct flt_error **error)
{
        if (!ensure_tile_cache_directory(error))
                return false;

        char *filename = get_tile_filename(zoom, x, y);
        struct flt_buffer url_buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&url_buf,
                                 "%s/%i/%i/%i.png",
                                 renderer->url_base,
                                 zoom,
                                 x, y);

        char *args[] = {
                "curl",
                "--fail",
                "--location",
                "--silent",
                "--output", filename,
                (char *) url_buf.data,
                NULL
        };

        bool ret = true;

        pid_t pid = fork();

        if (pid == -1) {
                flt_file_error_set(error,
                                   errno,
                                   "fork failed: %s",
                                   strerror(errno));
                ret = false;
        } else if (pid == 0) {
                execvp(args[0], args);

                fprintf(stderr,
                        "exec failed: %s: %s\n",
                        args[0],
                        strerror(errno));

                exit(EXIT_FAILURE);

                return false;
        } else {
                int status;

                if (waitpid(pid, &status, 0 /* options */) == -1) {
                        flt_file_error_set(error,
                                           errno,
                                           "waitpid failed: %s",
                                           strerror(errno));
                        ret = false;
                } else if (!WIFEXITED(status)) {
                        flt_file_error_set(error,
                                           errno,
                                           "%s exited abnormally",
                                           args[0]);
                        ret = false;
                } else if (WEXITSTATUS(status) != EXIT_SUCCESS) {
                        flt_set_error(error,
                                      &flt_map_renderer_error,
                                      FLT_MAP_RENDERER_ERROR_FETCH_FAILED,
                                      "%s exited with code %i",
                                      args[0],
                                      WEXITSTATUS(status));
                        ret = false;
                }
        }

        flt_buffer_destroy(&url_buf);
        flt_free(filename);

        return ret;
}

static struct cached_tile *
get_tile(struct flt_map_renderer *renderer,
         int zoom,
         int x, int y,
         struct flt_error **error)
{
        struct cached_tile *tile = get_cached_tile(renderer, zoom, x, y);

        if (tile == NULL) {
                struct flt_error *tmp_error = NULL;

                tile = load_tile(renderer, zoom, x, y, &tmp_error);

                if (tile == NULL) {
                        if (tmp_error->domain != &flt_file_error ||
                            tmp_error->code != FLT_FILE_ERROR_NOENT) {
                                flt_error_propagate(error, tmp_error);
                                return NULL;
                        }

                        flt_error_free(tmp_error);

                        if (!download_tile(renderer, zoom, x, y, error))
                                return NULL;

                        tile = load_tile(renderer, zoom, x, y, error);

                        if (tile == NULL)
                                return NULL;
                }
        }

        return tile;
}

static void
lon_to_x(double lon, int zoom,
         int *tile_x_out,
         int *pixel_x_out)
{
        double x = (lon + 180.0) / 360.0 * (1 << zoom);

        double tile_x;
        double frac_x = modf(x, &tile_x);

        *tile_x_out = tile_x;
        *pixel_x_out = round(frac_x * TILE_SIZE);
}

static void
lat_to_y(double lat, int zoom,
         int *tile_y_out,
         int *pixel_y_out)
{
    double lat_rad = lat * M_PI / 180.0;
    double y = (1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * (1 << zoom);

    double tile_y;
    double frac_y = modf(y, &tile_y);

    *tile_y_out = tile_y;
    *pixel_y_out = round(frac_y * TILE_SIZE);
}

static void
render_tile(cairo_t *cr,
            struct cached_tile *tile,
            double x, double y)
{
        cairo_save(cr);

        cairo_matrix_t matrix;

        cairo_matrix_init_translate(&matrix, -x, -y);

        cairo_pattern_t *p = cairo_pattern_create_for_surface(tile->surface);
        cairo_pattern_set_extend(p, CAIRO_EXTEND_PAD);
        cairo_pattern_set_matrix(p, &matrix);

        cairo_set_source(cr, p);
        cairo_rectangle(cr, x - 0.5, y - 0.5, TILE_SIZE + 1.0, TILE_SIZE + 1.0);
        cairo_fill(cr);

        cairo_pattern_destroy(p);

        cairo_restore(cr);
}

bool
flt_map_renderer_render(struct flt_map_renderer *renderer,
                        cairo_t *cr,
                        int zoom,
                        double lat, double lon,
                        double draw_center_x, double draw_center_y,
                        int map_width, int map_height,
                        struct flt_error **error)
{
        bool ret = true;

        cairo_save(cr);
        cairo_rectangle(cr,
                        draw_center_x - map_width / 2.0,
                        draw_center_y - map_height / 2.0,
                        map_width,
                        map_height);
        cairo_clip(cr);

        int tile_x, tile_y, pixel_x, pixel_y;

        lon_to_x(lon, zoom, &tile_x, &pixel_x);
        lat_to_y(lat, zoom, &tile_y, &pixel_y);

        int x_tile_start = -((map_width / 2 - pixel_x + TILE_SIZE - 1) /
                             TILE_SIZE);
        int y_tile_start = -((map_height / 2 - pixel_y + TILE_SIZE - 1) /
                             TILE_SIZE);

        for (int y = y_tile_start;
             y * TILE_SIZE - pixel_y < map_height;
             y++) {
                for (int x = x_tile_start;
                     x * TILE_SIZE - pixel_x < map_width;
                     x++) {
                        struct cached_tile *tile = get_tile(renderer,
                                                            zoom,
                                                            x + tile_x,
                                                            y + tile_y,
                                                            error);

                        if (tile == NULL) {
                                ret = false;
                                goto out;
                        }

                        render_tile(cr,
                                    tile,
                                    draw_center_x -
                                    pixel_x +
                                    x * TILE_SIZE,
                                    draw_center_y -
                                    pixel_y +
                                    y * TILE_SIZE);
                }
        }

out:
        cairo_restore(cr);

        return ret;
}

static void
free_tile_cache(struct flt_map_renderer *renderer)
{
        struct cached_tile *t, *tmp;

        flt_list_for_each_safe(t, tmp, &renderer->tile_cache, link) {
                delete_cached_tile(t);
        }
}

void
flt_map_renderer_free(struct flt_map_renderer *renderer)
{
        flt_free(renderer->url_base);
        free_tile_cache(renderer);
        flt_free(renderer);
}
