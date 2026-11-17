
#include "shell.h"
// Shell state
int shell_interactive = 0;
int in_subshell = 0;
int last_exit_status = 0;
int shell_running = 1;
int shell_exit_code = 0;

// Handle memory allocation errors.
static void out_of_memory(void) {
    fprintf(stderr, "%s: out of memory\n", SHELL_NAME);
    exit(1);
}

// Memory allocation helpers
void *xmalloc(size_t size) {
    void *ptr = malloc(size ? size : 1);

    if (ptr == NULL) {
        out_of_memory();
    }

    return ptr;
}

void *xcalloc(size_t count, size_t size) {
    void *ptr = calloc(count ? count : 1, size ? size : 1);

    if (ptr == NULL) {
        out_of_memory();
    }

    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size ? size : 1);

    if (new_ptr == NULL) {
        out_of_memory();
    }

    return new_ptr;
}

char *xstrdup(const char *str) {
    return xstrndup(str, strlen(str));
}

char *xstrndup(const char *str, size_t len) {
    char *copy = xmalloc(len + 1);

    memcpy(copy, str, len);
    copy[len] = '\0';

    return copy;
}

// Initialize the shell and load command history.
void init_shell(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    setup_shell();

    if (shell_interactive) {
        char path[1024];
        history_load(history_default_path(path, sizeof(path)));
    }
}

// Clean up jobs and save history before exiting.
int shell_finish(int save_history) {
    terminate_all_jobs();

    if (save_history && shell_interactive) {
        char path[1024];
        history_save(history_default_path(path, sizeof(path)));
    }

    history_clear();

    return shell_running ? last_exit_status : shell_exit_code;
}

// Parse and execute one command line.
int process_line(const char *line) {
    CommandList list;
    char error[256];

    if (parse_line(line, &list, error, sizeof(error)) < 0) {
        fprintf(stderr, "%s: %s\n", SHELL_NAME, error);
        last_exit_status = 2;
        return last_exit_status;
    }

    execute_command_list(&list);
    free_command_list(&list);

    return last_exit_status;
}

// Main shell loop.
void shell_loop(void) {
    while (shell_running) {
        reap_jobs();

        char *line = read_input_line(PROMPT);

        if (line == NULL) {
            if (shell_interactive) {
                printf("exit\n");
                fflush(stdout);
            }
            break;
        }

        int error = 0;
        char *expanded = history_expand(line, &error);

        if (error) {
            free(line);
            last_exit_status = 1;
            continue;
        }

        if (expanded != NULL) {
            printf("%s\n", expanded);
            free(line);
            line = expanded;
        }

        history_add(line);
        process_line(line);

        free(line);
    }
}
