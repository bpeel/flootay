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
#define SAMPLE_MAX_VALUE ((1 << (SAMPLE_SIZE * 8)) - 1)

#define BUFFER_SIZE 4096

#define VOLUME_SLIDE_TIME 1.0
#define QUIET_VOLUME 0.1

struct sound {
        struct flt_list link;
        double volume;
        double start_time;
        double length;
        char *filename;
};

struct running_sound {
        struct flt_list link;
        const struct sound *sound;
        struct flt_child_proc cp;
        FILE *f;
};

static const struct sound
default_sound = {
        .start_time = 0.0,
        .volume = 1.0,
};

static const struct flt_child_proc
child_proc_init = FLT_CHILD_PROC_INIT;

static void
free_running_sound(struct running_sound *sound)
{
        if (sound->f)
                fclose(sound->f);

        flt_child_proc_close(&sound->cp);

        flt_free(sound);
}

static void
free_running_sounds(struct flt_list *list)
{
        struct running_sound *sound, *tmp;

        flt_list_for_each_safe(sound, tmp, list, link) {
                free_running_sound(sound);
        }
}

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

static double
get_sound_sample_volume(const struct flt_list *sounds,
                        const struct sound *sound,
                        double sample_time)
{
        double max_volume = 1.0;

        for (const struct sound *s = flt_container_of(sound->link.next,
                                                      struct sound,
                                                      link);
             &s->link != sounds;
             s = flt_container_of(s->link.next, struct sound, link)) {
                if (sample_time < s->start_time) {
                        if (sound->start_time + sound->length > s->start_time &&
                            sample_time >= s->start_time - VOLUME_SLIDE_TIME) {
                                float v = ((1.0 - ((sample_time +
                                                    VOLUME_SLIDE_TIME -
                                                    s->start_time) /
                                                   VOLUME_SLIDE_TIME)) *
                                           (1.0 - QUIET_VOLUME) +
                                           QUIET_VOLUME);

                                if (v < max_volume)
                                        max_volume = v;
                        }
                } else if (sample_time >= s->start_time + s->length) {
                        if (sound->start_time < s->start_time + s->length &&
                            sample_time < (s->start_time +
                                           s->length +
                                           VOLUME_SLIDE_TIME)) {
                                float v = (((sample_time -
                                             s->start_time -
                                             s->length) /
                                            VOLUME_SLIDE_TIME) *
                                           (1.0 - QUIET_VOLUME) +
                                           QUIET_VOLUME);

                                if (v < max_volume)
                                        max_volume = v;
                        }
                } else {
                        max_volume = QUIET_VOLUME;
                        break;
                }
        }

        return max_volume * sound->volume;
}

static bool
start_sound(struct flt_list *running_sounds,
            const struct sound *sound)
{
        struct running_sound *rs = flt_calloc(sizeof *rs);

        rs->sound = sound;
        rs->cp = child_proc_init;

        const char * const ffmpeg_args[] = {
                "-i", sound->filename,
                "-ar", FLT_STRINGIFY(SAMPLE_RATE),
                "-ac", "2",
                "-f", "s24le",
                "-c:a", "pcm_s24le",
                "-hide_banner",
                "-loglevel", "error",
                "-nostdin",
                "-",
                NULL,
        };

        if (!flt_child_proc_open(NULL, /* source_dir */
                                 "ffmpeg",
                                 ffmpeg_args,
                                 &rs->cp)) {
                free_running_sound(rs);
                return false;
        }

        rs->f = fdopen(rs->cp.read_fd, "r");

        if (rs->f == NULL) {
                fprintf(stderr, "fdopen failed: %s\n", strerror(errno));
                free_running_sound(rs);
                return false;
        }

        rs->cp.read_fd = -1;

        flt_list_insert(running_sounds->prev, &rs->link);

        return true;
}

static bool
read_samples(struct running_sound *rs, double samples[CHANNELS])
{
        for (int i = 0; i < CHANNELS; i++) {
                uint8_t buf[SAMPLE_SIZE];

                if (fread(buf, 1, sizeof buf, rs->f) != sizeof buf)
                        return false;

                int value = 0;

                for (int j = 0; j < SAMPLE_SIZE; j++)
                        value |= buf[j] << (8 * j);

                if ((buf[SAMPLE_SIZE - 1] & 0x80))
                        value |= -1 << (SAMPLE_SIZE * 8);

                samples[i] = value / (double) SAMPLE_MAX_VALUE;
        }

        return true;
}

static bool
get_samples(struct flt_list *running_sounds,
            const struct flt_list *sounds,
            double current_time,
            double samples[CHANNELS])
{
        struct running_sound *rs, *tmp;

        for (int i = 0; i < CHANNELS; i++)
                samples[i] = 0.0;

        flt_list_for_each_safe(rs, tmp, running_sounds, link) {
                double sound_samples[CHANNELS];

                if (!read_samples(rs, sound_samples)) {
                        bool close_ret = flt_child_proc_close(&rs->cp);
                        rs->cp = child_proc_init;
                        if (!close_ret)
                                return false;
                        flt_list_remove(&rs->link);
                        free_running_sound(rs);
                        continue;
                }

                double volume = get_sound_sample_volume(sounds,
                                                        rs->sound,
                                                        current_time);

                for (int i = 0; i < CHANNELS; i++)
                        samples[i] += sound_samples[i] * volume;
        }

        return true;
}

static bool
write_samples(const double samples[CHANNELS])
{
        for (int i = 0; i < CHANNELS; i++) {
                int value = (samples[i] >= 1.0 ? SAMPLE_MAX_VALUE :
                             samples[i] <= -1.0 ? -SAMPLE_MAX_VALUE :
                             round(samples[i] * SAMPLE_MAX_VALUE));
                for (int j = 0; j < SAMPLE_SIZE; j++) {
                        if (fputc(value & 0xff, stdout) == EOF) {
                                fprintf(stderr, "error writing to stdout\n");
                                return false;
                        }

                        value >>= 8;
                }
        }

        return true;
}

static bool
write_sounds(const struct flt_list *sounds)
{
        size_t samples_written = 0;
        const struct flt_list *next_sound_link = sounds->next;
        struct flt_list running_sounds;
        bool ret = true;

        flt_list_init(&running_sounds);

        while (next_sound_link != sounds || !flt_list_empty(&running_sounds)) {
                double current_time = samples_written / (double) SAMPLE_RATE;

                while (next_sound_link != sounds) {
                        const struct sound *next_sound =
                                flt_container_of(next_sound_link,
                                                 struct sound,
                                                 link);

                        if (next_sound->start_time > current_time)
                                break;

                        if (!start_sound(&running_sounds, next_sound)) {
                                ret = false;
                                goto out;
                        }

                        next_sound_link = next_sound_link->next;
                }

                double samples[CHANNELS];

                if (!get_samples(&running_sounds,
                                 sounds,
                                 current_time,
                                 samples) ||
                    !write_samples(samples)) {
                        ret = false;
                        goto out;
                }

                samples_written++;
        }

out:
        free_running_sounds(&running_sounds);

        return ret;
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

        char *tail;

        while (true) {
                switch (getopt(argc, argv, "-s:v:")) {
                case 's':
                        errno = 0;

                        sound_template.start_time = strtod(optarg, &tail);

                        if (errno ||
                            (!isnormal(sound_template.start_time) &&
                             sound_template.start_time != 0.0) ||
                            *tail ||
                            sound_template.start_time < 0) {
                                fprintf(stderr,
                                        "invalid start_time: %s\n",
                                        optarg);
                                return false;
                        }
                        break;

                case 'v':
                        errno = 0;

                        sound_template.volume = strtod(optarg, &tail);

                        if (errno ||
                            !isnormal(sound_template.volume) ||
                            *tail ||
                            sound_template.volume <= 0.0 ||
                            sound_template.volume > 1.0) {
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
