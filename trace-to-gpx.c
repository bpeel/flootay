/*
 * Flootay – a video overlay generator
 * Copyright (C) 2024  Neil Roberts
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
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "flt-trace.h"

static const char header[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.0\" creator=\"trace-to-gpx\" "
        "xmlns=\"http://www.topografix.com/GPX/1/0\">\n"
        "  <trk>\n";
static const char footer[] =
        "  </trk>\n"
        "</gpx>\n";

struct config {
        const char *trace_filename;
};

static bool
process_options(int argc, char **argv, struct config *config)
{
        config->trace_filename = NULL;

        while (true) {
                switch (getopt(argc, argv, "-t:")) {
                case 't':
                        config->trace_filename = optarg;
                        break;

                case 1:
                        fprintf(stderr, "unexpected argument: %s\n", optarg);
                        return false;

                case -1:
                        goto done;

                default:
                        return false;
                }
        }

done:
        if (config->trace_filename == NULL) {
                fprintf(stderr,
                        "usage: trace-to-gpx -t <trace_file>\n");
                return false;
        }

        return true;
}

static const char *
status_to_text(enum flt_trace_segment_status status)
{
        switch (status) {
        case FLT_TRACE_SEGMENT_STATUS_DONE:
                return "done";
        case FLT_TRACE_SEGMENT_STATUS_PLANNED:
                return "planned";
        case FLT_TRACE_SEGMENT_STATUS_TESTED:
                return "tested";
        case FLT_TRACE_SEGMENT_STATUS_POSTPONED:
                return "postponed";
        case FLT_TRACE_SEGMENT_STATUS_UNKNOWN:
                return "unknown";
        case FLT_TRACE_SEGMENT_STATUS_VARIANT:
                return "variant";
        case FLT_TRACE_SEGMENT_STATUS_VARIANT_POSTPONED:
                return "postponed variant";
        case FLT_TRACE_SEGMENT_STATUS_WIP:
                return "wip";
        }

        return NULL;
}

static void
dump_status(enum flt_trace_segment_status status)
{
        const char *name = status_to_text(status);

        if (name)
                printf("    <!-- %s -->\n", name);
}

static void
dump_segment(const struct flt_trace_segment *segment)
{
        dump_status(segment->status);

        fputs("    <trkseg>\n", stdout);

        for (size_t i = 0; i < segment->n_points; i++) {
                printf("      <trkpt lat=\"%f\" lon=\"%f\"></trkpt>\n",
                       segment->points[i].lat,
                       segment->points[i].lon);
        }

        fputs("    </trkseg>\n", stdout);
}

static void
dump_trace(const struct flt_trace *trace)
{
        fputs(header, stdout);

        for (size_t i = 0; i < trace->n_segments; i++)
                dump_segment(trace->segments + i);

        fputs(footer, stdout);
}

int
main(int argc, char **argv)
{
        struct config config;

        if (!process_options(argc, argv, &config))
                return EXIT_FAILURE;

        struct flt_error *error = NULL;
        struct flt_trace *trace = flt_trace_parse(config.trace_filename,
                                                  &error);

        if (trace == NULL) {
                fprintf(stderr, "%s\n", error->message);
                flt_error_free(error);
                return EXIT_FAILURE;
        }

        dump_trace(trace);

        flt_trace_free(trace);

        return EXIT_SUCCESS;
}
