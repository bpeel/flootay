/*
 * Flootay – a video overlay generator
 * Copyright (C) 2024  Neil Roberts
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

#include "flt-source-color.h"

void
flt_source_color_set(cairo_t *cr, uint32_t color, double alpha)
{
        cairo_set_source_rgba(cr,
                             ((color >> 16) & 0xff) / 255.0,
                             ((color >> 8) & 0xff) / 255.0,
                             (color & 0xff) / 255.0,
                              alpha);
}
