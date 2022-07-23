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

#include "flt-child-proc.h"

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "flt-util.h"

bool
flt_child_proc_open(const char *source_dir,
                    const char *program_name,
                    const char *const argv[],
                    struct flt_child_proc *cp)
{
        int n_args = 0;

        for (const char *const *p = argv; *p; p++)
                n_args++;

        int pipe_fds[2];

        if (pipe(pipe_fds) == -1) {
                fprintf(stderr, "pipe failed: %s\n", strerror(errno));
                return false;
        }

        pid_t pid = fork();

        if (pid == -1) {
                close(pipe_fds[0]);
                close(pipe_fds[1]);
                fprintf(stderr, "fork failed: %s\n", strerror(errno));
                return false;
        }

        if (pid == 0) {
                close(pipe_fds[0]);

                if (dup2(pipe_fds[1], STDOUT_FILENO) == -1) {
                        fprintf(stderr, "dup2 failed: %s\n", strerror(errno));
                } else {
                        close(pipe_fds[1]);

                        const char **argv_copy =
                                flt_alloc((n_args + 2) *
                                          sizeof (const char *));
                        char *full_program;

                        if (source_dir) {
                                full_program = flt_strconcat(source_dir,
                                                             "/",
                                                             program_name,
                                                             NULL);
                        } else {
                                full_program = flt_strdup(program_name);
                        }

                        argv_copy[0] = full_program;
                        memcpy(argv_copy + 1,
                               argv, (n_args + 1) * sizeof (const char *));

                        execvp(full_program, (char **) argv_copy);

                        fprintf(stderr,
                                "exec failed: %s: %s\n",
                                full_program,
                                strerror(errno));

                        flt_free(argv_copy);
                        flt_free(full_program);
                }

                exit(EXIT_FAILURE);

                return false;
        }

        close(pipe_fds[1]);

        cp->pid = pid;
        cp->read_fd = pipe_fds[0];
        cp->program_name = flt_strdup(program_name);

        return true;
}

bool
flt_child_proc_close(struct flt_child_proc *cp)
{
        if (cp->read_fd != -1)
                close(cp->read_fd);

        if (cp->pid > 0) {
                int status = EXIT_FAILURE;

                if (waitpid(cp->pid, &status, 0 /* options */) == -1 ||
                    !WIFEXITED(status) ||
                    WEXITSTATUS(status) != EXIT_SUCCESS) {
                        if (cp->program_name) {
                                fprintf(stderr,
                                        "%s: subprocess failed\n",
                                        cp->program_name);
                        } else {
                                fprintf(stderr, "subprocess failed\n");
                        }

                        return false;
                }
        }

        if (cp->program_name)
                flt_free(cp->program_name);

        return true;
}
