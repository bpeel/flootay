#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "flt-buffer.h"
#include "flt-util.h"

struct child_proc {
        pid_t pid;
        int read_fd;
};

static bool
open_child_proc(char *const argv[],
                struct child_proc *cp)
{
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
                        execvp(argv[0], argv);
                        fprintf(stderr,
                                "exec failed: %s: %s\n",
                                argv[0],
                                strerror(errno));
                }

                exit(EXIT_FAILURE);

                return false;
        }

        close(pipe_fds[1]);

        cp->pid = pid;
        cp->read_fd = pipe_fds[0];

        return true;
}

static bool
close_child_proc(struct child_proc *cp)
{
        if (cp->read_fd != -1)
                close(cp->read_fd);

        int status = EXIT_FAILURE;

        if (waitpid(cp->pid, &status, 0 /* options */) == -1 ||
            !WIFEXITED(status) ||
            WEXITSTATUS(status) != EXIT_SUCCESS) {
                fprintf(stderr, "subprocess failed\n");
                return false;
        } else {
                return true;
        }
}

static char *
get_process_output(char *const argv[])
{
        struct child_proc cp;

        if (!open_child_proc(argv, &cp))
                return NULL;

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        while (true) {
                flt_buffer_ensure_size(&buf, buf.length + 1024);

                ssize_t got = read(cp.read_fd,
                                   buf.data + buf.length,
                                   buf.size - buf.length);

                if (got <= 0)
                        break;

                buf.length += got;
        }

        if (!close_child_proc(&cp)) {
                flt_buffer_destroy(&buf);
                return NULL;
        } else {
                flt_buffer_append_c(&buf, '\0');
                return (char *) buf.data;
        }
}

static bool
get_speedy_args(const char *speedy_file,
                struct flt_buffer *buf,
                int *n_inputs,
                char **filter_arg_out)
{
        char *proc_args[] = {
                "./speedy.py",
                (char *) speedy_file,
                NULL,
        };

        char *output = get_process_output(proc_args);

        if (output == NULL)
                return false;

        const char *end;
        bool had_filter_arg = false;
        bool had_filter_value = false;
        *n_inputs = 0;

        for (const char *p = output; (end = strchr(p, '\n')); p = end + 1) {
                char *arg = flt_strndup(p, end - p);

                if (had_filter_arg) {
                        *filter_arg_out = arg;
                        had_filter_value = true;
                        break;
                } else if (!strcmp(arg, "-filter_complex")) {
                        flt_free(arg);
                        had_filter_arg = true;
                } else {
                        if (!strcmp(arg, "-i"))
                                (*n_inputs)++;

                        flt_buffer_append(buf, &arg, sizeof arg);
                }
        }

        flt_free(output);

        if (!had_filter_arg || !had_filter_value) {
                fprintf(stderr,
                        "missing -filter_complex argument from speedy\n");
                return false;
        }

        return true;
}

static void
add_arg(struct flt_buffer *args, const char *arg)
{
        char *arg_dup = flt_strdup(arg);
        flt_buffer_append(args, &arg_dup, sizeof arg_dup);
}

static void
add_ffmpeg_args(struct flt_buffer *args,
                int n_inputs,
                const char *filter_arg,
                struct child_proc *logo_proc)
{
        int logo_input = n_inputs;

        add_arg(args, "-f");
        add_arg(args, "rawvideo");
        add_arg(args, "-pixel_format");
        add_arg(args, "rgb32");
        add_arg(args, "-video_size");
        add_arg(args, "1920x1080");
        add_arg(args, "-framerate");
        add_arg(args, "30");
        add_arg(args, "-i");

        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf, "pipe:%i", logo_proc->read_fd);

        add_arg(args, (const char *) buf.data);

        add_arg(args, "-filter_complex");

        flt_buffer_set_length(&buf, 0);

        flt_buffer_append_printf(&buf,
                                 "%s;"
                                 "[outv]scale=1920:1080[soutv];"
                                 "[%i:v][soutv]concat=n=2:v=1:a=0[finalv]",
                                 filter_arg,
                                 logo_input);

        add_arg(args, (const char *) buf.data);

        add_arg(args, "-map");
        add_arg(args, "[finalv]");
        add_arg(args, "film.mp4");

        char *terminator = NULL;
        flt_buffer_append(args, &terminator, sizeof terminator);

        flt_buffer_destroy(&buf);
}

static bool
run_ffmpeg(struct flt_buffer *args,
           struct child_proc *logo_proc)
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

        close(logo_proc->read_fd);
        logo_proc->read_fd = -1;

        int status = EXIT_FAILURE;

        if (waitpid(pid, &status, 0 /* options */) == -1 ||
            !WIFEXITED(status) ||
            WEXITSTATUS(status) != EXIT_SUCCESS) {
                fprintf(stderr, "%s failed\n", argv[0]);
                return false;
        } else {
                return true;
        }
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

int
main(int argc, char **argv)
{
        if (argc != 3) {
                fprintf(stderr,
                        "usage: generate-film "
                        "<speedy-file> <flootay-file>\n");
                return EXIT_FAILURE;
        }

        const char *speedy_file = argv[1];
        const char *flootay_file = argv[2];

        struct flt_buffer args = FLT_BUFFER_STATIC_INIT;
        char *filter_arg = NULL;
        int n_inputs;

        int ret = EXIT_SUCCESS;

        add_arg(&args, "ffmpeg");

        if (!get_speedy_args(speedy_file, &args, &n_inputs, &filter_arg)) {
                ret = EXIT_FAILURE;
        } else {
                struct child_proc logo_proc;
                char *logo_args[] = { "./build/generate-logo", NULL };

                if (!open_child_proc(logo_args, &logo_proc)) {
                        ret = EXIT_FAILURE;
                } else {
                        add_ffmpeg_args(&args,
                                        n_inputs,
                                        filter_arg,
                                        &logo_proc);

                        if (!run_ffmpeg(&args, &logo_proc))
                                ret = EXIT_FAILURE;

                        if (!close_child_proc(&logo_proc))
                                ret = EXIT_FAILURE;
                }
        }

        free_args(&args);
        flt_free(filter_arg);

        return ret;
}
