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

#include "flt-lexer.h"

#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "flt-buffer.h"
#include "flt-utf8.h"

struct flt_error_domain
flt_lexer_error;

enum flt_lexer_state {
        FLT_LEXER_STATE_SKIPPING_WHITESPACE,
        FLT_LEXER_STATE_SKIPPING_COMMENT,
        FLT_LEXER_STATE_READING_NUMBER,
        FLT_LEXER_STATE_READING_STRING,
        FLT_LEXER_STATE_READING_STRING_ESCAPE,
        FLT_LEXER_STATE_READING_SYMBOL,
};

struct token_data {
        struct flt_lexer_token token;
        /* Each token in the token queue has its own buffer. This is
         * necessary because the buffer will be used directly for the
         * string value of the token so we can’t override it if we
         * start reading another token.
         */
        struct flt_buffer buffer;
        /* Line number that the token was parsed at so that if we go
         * back to it we can reset the lexer line number.
         */
        int line_num;
};

#define TOKEN_QUEUE_SIZE 3

struct flt_lexer {
        int line_num;
        struct flt_source *source;

        enum flt_lexer_state state;

        struct token_data token_queue[TOKEN_QUEUE_SIZE];
        /* Every time we read a new token we will store it at the next
         * circular position in the queue so that the last tokens read
         * are always available to be ready to be put back on the
         * queue.
         */
        int queue_start;
        /* If a token has been put back then this will be > 0. */
        int n_put_tokens;

        bool had_eof;

        uint8_t buf[128];
        int buf_pos;
        int buf_size;

        /* Array of char* */
        struct flt_buffer symbols;

        int string_start_line;
};

static const char * const
keywords[] = {
        [FLT_LEXER_KEYWORD_RECTANGLE] = "rectangle",
};

_Static_assert(FLT_N_ELEMENTS(keywords) == FLT_LEXER_N_KEYWORDS,
               "Keyword is a missing a name");

static void
set_verror(struct flt_lexer *lexer,
           struct flt_error **error,
           enum flt_lexer_error code,
           int line_num,
           const char *format,
           va_list ap)
{
        struct flt_buffer buf = FLT_BUFFER_STATIC_INIT;

        flt_buffer_append_printf(&buf,
                                 "line %i: ",
                                 line_num);

        flt_buffer_append_vprintf(&buf, format, ap);

        flt_set_error(error,
                      &flt_lexer_error,
                      code,
                      "%s",
                      (const char *) buf.data);

        flt_buffer_destroy(&buf);
}

FLT_PRINTF_FORMAT(4, 5)
static void
set_error(struct flt_lexer *lexer,
           struct flt_error **error,
           enum flt_lexer_error code,
           const char *format,
           ...)
{
        va_list ap;

        va_start(ap, format);
        set_verror(lexer, error, code, lexer->line_num, format, ap);
        va_end(ap);
}

FLT_PRINTF_FORMAT(5, 6)
static void
set_error_with_line(struct flt_lexer *lexer,
                    struct flt_error **error,
                    enum flt_lexer_error code,
                    int line_num,
                    const char *format,
                    ...)
{
        va_list ap;

        va_start(ap, format);
        set_verror(lexer, error, code, line_num, format, ap);
        va_end(ap);
}

static bool
get_character(struct flt_lexer *lexer,
              int *ch,
              struct flt_error **error)
{
        if (lexer->buf_pos >= lexer->buf_size) {
                if (lexer->had_eof) {
                        *ch = -1;
                        return true;
                }

                size_t length = sizeof lexer->buf;

                if (!lexer->source->read_source(lexer->source,
                                                lexer->buf,
                                                &length,
                                                error))
                        return false;

                lexer->had_eof = length < sizeof lexer->buf;
                lexer->buf_size = length;
                lexer->buf_pos = 0;

                if (length <= 0) {
                        *ch = -1;
                        return true;
                }
        }

        *ch = lexer->buf[lexer->buf_pos++];
        return true;
}

static void
put_character(struct flt_lexer *lexer, int ch)
{
        if (ch == -1) {
                assert(lexer->had_eof);
                assert(lexer->buf_pos >= lexer->buf_size);
                return;
        }

        assert(lexer->buf_pos > 0);

        if (ch == '\n')
                lexer->line_num--;

        lexer->buf[--lexer->buf_pos] = ch;
}

struct flt_lexer *
flt_lexer_new(struct flt_source *source)
{
        struct flt_lexer *lexer = flt_alloc(sizeof *lexer);

        lexer->source = source;
        lexer->line_num = 1;
        lexer->had_eof = false;
        lexer->buf_pos = 0;
        lexer->buf_size = 0;
        lexer->queue_start = 0;
        lexer->n_put_tokens = 0;
        lexer->state = FLT_LEXER_STATE_SKIPPING_WHITESPACE;
        flt_buffer_init(&lexer->symbols);

        for (int i = 0; i < TOKEN_QUEUE_SIZE; i++)
                flt_buffer_init(&lexer->token_queue[i].buffer);

        return lexer;
}

static bool
is_space(char ch)
{
        return ch && strchr(" \n\r\t", ch) != NULL;
}

static bool
normalize_string(struct flt_lexer *lexer,
                 char *str,
                 struct flt_error **error)
{
        char *src = str, *dst = str;

        enum {
                start,
                had_space,
                had_newline,
                had_other,
        } state = start;

        int newline_count = 0;

        while (*src) {
                switch (state) {
                case start:
                        if (!is_space(*src)) {
                                *(dst++) = *src;
                                state = had_other;
                        }
                        break;
                case had_space:
                        if (*src == '\n') {
                                state = had_newline;
                                newline_count = 1;
                        } else if (!is_space(*src)) {
                                *(dst++) = ' ';
                                *(dst++) = *src;
                                state = had_other;
                        }
                        break;
                case had_newline:
                        if (*src == '\n') {
                                newline_count++;
                        } else if (!is_space(*src)) {
                                if (newline_count == 1) {
                                        *(dst++) = ' ';
                                } else {
                                        for (int i = 0; i < newline_count; i++)
                                                *(dst++) = '\n';
                                }
                                *(dst++) = *src;
                                state = had_other;
                        }
                        break;
                case had_other:
                        if (*src == '\n') {
                                state = had_newline;
                                newline_count = 1;
                        } else if (is_space(*src)) {
                                state = had_space;
                        } else {
                                *(dst++) = *src;
                        }
                        break;
                }

                src++;
        }

        *dst = '\0';

        if (!flt_utf8_is_valid_string(str)) {
                set_error(lexer,
                          error,
                          FLT_LEXER_ERROR_INVALID_STRING,
                          "String contains invalid UTF-8");
                return false;
        }

        return true;
}

static bool
find_symbol(struct flt_lexer *lexer,
            const char *str,
            struct flt_lexer_token *token,
            struct flt_error **error)
{
        if (!flt_utf8_is_valid_string(str)) {
                set_error(lexer,
                          error,
                          FLT_LEXER_ERROR_INVALID_SYMBOL,
                          "Invalid UTF-8 encountered");
                return false;
        }

        for (size_t i = 1; i < FLT_LEXER_N_KEYWORDS; i++) {
                if (!strcmp(keywords[i], str)) {
                        token->type = FLT_LEXER_TOKEN_TYPE_SYMBOL;
                        token->symbol_value = i;
                        return true;
                }
        }

        char **symbols = (char **) lexer->symbols.data;
        size_t n_symbols = lexer->symbols.length / sizeof (char *);

        for (size_t i = 0; i < n_symbols; i++) {
                if (!strcmp(symbols[i], str)) {
                        token->type = FLT_LEXER_TOKEN_TYPE_SYMBOL;
                        token->symbol_value = i + FLT_LEXER_N_KEYWORDS;
                        return true;
                }
        }

        char *symbol = flt_strdup(str);
        flt_buffer_append(&lexer->symbols, &symbol, sizeof symbol);

        token->type = FLT_LEXER_TOKEN_TYPE_SYMBOL;
        token->symbol_value = n_symbols + FLT_LEXER_N_KEYWORDS;

        return true;
}

void
flt_lexer_put_token(struct flt_lexer *lexer)
{
        assert(lexer->n_put_tokens < TOKEN_QUEUE_SIZE);
        lexer->n_put_tokens++;
}

static bool
parse_number(struct flt_lexer *lexer,
             const char *str,
             struct flt_lexer_token *token,
             struct flt_error **error)
{
        char *tail;

        errno = 0;

        token->number_value = strtol(str, &tail, 10);

        if (errno || (*tail && *tail != '.')) {
                set_error(lexer,
                          error,
                          FLT_LEXER_ERROR_INVALID_NUMBER,
                          "Invalid number “%s”",
                          str);
                return false;
        }

        if (*tail == '.') {
                long multiplier = FLT_LEXER_FRACTION_RANGE;
                long fraction = 0;

                for (const char *p = tail + 1; *p; p++) {
                        if (*p < '0' || *p > '9') {
                                set_error(lexer,
                                          error,
                                          FLT_LEXER_ERROR_INVALID_FLOAT,
                                          "Invalid float “%s”",
                                          str);
                                return false;
                        }

                        multiplier /= 10;

                        fraction += (*p - '0') * multiplier;
                }

                if (*str == '-')
                        fraction = -fraction;

                token->type = FLT_LEXER_TOKEN_TYPE_FLOAT;
                token->fraction = fraction;
        } else {
                token->type = FLT_LEXER_TOKEN_TYPE_NUMBER;
        }

        return true;
}

const struct flt_lexer_token *
flt_lexer_get_token(struct flt_lexer *lexer,
                    struct flt_error **error)
{
        if (lexer->n_put_tokens > 0) {
                struct token_data *token_data =
                        lexer->token_queue + ((lexer->queue_start +
                                               TOKEN_QUEUE_SIZE -
                                               lexer->n_put_tokens) %
                                              TOKEN_QUEUE_SIZE);

                lexer->line_num = token_data->line_num;
                lexer->n_put_tokens--;

                return &token_data->token;
        }

        const char *str;
        struct token_data *token_data = lexer->token_queue + lexer->queue_start;
        struct flt_buffer *buf = &token_data->buffer;
        struct flt_lexer_token *token = &token_data->token;

        lexer->queue_start = (lexer->queue_start + 1) % TOKEN_QUEUE_SIZE;

        while (true) {
                int ch;

                if (!get_character(lexer, &ch, error))
                        return NULL;

                if (ch == '\n')
                        lexer->line_num++;

                switch (lexer->state) {
                case FLT_LEXER_STATE_SKIPPING_WHITESPACE:
                        if (is_space(ch))
                                break;

                        token_data->line_num = lexer->line_num;
                        flt_buffer_set_length(buf, 0);

                        if ((ch >= '0' && ch <= '9') || ch == '-') {
                                put_character(lexer, ch);
                                lexer->state = FLT_LEXER_STATE_READING_NUMBER;
                        } else if ((ch >= 'a' && ch <= 'z') ||
                                   (ch >= 'A' && ch <= 'Z') ||
                                   ch >= 0x80 ||
                                   ch == '_') {
                                put_character(lexer, ch);
                                lexer->state = FLT_LEXER_STATE_READING_SYMBOL;
                        } else if (ch == '"') {
                                lexer->state = FLT_LEXER_STATE_READING_STRING;
                                lexer->string_start_line = lexer->line_num;
                        } else if (ch == '{') {
                                token->type = FLT_LEXER_TOKEN_TYPE_OPEN_BRACKET;
                                return token;
                        } else if (ch == '}') {
                                token->type =
                                        FLT_LEXER_TOKEN_TYPE_CLOSE_BRACKET;
                                return token;
                        } else if (ch == '#') {
                                lexer->state = FLT_LEXER_STATE_SKIPPING_COMMENT;
                        } else if (ch == -1) {
                                token->type = FLT_LEXER_TOKEN_TYPE_EOF;
                                return token;
                        } else {
                                set_error(lexer,
                                          error,
                                          FLT_LEXER_ERROR_UNEXPECTED_CHAR,
                                          "Unexpected character ‘%c’",
                                          ch);
                                return NULL;
                        }
                        break;

                case FLT_LEXER_STATE_READING_NUMBER:
                        if ((ch >= '0' && ch <= '9') ||
                            (ch >= 'a' && ch <= 'z') ||
                            (ch >= 'A' && ch <= 'Z') ||
                            ch == '.' ||
                            ch >= 0x80 ||
                            (buf->length == 0 && ch == '-')) {
                                flt_buffer_append_c(buf, ch);
                                break;
                        } else if (ch == '_') {
                                break;
                        }

                        put_character(lexer, ch);

                        flt_buffer_append_c(buf, '\0');

                        str = (const char *) buf->data;

                        if (!parse_number(lexer, str, token, error))
                                return NULL;

                        lexer->state = FLT_LEXER_STATE_SKIPPING_WHITESPACE;
                        return token;

                case FLT_LEXER_STATE_READING_SYMBOL:
                        if ((ch >= '0' && ch <= '9') ||
                            (ch >= 'a' && ch <= 'z') ||
                            (ch >= 'A' && ch <= 'Z') ||
                            ch >= 0x80 || ch == '_') {
                                flt_buffer_append_c(buf, ch);
                                break;
                        }

                        put_character(lexer, ch);
                        flt_buffer_append_c(buf, '\0');
                        str = (const char *) buf->data;

                        if (!find_symbol(lexer, str, token, error))
                                return NULL;

                        lexer->state = FLT_LEXER_STATE_SKIPPING_WHITESPACE;
                        return token;

                case FLT_LEXER_STATE_READING_STRING:
                        if (ch == '\\') {
                                lexer->state =
                                        FLT_LEXER_STATE_READING_STRING_ESCAPE;
                                break;
                        } else if (ch == -1) {
                                set_error_with_line
                                        (lexer,
                                         error,
                                         FLT_LEXER_ERROR_INVALID_STRING,
                                         lexer->string_start_line,
                                         "Unterminated string");
                                return NULL;
                        } else if (ch != '"') {
                                flt_buffer_append_c(buf, ch);
                                break;
                        }

                        flt_buffer_append_c(buf, '\0');

                        if (!normalize_string(lexer, (char *) buf->data, error))
                                return NULL;

                        token_data->line_num = lexer->line_num;

                        lexer->state = FLT_LEXER_STATE_SKIPPING_WHITESPACE;
                        token->type = FLT_LEXER_TOKEN_TYPE_STRING;
                        token->string_value = (const char *) buf->data;
                        return token;

                case FLT_LEXER_STATE_READING_STRING_ESCAPE:
                        if (ch == '"' || ch == '\\') {
                                flt_buffer_append_c(buf, ch);
                                lexer->state = FLT_LEXER_STATE_READING_STRING;
                                break;
                        }
                        set_error(lexer,
                                  error,
                                  FLT_LEXER_ERROR_INVALID_NUMBER,
                                  "Invalid escape sequence");
                        return NULL;

                case FLT_LEXER_STATE_SKIPPING_COMMENT:
                        if (ch == '\n') {
                                lexer->state =
                                        FLT_LEXER_STATE_SKIPPING_WHITESPACE;
                        }
                        break;
                }
        }
}

int
flt_lexer_get_line_num(struct flt_lexer *lexer)
{
        return lexer->line_num;
}

const char *
flt_lexer_get_symbol_name(struct flt_lexer *lexer,
                          int symbol_num)
{
        if (symbol_num < FLT_LEXER_N_KEYWORDS) {
                return keywords[symbol_num];
        } else {
                char **symbols = (char **) lexer->symbols.data;

                return symbols[symbol_num - FLT_LEXER_N_KEYWORDS];
        }
}

void
flt_lexer_free(struct flt_lexer *lexer)
{
        char **symbols = (char **) lexer->symbols.data;
        size_t n_symbols = lexer->symbols.length / sizeof (char *);

        for (size_t i = 0; i < n_symbols; i++)
                flt_free(symbols[i]);

        flt_buffer_destroy(&lexer->symbols);

        for (int i = 0; i < TOKEN_QUEUE_SIZE; i++)
                flt_buffer_destroy(&lexer->token_queue[i].buffer);

        flt_free(lexer);
}
