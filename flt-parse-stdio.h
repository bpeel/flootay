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

#ifndef FLT_PARSE_STDIO
#define FLT_PARSE_STDIO

#include <stdbool.h>
#include <stdio.h>

#include "flt-error.h"
#include "flt-scene.h"

bool
flt_parse_stdio(struct flt_scene *scene,
                const char *base_dir,
                FILE *file,
                struct flt_error **error);

bool
flt_parse_stdio_from_file(struct flt_scene *scene,
                          const char *filename,
                          struct flt_error **error);

#endif /* FLT_PARSE_STDIO */
