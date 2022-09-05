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

#ifndef FLOOTAY_H
#define FLOOTAY_H

#include <stdbool.h>
#include <stdio.h>
#include <cairo.h>

struct flootay;

struct flootay *
flootay_new(void);

const char *
flootay_get_error(struct flootay *flootay);

/* base_dir is the base directory to load additional resources from
 * that are referenced by the script. It can be NULL to use the
 * current directory.
 */
bool
flootay_load_script(struct flootay *flootay,
                    const char *base_dir,
                    FILE *file);

bool
flootay_render(struct flootay *flootay,
               cairo_t *cr,
               double timestamp);

void
flootay_free(struct flootay *flootay);

#endif /* FLOOTAY_H */
