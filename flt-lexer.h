/*
 * Flootay – a video overlay generator
 * Copyright (C) 2021, 2022  Neil Roberts
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

#ifndef FLT_LEXER
#define FLT_LEXER

#include "flt-error.h"
#include "flt-source.h"

extern struct flt_error_domain
flt_lexer_error;

enum flt_lexer_error {
        FLT_LEXER_ERROR_INVALID_STRING,
        FLT_LEXER_ERROR_INVALID_SYMBOL,
        FLT_LEXER_ERROR_INVALID_NUMBER,
        FLT_LEXER_ERROR_INVALID_FLOAT,
        FLT_LEXER_ERROR_UNEXPECTED_CHAR,
};

enum flt_lexer_keyword {
        FLT_LEXER_KEYWORD_RECTANGLE = 1,
        FLT_LEXER_KEYWORD_SVG,
        FLT_LEXER_KEYWORD_KEY_FRAME,
        FLT_LEXER_KEYWORD_VIDEO_WIDTH,
        FLT_LEXER_KEYWORD_VIDEO_HEIGHT,
        FLT_LEXER_KEYWORD_X,
        FLT_LEXER_KEYWORD_Y,
        FLT_LEXER_KEYWORD_X1,
        FLT_LEXER_KEYWORD_Y1,
        FLT_LEXER_KEYWORD_X2,
        FLT_LEXER_KEYWORD_Y2,
        FLT_LEXER_KEYWORD_FILE,

        FLT_LEXER_N_KEYWORDS,
};

enum flt_lexer_token_type {
        FLT_LEXER_TOKEN_TYPE_OPEN_BRACKET,
        FLT_LEXER_TOKEN_TYPE_CLOSE_BRACKET,
        FLT_LEXER_TOKEN_TYPE_SYMBOL,
        FLT_LEXER_TOKEN_TYPE_STRING,
        FLT_LEXER_TOKEN_TYPE_NUMBER,
        FLT_LEXER_TOKEN_TYPE_FLOAT,
        FLT_LEXER_TOKEN_TYPE_EOF,
};

#define FLT_LEXER_FRACTION_RANGE 1000000000

struct flt_lexer_token {
        enum flt_lexer_token_type type;

        union {
                struct {
                        long number_value;
                        /* The fractional part of the number
                         * multiplied by FLT_LEXER_FRACTION_RANGE.
                         * This will be negative if the float is
                         * negative.
                         */
                        long fraction;
                };
                unsigned symbol_value;
                const char *string_value;
        };
};

struct flt_lexer *
flt_lexer_new(struct flt_source *source);

const struct flt_lexer_token *
flt_lexer_get_token(struct flt_lexer *lexer,
                    struct flt_error **error);

void
flt_lexer_put_token(struct flt_lexer *lexer);

int
flt_lexer_get_line_num(struct flt_lexer *lexer);

const char *
flt_lexer_get_symbol_name(struct flt_lexer *lexer,
                          int symbol_num);

void
flt_lexer_free(struct flt_lexer *lexer);

#endif /* FLT_LEXER */
