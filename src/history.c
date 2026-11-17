/*
 * history.c - Command history and interactive line editor.
 *
 * Supports:
 * - Loading and saving command history.
 * - History expansion: !!, !n, !-n and !text.
 * - Interactive input with arrow keys and keyboard shortcuts.
 */

#include "shell.h"


#include <poll.h>
#include <sys/select.h>
#include <ctype.h>

/* ---------- History storage ---------- */

static char *entries[HISTORY_MAX];
static int entry_count = 0;

/* Add a command to history. */
void history_add(const char *line) {
    const char *p = line;

    while (*p == ' ' || *p == '\t')
        p++;

    if (*p == '\0')
        return;

    /* Avoid storing the same command twice in a row. */
    if (entry_count > 0 &&
        strcmp(entries[entry_count - 1], line) == 0) {
        return;
    }

    /* Remove the oldest command when history is full. */
    if (entry_count == HISTORY_MAX) {
        free(entries[0]);

        memmove(entries, entries + 1,
                (HISTORY_MAX - 1) * sizeof(char *));

        entry_count--;
    }

    entries[entry_count++] = xstrdup(line);
}

int history_count(void) {
    return entry_count;
}

const char *history_get(int index) {
    if (index < 0 || index >= entry_count)
        return NULL;

    return entries[index];
}

/* Free all stored history entries. */
void history_clear(void) {
    for (int i = 0; i < entry_count; i++)
        free(entries[i]);

    entry_count = 0;
}

/* Get the default history file path. */
const char *history_default_path(char *buf, size_t size) {
    const char *env = getenv("MYSHELL_HISTFILE");
    const char *home = getenv("HOME");

    if (env != NULL && *env) {
        snprintf(buf, size, "%s", env);
    } else if (home != NULL) {
        snprintf(buf, size, "%s/.myshell_history", home);
    } else {
        snprintf(buf, size, ".myshell_history");
    }

    return buf;
}

/* Load previously saved commands. */
void history_load(const char *path) {
    FILE *file = fopen(path, "r");

    if (file == NULL)
        return;

    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;

    while ((length = getline(&line, &capacity, file)) >= 0) {
        if (length > 0 && line[length - 1] == '\n')
            line[length - 1] = '\0';

        history_add(line);
    }

    free(line);
    fclose(file);
}

/* Save history to a file. */
void history_save(const char *path) {
    FILE *file = fopen(path, "w");

    if (file == NULL)
        return;

    for (int i = 0; i < entry_count; i++)
        fprintf(file, "%s\n", entries[i]);

    fclose(file);
}

/* ---------- History expansion ---------- */

/*
 * Supported formats:
 * !!       Previous command
 * !n       Command number n
 * !-n      nth previous command
 * !text    Most recent command beginning with text
 *
 * Returns NULL when no expansion is required.
 * Sets *error to 1 when expansion fails.
 */

char *history_expand(const char *line, int *error) {
    *error = 0;

    const char *p = line;

    while (*p == ' ' || *p == '\t')
        p++;

    if (*p != '!' || p[1] == '\0' ||
        isspace((unsigned char)p[1]) || p[1] == '=') {
        return NULL;
    }

    const char *end = p;

    while (*end && !isspace((unsigned char)*end))
        end++;

    char *event = xstrndup(p, (size_t)(end - p));
    const char *found = NULL;

    if (strcmp(event, "!!") == 0) {
        found = history_get(entry_count - 1);
    } else if (event[1] == '-' &&
               isdigit((unsigned char)event[2])) {

        char *number_end;
        errno = 0;

        long number = strtol(event + 2, &number_end, 10);

        if (*number_end == '\0' && errno != ERANGE &&
            number > 0 && number <= entry_count) {
            found = history_get(entry_count - (int)number);
        }

    } else if (isdigit((unsigned char)event[1])) {

        char *number_end;
        errno = 0;

        long number = strtol(event + 1, &number_end, 10);

        if (*number_end == '\0' && errno != ERANGE &&
            number > 0 && number <= entry_count) {
            found = history_get((int)number - 1);
        }

    } else {
        /* Search for the most recent command with this prefix. */
        for (int i = entry_count - 1; i >= 0; i--) {
            if (strncmp(entries[i], event + 1,
                        strlen(event + 1)) == 0) {
                found = entries[i];
                break;
            }
        }
    }

    if (found == NULL) {
        fprintf(stderr, "%s: %s: event not found\n",
                SHELL_NAME, event);

        free(event);
        *error = 1;
        return NULL;
    }

    size_t prefix = (size_t)(p - line);

    char *result = xmalloc(
        prefix + strlen(found) + strlen(end) + 1
    );

    memcpy(result, line, prefix);
    strcpy(result + prefix, found);
    strcat(result, end);

    free(event);

    return result;
}

/* ---------- Interactive line editor ---------- */

typedef struct {
    char buf[MAX_LINE];
    size_t len;
    size_t pos;
} LineState;

/* Write all requested characters to the terminal. */
static void write_all(const char *data, size_t len) {
    while (len > 0) {
        ssize_t n = write(STDOUT_FILENO, data, len);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return;
        }

        if (n == 0)
            return;

        data += n;
        len -= (size_t)n;
    }
}

static void write_str(const char *str) {
    write_all(str, strlen(str));
}

/* Redraw the prompt and command line. */
static void redraw_line(const char *prompt, const LineState *ls) {
    static char output[MAX_LINE + 256];

    size_t n = 0;
    size_t prompt_len = strlen(prompt);

    output[n++] = '\r';

    memcpy(output + n, prompt, prompt_len);
    n += prompt_len;

    memcpy(output + n, ls->buf, ls->len);
    n += ls->len;

    memcpy(output + n, "\x1b[K", 3);
    n += 3;

    /* Move cursor back to its correct position. */
    if (ls->len > ls->pos) {
        n += (size_t)snprintf(
            output + n,
            sizeof output - n,
            "\x1b[%zuD",
            ls->len - ls->pos
        );
    }

    write_all(output, n);
}

/* Replace the current line with another string. */
static void set_line(LineState *ls, const char *text) {
    ls->len = strlen(text);

    if (ls->len > MAX_LINE - 1)
        ls->len = MAX_LINE - 1;

    memcpy(ls->buf, text, ls->len);

    ls->buf[ls->len] = '\0';
    ls->pos = ls->len;
}

/* Insert a character at the cursor position. */
static void insert_char(LineState *ls, char c) {
    if (ls->len >= MAX_LINE - 1)
        return;

    memmove(
        ls->buf + ls->pos + 1,
        ls->buf + ls->pos,
        ls->len - ls->pos
    );

    ls->buf[ls->pos] = c;

    ls->pos++;
    ls->len++;

    ls->buf[ls->len] = '\0';
}

/* Delete characters between two positions. */
static void delete_range(LineState *ls, size_t from, size_t to) {
    if (from > to || to > ls->len)
        return;

    memmove(
        ls->buf + from,
        ls->buf + to,
        ls->len - to
    );

    ls->len -= (to - from);
    ls->pos = from;

    ls->buf[ls->len] = '\0';
}

/* Read one byte with a timeout. */
static int read_byte_timeout(int ms) {
    struct pollfd pfd = {
        STDIN_FILENO,
        POLLIN,
        0
    };

    unsigned char c;

    int result = poll(&pfd, 1, ms);

    if (result <= 0)
        return -1;

    if (read(STDIN_FILENO, &c, 1) != 1)
        return -1;

    return c;
}

/* Move to the previous history command. */
static void history_up(LineState *ls, int *hist_pos, char *saved) {
    if (*hist_pos <= 0)
        return;

    /* Save the current unsent command before browsing history. */
    if (*hist_pos == entry_count) {
        memcpy(saved, ls->buf, ls->len);
        saved[ls->len] = '\0';
    }

    (*hist_pos)--;

    set_line(ls, entries[*hist_pos]);
}

/* Move to the next history command. */
static void history_down(LineState *ls, int *hist_pos,
                         const char *saved) {
    if (*hist_pos >= entry_count)
        return;

    (*hist_pos)++;

    if (*hist_pos == entry_count)
        set_line(ls, saved);
    else
        set_line(ls, entries[*hist_pos]);
}

/* Handle arrow keys and other escape sequences. */
static void handle_escape(LineState *ls, int *hist_pos, char *saved) {
    int b1 = read_byte_timeout(50);

    if (b1 != '[' && b1 != 'O')
        return;

    int b2 = read_byte_timeout(50);

    switch (b2) {
        case 'A':
            history_up(ls, hist_pos, saved);
            break;

        case 'B':
            history_down(ls, hist_pos, saved);
            break;

        case 'C':
            if (ls->pos < ls->len)
                ls->pos++;
            break;

        case 'D':
            if (ls->pos > 0)
                ls->pos--;
            break;

        case 'H':
            ls->pos = 0;
            break;

        case 'F':
            ls->pos = ls->len;
            break;

        case '1':
        case '3':
        case '4':
        case '7':
        case '8':
            if (read_byte_timeout(50) == '~') {
                if (b2 == '1' || b2 == '7')
                    ls->pos = 0;
                else if (b2 == '4' || b2 == '8')
                    ls->pos = ls->len;
                else if (ls->pos < ls->len)
                    delete_range(ls, ls->pos, ls->pos + 1);
            }
            break;

        default:
            break;
    }
}

/*
 * Wait for input while allowing signal handlers to run.
 * pselect() temporarily restores the normal signal mask.
 */
static int wait_for_input(const sigset_t *wait_mask) {
    fd_set readable;

    FD_ZERO(&readable);
    FD_SET(STDIN_FILENO, &readable);

    return pselect(
        STDIN_FILENO + 1,
        &readable,
        NULL,
        NULL,
        NULL,
        wait_mask
    );
}

/* Read and edit one interactive command line. */
static char *edit_line(const char *prompt, const struct termios *raw) {
    static char saved[MAX_LINE];

    LineState ls;
    int hist_pos = entry_count;
    int eof = 0;

    sigset_t block_mask;
    sigset_t normal_mask;

    memset(&ls, 0, sizeof ls);
    saved[0] = '\0';

    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGCHLD);
    sigaddset(&block_mask, SIGCONT);

    sigprocmask(SIG_BLOCK, &block_mask, &normal_mask);

    redraw_line(prompt, &ls);

    for (;;) {
        /* Check signal flags while signals are blocked. */
        if (got_sigcont) {
            got_sigcont = 0;

            tcsetattr(STDIN_FILENO, TCSADRAIN, raw);
            redraw_line(prompt, &ls);
        }

        if (got_sigchld) {
            write_str("\r\x1b[K");

            reap_jobs();
            redraw_line(prompt, &ls);
        }

        if (wait_for_input(&normal_mask) < 0) {
            if (errno == EINTR)
                continue;

            eof = 1;
            break;
        }

        unsigned char c;

        ssize_t n = read(STDIN_FILENO, &c, 1);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            eof = 1;
            break;
        }

        if (n == 0) {
            eof = 1;
            break;
        }

        if (c == '\r' || c == '\n') {
            write_str("\r\n");
            break;
        }

        /* Ctrl+C: cancel the current input line. */
        else if (c == 3) {
            write_str("^C\r\n");
            ls.len = 0;
            ls.pos = 0;
            ls.buf[0] = '\0';
            break;
        }

        /* Ctrl+D: exit on an empty line, otherwise delete a character. */
        else if (c == 4) {
            if (ls.len == 0) {
                eof = 1;
                break;
            }

            if (ls.pos < ls.len)
                delete_range(&ls, ls.pos, ls.pos + 1);
        }

        /* Backspace. */
        else if (c == 127 || c == 8) {
            if (ls.pos > 0)
                delete_range(&ls, ls.pos - 1, ls.pos);
        }

        /* Ctrl+A: move to the beginning. */
        else if (c == 1) {
            ls.pos = 0;
        }

        /* Ctrl+E: move to the end. */
        else if (c == 5) {
            ls.pos = ls.len;
        }

        /* Ctrl+K: delete from cursor to the end. */
        else if (c == 11) {
            ls.len = ls.pos;
            ls.buf[ls.len] = '\0';
        }

        /* Ctrl+U: delete from beginning to cursor. */
        else if (c == 21) {
            delete_range(&ls, 0, ls.pos);
        }

        /* Ctrl+W: delete the previous word. */
        else if (c == 23) {
            size_t start = ls.pos;

            while (start > 0 && ls.buf[start - 1] == ' ')
                start--;

            while (start > 0 && ls.buf[start - 1] != ' ')
                start--;

            delete_range(&ls, start, ls.pos);
        }

        /* Ctrl+L: clear the terminal. */
        else if (c == 12) {
            write_str("\x1b[H\x1b[2J");
        }

        /* Escape: arrow keys and navigation keys. */
        else if (c == 27) {
            handle_escape(&ls, &hist_pos, saved);
        }

        /* Normal printable character. */
        else if (c >= 32) {
            insert_char(&ls, (char)c);
        }

        redraw_line(prompt, &ls);
    }

    sigprocmask(SIG_SETMASK, &normal_mask, NULL);

    if (eof && ls.len == 0)
        return NULL;

    ls.buf[ls.len] = '\0';

    return xstrdup(ls.buf);
}

/* ---------- Non-interactive input ---------- */

/* Read a line using getline() when stdin is not a terminal. */
static char *read_line_plain(void) {
    char *line = NULL;
    size_t capacity = 0;

    for (;;) {
        ssize_t length = getline(&line, &capacity, stdin);

        if (length >= 0) {
            if (length > 0 && line[length - 1] == '\n')
                line[length - 1] = '\0';

            return line;
        }

        if (ferror(stdin) && errno == EINTR) {
            clearerr(stdin);
            continue;
        }

        free(line);
        return NULL;
    }
}

/* ---------- Public input function ---------- */

char *read_input_line(const char *prompt) {
    struct termios original;
    struct termios raw;

    fflush(stdout);

    if (!shell_interactive)
        return read_line_plain();

    if (tcgetattr(STDIN_FILENO, &original) < 0) {
        fputs(prompt, stdout);
        fflush(stdout);
        return read_line_plain();
    }

    raw = original;

    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= (tcflag_t)~(ICRNL | INLCR | IXON);

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) < 0) {
        fputs(prompt, stdout);
        fflush(stdout);
        return read_line_plain();
    }

    char *line = edit_line(prompt, &raw);

    /* Always restore the terminal before executing the command. */
    tcsetattr(STDIN_FILENO, TCSADRAIN, &original);

    return line;
}