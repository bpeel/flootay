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

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>

#include "flt-buffer.h"
#include "flt-util.h"
#include "flt-child-proc.h"
#include "flt-list.h"

struct proc_input {
        struct flt_list link;

        struct flt_child_proc cp;
};

static struct flt_child_proc *
add_child_proc(struct flt_list *list)
{
        struct proc_input *pi = flt_alloc(sizeof *pi);

        static const struct flt_child_proc cp_init = FLT_CHILD_PROC_INIT;

        pi->cp = cp_init;

        flt_list_insert(list->prev, &pi->link);

        return &pi->cp;
}

static bool
add_input_arg(struct flt_list *proc_inputs,
              struct flt_buffer *buf,
              const char *arg)
{
        bool ret = true;

        if (*arg == '|') {
                static const char *const proc_args[] = { NULL };

                struct flt_child_proc *cp = add_child_proc(proc_inputs);

                ret = flt_child_proc_open(NULL,
                                          arg + 1,
                                          proc_args,
                                          cp);

                struct flt_buffer str_buf = FLT_BUFFER_STATIC_INIT;

                flt_buffer_append_printf(&str_buf, "pipe:%i", cp->read_fd);

                flt_buffer_append(buf, &str_buf.data, sizeof str_buf.data);
        } else {
                char *arg_copy = flt_strdup(arg);
                flt_buffer_append(buf, &arg_copy, sizeof arg_copy);
        }

        return ret;
}

static bool
get_speedy_args(int argc,
                char * const *argv,
                struct flt_list *proc_inputs,
                struct flt_buffer *buf)
{
        bool is_input = false;

        bool ret = true;

        for (int i = 0; i < argc; i++) {
                const char *arg = argv[i];

                if (is_input) {
                        if (!add_input_arg(proc_inputs, buf, arg)) {
                                ret = false;
                                break;
                        }
                        is_input = false;
                } else {
                        if (!strcmp(arg, "-i"))
                                is_input = true;

                        char *arg_copy = flt_strdup(arg);
                        flt_buffer_append(buf, &arg_copy, sizeof arg_copy);
                }
        }

        char *terminator = NULL;
        flt_buffer_append(buf, &terminator, sizeof terminator);

        return ret;
}

static bool
run_ffmpeg(struct flt_buffer *args,
           struct flt_list *proc_inputs)
{
        pid_t pid = fork();

        if (pid == -1) {
                fprintf(stderr, "fork failed: %s\n", strerror(errno));
                return false;
        }

        char **argv = (char **) args->data;

        if (pid == 0) {
                execvp(argv[0], argv);

                fprintf(stderr,
                        "exec failed: %s: %s\n",
                        argv[0],
                        strerror(errno));

                exit(EXIT_FAILURE);

                return false;
        }

        struct proc_input *pi;

        flt_list_for_each(pi, proc_inputs, link) {
                close(pi->cp.read_fd);
                pi->cp.read_fd = -1;
        }

        struct flt_child_proc *ffmpeg_proc = add_child_proc(proc_inputs);

        ffmpeg_proc->pid = pid;
        ffmpeg_proc->program_name = flt_strdup(argv[0]);

        return true;
}

static bool
has_child(struct flt_list *proc_inputs)
{
        struct proc_input *pi;

        flt_list_for_each(pi, proc_inputs, link) {
                if (pi->cp.pid != (pid_t) -1)
                        return true;
        }

        return false;
}

static bool
finish_child(struct proc_input *pi, int status)
{
        pi->cp.pid = (pid_t) -1;

        if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
                fprintf(stderr, "%s failed\n", pi->cp.program_name);
                return false;
        } else {
                return true;
        }
}

static bool
wait_for_children(struct flt_list *proc_inputs)
{
        while (has_child(proc_inputs)) {
                int status = EXIT_FAILURE;

                pid_t pid = wait(&status);

                if (pid == -1) {
                        fprintf(stderr,
                                "waitpid failed: %s\n",
                                strerror(errno));
                        return false;
                } else {
                        struct proc_input *pi;

                        flt_list_for_each(pi, proc_inputs, link) {
                                if (pi->cp.pid == pid) {
                                        if (!finish_child(pi, status))
                                                return false;
                                        break;
                                }
                        }
                }
        }

        return true;
}

static void
free_args(struct flt_buffer *buf)
{
        size_t n_args = buf->length / sizeof (char *);
        char **args = (char **) buf->data;

        for (int i = 0; i < n_args; i++)
                flt_free(args[i]);

        flt_buffer_destroy(buf);
}

static bool
close_proc_inputs(struct flt_list *list)
{
        struct proc_input *pi, *tmp;
        bool ret = true;

        flt_list_for_each_safe(pi, tmp, list, link) {
                if (!flt_child_proc_close(&pi->cp))
                        ret = false;

                flt_free(pi);
        }

        return ret;
}

static void
kill_children(struct flt_list *proc_inputs)
{
        struct proc_input *pi;

        flt_list_for_each(pi, proc_inputs, link) {
                if (pi->cp.pid != (pid_t) -1)
                        kill(pi->cp.pid, SIGTERM);
        }
}

int
main(int argc, char **argv)
{
        if (argc < 2) {
                fprintf(stderr,
                        "usage: run-ffmpeg <exe> [args]…\n");
                return EXIT_FAILURE;
        }

        struct flt_buffer args = FLT_BUFFER_STATIC_INIT;

        int ret = EXIT_SUCCESS;

        struct flt_list proc_inputs;

        flt_list_init(&proc_inputs);

        if (!get_speedy_args(argc - 1, argv + 1,
                             &proc_inputs,
                             &args)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        if (!run_ffmpeg(&args, &proc_inputs)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        if (!wait_for_children(&proc_inputs)) {
                ret = EXIT_FAILURE;
                kill_children(&proc_inputs);
        }

out:
        if (!close_proc_inputs(&proc_inputs))
                ret = EXIT_FAILURE;

        free_args(&args);

        return ret;
}
