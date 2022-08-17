#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

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
add_input_arg(const char *source_dir,
              struct flt_list *proc_inputs,
              struct flt_buffer *buf,
              char *arg)
{
        bool ret = true;

        if (*arg == '|') {
                static const char *const proc_args[] = { NULL };

                struct flt_child_proc *cp = add_child_proc(proc_inputs);

                ret = flt_child_proc_open(NULL,
                                          arg + 1,
                                          proc_args,
                                          cp);

                flt_free(arg);

                struct flt_buffer str_buf = FLT_BUFFER_STATIC_INIT;

                flt_buffer_append_printf(&str_buf, "pipe:%i", cp->read_fd);

                arg = (char *) str_buf.data;
        }

        flt_buffer_append(buf, &arg, sizeof arg);

        return ret;
}

static bool
get_speedy_args(const char *source_dir,
                const char *speedy_file,
                struct flt_list *proc_inputs,
                struct flt_buffer *buf,
                int *n_inputs,
                char **filter_arg_out)
{
        const char * const proc_args[] = {
                (char *) speedy_file,
                NULL,
        };

        char *output = flt_child_proc_get_output(source_dir,
                                                 "speedy.py",
                                                 proc_args);

        if (output == NULL)
                return false;

        const char *end;
        bool had_filter_arg = false;
        bool had_filter_value = false;
        bool is_input = false;
        *n_inputs = 0;

        bool ret = true;

        for (const char *p = output; (end = strchr(p, '\n')); p = end + 1) {
                char *arg = flt_strndup(p, end - p);

                if (is_input) {
                        if (!add_input_arg(source_dir, proc_inputs, buf, arg)) {
                                ret = false;
                                break;
                        }
                        is_input = false;
                } else if (had_filter_arg) {
                        *filter_arg_out = arg;
                        had_filter_value = true;
                        break;
                } else if (!strcmp(arg, "-filter_complex")) {
                        flt_free(arg);
                        had_filter_arg = true;
                } else {
                        if (!strcmp(arg, "-i")) {
                                is_input = true;
                                (*n_inputs)++;
                        }

                        flt_buffer_append(buf, &arg, sizeof arg);
                }
        }

        flt_free(output);

        if (ret && (!had_filter_arg || !had_filter_value)) {
                fprintf(stderr,
                        "missing -filter_complex argument from speedy\n");
                ret = false;
        }

        return ret;
}

static void
add_arg(struct flt_buffer *args, const char *arg)
{
        char *arg_dup = flt_strdup(arg);
        flt_buffer_append(args, &arg_dup, sizeof arg_dup);
}

FLT_NULL_TERMINATED static void
add_args(struct flt_buffer *args,
         ...)
{
        va_list ap;

        va_start(ap, args);

        const char *arg;

        while ((arg = va_arg(ap, const char *)))
                add_arg(args, arg);

        va_end(ap);
}

FLT_PRINTF_FORMAT(2, 3) static void
add_arg_printf(struct flt_buffer *args,
               const char *format,
               ...)
{
        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        va_list ap;
        va_start(ap, format);

        flt_buffer_append_vprintf(&buf, format, ap);

        va_end(ap);

        flt_buffer_append(args, &buf.data, sizeof buf.data);
}

static void
add_ffmpeg_args(const char *source_dir,
                struct flt_buffer *args,
                int n_inputs,
                const char *filter_arg,
                struct flt_child_proc *flootay_proc,
                struct flt_child_proc *sound_proc)
{
        int flootay_input = n_inputs++;
        int sound_input = n_inputs++;

        add_args(args,
                 "-f", "rawvideo",
                 "-pixel_format", "rgba",
                 "-video_size", "1920x1080",
                 "-framerate", "30",
                 "-i",
                 NULL);
        add_arg_printf(args, "pipe:%i", flootay_proc->read_fd);

        add_args(args,
                 "-ar", "48000",
                 "-ac", "2",
                 "-f", "s24le",
                 "-c:a", "pcm_s24le",
                 "-i",
                 NULL);
        add_arg_printf(args, "pipe:%i", sound_proc->read_fd);

        add_arg(args, "-filter_complex");
        add_arg_printf(args,
                       "%s;"
                       "[outv][%i]overlay[overoutv]",
                       filter_arg,
                       flootay_input);

        add_args(args, "-map", "[overoutv]", NULL);

        add_arg(args, "-map");
        add_arg_printf(args, "%i:a", sound_input);

        add_args(args,
                 "-c:v", "prores_ks",
                 "-profile:v", "3",
                 "-vendor", "apl0",
                 "-bits_per_mb", "8000",
                 "-pix_fmt", "yuv422p10le",
                 "film.mov",
                 NULL);

        char *terminator = NULL;
        flt_buffer_append(args, &terminator, sizeof terminator);
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

        struct flt_list proc_inputs;

        flt_list_init(&proc_inputs);

        struct flt_child_proc *flootay_proc = add_child_proc(&proc_inputs);
        struct flt_child_proc *sound_proc = add_child_proc(&proc_inputs);

        if (!get_speedy_args(source_dir,
                             speedy_file,
                             &proc_inputs,
                             &args,
                             &n_inputs,
                             &filter_arg)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        const char * const flootay_args[] = {
                "scores.flt",
                NULL
        };

        if (!flt_child_proc_open(source_dir,
                                 "build/flootay",
                                 flootay_args,
                                 flootay_proc)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        static const char * const sound_args[] = {
                NULL,
        };

        if (!flt_child_proc_open(NULL, /* source_dir */
                                 "./sound.sh",
                                 sound_args,
                                 sound_proc)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        add_ffmpeg_args(source_dir,
                        &args,
                        n_inputs,
                        filter_arg,
                        flootay_proc,
                        sound_proc);

        if (!run_ffmpeg(&args, &proc_inputs))
                ret = EXIT_FAILURE;

out:
        if (!close_proc_inputs(&proc_inputs))
                ret = EXIT_FAILURE;

        free_args(&args);
        flt_free(filter_arg);

        flt_free(source_dir);

        return ret;
}
