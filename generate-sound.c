#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdint.h>
#include <math.h>

#include "flt-buffer.h"
#include "flt-util.h"
#include "flt-child-proc.h"
#include "flt-list.h"

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define SAMPLE_SIZE 3

#define BUFFER_SIZE 4096

struct sound {
        struct flt_list link;
        double start_time;
        double length;
        char *filename;
};

static bool
get_sound_length(const char *filename,
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

static int
copy_file_to_stdout(const char *filename)
{
        struct flt_child_proc cp = FLT_CHILD_PROC_INIT;

        const char * const ffmpeg_args[] = {
                "-i", filename,
                "-ar", FLT_STRINGIFY(SAMPLE_RATE),
                "-ac", "2",
                "-f", "s24le",
                "-c:a", "pcm_s24le",
                "-hide_banner",
                "-loglevel", "error",
                "-",
                NULL,
        };

        if (!flt_child_proc_open(NULL, /* source_dir */
                                 "ffmpeg",
                                 ffmpeg_args,
                                 &cp))
                return -1;

        int ret = 0;

        uint8_t *buf = flt_alloc(BUFFER_SIZE);

        while (true) {
                ssize_t got = read(cp.read_fd, buf, BUFFER_SIZE);

                if (got == 0)
                        break;

                if (got < 0) {
                        fprintf(stderr,
                                "error reading from ffmpeg: %s\n",
                                strerror(errno));
                        ret = -1;
                        break;
                }

                size_t wrote = fwrite(buf, 1, got, stdout);

                if (wrote < got) {
                        fprintf(stderr,
                                "error writing to stdout: %s\n",
                                strerror(errno));
                        ret = -1;
                        break;
                }

                ret += got;
        }

        if (!flt_child_proc_close(&cp))
                ret = -1;

        flt_free(buf);

        return ret;
}

static bool
generate_silence(size_t n_samples)
{
        static const uint8_t zero_sample[CHANNELS * SAMPLE_SIZE] = { 0 };

        for (size_t i = 0; i < n_samples; i++) {
                size_t wrote = fwrite(zero_sample,
                                      1, sizeof zero_sample,
                                      stdout);

                if (wrote != sizeof zero_sample) {
                        fprintf(stderr,
                                "error writing to stdout: %s\n",
                                strerror(errno));
                        return false;
                }
        }

        return true;
}

static bool
write_sounds(const struct flt_list *sounds)
{
        size_t samples_written = 0;
        const struct sound *sound;

        flt_list_for_each(sound, sounds, link) {
                size_t start_samples = round(sound->start_time * SAMPLE_RATE);

                if (start_samples > samples_written &&
                    !generate_silence(start_samples - samples_written))
                        return false;

                samples_written = start_samples;

                int sound_samples = copy_file_to_stdout(sound->filename);

                if (sound_samples < 0)
                        return false;

                samples_written += sound_samples / (CHANNELS * SAMPLE_SIZE);
        }

        return true;
}

static void
free_sounds(struct flt_list *list)
{
        struct sound *s, *tmp;

        flt_list_for_each_safe(s, tmp, list, link) {
                flt_free(s->filename);
                flt_free(s);
        }
}

int
main(int argc, char **argv)
{
        struct flt_list sounds;

        flt_list_init(&sounds);

        int ret = EXIT_SUCCESS;

        for (int i = 1; i + 2 <= argc; i += 2) {
                struct sound *sound = flt_calloc(sizeof *sound);

                flt_list_insert(sounds.prev, &sound->link);

                errno = 0;

                char *tail;

                double start_time = strtod(argv[i], &tail);

                if (errno || !isnormal(start_time) || *tail || start_time < 0) {
                        fprintf(stderr,
                                "invalid start_time: %s\n",
                                argv[i]);
                        ret = EXIT_FAILURE;
                        goto out;
                }

                sound->start_time = start_time;
                sound->filename = flt_strdup(argv[i + 1]);

                if (!get_sound_length(sound->filename, &sound->length)) {
                        ret = EXIT_FAILURE;
                        goto out;
                }
        }

        if (!write_sounds(&sounds)) {
                ret = EXIT_FAILURE;
                goto out;
        }

out:
        free_sounds(&sounds);

        return ret;
}
