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

#include "flt-parse-stdio.h"

#include "flt-file-error.h"
#include "flt-parser.h"

struct stdio_source {
        struct flt_source base;
        FILE *infile;
};

static bool
read_stdio_cb(struct flt_source *source,
              void *ptr,
              size_t *length,
              struct flt_error **error)
{
        struct stdio_source *stdio_source = (struct stdio_source *) source;

        size_t got = fread(ptr, 1, *length, stdio_source->infile);

        if (got < *length) {
                if (ferror(stdio_source->infile)) {
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

bool
flt_parse_stdio(struct flt_scene *scene,
                const char *base_dir,
                FILE *file,
                struct flt_error **error)
{
        struct stdio_source source = {
                .base = { .read_source = read_stdio_cb },
                .infile = file,
        };

        return flt_parser_parse(scene, &source.base, base_dir, error);
}

bool
flt_parse_stdio_from_file(struct flt_scene *scene,
                          const char *filename,
                          struct flt_error **error)
{
        FILE *file = fopen(filename, "r");

        if (file == NULL) {
                flt_file_error_set(error,
                                   errno,
                                   "%s: %s",
                                   filename,
                                   strerror(errno));
                return false;
        }

        char *base_dir;

        const char *last_part = strrchr(filename, '/');

        if (last_part == NULL)
                base_dir = NULL;
        else
                base_dir = flt_strndup(filename, last_part - filename);

        bool ret = flt_parse_stdio(scene, base_dir, file, error);

        fclose(file);

        flt_free(base_dir);

        return ret;
}
