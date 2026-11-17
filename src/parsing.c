/*
 * parsing.c - Lexer, parser and word expansion.
 * 1. tokenize()       : Splits input into words and operators.
 * 2. parse_line()     : Builds command and pipeline structures.
 * 3. expand_command() : Expands variables, quotes, ~ and wildcards.
 */

#include "shell.h"

#include <ctype.h>
#include <glob.h>

/*LEXER */

typedef enum {
    TOK_WORD,
    TOK_PIPE,
    TOK_AND,
    TOK_OR,
    TOK_SEMI,
    TOK_BG,
    TOK_IN,
    TOK_OUT,
    TOK_APPEND,
    TOK_ERR,
    TOK_ERR_APPEND,
    TOK_ERR_TO_OUT,
    TOK_OUT_TO_ERR,
    TOK_END
} TokenType;

typedef struct {
    TokenType type;
    char *text;
    size_t start;
    size_t end;
} Token;

static int is_operator_char(char c) {
    return c == '|' || c == '&' || c == ';' ||
           c == '<' || c == '>';
}

static int is_boundary(char c) {
    return c == '\0' || isspace((unsigned char)c) ||
           is_operator_char(c);
}

static void add_token(Token **tokens, int *count, int *capacity,
                      TokenType type, char *text,
                      size_t start, size_t end) {
    if (*count >= *capacity) {
        *capacity = *capacity ? *capacity * 2 : 16;

        *tokens = xrealloc(
            *tokens,
            (size_t)*capacity * sizeof(Token)
        );
    }

    (*tokens)[*count].type = type;
    (*tokens)[*count].text = text;
    (*tokens)[*count].start = start;
    (*tokens)[*count].end = end;

    (*count)++;
}

static void free_tokens(Token *tokens, int count) {
    for (int i = 0; i < count; i++)
        free(tokens[i].text);

    free(tokens);
}

/* Recognize operators such as |, ||, &, >> and 2>. */
static int lex_operator(const char *line, size_t i,
                        TokenType *type, char *err, size_t errsize) {
    char c = line[i];
    char next = line[i + 1];

    switch (c) {
        case '|':
            if (next == '|') {
                *type = TOK_OR;
                return 2;
            }

            *type = TOK_PIPE;
            return 1;

        case '&':
            if (next == '&') {
                *type = TOK_AND;
                return 2;
            }

            *type = TOK_BG;
            return 1;

        case ';':
            *type = TOK_SEMI;
            return 1;

        case '<':
            if (next == '<') {
                snprintf(err, errsize,
                         "here-documents (<<) are not supported");
                return -1;
            }

            *type = TOK_IN;
            return 1;

        case '>':
            if (next == '>') {
                *type = TOK_APPEND;
                return 2;
            }

            if (next == '&') {
                if (line[i + 2] == '2' &&
                    is_boundary(line[i + 3])) {
                    *type = TOK_OUT_TO_ERR;
                    return 3;
                }

                snprintf(err, errsize, "unsupported redirection '>&'");
                return -1;
            }

            *type = TOK_OUT;
            return 1;

        case '1':
        case '2':
            if (next != '>')
                return 0;

            if (line[i + 2] == '>') {
                *type = (c == '2') ? TOK_ERR_APPEND : TOK_APPEND;
                return 3;
            }

            if (line[i + 2] == '&') {
                if (c == '2' && line[i + 3] == '1' &&
                    is_boundary(line[i + 4])) {
                    *type = TOK_ERR_TO_OUT;
                    return 4;
                }

                if (c == '1' && line[i + 3] == '2' &&
                    is_boundary(line[i + 4])) {
                    *type = TOK_OUT_TO_ERR;
                    return 4;
                }

                snprintf(err, errsize,
                         "unsupported redirection '%c>&'", c);
                return -1;
            }

            *type = (c == '2') ? TOK_ERR : TOK_OUT;
            return 2;

        default:
            return 0;
    }
}

/* Find the end of a word while respecting quotes and escapes. */
static long scan_word(const char *line, size_t i,
                      char *err, size_t errsize) {
    size_t j = i;

    while (line[j] != '\0' &&
           !isspace((unsigned char)line[j]) &&
           !is_operator_char(line[j])) {

        if (line[j] == '\\') {
            j += (line[j + 1] != '\0') ? 2 : 1;
        }

        else if (line[j] == '\'') {
            const char *close = strchr(line + j + 1, '\'');

            if (close == NULL) {
                snprintf(err, errsize,
                         "unterminated single quote");
                return -1;
            }

            j = (size_t)(close - line) + 1;
        }

        else if (line[j] == '"') {
            size_t k = j + 1;

            while (line[k] != '\0' && line[k] != '"') {
                if (line[k] == '\\' && line[k + 1] != '\0')
                    k += 2;
                else
                    k++;
            }

            if (line[k] == '\0') {
                snprintf(err, errsize,
                         "unterminated double quote");
                return -1;
            }

            j = k + 1;
        }

        else {
            j++;
        }
    }

    return (long)j;
}

/* Convert the input line into tokens. */
static int tokenize(const char *line, Token **out,
                    int *count, char *err, size_t errsize) {
    Token *tokens = NULL;
    int n = 0;
    int capacity = 0;
    size_t i = 0;

    for (;;) {
        while (isspace((unsigned char)line[i]))
            i++;

        /* A comment starts when # begins a new token. */
        if (line[i] == '\0' || line[i] == '#')
            break;

        TokenType type;

        int len = lex_operator(line, i, &type, err, errsize);

        if (len < 0) {
            free_tokens(tokens, n);
            return -1;
        }

        if (len > 0) {
            add_token(&tokens, &n, &capacity,
                      type, NULL, i, i + (size_t)len);

            i += (size_t)len;
            continue;
        }

        long end = scan_word(line, i, err, errsize);

        if (end < 0) {
            free_tokens(tokens, n);
            return -1;
        }

        add_token(
            &tokens,
            &n,
            &capacity,
            TOK_WORD,
            xstrndup(line + i, (size_t)end - i),
            i,
            (size_t)end
        );

        i = (size_t)end;
    }

    add_token(&tokens, &n, &capacity, TOK_END, NULL, i, i);

    *out = tokens;
    *count = n;

    return 0;
}

/* ============================================================
   PARSER
   ============================================================ */

typedef struct {
    Token *tokens;
    int pos;
    const char *line;
    char *err;
    size_t errsize;
} Parser;

static const char *token_display(const Token *token) {
    switch (token->type) {
        case TOK_WORD:        return token->text;
        case TOK_PIPE:        return "|";
        case TOK_AND:         return "&&";
        case TOK_OR:          return "||";
        case TOK_SEMI:        return ";";
        case TOK_BG:          return "&";
        case TOK_IN:          return "<";
        case TOK_OUT:         return ">";
        case TOK_APPEND:      return ">>";
        case TOK_ERR:         return "2>";
        case TOK_ERR_APPEND:  return "2>>";
        case TOK_ERR_TO_OUT:  return "2>&1";
        case TOK_OUT_TO_ERR:  return ">&2";
        case TOK_END:         return "newline";
    }

    return "?";
}

static int syntax_error(Parser *ps, const Token *token) {
    snprintf(
        ps->err,
        ps->errsize,
        "syntax error near unexpected token `%s'",
        token_display(token)
    );

    return -1;
}

static const Token *peek(const Parser *ps) {
    return &ps->tokens[ps->pos];
}

static int is_redir_with_target(TokenType type) {
    return type == TOK_IN ||
           type == TOK_OUT ||
           type == TOK_APPEND ||
           type == TOK_ERR ||
           type == TOK_ERR_APPEND;
}

static RedirType redir_for(TokenType type) {
    switch (type) {
        case TOK_IN:         return REDIR_IN;
        case TOK_OUT:        return REDIR_OUT;
        case TOK_APPEND:     return REDIR_APPEND;
        case TOK_ERR:        return REDIR_ERR;
        case TOK_ERR_APPEND: return REDIR_ERR_APPEND;
        case TOK_ERR_TO_OUT: return REDIR_ERR_TO_OUT;
        default:             return REDIR_OUT_TO_ERR;
    }
}

static void add_redirection(Command *cmd, RedirType type,
                            const char *target) {
    cmd->redirs = xrealloc(
        cmd->redirs,
        (size_t)(cmd->nredirs + 1) * sizeof(Redirection)
    );

    cmd->redirs[cmd->nredirs].type = type;

    cmd->redirs[cmd->nredirs].target =
        target ? xstrdup(target) : NULL;

    cmd->nredirs++;
}

/* command := (word | redirection)+ */
static int parse_command(Parser *ps, Command *cmd) {
    int got_something = 0;

    for (;;) {
        const Token *token = peek(ps);

        if (token->type == TOK_WORD) {
            cmd->words = xrealloc(
                cmd->words,
                (size_t)(cmd->nwords + 1) * sizeof(char *)
            );

            cmd->words[cmd->nwords++] = xstrdup(token->text);

            ps->pos++;
            got_something = 1;
        }

        else if (is_redir_with_target(token->type)) {
            ps->pos++;

            const Token *target = peek(ps);

            if (target->type != TOK_WORD)
                return syntax_error(ps, target);

            add_redirection(
                cmd,
                redir_for(token->type),
                target->text
            );

            ps->pos++;
            got_something = 1;
        }

        else if (token->type == TOK_ERR_TO_OUT ||
                 token->type == TOK_OUT_TO_ERR) {

            add_redirection(cmd, redir_for(token->type), NULL);

            ps->pos++;
            got_something = 1;
        }

        else {
            break;
        }
    }

    if (!got_something)
        return syntax_error(ps, peek(ps));

    return 0;
}
/* pipeline := command ('|' command)* */
static int parse_pipeline(Parser *ps, Pipeline *pl) {
    int first_token = ps->pos;

    for (;;) {
        pl->cmds = xrealloc(
            pl->cmds,
            (size_t)(pl->ncmds + 1) * sizeof(Command)
        );

        memset(&pl->cmds[pl->ncmds], 0, sizeof(Command));
        pl->ncmds++;

        if (parse_command(ps, &pl->cmds[pl->ncmds - 1]) < 0)
            return -1;

        if (peek(ps)->type == TOK_PIPE) {
            ps->pos++;
            continue;
        }

        break;
    }

    size_t from = ps->tokens[first_token].start;
    size_t to = ps->tokens[ps->pos - 1].end;

    pl->text = xstrndup(ps->line + from, to - from);

    return 0;
}

/* and_or := pipeline (('&&' | '||') pipeline)* */
static int parse_and_or(Parser *ps, AndOrList *ao) {
    int first_token = ps->pos;

    for (;;) {
        ao->pipelines = xrealloc(
            ao->pipelines,
            (size_t)(ao->npipelines + 1) * sizeof(Pipeline)
        );

        memset(&ao->pipelines[ao->npipelines], 0, sizeof(Pipeline));
        ao->npipelines++;

        if (parse_pipeline(
                ps,
                &ao->pipelines[ao->npipelines - 1]) < 0) {
            return -1;
        }

        TokenType type = peek(ps)->type;

        if (type == TOK_AND || type == TOK_OR) {
            ao->connectors = xrealloc(
                ao->connectors,
                (size_t)ao->npipelines * sizeof(Connector)
            );

            ao->connectors[ao->npipelines - 1] =
                (type == TOK_AND) ? CONN_AND : CONN_OR;

            ps->pos++;
            continue;
        }

        break;
    }

    size_t from = ps->tokens[first_token].start;
    size_t to = ps->tokens[ps->pos - 1].end;

    ao->text = xstrndup(ps->line + from, to - from);

    return 0;
}

/* list := and_or ((';' | '&') and_or)* [';' | '&'] */
static int parse_list(Parser *ps, CommandList *list) {
    while (peek(ps)->type != TOK_END) {
        list->items = xrealloc(
            list->items,
            (size_t)(list->nitems + 1) * sizeof(AndOrList)
        );

        memset(&list->items[list->nitems], 0, sizeof(AndOrList));
        list->nitems++;

        AndOrList *item = &list->items[list->nitems - 1];

        if (parse_and_or(ps, item) < 0)
            return -1;

        const Token *token = peek(ps);

        if (token->type == TOK_SEMI) {
            ps->pos++;
        }

        else if (token->type == TOK_BG) {
            item->background = 1;
            ps->pos++;
        }

        else if (token->type != TOK_END) {
            return syntax_error(ps, token);
        }
    }

    return 0;
}

/* ============================================================
   MEMORY CLEANUP
   ============================================================ */

void free_command_list(CommandList *list) {
    for (int i = 0; i < list->nitems; i++) {
        AndOrList *ao = &list->items[i];

        for (int p = 0; p < ao->npipelines; p++) {
            Pipeline *pl = &ao->pipelines[p];

            for (int c = 0; c < pl->ncmds; c++) {
                Command *cmd = &pl->cmds[c];

                for (int w = 0; w < cmd->nwords; w++)
                    free(cmd->words[w]);

                free(cmd->words);

                for (int r = 0; r < cmd->nredirs; r++)
                    free(cmd->redirs[r].target);

                free(cmd->redirs);
            }

            free(pl->cmds);
            free(pl->text);
        }

        free(ao->pipelines);
        free(ao->connectors);
        free(ao->text);
    }

    free(list->items);

    list->items = NULL;
    list->nitems = 0;
}

/* Parse one complete input line. */
int parse_line(const char *line, CommandList *list,
               char *err, size_t errsize) {
    Token *tokens = NULL;
    int token_count = 0;

    memset(list, 0, sizeof *list);

    if (errsize > 0)
        err[0] = '\0';

    if (tokenize(line, &tokens, &token_count, err, errsize) < 0)
        return -1;

    Parser ps = {
        tokens,
        0,
        line,
        err,
        errsize
    };

    int result = parse_list(&ps, list);

    free_tokens(tokens, token_count);

    if (result < 0) {
        free_command_list(list);
        return -1;
    }

    return 0;
}

/* ============================================================
   WORD EXPANSION
   ============================================================ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static void buf_putc(Buf *buf, char c) {
    if (buf->len + 2 > buf->cap) {
        buf->cap = buf->cap ? buf->cap * 2 : 32;
        buf->data = xrealloc(buf->data, buf->cap);
    }

    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
}

static char *buf_take(Buf *buf) {
    return buf->data ? buf->data : xstrdup("");
}

typedef struct {
    char **items;
    int count;
    int cap;
} StrVec;

static void vec_push(StrVec *vec, char *str) {
    if (vec->count + 2 > vec->cap) {
        vec->cap = vec->cap ? vec->cap * 2 : 8;

        vec->items = xrealloc(
            vec->items,
            (size_t)vec->cap * sizeof(char *)
        );
    }

    vec->items[vec->count++] = str;
    vec->items[vec->count] = NULL;
}

static void vec_free(StrVec *vec) {
    for (int i = 0; i < vec->count; i++)
        free(vec->items[i]);

    free(vec->items);

    vec->items = NULL;
    vec->count = 0;
}

/* Add a quoted character so it cannot become a wildcard. */
static void put_quoted(Buf *text, Buf *pattern, char c) {
    buf_putc(text, c);

    if (c == '*' || c == '?' || c == '[' || c == '\\')
        buf_putc(pattern, '\\');

    buf_putc(pattern, c);
}

/* Add a normal character, keeping wildcard characters active. */
static void put_plain(Buf *text, Buf *pattern,
                      char c, int *has_glob) {
    buf_putc(text, c);

    if (c == '\\') {
        buf_putc(pattern, '\\');
    } else if (c == '*' || c == '?' || c == '[') {
        *has_glob = 1;
    }

    buf_putc(pattern, c);
}

/* Expand variables beginning with $. */
static size_t expand_dollar(const char *p, Buf *text,
                            Buf *pattern, int quoted,
                            int *has_glob) {
    char number[32];
    const char *value = NULL;
    size_t used;

    if (p[1] == '?') {
        snprintf(number, sizeof number, "%d", last_exit_status);
        value = number;
        used = 2;
    }

    else if (p[1] == '$') {
        snprintf(number, sizeof number, "%d", (int)getpid());
        value = number;
        used = 2;
    }

    else if (p[1] == '{') {
        const char *close = strchr(p + 2, '}');

        if (close == NULL || close == p + 2) {
            put_plain(text, pattern, '$', has_glob);
            return 1;
        }

        char *name = xstrndup(
            p + 2,
            (size_t)(close - (p + 2))
        );

        value = getenv(name);
        free(name);

        used = (size_t)(close - p) + 1;
    }

    else if (isalpha((unsigned char)p[1]) || p[1] == '_') {
        const char *q = p + 1;

        while (isalnum((unsigned char)*q) || *q == '_')
            q++;

        char *name = xstrndup(
            p + 1,
            (size_t)(q - (p + 1))
        );

        value = getenv(name);
        free(name);

        used = (size_t)(q - p);
    }

    else {
        put_plain(text, pattern, '$', has_glob);
        return 1;
    }

    for (; value != NULL && *value; value++) {
        if (quoted)
            put_quoted(text, pattern, *value);
        else
            put_plain(text, pattern, *value, has_glob);
    }

    return used;
}

/* Expand one word and add its result to the argument vector. */
static void expand_word(const char *raw, StrVec *out) {
    Buf text = {0};
    Buf pattern = {0};

    int had_quotes = 0;
    int has_glob = 0;

    const char *p = raw;

    /* Expand ~ at the beginning of an unquoted word. */
    if (p[0] == '~' && (p[1] == '\0' || p[1] == '/')) {
        const char *home = getenv("HOME");

        if (home != NULL) {
            for (const char *h = home; *h; h++)
                put_quoted(&text, &pattern, *h);

            p++;
        }
    }

    while (*p) {
        if (*p == '\'') {
            had_quotes = 1;
            p++;

            while (*p && *p != '\'')
                put_quoted(&text, &pattern, *p++);

            if (*p)
                p++;
        }

        else if (*p == '"') {
            had_quotes = 1;
            p++;

            while (*p && *p != '"') {
                if (*p == '\\' &&
                    (p[1] == '"' || p[1] == '\\' ||
                     p[1] == '$' || p[1] == '`')) {

                    put_quoted(&text, &pattern, p[1]);
                    p += 2;
                }

                else if (*p == '$') {
                    p += expand_dollar(
                        p, &text, &pattern, 1, &has_glob
                    );
                }

                else {
                    put_quoted(&text, &pattern, *p++);
                }
            }

            if (*p)
                p++;
        }

        else if (*p == '\\') {
            had_quotes = 1;

            if (p[1] != '\0') {
                put_quoted(&text, &pattern, p[1]);
                p += 2;
            } else {
                put_quoted(&text, &pattern, '\\');
                p++;
            }
        }

        else if (*p == '$') {
            p += expand_dollar(
                p, &text, &pattern, 0, &has_glob
            );
        }

        else {
            put_plain(&text, &pattern, *p++, &has_glob);
        }
    }

    /* Preserve quoted empty strings, but remove unquoted empty expansions. */
    if (!had_quotes && text.len == 0) {
        free(text.data);
        free(pattern.data);
        return;
    }

    /* Expand wildcard patterns. */
    if (has_glob) {
        glob_t matches = {0};

        int result = glob(pattern.data, 0, NULL, &matches);

        if (result == 0) {
            for (size_t i = 0; i < matches.gl_pathc; i++)
                vec_push(out, xstrdup(matches.gl_pathv[i]));

            globfree(&matches);

            free(text.data);
            free(pattern.data);

            return;
        }

        globfree(&matches);

        /* No match: keep the original word. */
    }

    vec_push(out, buf_take(&text));

    free(pattern.data);
}

/* Expand all words and redirections in one command. */
int expand_command(const Command *cmd, ExpandedCommand *out) {
    StrVec argv = {0};

    memset(out, 0, sizeof *out);

    for (int i = 0; i < cmd->nwords; i++)
        expand_word(cmd->words[i], &argv);

    if (argv.items == NULL)
        argv.items = xcalloc(1, sizeof(char *));

    out->argv = argv.items;
    out->argc = argv.count;

    if (cmd->nredirs > 0) {
        out->redirs = xcalloc(
            (size_t)cmd->nredirs,
            sizeof(ExpandedRedir)
        );
    }

    for (int i = 0; i < cmd->nredirs; i++) {
        const Redirection *redir = &cmd->redirs[i];

        out->redirs[i].type = redir->type;
        out->nredirs = i + 1;

        if (redir->target == NULL)
            continue;

        StrVec target = {0};

        expand_word(redir->target, &target);

        if (target.count != 1) {
            fprintf(stderr, "%s: %s: ambiguous redirect\n",
                    SHELL_NAME, redir->target);

            vec_free(&target);
            free_expanded_command(out);

            return -1;
        }

        out->redirs[i].path = target.items[0];
        free(target.items);
    }

    return 0;
}

/* Free expanded arguments and redirections. */
void free_expanded_command(ExpandedCommand *ec) {
    if (ec->argv != NULL) {
        for (int i = 0; i < ec->argc; i++)
            free(ec->argv[i]);

        free(ec->argv);
    }

    for (int i = 0; i < ec->nredirs; i++)
        free(ec->redirs[i].path);

    free(ec->redirs);

    memset(ec, 0, sizeof *ec);
}