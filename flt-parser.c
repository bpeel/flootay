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

#include "flt-parser.h"

#include "flt-lexer.h"
#include "flt-utf8.h"
#include "flt-list.h"
#include "flt-buffer.h"

struct flt_error_domain
flt_parser_error;

enum flt_parser_return {
        FLT_PARSER_RETURN_OK,
        FLT_PARSER_RETURN_NOT_MATCHED,
        FLT_PARSER_RETURN_ERROR,
};

struct flt_parser {
        struct flt_lexer *lexer;

        struct flt_scene *scene;
};

typedef enum flt_parser_return
(* item_parse_func)(struct flt_parser *parser,
                    struct flt_error **error);

static void
set_verror(struct flt_parser *parser,
           struct flt_error **error,
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
                      &flt_parser_error,
                      FLT_PARSER_ERROR_INVALID,
                      "%s",
                      (const char *) buf.data);

        flt_buffer_destroy(&buf);
}

FLT_PRINTF_FORMAT(3, 4)
static void
set_error(struct flt_parser *parser,
          struct flt_error **error,
          const char *format,
          ...)
{
        va_list ap;

        va_start(ap, format);
        set_verror(parser,
                   error,
                   flt_lexer_get_line_num(parser->lexer),
                   format,
                   ap);
        va_end(ap);
}

FLT_PRINTF_FORMAT(4, 5)
static void
set_error_with_line(struct flt_parser *parser,
                    struct flt_error **error,
                    int line_num,
                    const char *format,
                    ...)
{
        va_list ap;

        va_start(ap, format);
        set_verror(parser, error, line_num, format, ap);
        va_end(ap);
}

static enum flt_parser_return
parse_items(struct flt_parser *parser,
            const item_parse_func *funcs,
            size_t n_funcs,
            struct flt_error **error)
{
        for (unsigned i = 0; i < n_funcs; i++) {
                enum flt_parser_return ret = funcs[i](parser, error);

                if (ret != FLT_PARSER_RETURN_NOT_MATCHED)
                        return ret;
        }

        return FLT_PARSER_RETURN_NOT_MATCHED;
}

static enum flt_parser_return
parse_key_frame(struct flt_parser *parser,
                struct flt_error **error)
{
        const struct flt_lexer_token *token;

        token = flt_lexer_get_token(parser->lexer, error);

        if (token == NULL)
                return FLT_PARSER_RETURN_ERROR;

        if (token->type != FLT_LEXER_TOKEN_TYPE_SYMBOL ||
            token->symbol_value != FLT_LEXER_KEYWORD_KEY_FRAME) {
                flt_lexer_put_token(parser->lexer);
                return FLT_PARSER_RETURN_NOT_MATCHED;
        }

        int key_frame_line_num = flt_lexer_get_line_num(parser->lexer);

        int parts[5];

        for (int i = 0; i < FLT_N_ELEMENTS(parts); i++) {
                token = flt_lexer_get_token(parser->lexer, error);

                if (token == NULL)
                        return FLT_PARSER_RETURN_ERROR;

                if (token->type != FLT_LEXER_TOKEN_TYPE_NUMBER) {
                        set_error(parser,
                                  error,
                                  "expected key_frame "
                                  "<frame_num> <x1> <y1> <x2> <y2>");
                        return FLT_PARSER_RETURN_ERROR;
                }

                parts[i] = token->number_value;
        }

        struct flt_scene_rectangle_key_frame *key_frame =
                flt_alloc(sizeof *key_frame);

        key_frame->num = parts[0];
        key_frame->x1 = parts[1];
        key_frame->y1 = parts[2];
        key_frame->x2 = parts[3];
        key_frame->y2 = parts[4];

        struct flt_scene_rectangle *rectangle =
                flt_container_of(parser->scene->rectangles.prev,
                                 struct flt_scene_rectangle,
                                 link);

        int last_frame_num = -1;

        if (!flt_list_empty(&rectangle->key_frames)) {
                const struct flt_scene_rectangle_key_frame *last_key_frame =
                        flt_container_of(rectangle->key_frames.prev,
                                         struct flt_scene_rectangle_key_frame,
                                         link);
                last_frame_num = last_key_frame->num;
        }

        flt_list_insert(rectangle->key_frames.prev, &key_frame->link);

        if (key_frame->num <= last_frame_num) {
                set_error_with_line(parser,
                                    error,
                                    key_frame_line_num,
                                    "frame numbers out of order");
                return FLT_PARSER_RETURN_ERROR;
        }

        return FLT_PARSER_RETURN_OK;
}

static enum flt_parser_return
parse_rectangle(struct flt_parser *parser,
                struct flt_error **error)
{
        const struct flt_lexer_token *token;

        token = flt_lexer_get_token(parser->lexer, error);

        if (token == NULL)
                return FLT_PARSER_RETURN_ERROR;

        if (token->type != FLT_LEXER_TOKEN_TYPE_SYMBOL ||
            token->symbol_value != FLT_LEXER_KEYWORD_RECTANGLE) {
                flt_lexer_put_token(parser->lexer);
                return FLT_PARSER_RETURN_NOT_MATCHED;
        }

        int rectangle_line_num = flt_lexer_get_line_num(parser->lexer);

        token = flt_lexer_get_token(parser->lexer, error);

        if (token == NULL)
                return FLT_PARSER_RETURN_ERROR;

        if (token->type != FLT_LEXER_TOKEN_TYPE_OPEN_BRACKET) {
                set_error(parser,
                          error,
                          "expected ‘{’");
                return FLT_PARSER_RETURN_ERROR;
        }

        struct flt_scene_rectangle *rectangle = flt_alloc(sizeof *rectangle);

        flt_list_init(&rectangle->key_frames);
        flt_list_insert(parser->scene->rectangles.prev, &rectangle->link);

        while (true) {
                token = flt_lexer_get_token(parser->lexer, error);

                if (token == NULL)
                        return FLT_PARSER_RETURN_ERROR;

                if (token->type == FLT_LEXER_TOKEN_TYPE_CLOSE_BRACKET)
                        break;

                flt_lexer_put_token(parser->lexer);

                static const item_parse_func funcs[] = {
                        parse_key_frame,
                };

                switch (parse_items(parser,
                                    funcs,
                                    FLT_N_ELEMENTS(funcs),
                                    error)) {
                case FLT_PARSER_RETURN_OK:
                        continue;
                case FLT_PARSER_RETURN_NOT_MATCHED:
                        break;
                case FLT_PARSER_RETURN_ERROR:
                        return FLT_PARSER_RETURN_ERROR;
                }

                set_error(parser,
                          error,
                          "Expected rectangle item (like a key_frame)");

                return FLT_PARSER_RETURN_ERROR;
        }

        if (flt_list_empty(&rectangle->key_frames)) {
                set_error_with_line(parser,
                                    error,
                                    rectangle_line_num,
                                    "rectangle has no key frames");
                return FLT_PARSER_RETURN_ERROR;
        }

        return FLT_PARSER_RETURN_OK;
}

static bool
parse_file(struct flt_parser *parser,
           struct flt_error **error)
{
        while (true) {
                const struct flt_lexer_token *token =
                        flt_lexer_get_token(parser->lexer, error);

                if (token == NULL)
                        return false;

                if (token->type == FLT_LEXER_TOKEN_TYPE_EOF)
                        break;

                flt_lexer_put_token(parser->lexer);

                static const item_parse_func funcs[] = {
                        parse_rectangle,
                };

                switch (parse_items(parser,
                                    funcs,
                                    FLT_N_ELEMENTS(funcs),
                                    error)) {
                case FLT_PARSER_RETURN_OK:
                        continue;
                case FLT_PARSER_RETURN_NOT_MATCHED:
                        break;
                case FLT_PARSER_RETURN_ERROR:
                        return false;
                }

                set_error(parser,
                          error,
                          "Expected file-level item (like an waypoint etc)");

                return false;
        }

        return true;
}

struct flt_scene *
flt_parser_parse(struct flt_source *source,
                 struct flt_error **error)
{
        struct flt_parser parser = {
                .lexer = flt_lexer_new(source),
                .scene = flt_scene_new(),
        };

        if (!parse_file(&parser, error)) {
                flt_scene_free(parser.scene);
                parser.scene = NULL;
        }

        flt_lexer_free(parser.lexer);

        return parser.scene;
}
