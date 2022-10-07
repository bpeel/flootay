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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <strings.h>
#include <limits.h>

#include "flt-lexer.h"
#include "flt-list.h"

struct load_data {
        struct flt_source source;
        const char *data;
        size_t pos, size;
};

struct fail_check {
        const char *source;
        enum flt_lexer_error error_code;
        const char *error_message;
};

struct number {
        long number_value;
        long fraction;
        bool is_float;
};

struct number_check {
        const char *source;
        struct number expected;
};

static const struct fail_check
fail_checks[] = {
        {
                "0:",
                FLT_LEXER_ERROR_INVALID_NUMBER,
                "line 1: Invalid number “0:”"
        },
        {
                "-9223372036854775809",
                FLT_LEXER_ERROR_INVALID_NUMBER,
                "line 1: Invalid number “-9223372036854775809”"
        },
        {
                "9223372036854775808",
                FLT_LEXER_ERROR_INVALID_NUMBER,
                "line 1: Invalid number “9223372036854775808”"
        },
        {
                "153722867280912930:8",
                FLT_LEXER_ERROR_INVALID_NUMBER,
                "line 1: Invalid number “153722867280912930:8”"
        },
        {
                "153722867280912931:0",
                FLT_LEXER_ERROR_INVALID_NUMBER,
                "line 1: Invalid number “153722867280912931:0”"
        },
        {
                "1::0",
                FLT_LEXER_ERROR_INVALID_NUMBER,
                "line 1: Invalid number “1::0”"
        },
        {
                "0.12:",
                FLT_LEXER_ERROR_INVALID_FLOAT,
                "line 1: Invalid float “0.12:”"
        },
        {
                "1ĉ",
                FLT_LEXER_ERROR_INVALID_NUMBER,
                "line 1: Invalid number “1ĉ”"
        },
        {
                "0:18446744073709551616",
                FLT_LEXER_ERROR_INVALID_NUMBER,
                "line 1: Invalid number “0:18446744073709551616”"
        },
        {
                "0x50x5",
                FLT_LEXER_ERROR_INVALID_NUMBER,
                "line 1: Invalid number “0x50x5”",
        },
};

static const struct number_check
number_checks[] = {
        {
                "0",
                { 0 }
        },
        {
                "0.999999999",
                { 0, 999999999, true }
        },
        {
                "-0.999999999",
                { 0, -999999999, true }
        },
        {
                "-128.123456789",
                { -128, -123456789, true }
        },
        {
                "0.1234567899",
                { 0, 123456789, true }
        },
        {
                "-9223372036854775808",
                { LONG_MIN }
        },
        {
                "9223372036854775807",
                { LONG_MAX }
        },
        {
                "0:0:9223372036854775807",
                { LONG_MAX }
        },
        {
                "153722867280912930:7",
                { LONG_MAX }
        },
        {
                "1:2:3:4",
                { 223384 }
        },
        {
                "-1:2:3:4",
                { -223384 }
        },
        {
                "1:34.12",
                { 94, 120000000, true }
        },
        {
                "-1:34.12",
                { -94, -120000000, true }
        },
        {
                "010",
                { 10 }
        },
        {
                "-010",
                { -10 }
        },
        {
                "0x10",
                { 16 }
        },
        {
                "-0x10",
                { -16 }
        },
        {
                "0x7fffffffffffffff",
                { LONG_MAX }
        },
        {
                "-0x8000000000000000",
                { LONG_MIN }
        },
        {
                "0X10",
                { 16 }
        },
        {
                "0x0123456789abcdef",
                { 0x0123456789abcdef }
        },
};

static bool
read_source_cb(struct flt_source *source,
               void *ptr,
               size_t *length,
               struct flt_error **error)
{
        struct load_data *data =
                flt_container_of(source, struct load_data, source);

        if (*length + data->pos > data->size)
                *length = data->size - data->pos;

        memcpy(ptr, data->data + data->pos, *length);

        data->pos += *length;

        return true;
}

static bool
load_number_from_string(const char *str,
                        struct number *number)
{
        struct load_data data = {
                .source = {
                        .read_source = read_source_cb,
                },
                .data = str,
                .pos = 0,
                .size = strlen(str),
        };

        struct flt_lexer *lexer = flt_lexer_new(&data.source);

        struct flt_error *error = NULL;
        const struct flt_lexer_token *token =
                flt_lexer_get_token(lexer, &error);
        bool ret = true;

        if (token == NULL) {
                fprintf(stderr,
                        "unexpected error while parsing “%s”: %s\n",
                        str,
                        error->message);
                flt_error_free(error);
                ret = false;
        } else {
                switch (token->type) {
                case FLT_LEXER_TOKEN_TYPE_NUMBER:
                        number->is_float = false;
                        number->number_value = token->number_value;
                        number->fraction = LONG_MIN;
                        break;

                case FLT_LEXER_TOKEN_TYPE_FLOAT:
                        number->is_float = true;
                        number->number_value = token->number_value;
                        number->fraction = token->fraction;
                        break;

                default:
                        fprintf(stderr,
                                "expected number or float token while parsing "
                                "“%s” but got %i\n",
                                str,
                                token->type);
                        ret = false;
                        break;
                }
        }

        flt_lexer_free(lexer);

        return ret;
}

static void
print_number(FILE *out, const struct number *number)
{
        fprintf(out, "%li", number->number_value);

        if (number->is_float) {
                long fraction = labs(number->fraction);
                fputc('.', out);

                do {
                        fputc(fraction /
                              (FLT_LEXER_FRACTION_RANGE / 10) %
                              10 +
                              '0',
                              out);
                        fraction = (fraction * 10) % FLT_LEXER_FRACTION_RANGE;
                } while (fraction);
        }
}

static bool
run_number_check(const struct number_check *check)
{
        struct number number;

        if (!load_number_from_string(check->source, &number))
                return false;

        if (number.is_float &&
            number.number_value != 0 &&
            (number.number_value < 0) != (number.fraction < 0)) {
                fprintf(stderr,
                        "Number value and fraction value do not have the same "
                        "sign for “%s”: %li %li\n",
                        check->source,
                        number.number_value,
                        number.fraction);
                return false;
        }

        if (labs(number.fraction) >= FLT_LEXER_FRACTION_RANGE) {
                fprintf(stderr,
                        "Fractional part for “%s” is greater than 1: %li %li\n",
                        check->source,
                        number.number_value,
                        number.fraction);
                return false;
        }

        if (number.is_float != check->expected.is_float ||
            number.number_value != check->expected.number_value ||
            (number.is_float &&
             number.fraction != check->expected.fraction)) {
                fprintf(stderr,
                        "Number for “%s” not as expected.\n"
                        "Expected: ",
                        check->source);
                print_number(stderr, &check->expected);
                fprintf(stderr,
                        "\n"
                        "Received: ");
                print_number(stderr, &number);
                fputc('\n', stderr);
                return false;
        }

        return true;
}

static bool
run_fail_check(const struct fail_check *check)
{
        struct load_data data = {
                .source = {
                        .read_source = read_source_cb,
                },
                .data = check->source,
                .pos = 0,
                .size = strlen(check->source),
        };

        struct flt_lexer *lexer = flt_lexer_new(&data.source);

        struct flt_error *error = NULL;
        const struct flt_lexer_token *token =
                flt_lexer_get_token(lexer, &error);
        bool ret = true;

        if (token) {
                fprintf(stderr,
                        "Expected error message but got token:\n"
                        "%s\n",
                        check->source);
                ret = false;
        } else {
                assert(error);

                if (error->domain != &flt_lexer_error) {
                        fprintf(stderr,
                                "Returned error is not a lexer error:\n"
                                "%s\n"
                                "%s\n",
                                error->message,
                                check->source);
                        ret = false;
                } else if (error->code != check->error_code) {
                        fprintf(stderr,
                                "Expected error message code %i but "
                                "received %i:\n"
                                "%s\n"
                                "%s\n",
                                check->error_code,
                                error->code,
                                error->message,
                                check->source);
                        ret = false;
                } else if (strcmp(error->message, check->error_message)) {
                        fprintf(stderr,
                                "Error message does not match expected:\n"
                                "  Expected: %s\n"
                                "  Received: %s\n"
                                "\n"
                                "Source:\n"
                                "%s\n",
                                check->error_message,
                                error->message,
                                check->source);
                        ret = false;
                }

                flt_error_free(error);
        }

        flt_lexer_free(lexer);

        return ret;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        for (unsigned i = 0; i < FLT_N_ELEMENTS(fail_checks); i++) {
                if (!run_fail_check(fail_checks + i))
                        ret = EXIT_FAILURE;
        }

        for (unsigned i = 0; i < FLT_N_ELEMENTS(number_checks); i++) {
                if (!run_number_check(number_checks + i))
                        ret = EXIT_FAILURE;
        }

        return ret;
}
