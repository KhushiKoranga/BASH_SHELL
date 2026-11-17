/* builtins.c - Built-in commands of our shell.
 These commands are handled by the shell itself.*/
 

#include "shell.h"
#include <ctype.h>
#include <limits.h>

extern char **environ;   

/* Change current directory */
int builtin_cd(int argc, char **argv) { 
    const char *target;  
    int print_target = 0;

    if (argc > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }
    //strcmp() compares two strings.It returns 0 when both strings are equal
    if (argc == 1 || strcmp(argv[1], "~") == 0) {   //cd ~ 
        target = getenv("HOME");  //getenv("HOME") retrieves the value of HOME environment variable .

        if (target == NULL) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
    } else if (strcmp(argv[1], "-") == 0) {  //cd - means go to the previous working directory 
        target = getenv("OLDPWD"); 

        if (target == NULL) {
            fprintf(stderr, "cd: OLDPWD not set\n");
            return 1;
        }

        print_target = 1;
    } else {
        target = argv[1];
    }

    char old[PATH_MAX]; //creates a char array to store the current working directory

    if (getcwd(old, sizeof(old)) == NULL) {  
        old[0] = '\0'; 
    }

    if (chdir(target) < 0) { 
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
        return 1;
    }

    char cwd[PATH_MAX];

    if (old[0] != '\0') {
        if (setenv("OLDPWD", old, 1) < 0) {
            perror("cd: OLDPWD");
            return 1;
        }
    }

    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        if (setenv("PWD", cwd, 1) < 0) {
            perror("cd: PWD");
            return 1;
        }

        if (print_target) {
            printf("%s\n", cwd);
        }
    }

    return 0;
}

/* Print current working directory */
int builtin_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) == NULL) { //getcwd(buffer,buffer_size)
        fprintf(stderr, "pwd: %s\n", strerror(errno));
        return 1;
    }

    printf("%s\n", cwd);
    return 0;
}

/* Print text and handle escape characters */

static int echo_print(const char *str, int interpret) { 
    for (; *str; str++) {
        if (!interpret || *str != '\\' || str[1] == '\0') {
            putchar(*str);
            continue;
        }

        str++;

        switch (*str) {
            case 'n':
                putchar('\n');
                break;

            case 't':
                putchar('\t');
                break;

            case 'r':
                putchar('\r');
                break;

            case 'a':
                putchar('\a');
                break;

            case 'b':
                putchar('\b');
                break;

            case 'e':
                putchar(27);
                break;

            case '\\':
                putchar('\\');
                break;

            case 'c':
                return 1;

            default:
                putchar('\\');
                putchar(*str);
                break;
        }
    }

    return 0;
}

int builtin_echo(int argc, char **argv) {
    int newline = 1;
    int interpret = 0;
    int i = 1;

    /* Check echo options */
    for (; i < argc; i++) {
        const char *arg = argv[i];

        if (arg[0] != '-' || arg[1] == '\0') {
            break;
        }

        int valid = 1;

        for (const char *p = arg + 1; *p; p++) {
            if (*p != 'n' && *p != 'e' && *p != 'E') {
                valid = 0;
                break;
            }
        }

        if (!valid) {
            break;
        }

        for (const char *p = arg + 1; *p; p++) {
            if (*p == 'n') {
                newline = 0;
            } else if (*p == 'e') {
                interpret = 1;
            } else {
                interpret = 0;
            }
        }
    }

    int first = i;

    for (; i < argc; i++) {
        if (i > first) {
            putchar(' ');
        }

        if (echo_print(argv[i], interpret)) {
            newline = 0;
            break;
        }
    }

    if (newline) {
        putchar('\n');
    }

    return 0;
}

/* Exit the shell */
int builtin_exit(int argc, char **argv) {
    static int warned = 0;
    int code = last_exit_status;

    if (argc > 2) {
        fprintf(stderr, "exit: too many arguments\n");
        return 1;
    }

    if (argc == 2) {
        char *end;
        errno = 0;

        long number = strtol(argv[1], &end, 10);

        if (*argv[1] == '\0' || *end != '\0' || errno == ERANGE) {
            fprintf(stderr, "exit: %s: numeric argument required\n", argv[1]);
            code = 2;
        } else {
            code = (unsigned char)number;
        }
    }

    if (shell_interactive && !warned &&
        count_jobs_in_state(JOB_STOPPED) > 0) {

        fprintf(stderr, "There are stopped jobs.\n");
        warned = 1;
        return 1;
    }

    shell_exit_code = code;
    shell_running = 0;

    return code;
}

/* Show command history */
int builtin_history(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        if (argc > 2) {
            fprintf(stderr, "history: too many arguments\n");
            return 1;
        }

        history_clear();
        return 0;
    }

    if (argc > 2) {
        fprintf(stderr, "history: too many arguments\n");
        return 1;
    }

    int total = history_count();
    int start = 0;

    if (argc == 2) {
        char *end;
        long number = strtol(argv[1], &end, 10);

        if (*argv[1] == '\0' || *end != '\0' || number < 0) {
            fprintf(stderr, "history: %s: numeric argument required\n", argv[1]);
            return 1;
        }

        if (number < total) {
            start = total - (int)number;
        }
    }

    for (int i = start; i < total; i++) {
        printf("%5d  %s\n", i + 1, history_get(i));
    }

    return 0;
}

/* Check whether a variable name is valid */
static int valid_name(const char *name, size_t len) {
    if (len == 0) {
        return 0;
    }

    if (!isalpha((unsigned char)name[0]) && name[0] != '_') {
        return 0;
    }

    for (size_t i = 1; i < len; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_') {
            return 0;
        }
    }

    return 1;
}

/* Check for NAME=value */
int is_assignment_word(const char *word) {
    if (word == NULL) {
        return 0;
    }

    const char *eq = strchr(word, '=');

    if (eq == NULL) {
        return 0;
    }

    return valid_name(word, (size_t)(eq - word));
}

/* Store variable assignments in the shell */
int run_assignments(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        char *eq = strchr(argv[i], '=');

        if (eq == NULL || !valid_name(argv[i], (size_t)(eq - argv[i]))) {
            fprintf(stderr, "shell: invalid assignment: %s\n", argv[i]);
            return 1;
        }

        char *name = xstrndup(argv[i], (size_t)(eq - argv[i]));

        if (setenv(name, eq + 1, 1) < 0) {
            perror("setenv");
            free(name);
            return 1;
        }

        free(name);
    }

    return 0;
}

/* Display or set environment variables */
int builtin_export(int argc, char **argv) {
    if (argc == 1) {
        for (char **e = environ; e != NULL && *e != NULL; e++) {
            const char *eq = strchr(*e, '=');

            if (eq != NULL) {
                printf("declare -x %.*s=\"%s\"\n",
                       (int)(eq - *e), *e, eq + 1);
            }
        }

        return 0;
    }

    int status = 0;

    for (int i = 1; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');

        size_t name_len;

        if (eq != NULL) {
            name_len = (size_t)(eq - argv[i]);
        } else {
            name_len = strlen(argv[i]);
        }

        if (!valid_name(argv[i], name_len)) {
            fprintf(stderr, "export: `%s': not a valid identifier\n", argv[i]);
            status = 1;
            continue;
        }

        if (eq != NULL) {
            char *name = xstrndup(argv[i], name_len);

            if (setenv(name, eq + 1, 1) < 0) {
                perror("export");
                status = 1;
            }

            free(name);
        }
    }

    return status;
}

/* Remove environment variables */
int builtin_unset(int argc, char **argv) {
    int status = 0;

    for (int i = 1; i < argc; i++) {
        if (!valid_name(argv[i], strlen(argv[i]))) {
            fprintf(stderr, "unset: `%s': not a valid identifier\n", argv[i]);
            status = 1;
            continue;
        }

        if (unsetenv(argv[i]) < 0) {
            perror("unset");
            status = 1;
        }
    }

    return status;
}

/* List of built-in commands */
typedef int (*builtin_fn)(int argc, char **argv);

static const struct {
    const char *name;
    builtin_fn fn;
} builtin_table[] = {
    {"cd", builtin_cd},
    {"pwd", builtin_pwd},
    {"echo", builtin_echo},
    {"exit", builtin_exit},
    {"history", builtin_history},
    {"jobs", builtin_jobs},
    {"fg", builtin_fg},
    {"bg", builtin_bg},
    {"kill", builtin_kill},
    {"export", builtin_export},
    {"unset", builtin_unset}
};

#define NUM_BUILTINS (sizeof(builtin_table) / sizeof(builtin_table[0]))

int is_builtin(const char *name) {
    if (name == NULL) {
        return 0;
    }

    for (int i = 0; i < NUM_BUILTINS; i++) {
        if (strcmp(name, builtin_table[i].name) == 0) {
            return 1;
        }
    }

    return 0;
}

int run_builtin(int argc, char **argv) {
    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return 1;
    }

    for (size_t i = 0; i < NUM_BUILTINS; i++) {
        if (strcmp(argv[0], builtin_table[i].name) == 0) {
            return builtin_table[i].fn(argc, argv);
        }
    }

    return 127;
}