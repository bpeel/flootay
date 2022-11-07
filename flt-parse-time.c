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

#include "flt-parse-time.h"

#include <time.h>
#include <string.h>

struct flt_error_domain
flt_parse_time_error;

static bool
is_space(char ch)
{
        return ch && strchr(" \n\r\t", ch) != NULL;
}

static int
parse_digits(const char *digits,
             int n_digits)
{
        int value = 0;

        for (int i = 0; i < n_digits; i++) {
                if (digits[i] < '0' || digits[i] > '9')
                        return -1;

                value = (value * 10) + digits[i] - '0';
        }

        return value;
}

bool
flt_parse_time(const char *time_str,
               double *time_out,
               struct flt_error **error)
{
        while (is_space(*time_str))
                time_str++;

        int year = parse_digits(time_str, 4);
        time_str += 4;
        if (year == -1 || *(time_str++) != '-')
                goto fail;

        int month = parse_digits(time_str, 2);
        time_str += 2;
        if (month == -1 || *(time_str++) != '-')
                goto fail;

        int day = parse_digits(time_str, 2);
        time_str += 2;
        if (day == -1 || *(time_str++) != 'T')
                goto fail;

        int hour = parse_digits(time_str, 2);
        time_str += 2;
        if (hour == -1 || *(time_str++) != ':')
                goto fail;

        int minute = parse_digits(time_str, 2);
        time_str += 2;
        if (minute == -1 || *(time_str++) != ':')
                goto fail;

        int second = parse_digits(time_str, 2);
        time_str += 2;
        if (second == -1)
                goto fail;

        int sub_second_divisor = 1;
        int sub_second_dividend = 0;

        if (*time_str == '.') {
                for (time_str++;
                     *time_str >= '0' && *time_str <= '9';
                     time_str++) {
                        sub_second_dividend = (sub_second_dividend * 10 +
                                               *time_str - '0');
                        sub_second_divisor *= 10;
                }
        }

        if (*time_str != 'Z') {
                flt_set_error(error,
                              &flt_parse_time_error,
                              FLT_PARSE_TIME_ERROR_INVALID_TIMEZONE,
                              "timezone is not Z");
                return false;
        }

        for (time_str++; is_space(*time_str); time_str++);

        if (*time_str != '\0')
                goto fail;

        struct tm tm = {
                .tm_sec = second,
                .tm_min = minute,
                .tm_hour = hour,
                .tm_mday = day,
                .tm_mon = month - 1,
                .tm_year = year - 1900,
                .tm_isdst = 0,
        };

        time_t t = timegm(&tm);

        if (t == (time_t) -1)
                goto fail;

        *time_out = t + sub_second_dividend / (double) sub_second_divisor;

        return true;

fail:
        flt_set_error(error,
                      &flt_parse_time_error,
                      FLT_PARSE_TIME_ERROR_INVALID,
                      "invalid time");
        return false;
}
