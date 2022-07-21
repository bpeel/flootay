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
        char *program_name;
        pid_t pid;
        int read_fd;
};

#define CHILD_PROC_INIT { .program_name = NULL, .pid = -1, .read_fd = -1 }

static bool
open_child_proc(const char *source_dir,
                const char *program_name,
                const char *const argv[],
                struct child_proc *cp)
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
                        char *full_program = flt_strconcat(source_dir,
                                                           "/",
                                                           program_name,
                                                           NULL);
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

static bool
close_child_proc(struct child_proc *cp)
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

static char *
get_process_output(const char *source_dir,
                   const char *program_name,
                   const char *const argv[])
{
        struct child_proc cp;

        if (!open_child_proc(source_dir, program_name, argv, &cp))
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
get_speedy_args(const char *source_dir,
                const char *speedy_file,
                struct flt_buffer *buf,
                int *n_inputs,
                char **filter_arg_out)
{
        const char * const proc_args[] = {
                (char *) speedy_file,
                NULL,
        };

        char *output = get_process_output(source_dir, "speedy.py", proc_args);

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
add_ffmpeg_args(const char *source_dir,
                struct flt_buffer *args,
                int n_inputs,
                const char *filter_arg,
                struct child_proc *logo_proc,
                struct child_proc *flootay_proc)
{
        int logo_input = n_inputs++;
        int logo_sound_input = n_inputs++;
        int flootay_input = n_inputs++;

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

        add_arg(args, "-i");

        char *logo_sound_file =
                flt_strconcat(source_dir, "/logo-sound.flac", NULL);
        flt_buffer_append(args, &logo_sound_file, sizeof logo_sound_file);

        add_arg(args, "-f");
        add_arg(args, "rawvideo");
        add_arg(args, "-pixel_format");
        add_arg(args, "rgba");
        add_arg(args, "-video_size");
        add_arg(args, "1920x1080");
        add_arg(args, "-framerate");
        add_arg(args, "30");
        add_arg(args, "-i");

        flt_buffer_set_length(&buf, 0);
        flt_buffer_append_printf(&buf, "pipe:%i", flootay_proc->read_fd);
        add_arg(args, (const char *) buf.data);

        add_arg(args, "-filter_complex");

        flt_buffer_set_length(&buf, 0);

        flt_buffer_append_printf(&buf,
                                 "%s;"
                                 "[outv]scale=1920:1080[soutv];"
                                 "[soutv][%i]overlay[overoutv];"
                                 "[%i:v][%i:a]"
                                 "[overoutv][outa]concat=n=2:v=1:a=1"
                                 "[finalv][finala]",
                                 filter_arg,
                                 flootay_input,
                                 logo_input,
                                 logo_sound_input);

        add_arg(args, (const char *) buf.data);

        add_arg(args, "-map");
        add_arg(args, "[finalv]");
        add_arg(args, "-map");
        add_arg(args, "[finala]");
        add_arg(args, "film.mp4");

        char *terminator = NULL;
        flt_buffer_append(args, &terminator, sizeof terminator);

        flt_buffer_destroy(&buf);
}

static bool
run_ffmpeg(struct flt_buffer *args,
           struct child_proc *logo_proc,
           struct child_proc *flootay_proc)
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

        close(flootay_proc->read_fd);
        flootay_proc->read_fd = -1;

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

static char *
get_source_dir(const char *exe)
{
        const char *end = strrchr(exe, '/');

        if (end == NULL)
                return flt_strdup(".");

        static const char build_end[] = "/build";

        if (end - exe >= sizeof build_end &&
            !memcmp(build_end,
                    end - (sizeof build_end) + 1,
                    (sizeof build_end) - 1))
                end -= (sizeof build_end) - 1;

        return flt_strndup(exe, end - exe);
}

int
main(int argc, char **argv)
{
        if (argc != 2) {
                fprintf(stderr,
                        "usage: generate-film <speedy-file>\n");
                return EXIT_FAILURE;
        }

        const char *speedy_file = argv[1];

        struct flt_buffer args = FLT_BUFFER_STATIC_INIT;
        char *filter_arg = NULL;
        int n_inputs;

        char *source_dir = get_source_dir(argv[0]);

        int ret = EXIT_SUCCESS;

        add_arg(&args, "ffmpeg");

        struct child_proc logo_proc = CHILD_PROC_INIT;
        struct child_proc flootay_proc = CHILD_PROC_INIT;

        if (!get_speedy_args(source_dir,
                             speedy_file,
                             &args,
                             &n_inputs,
                             &filter_arg)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        static const char * const logo_args[] = { NULL };

        if (!open_child_proc(source_dir,
                             "build/generate-logo",
                             logo_args,
                             &logo_proc)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        const char * const flootay_args[] = {
                "scores.flt",
                NULL
        };

        if (!open_child_proc(source_dir,
                             "build/flootay",
                             flootay_args,
                             &flootay_proc)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        add_ffmpeg_args(source_dir,
                        &args,
                        n_inputs,
                        filter_arg,
                        &logo_proc,
                        &flootay_proc);

        if (!run_ffmpeg(&args, &logo_proc, &flootay_proc))
                ret = EXIT_FAILURE;

out:
        if (!close_child_proc(&logo_proc))
                ret = EXIT_FAILURE;

        if (!close_child_proc(&flootay_proc))
                ret = EXIT_FAILURE;

        free_args(&args);
        flt_free(filter_arg);

        flt_free(source_dir);

        return ret;
}
