/*
 * Flootay – a video overlay generator
 * Copyright (C) 2021, 2022  Neil Roberts
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

#ifndef FLT_PARSER
#define FLT_PARSER

#include <stdbool.h>

#include "flt-error.h"
#include "flt-scene.h"
#include "flt-source.h"

extern struct flt_error_domain
flt_parser_error;

enum flt_parser_error {
        FLT_PARSER_ERROR_INVALID,
};

bool
flt_parser_parse(struct flt_scene *scene,
                 struct flt_source *source,
                 const char *base_dir,
                 struct flt_error **error);

#endif /* FLT_PARSER */
