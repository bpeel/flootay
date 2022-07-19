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

#include <limits.h>
#include <string.h>

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

        const char *base_dir;

        struct flt_scene *scene;
};

enum flt_parser_value_type {
        FLT_PARSER_VALUE_TYPE_STRING,
        FLT_PARSER_VALUE_TYPE_INT,
        FLT_PARSER_VALUE_TYPE_BOOL,
};

typedef enum flt_parser_return
(* item_parse_func)(struct flt_parser *parser,
                    struct flt_error **error);

struct flt_parser_property {
        size_t offset;
        enum flt_parser_value_type value_type;
        enum flt_lexer_keyword prop_keyword;
        union {
                struct {
                        long min_value, max_value;
                };
        };
};

#define check_item_keyword(parser, keyword, error)                      \
        do {                                                            \
                token = flt_lexer_get_token((parser)->lexer, (error));  \
                                                                        \
                if (token == NULL)                                      \
                        return FLT_PARSER_RETURN_ERROR;                 \
                                                                        \
                if (token->type != FLT_LEXER_TOKEN_TYPE_SYMBOL ||       \
                    token->symbol_value != (keyword)) {                 \
                        flt_lexer_put_token(parser->lexer);             \
                        return FLT_PARSER_RETURN_NOT_MATCHED;           \
                }                                                       \
        } while (0)

#define require_token(parser, token_type, msg, error)                   \
        do {                                                            \
                token = flt_lexer_get_token((parser)->lexer, (error));  \
                                                                        \
                if (token == NULL)                                      \
                        return FLT_PARSER_RETURN_ERROR;                 \
                                                                        \
                if (token->type != (token_type)) {                      \
                        set_error(parser,                               \
                                  error,                                \
                                  "%s",                                 \
                                  (msg));                               \
                        return FLT_PARSER_RETURN_ERROR;                 \
                }                                                       \
        } while (0)

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

static void
set_multiple_property_values_error(struct flt_parser *parser,
                                   const struct flt_parser_property *prop,
                                   struct flt_error **error)
{
        const char *prop_name =
                flt_lexer_get_symbol_name(parser->lexer,
                                          prop->prop_keyword);

        set_error(parser,
                  error,
                  "The property “%s” is set more than once",
                  prop_name);
}

static enum flt_parser_return
parse_string_property(struct flt_parser *parser,
                      const struct flt_parser_property *prop,
                      void *object,
                      struct flt_error **error)
{
        const struct flt_lexer_token *token;

        check_item_keyword(parser, prop->prop_keyword, error);
        require_token(parser,
                      FLT_LEXER_TOKEN_TYPE_STRING,
                      "String expected",
                      error);

        char **field = (char **) (((uint8_t *) object) + prop->offset);

        if (*field) {
                set_multiple_property_values_error(parser, prop, error);
                return FLT_PARSER_RETURN_ERROR;
        }

        *field = flt_strdup(token->string_value);

        return FLT_PARSER_RETURN_OK;
}

static enum flt_parser_return
parse_int_property(struct flt_parser *parser,
                   const struct flt_parser_property *prop,
                   void *object,
                   struct flt_error **error)
{
        const struct flt_lexer_token *token;

        check_item_keyword(parser, prop->prop_keyword, error);
        require_token(parser,
                      FLT_LEXER_TOKEN_TYPE_NUMBER,
                      "Expected number",
                      error);

        unsigned *field = (unsigned *) (((uint8_t *) object) + prop->offset);

        if (token->number_value < prop->min_value ||
            token->number_value > prop->max_value) {
                set_error(parser,
                          error,
                          "Number is out of range");
                return FLT_PARSER_RETURN_ERROR;
        }

        *field = token->number_value;

        return FLT_PARSER_RETURN_OK;
}

static enum flt_parser_return
parse_bool_property(struct flt_parser *parser,
                    const struct flt_parser_property *prop,
                    void *object,
                    struct flt_error **error)
{
        const struct flt_lexer_token *token;

        check_item_keyword(parser, prop->prop_keyword, error);

        bool *field = (bool *) (((uint8_t *) object) + prop->offset);

        *field = true;

        return FLT_PARSER_RETURN_OK;
}

static enum flt_parser_return
parse_properties(struct flt_parser *parser,
                 const struct flt_parser_property *props,
                 size_t n_props,
                 void *object,
                 struct flt_error **error)
{
        for (unsigned i = 0; i < n_props; i++) {
                enum flt_parser_return ret = FLT_PARSER_RETURN_NOT_MATCHED;

                switch (props[i].value_type) {
                case FLT_PARSER_VALUE_TYPE_STRING:
                        ret = parse_string_property(parser,
                                                    props + i,
                                                    object,
                                                    error);
                        break;
                case FLT_PARSER_VALUE_TYPE_INT:
                        ret = parse_int_property(parser,
                                                 props + i,
                                                 object,
                                                 error);
                        break;
                case FLT_PARSER_VALUE_TYPE_BOOL:
                        ret = parse_bool_property(parser,
                                                  props + i,
                                                  object,
                                                  error);
                        break;
                }

                if (ret != FLT_PARSER_RETURN_NOT_MATCHED)
                        return ret;
        }

        return FLT_PARSER_RETURN_NOT_MATCHED;
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
parse_base_key_frame(struct flt_parser *parser,
                     size_t struct_size,
                     struct flt_scene_key_frame **key_frame_out,
                     struct flt_error **error)
{
        const struct flt_lexer_token *token;

        check_item_keyword(parser, FLT_LEXER_KEYWORD_KEY_FRAME, error);

        require_token(parser,
                      FLT_LEXER_TOKEN_TYPE_NUMBER,
                      "Frame number expected",
                      error);

        struct flt_scene_object *object =
                flt_container_of(parser->scene->objects.prev,
                                 struct flt_scene_object,
                                 link);

        struct flt_scene_key_frame *key_frame =
                flt_calloc(struct_size);

        int last_frame_num = -1;

        if (!flt_list_empty(&object->key_frames)) {
                const struct flt_scene_key_frame *last_key_frame =
                        flt_container_of(object->key_frames.prev,
                                         struct flt_scene_key_frame,
                                         link);

                memcpy(key_frame, last_key_frame, struct_size);

                last_frame_num = last_key_frame->num;
        }

        flt_list_insert(object->key_frames.prev, &key_frame->link);

        if (token->number_value <= last_frame_num) {
                set_error(parser,
                          error,
                          "frame numbers out of order");
                return FLT_PARSER_RETURN_ERROR;
        }

        key_frame->num = token->number_value;

        require_token(parser,
                      FLT_LEXER_TOKEN_TYPE_OPEN_BRACKET,
                      "expected ‘{’",
                      error);

        *key_frame_out = key_frame;

        return FLT_PARSER_RETURN_OK;
}

static const struct flt_parser_property
key_frame_props[] = {
        {
                offsetof(struct flt_scene_rectangle_key_frame, x1),
                FLT_PARSER_VALUE_TYPE_INT,
                FLT_LEXER_KEYWORD_X1,
                .min_value = INT_MIN, .max_value = INT_MAX,
        },
        {
                offsetof(struct flt_scene_rectangle_key_frame, y1),
                FLT_PARSER_VALUE_TYPE_INT,
                FLT_LEXER_KEYWORD_Y1,
                .min_value = INT_MIN, .max_value = INT_MAX,
        },
        {
                offsetof(struct flt_scene_rectangle_key_frame, x2),
                FLT_PARSER_VALUE_TYPE_INT,
                FLT_LEXER_KEYWORD_X2,
                .min_value = INT_MIN, .max_value = INT_MAX,
        },
        {
                offsetof(struct flt_scene_rectangle_key_frame, y2),
                FLT_PARSER_VALUE_TYPE_INT,
                FLT_LEXER_KEYWORD_Y2,
                .min_value = INT_MIN, .max_value = INT_MAX,
        },
};

static enum flt_parser_return
parse_rectangle_key_frame(struct flt_parser *parser,
                          struct flt_error **error)
{
        struct flt_scene_key_frame *base_key_frame;

        const size_t struct_size =
                sizeof (struct flt_scene_rectangle_key_frame);

        enum flt_parser_return base_ret =
                parse_base_key_frame(parser,
                                     struct_size,
                                     &base_key_frame,
                                     error);

        if (base_ret != FLT_PARSER_RETURN_OK)
                return base_ret;

        struct flt_scene_rectangle_key_frame *key_frame =
                (struct flt_scene_rectangle_key_frame *) base_key_frame;

        while (true) {
                const struct flt_lexer_token *token =
                        flt_lexer_get_token(parser->lexer, error);

                if (token == NULL)
                        return FLT_PARSER_RETURN_ERROR;

                if (token->type == FLT_LEXER_TOKEN_TYPE_CLOSE_BRACKET)
                        break;

                flt_lexer_put_token(parser->lexer);

                switch (parse_properties(parser,
                                         key_frame_props,
                                         FLT_N_ELEMENTS(key_frame_props),
                                         key_frame,
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
                          "Expected key_frame item (like x1, y1, x2, y2 etc)");

                return FLT_PARSER_RETURN_ERROR;
        }

        return FLT_PARSER_RETURN_OK;
}

static enum flt_parser_return
parse_rectangle(struct flt_parser *parser,
                struct flt_error **error)
{
        const struct flt_lexer_token *token;

        check_item_keyword(parser, FLT_LEXER_KEYWORD_RECTANGLE, error);

        int rectangle_line_num = flt_lexer_get_line_num(parser->lexer);

        require_token(parser,
                      FLT_LEXER_TOKEN_TYPE_OPEN_BRACKET,
                      "expected ‘{’",
                      error);

        struct flt_scene_rectangle *rectangle = flt_alloc(sizeof *rectangle);

        rectangle->base.type = FLT_SCENE_OBJECT_TYPE_RECTANGLE;

        flt_list_init(&rectangle->base.key_frames);
        flt_list_insert(parser->scene->objects.prev, &rectangle->base.link);

        while (true) {
                token = flt_lexer_get_token(parser->lexer, error);

                if (token == NULL)
                        return FLT_PARSER_RETURN_ERROR;

                if (token->type == FLT_LEXER_TOKEN_TYPE_CLOSE_BRACKET)
                        break;

                flt_lexer_put_token(parser->lexer);

                static const item_parse_func funcs[] = {
                        parse_rectangle_key_frame,
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

        if (flt_list_empty(&rectangle->base.key_frames)) {
                set_error_with_line(parser,
                                    error,
                                    rectangle_line_num,
                                    "rectangle has no key frames");
                return FLT_PARSER_RETURN_ERROR;
        }

        return FLT_PARSER_RETURN_OK;
}

static const struct flt_parser_property
score_key_frame_props[] = {
        {
                offsetof(struct flt_scene_score_key_frame, value),
                FLT_PARSER_VALUE_TYPE_INT,
                FLT_LEXER_KEYWORD_V,
                .min_value = INT_MIN, .max_value = INT_MAX,
        },
};

static enum flt_parser_return
parse_score_key_frame(struct flt_parser *parser,
                          struct flt_error **error)
{
        struct flt_scene_key_frame *base_key_frame;

        const size_t struct_size =
                sizeof (struct flt_scene_score_key_frame);

        enum flt_parser_return base_ret =
                parse_base_key_frame(parser,
                                     struct_size,
                                     &base_key_frame,
                                     error);

        if (base_ret != FLT_PARSER_RETURN_OK)
                return base_ret;

        struct flt_scene_score_key_frame *key_frame =
                (struct flt_scene_score_key_frame *) base_key_frame;

        while (true) {
                const struct flt_lexer_token *token =
                        flt_lexer_get_token(parser->lexer, error);

                if (token == NULL)
                        return FLT_PARSER_RETURN_ERROR;

                if (token->type == FLT_LEXER_TOKEN_TYPE_CLOSE_BRACKET)
                        break;

                flt_lexer_put_token(parser->lexer);

                switch (parse_properties(parser,
                                         score_key_frame_props,
                                         FLT_N_ELEMENTS(score_key_frame_props),
                                         key_frame,
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
                          "Expected key_frame item (like v)");

                return FLT_PARSER_RETURN_ERROR;
        }

        return FLT_PARSER_RETURN_OK;
}

static enum flt_parser_return
parse_score(struct flt_parser *parser,
                struct flt_error **error)
{
        const struct flt_lexer_token *token;

        check_item_keyword(parser, FLT_LEXER_KEYWORD_SCORE, error);

        int score_line_num = flt_lexer_get_line_num(parser->lexer);

        require_token(parser,
                      FLT_LEXER_TOKEN_TYPE_OPEN_BRACKET,
                      "expected ‘{’",
                      error);

        struct flt_scene_score *score = flt_alloc(sizeof *score);

        score->base.type = FLT_SCENE_OBJECT_TYPE_SCORE;

        flt_list_init(&score->base.key_frames);
        flt_list_insert(parser->scene->objects.prev, &score->base.link);

        while (true) {
                token = flt_lexer_get_token(parser->lexer, error);

                if (token == NULL)
                        return FLT_PARSER_RETURN_ERROR;

                if (token->type == FLT_LEXER_TOKEN_TYPE_CLOSE_BRACKET)
                        break;

                flt_lexer_put_token(parser->lexer);

                static const item_parse_func funcs[] = {
                        parse_score_key_frame,
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
                          "Expected score item (like a key_frame)");

                return FLT_PARSER_RETURN_ERROR;
        }

        if (flt_list_empty(&score->base.key_frames)) {
                set_error_with_line(parser,
                                    error,
                                    score_line_num,
                                    "score has no key frames");
                return FLT_PARSER_RETURN_ERROR;
        }

        return FLT_PARSER_RETURN_OK;
}

static const struct flt_parser_property
svg_key_frame_props[] = {
        {
                offsetof(struct flt_scene_svg_key_frame, x),
                FLT_PARSER_VALUE_TYPE_INT,
                FLT_LEXER_KEYWORD_X,
                .min_value = INT_MIN, .max_value = INT_MAX,
        },
        {
                offsetof(struct flt_scene_svg_key_frame, y),
                FLT_PARSER_VALUE_TYPE_INT,
                FLT_LEXER_KEYWORD_Y,
                .min_value = INT_MIN, .max_value = INT_MAX,
        },
};

static enum flt_parser_return
parse_svg_key_frame(struct flt_parser *parser,
                    struct flt_error **error)
{
        struct flt_scene_key_frame *base_key_frame;

        const size_t struct_size =
                sizeof (struct flt_scene_svg_key_frame);

        enum flt_parser_return base_ret =
                parse_base_key_frame(parser,
                                     struct_size,
                                     &base_key_frame,
                                     error);

        if (base_ret != FLT_PARSER_RETURN_OK)
                return base_ret;

        struct flt_scene_svg_key_frame *key_frame =
                (struct flt_scene_svg_key_frame *) base_key_frame;

        while (true) {
                const struct flt_lexer_token *token =
                        flt_lexer_get_token(parser->lexer, error);

                if (token == NULL)
                        return FLT_PARSER_RETURN_ERROR;

                if (token->type == FLT_LEXER_TOKEN_TYPE_CLOSE_BRACKET)
                        break;

                flt_lexer_put_token(parser->lexer);

                switch (parse_properties(parser,
                                         svg_key_frame_props,
                                         FLT_N_ELEMENTS(svg_key_frame_props),
                                         key_frame,
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
                          "Expected key_frame item (like x, y etc)");

                return FLT_PARSER_RETURN_ERROR;
        }

        return FLT_PARSER_RETURN_OK;
}

static char *
get_relative_filename(struct flt_parser *parser,
                      const char *filename)
{
        if (filename[0] == '/' ||
            parser->base_dir == NULL ||
            parser->base_dir[0] == '\0' ||
            !strcmp(parser->base_dir, "."))
                return flt_strdup(filename);
        else
                return flt_strconcat(parser->base_dir, "/", filename, NULL);
 }

static enum flt_parser_return
parse_svg_file(struct flt_parser *parser,
               struct flt_scene_svg *svg,
               struct flt_error **error)
{
        const struct flt_lexer_token *token;

        check_item_keyword(parser, FLT_LEXER_KEYWORD_FILE, error);

        require_token(parser,
                      FLT_LEXER_TOKEN_TYPE_STRING,
                      "expected filename",
                      error);

        if (svg->handle != NULL) {
                set_error(parser,
                          error,
                          "SVG object already has a file");
                return FLT_PARSER_RETURN_ERROR;
        }

        char *filename = get_relative_filename(parser, token->string_value);

        GError *svg_error = NULL;

        svg->handle = rsvg_handle_new_from_file(filename, &svg_error);

        flt_free(filename);

        if (svg->handle == NULL) {
                set_error(parser,
                          error,
                          "%s",
                          svg_error->message);

                g_error_free(svg_error);

                return FLT_PARSER_RETURN_ERROR;
        }

        return FLT_PARSER_RETURN_OK;
}

static enum flt_parser_return
parse_svg(struct flt_parser *parser,
          struct flt_error **error)
{
        const struct flt_lexer_token *token;

        check_item_keyword(parser, FLT_LEXER_KEYWORD_SVG, error);

        int svg_line_num = flt_lexer_get_line_num(parser->lexer);

        require_token(parser,
                      FLT_LEXER_TOKEN_TYPE_OPEN_BRACKET,
                      "expected ‘{’",
                      error);

        struct flt_scene_svg *svg = flt_calloc(sizeof *svg);

        svg->base.type = FLT_SCENE_OBJECT_TYPE_SVG;

        flt_list_init(&svg->base.key_frames);
        flt_list_insert(parser->scene->objects.prev, &svg->base.link);

        while (true) {
                token = flt_lexer_get_token(parser->lexer, error);

                if (token == NULL)
                        return FLT_PARSER_RETURN_ERROR;

                if (token->type == FLT_LEXER_TOKEN_TYPE_CLOSE_BRACKET)
                        break;

                flt_lexer_put_token(parser->lexer);

                static const item_parse_func funcs[] = {
                        parse_svg_key_frame,
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

                switch (parse_svg_file(parser, svg, error)) {
                case FLT_PARSER_RETURN_OK:
                        continue;
                case FLT_PARSER_RETURN_NOT_MATCHED:
                        break;
                case FLT_PARSER_RETURN_ERROR:
                        return FLT_PARSER_RETURN_ERROR;
                }

                set_error(parser,
                          error,
                          "Expected svg item (like a key_frame)");

                return FLT_PARSER_RETURN_ERROR;
        }

        if (flt_list_empty(&svg->base.key_frames)) {
                set_error_with_line(parser,
                                    error,
                                    svg_line_num,
                                    "svg has no key frames");
                return FLT_PARSER_RETURN_ERROR;
        }

        if (svg->handle == NULL) {
                set_error_with_line(parser,
                                    error,
                                    svg_line_num,
                                    "svg has no file");
                return FLT_PARSER_RETURN_ERROR;
        }

        return FLT_PARSER_RETURN_OK;
}

static const struct flt_parser_property
file_props[] = {
        {
                offsetof(struct flt_scene, video_width),
                FLT_PARSER_VALUE_TYPE_INT,
                FLT_LEXER_KEYWORD_VIDEO_WIDTH,
                .min_value = 1, .max_value = UINT16_MAX,
        },
        {
                offsetof(struct flt_scene, video_height),
                FLT_PARSER_VALUE_TYPE_INT,
                FLT_LEXER_KEYWORD_VIDEO_HEIGHT,
                .min_value = 1, .max_value = UINT16_MAX,
        },
};

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
                        parse_svg,
                        parse_score,
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

                switch (parse_properties(parser,
                                         file_props,
                                         FLT_N_ELEMENTS(file_props),
                                         parser->scene,
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
                          "Expected file-level item (like a rectangle etc)");

                return false;
        }

        return true;
}

bool
flt_parser_parse(struct flt_scene *scene,
                 struct flt_source *source,
                 const char *base_dir,
                 struct flt_error **error)
{
        struct flt_parser parser = {
                .lexer = flt_lexer_new(source),
                .scene = scene,
                .base_dir = base_dir,
        };

        bool ret = true;

        if (!parse_file(&parser, error))
                ret = false;

        flt_lexer_free(parser.lexer);

        return ret;
}
