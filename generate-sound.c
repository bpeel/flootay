#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdint.h>
#include <math.h>
#include <float.h>

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
#define MUSIC_FADE_OUT_TIME 3.0

struct sound {
        struct flt_list link;
        double volume;
        double start_time;
        double length;
        char *filename;
};

struct config {
        struct flt_list sounds;
        struct flt_list music;

        double music_start_time;
        double music_end_time;
};

struct running_sound {
        struct flt_list link;
        const struct sound *sound;
        struct flt_child_proc cp;
        FILE *f;
};

struct data {
        const struct config *config;
        size_t samples_written;
        const struct flt_list *next_sound_link;
        const struct flt_list *next_music_link;
        struct flt_list running_sounds;
        const struct sound *music_sound;
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
get_fade_out_volume(struct data *data,
                   double sample_time)
{
        if (sample_time > data->config->music_end_time - MUSIC_FADE_OUT_TIME) {
                double volume = (1.0 -
                                 (sample_time +
                                  MUSIC_FADE_OUT_TIME -
                                  data->config->music_end_time) /
                                 MUSIC_FADE_OUT_TIME);

                return MAX(0.0, volume);
        }

        return 1.0;
}

static double
get_music_volume(struct data *data,
                 double sample_time)
{
        double max_volume = 1.0;

        const struct sound *s;

        flt_list_for_each(s, &data->config->sounds, link) {
                if (data->config->music_end_time > s->start_time &&
                    sample_time < s->start_time) {
                        if (sample_time >= s->start_time - VOLUME_SLIDE_TIME) {
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
                        if (data->config->music_start_time < (s->start_time +
                                                              s->length) &&
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

        double fade_out_volume = get_fade_out_volume(data, sample_time);

        if (max_volume > fade_out_volume)
                max_volume = fade_out_volume;

        return max_volume;
}

static bool
start_sound(struct flt_list *running_sounds,
            const struct sound *sound,
            double length)
{
        struct running_sound *rs = flt_calloc(sizeof *rs);

        rs->sound = sound;
        rs->cp = child_proc_init;

        struct flt_buffer length_buf = FLT_BUFFER_STATIC_INIT;

        const char *ffmpeg_args[] = {
                "-i", sound->filename,
                "-ar", FLT_STRINGIFY(SAMPLE_RATE),
                "-ac", "2",
                "-f", "s24le",
                "-c:a", "pcm_s24le",
                "-hide_banner",
                "-loglevel", "error",
                "-nostdin",
                /* space for three extra args */
                NULL,
                NULL,
                NULL,
                /* terminator */
                NULL,
        };

        const char **args_end;

        for (args_end = ffmpeg_args; *args_end; args_end++);

        if (length < DBL_MAX) {
                flt_buffer_append_printf(&length_buf,
                                         "%f",
                                         length);

                *(args_end++) = "-to";
                *(args_end++) = (const char *) length_buf.data;
        }

        *(args_end++) = "-";

        bool run_ret = flt_child_proc_open(NULL, /* source_dir */
                                           "ffmpeg",
                                           ffmpeg_args,
                                           &rs->cp);

        flt_buffer_destroy(&length_buf);

        if (!run_ret) {
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
get_samples(struct data *data,
            double samples[CHANNELS])
{
        double current_time = data->samples_written / (double) SAMPLE_RATE;

        struct running_sound *rs, *tmp;

        for (int i = 0; i < CHANNELS; i++)
                samples[i] = 0.0;

        flt_list_for_each_safe(rs, tmp, &data->running_sounds, link) {
                double sound_samples[CHANNELS];

                if (!read_samples(rs, sound_samples)) {
                        bool close_ret = flt_child_proc_close(&rs->cp);
                        rs->cp = child_proc_init;
                        if (!close_ret)
                                return false;
                        if (rs->sound == data->music_sound)
                                data->music_sound = NULL;
                        flt_list_remove(&rs->link);
                        free_running_sound(rs);
                        continue;
                }

                double volume = rs->sound->volume;

                if (rs->sound == data->music_sound)
                        volume *= get_music_volume(data, current_time);

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
is_running(const struct data *data)
{
        if (data->next_sound_link != &data->config->sounds)
                return true;

        if (!flt_list_empty(&data->running_sounds))
                return true;

        if (!flt_list_empty(&data->config->music) &&
            data->samples_written / (double) SAMPLE_RATE <
            data->config->music_end_time)
                return true;

        return false;
}

static bool
add_sounds(struct data *data)
{
        double current_time = (data->samples_written /
                               (double) SAMPLE_RATE);

        while (data->next_sound_link != &data->config->sounds) {
                const struct sound *next_sound =
                        flt_container_of(data->next_sound_link,
                                         struct sound,
                                         link);

                if (next_sound->start_time > current_time)
                        break;

                if (!start_sound(&data->running_sounds,
                                 next_sound,
                                 DBL_MAX /* length */)) {
                        return false;
                }

                data->next_sound_link = data->next_sound_link->next;
        }

        return true;
}

static bool
add_music(struct data *data)
{
        if (data->music_sound)
                return true;

        if (flt_list_empty(&data->config->music))
                return true;

        double current_time = (data->samples_written /
                               (double) SAMPLE_RATE);

        if (current_time >= data->config->music_end_time ||
            current_time < data->config->music_start_time)
                return true;

        const struct sound *next_music =
                flt_container_of(data->next_music_link,
                                 struct sound,
                                 link);

        double length = DBL_MAX;

        if (current_time + next_music->length > data->config->music_end_time) {
                length = (data->config->music_end_time -
                          current_time +
                          /* add a sample to make sure we don’t start
                           * the next sound just for one sample due to
                           * rounding errors.
                           */
                          1.0 / SAMPLE_RATE);
        }

        if (!start_sound(&data->running_sounds,
                         next_music,
                         length)) {
                return false;
        }

        data->music_sound = next_music;

        data->next_music_link = data->next_music_link->next;

        if (data->next_music_link == &data->config->music)
                data->next_music_link = data->next_music_link->next;

        return true;
}

static bool
write_sounds(const struct config *config)
{
        struct data data = {
                .samples_written = 0,
                .next_sound_link = config->sounds.next,
                .next_music_link = config->music.next,
                .config = config,
        };
        bool ret = true;

        flt_list_init(&data.running_sounds);

        while (is_running(&data)) {
                if (!add_sounds(&data)) {
                        ret = false;
                        break;
                }

                if (!add_music(&data)) {
                        ret = false;
                        break;
                }

                double samples[CHANNELS];

                if (!get_samples(&data, samples) ||
                    !write_samples(samples)) {
                        ret = false;
                        break;
                }

                data.samples_written++;
        }

        free_running_sounds(&data.running_sounds);

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
parse_time(const char *str, double *value)
{
        char *tail;

        errno = 0;

        *value = strtod(optarg, &tail);

        return (errno == 0 &&
                (isnormal(*value) || *value == 0.0) &&
                *tail == '\0' &&
                *value >= 0.0);
}

static bool
process_options(int argc, char **argv, struct config *config)
{
        struct sound sound_template = default_sound;

        char *tail;

        while (true) {
                switch (getopt(argc, argv, "-s:v:m:S:E:")) {
                case 's':
                        if (!parse_time(optarg, &sound_template.start_time)) {
                                fprintf(stderr,
                                        "invalid start_time: %s\n",
                                        optarg);
                        }
                        break;

                case 'S':
                        if (!parse_time(optarg, &config->music_start_time)) {
                                fprintf(stderr,
                                        "invalid music_start_time: %s\n",
                                        optarg);
                        }
                        break;

                case 'E':
                        if (!parse_time(optarg, &config->music_end_time)) {
                                fprintf(stderr,
                                        "invalid music_end_time: %s\n",
                                        optarg);
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
                                        "invalid volume: %s\n",
                                        optarg);
                                return false;
                        }
                        break;

                case 'm': {
                        struct sound *sound = flt_alloc(sizeof *sound);

                        *sound = sound_template;

                        flt_list_insert(config->music.prev, &sound->link);

                        sound->filename = flt_strdup(optarg);

                        if (!get_sound_length(sound->filename,
                                              &sound->length))
                                return false;

                        sound_template = default_sound;

                        break;
                }

                case 1: {
                        struct sound *sound = flt_alloc(sizeof *sound);

                        *sound = sound_template;

                        flt_list_insert(config->sounds.prev, &sound->link);

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

static double
get_sound_end_time(const struct flt_list *sounds)
{
        const struct sound *sound;

        double end_time = 0.0;

        flt_list_for_each(sound, sounds, link) {
                double this_end_time = sound->start_time + sound->length;

                if (this_end_time > end_time)
                        end_time = this_end_time;
        }

        return end_time;
}

int
main(int argc, char **argv)
{
        struct config config = {
                .music_start_time = 0.0,
                .music_end_time = -1.0,
        };

        flt_list_init(&config.sounds);
        flt_list_init(&config.music);

        int ret = EXIT_SUCCESS;

        if (!process_options(argc, argv, &config)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        if (config.music_end_time < 0.0)
                config.music_end_time = get_sound_end_time(&config.sounds);

        sort_sounds(&config.sounds);

        if (!write_sounds(&config)) {
                ret = EXIT_FAILURE;
                goto out;
        }

out:
        free_sounds(&config.sounds);
        free_sounds(&config.music);

        return ret;
}
