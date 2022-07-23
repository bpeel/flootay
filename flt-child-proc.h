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

#ifndef FLT_CHILD_PROC_H
#define FLT_CHILD_PROC_H

#include <sys/types.h>
#include <stdbool.h>

struct flt_child_proc {
        char *program_name;
        pid_t pid;
        int read_fd;
};

#define FLT_CHILD_PROC_INIT { .program_name = NULL, .pid = -1, .read_fd = -1 }

bool
flt_child_proc_open(const char *source_dir,
                    const char *program_name,
                    const char *const argv[],
                    struct flt_child_proc *cp);

bool
flt_child_proc_close(struct flt_child_proc *cp);

#endif /* FLT_CHILD_PROC_H */
