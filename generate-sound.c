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

static const struct sound
default_sound = {
        .start_time = 0.0,
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

static int
compare_start_time_cb(const void *pa, const void *pb)
{
        const struct sound *a = *(const struct sound **) pa;
        const struct sound *b = *(const struct sound **) pb;

        if (a->start_time < b->start_time)
                return -1;
        if (a->start_time > b->start_time)
                return 1;
        return 0;
}

static void
sort_sounds(struct flt_list *sounds)
{
        int n_sounds = flt_list_length(sounds);
        struct sound **array = flt_alloc(sizeof (struct sound *) * n_sounds);

        int sound_num = 0;
        struct sound *sound;

        flt_list_for_each(sound, sounds, link) {
                array[sound_num++] = sound;
        }

        qsort(array, n_sounds, sizeof (struct sound *), compare_start_time_cb);

        flt_list_init(sounds);

        for (int i = 0; i < n_sounds; i++)
                flt_list_insert(sounds->prev, &array[i]->link);

        flt_free(array);
}

static bool
process_options(int argc, char **argv, struct flt_list *sounds)
{
        struct sound sound_template = default_sound;

        while (true) {
                switch (getopt(argc, argv, "-s:")) {
                case 's':
                        errno = 0;
                        char *tail;

                        sound_template.start_time = strtod(optarg, &tail);

                        if (errno ||
                            !isnormal(sound_template.start_time) ||
                            *tail ||
                            sound_template.start_time < 0) {
                                fprintf(stderr,
                                        "invalid start_time: %s\n",
                                        optarg);
                                return false;
                        }
                        break;

                case 1: {
                        struct sound *sound = flt_alloc(sizeof *sound);

                        *sound = sound_template;

                        flt_list_insert(sounds->prev, &sound->link);

                        sound->filename = flt_strdup(optarg);

                        if (!get_sound_length(sound->filename,
                                              &sound->length))
                                return false;

                        sound_template = default_sound;
                        /* By default the next sound will start
                         * immediately after this one.
                         */
                        sound_template.start_time = (sound->start_time +
                                                     sound->length);
                        break;
                }

                case -1:
                        return true;

                default:
                        return false;
                }
        }
}

int
main(int argc, char **argv)
{
        struct flt_list sounds;

        flt_list_init(&sounds);

        int ret = EXIT_SUCCESS;

        if (!process_options(argc, argv, &sounds)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        sort_sounds(&sounds);

        if (!write_sounds(&sounds)) {
                ret = EXIT_FAILURE;
                goto out;
        }

out:
        free_sounds(&sounds);

        return ret;
}
