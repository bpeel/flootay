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
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>

#include "flt-util.h"
#include "flt-buffer.h"
#include "flt-list.h"
#include "flt-file-error.h"
#include "flt-source-color.h"

#define N_CACHED_TILES 8

#define TILE_SIZE 256

#define TILE_CACHE_DIRECTORY "map-tiles"

#define DEFAULT_MAP_URL_BASE "https://tile.thunderforest.com/cycle/"

#define TRACE_LINE_WIDTH (TILE_SIZE / 16.0)
#define TRACE_ALPHA 0.75
#define TRACE_SECONDARY_COLOR 1.0, 1.0, 1.0, TRACE_ALPHA
#define TRACE_DASH_SIZE (TRACE_LINE_WIDTH * 2.0)
#define CROSS_DISTANCE (TRACE_LINE_WIDTH * 4)

struct flt_map_renderer {
        struct flt_list tile_cache;
        int n_cached_tiles;
        bool clip;

        char *url_base;
        char *api_key;

        CURL *curl;
        FILE *tile_output;
};

struct cached_tile {
        struct flt_list link;

        int zoom, x, y;

        cairo_surface_t *surface;
};

struct flt_error_domain
flt_map_renderer_error;

static size_t
write_tile_data_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
        struct flt_map_renderer *renderer = userdata;

        return fwrite(ptr, size, nmemb, renderer->tile_output);
}

struct flt_map_renderer *
flt_map_renderer_new(const char *url_base,
                     const char *api_key)
{
        struct flt_map_renderer *renderer = flt_calloc(sizeof *renderer);

        flt_list_init(&renderer->tile_cache);

        renderer->clip = true;

        if (url_base == NULL)
                url_base = DEFAULT_MAP_URL_BASE;

        size_t url_base_length = strlen(url_base);

        while (url_base_length > 0 && url_base[url_base_length - 1] == '/')
                url_base_length--;

        renderer->url_base = flt_strndup(url_base, url_base_length);

        if (api_key)
                renderer->api_key = flt_strdup(api_key);

        renderer->curl = curl_easy_init();
        curl_easy_setopt(renderer->curl, CURLOPT_FOLLOWLOCATION, 1l);
        curl_easy_setopt(renderer->curl, CURLOPT_FAILONERROR, 1l);
        curl_easy_setopt(renderer->curl,
                         CURLOPT_WRITEFUNCTION,
                         write_tile_data_cb);
        curl_easy_setopt(renderer->curl,
                         CURLOPT_WRITEDATA,
                         renderer);

        return renderer;
}

void
flt_map_renderer_set_clip(struct flt_map_renderer *renderer,
                          bool clip)
{
        renderer->clip = clip;
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

        bool ret = true;

        renderer->tile_output = fopen(filename, "wb");

        if (renderer->tile_output == NULL) {
                flt_file_error_set(error,
                                   errno,
                                   "%s: %s",
                                   filename,
                                   strerror(errno));
                ret = false;
                goto out;
        }

        struct flt_buffer url_buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&url_buf,
                                 "%s/%i/%i/%i.png",
                                 renderer->url_base,
                                 zoom,
                                 x, y);

        if (renderer->api_key) {
                flt_buffer_append_printf(&url_buf,
                                         "?apikey=%s",
                                         renderer->api_key);
        }

        curl_easy_setopt(renderer->curl, CURLOPT_URL, (char *) url_buf.data);

        flt_buffer_destroy(&url_buf);

        CURLcode res = curl_easy_perform(renderer->curl);

        if (res != CURLE_OK) {
                flt_set_error(error,
                              &flt_map_renderer_error,
                              FLT_MAP_RENDERER_ERROR_FETCH_FAILED,
                              "Error downloading tile: %s",
                              curl_easy_strerror(res));
                ret = false;
        }

out:
        if (renderer->tile_output) {
                fclose(renderer->tile_output);
                renderer->tile_output = NULL;
        }

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

static void
set_clip(cairo_t *cr,
         double draw_center_x, double draw_center_y,
         int map_width, int map_height)
{
        double corner_size = MIN(map_width, map_height) / 6.0;

        cairo_move_to(cr,
                      draw_center_x - map_width / 2.0,
                      draw_center_y - map_height / 2.0 + corner_size);
        cairo_arc(cr,
                  draw_center_x - map_width / 2.0 + corner_size,
                  draw_center_y - map_height / 2.0 + corner_size,
                  corner_size,
                  M_PI,
                  1.5 * M_PI);
        cairo_line_to(cr,
                      draw_center_x + map_width / 2.0 - corner_size,
                      draw_center_y - map_height / 2.0);
        cairo_arc(cr,
                  draw_center_x + map_width / 2.0 - corner_size,
                  draw_center_y - map_height / 2.0 + corner_size,
                  corner_size,
                  1.5 * M_PI,
                  2.0 * M_PI);
        cairo_line_to(cr,
                      draw_center_x + map_width / 2.0,
                      draw_center_y + map_height / 2.0 - corner_size);
        cairo_arc(cr,
                  draw_center_x + map_width / 2.0 - corner_size,
                  draw_center_y + map_height / 2.0 - corner_size,
                  corner_size,
                  0.0,
                  0.5 * M_PI);
        cairo_line_to(cr,
                      draw_center_x - map_width / 2.0 + corner_size,
                      draw_center_y + map_height / 2.0);
        cairo_arc(cr,
                  draw_center_x - map_width / 2.0 + corner_size,
                  draw_center_y + map_height / 2.0 - corner_size,
                  corner_size,
                  0.5 * M_PI,
                  M_PI);
        cairo_close_path(cr);
        cairo_clip(cr);
}

static void
add_segment_path(cairo_t *cr,
                 const struct flt_map_renderer_params *params,
                 int center_pixel_x, int center_pixel_y,
                 const struct flt_trace_segment *segment)
{
        for (size_t i = 0; i < segment->n_points; i++) {
                const struct flt_trace_point *point =
                        segment->points + i;
                int tile_x, tile_y, pixel_x, pixel_y;

                lon_to_x(point->lon, params->zoom, &tile_x, &pixel_x);
                lat_to_y(point->lat, params->zoom, &tile_y, &pixel_y);

                double x = params->draw_center_x +
                        tile_x * TILE_SIZE +
                        pixel_x -
                        center_pixel_x;
                double y = params->draw_center_y +
                        tile_y * TILE_SIZE +
                        pixel_y -
                        center_pixel_y;

                if (i == 0)
                        cairo_move_to(cr, x, y);
                else
                        cairo_line_to(cr, x, y);
        }
}

static void
stroke_dash(cairo_t *cr,
            const struct flt_map_renderer_params *params,
            double offset)
{
        const double dash_size = TRACE_DASH_SIZE;

        flt_source_color_set(cr, params->trace_color, TRACE_ALPHA);
        cairo_set_dash(cr, &dash_size, 1, offset);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgba(cr, TRACE_SECONDARY_COLOR);
        cairo_set_dash(cr, &dash_size, 1, dash_size + offset);
        cairo_stroke(cr);
}

static void
add_crosses(cairo_t *cr,
            double x1, double y1,
            double x2, double y2,
            double line_distance,
            double start_distance)
{
        for (double pos = fmodf(start_distance, CROSS_DISTANCE);
             pos < line_distance;
             pos += CROSS_DISTANCE) {
                double d = pos / line_distance;
                double cx = (x2 - x1) * d + x1;
                double cy = (y2 - y1) * d + y1;
                cairo_move_to(cr,
                              cx - TRACE_LINE_WIDTH / 2.0,
                              cy - TRACE_LINE_WIDTH / 2.0);
                cairo_rel_line_to(cr,
                                  TRACE_LINE_WIDTH,
                                  TRACE_LINE_WIDTH);
                cairo_rel_move_to(cr, 0.0, -TRACE_LINE_WIDTH);
                cairo_rel_line_to(cr, -TRACE_LINE_WIDTH, TRACE_LINE_WIDTH);
        }
}

static void
draw_crossed_segment(cairo_t *cr,
                     const struct flt_map_renderer_params *params)
{
        cairo_path_t *path = cairo_copy_path(cr);

        cairo_new_path(cr);

        double distance = 0.0;
        double last_x = 0.0, last_y = 0.0, next_x, next_y, dx, dy, d;

        for (int i = 0; i < path->num_data; i += path->data[i].header.length) {
                const cairo_path_data_t *data = path->data + i;

                switch (data->header.type) {
                case CAIRO_PATH_MOVE_TO:
                        distance = 0.0;
                        last_x = data[1].point.x;
                        last_y = data[1].point.y;
                        break;

                case CAIRO_PATH_LINE_TO:
                        next_x = data[1].point.x;
                        next_y = data[1].point.y;
                        dx = next_x - last_x;
                        dy = next_y - last_y;
                        d = sqrtf((dx * dx) + (dy * dy));
                        add_crosses(cr,
                                    last_x, last_y,
                                    next_x, next_y,
                                    d,
                                    distance);
                        distance += d;
                        last_x = next_x;
                        last_y = next_y;
                        break;

                default:
                        break;
                }
        }

        cairo_path_destroy(path);

        cairo_set_line_width(cr, TRACE_LINE_WIDTH / 8.0);
        flt_source_color_set(cr, params->trace_color, TRACE_ALPHA);
        cairo_stroke(cr);
        cairo_set_line_width(cr, TRACE_LINE_WIDTH);
}

static void
draw_trace(cairo_t *cr,
           const struct flt_map_renderer_params *params,
           int center_pixel_x, int center_pixel_y)
{
        cairo_save(cr);

        cairo_set_line_width(cr, TRACE_LINE_WIDTH);

        for (size_t segment_num = 0;
             segment_num < params->trace->n_segments;
             segment_num++) {
                const struct flt_trace_segment *segment =
                        params->trace->segments + segment_num;

                add_segment_path(cr,
                                 params,
                                 center_pixel_x, center_pixel_y,
                                 segment);

                switch (segment->status) {
                case FLT_TRACE_SEGMENT_STATUS_DONE:
                        flt_source_color_set(cr,
                                             params->trace_color,
                                             TRACE_ALPHA);
                        cairo_set_dash(cr, NULL, 0, 0.0);
                        cairo_stroke(cr);
                        break;

                case FLT_TRACE_SEGMENT_STATUS_TESTED:
                case FLT_TRACE_SEGMENT_STATUS_WIP: {
                        double ipart;
                        stroke_dash(cr,
                                    params,
                                    modf(params->video_timestamp, &ipart) *
                                    TRACE_DASH_SIZE * 4.0);
                        break;
                }

                case FLT_TRACE_SEGMENT_STATUS_POSTPONED:
                case FLT_TRACE_SEGMENT_STATUS_VARIANT_POSTPONED:
                        cairo_set_source_rgba(cr, TRACE_SECONDARY_COLOR);
                        cairo_set_dash(cr, NULL, 0, 0.0);
                        cairo_stroke_preserve(cr);
                        draw_crossed_segment(cr, params);
                        break;

                case FLT_TRACE_SEGMENT_STATUS_PLANNED:
                case FLT_TRACE_SEGMENT_STATUS_UNKNOWN:
                case FLT_TRACE_SEGMENT_STATUS_VARIANT:
                        stroke_dash(cr, params, 0.0);
                        break;
                }
        }

        cairo_stroke(cr);

        cairo_restore(cr);
}

bool
flt_map_renderer_render(struct flt_map_renderer *renderer,
                        cairo_t *cr,
                        const struct flt_map_renderer_params *params,
                        struct flt_error **error)
{
        bool ret = true;

        cairo_save(cr);

        if (renderer->clip) {
                set_clip(cr,
                         params->draw_center_x,
                         params->draw_center_y,
                         params->map_width,
                         params->map_height);
        }

        int tile_x, tile_y, pixel_x, pixel_y;

        lon_to_x(params->lon, params->zoom, &tile_x, &pixel_x);
        lat_to_y(params->lat, params->zoom, &tile_y, &pixel_y);

        int x_tile_start = -((params->map_width / 2 - pixel_x + TILE_SIZE - 1) /
                             TILE_SIZE);
        int y_tile_start = -((params->map_height / 2 -
                              pixel_y +
                              TILE_SIZE -
                              1) /
                             TILE_SIZE);

        for (int y = y_tile_start;
             y * TILE_SIZE - pixel_y < params->map_height;
             y++) {
                for (int x = x_tile_start;
                     x * TILE_SIZE - pixel_x < params->map_width;
                     x++) {
                        struct cached_tile *tile = get_tile(renderer,
                                                            params->zoom,
                                                            x + tile_x,
                                                            y + tile_y,
                                                            error);

                        if (tile == NULL) {
                                ret = false;
                                goto out;
                        }

                        render_tile(cr,
                                    tile,
                                    params->draw_center_x -
                                    pixel_x +
                                    x * TILE_SIZE,
                                    params->draw_center_y -
                                    pixel_y +
                                    y * TILE_SIZE);
                }
        }

        if (params->trace) {
                draw_trace(cr,
                           params,
                           tile_x * TILE_SIZE + pixel_x,
                           tile_y * TILE_SIZE + pixel_y);
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
        curl_easy_cleanup(renderer->curl);
        flt_free(renderer->url_base);
        flt_free(renderer->api_key);
        free_tile_cache(renderer);
        flt_free(renderer);
}
