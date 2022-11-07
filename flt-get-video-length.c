/*
 * Flootay – a video overlay generator
 * Copyright (C) 2013, 2021  Neil Roberts
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

#include "flt-get-video-length.h"

#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>

#include "flt-child-proc.h"
#include "flt-util.h"

bool
flt_get_video_length(const char *filename,
                     double *length_out)
{
        const char * const argv[] = {
                "-i",
                filename,
                "-show_entries", "format=duration",
                "-v", "quiet",
                "-of", "csv=p=0",
                NULL,
        };

        char *output = flt_child_proc_get_output(NULL, /* source_dir */
                                                 "ffprobe",
                                                 argv);

        if (output == NULL)
                return false;

        char *tail;

        errno = 0;
        *length_out = strtod(output, &tail);
        char end = *tail;

        flt_free(output);

        if (errno ||
            end != '\n' ||
            !isnormal(*length_out) ||
            *length_out < 0.0) {
                fprintf(stderr, "invalid length returned for %s\n", filename);
                return false;
        }

        return true;
}
