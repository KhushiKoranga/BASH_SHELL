#include "shell.h"
#include <strings.h>
#include <ctype.h>

pid_t shell_pgid; //Process groupID of the shell
int shell_terminal = STDIN_FILENO; //file descriptor of the terminal
struct termios shell_tmodes;  //stores terminal settings

// Initialize terminal control for the shell.
void setup_shell(void) {
    shell_terminal = STDIN_FILENO;
    shell_pgid = getpgrp(); //getgrep() returns the process groupID of the calling process
    shell_interactive = isatty(shell_terminal); //isatty() checks whether standard input is connected to a terminal

    if (shell_interactive) {  
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp())) { //tcgetgrp() returns the proccess group ID that currently controls the terminal
            //tcgetgrp(shell_terminal)->which process grpup currently owns the terminal? and getgrp() means which process group does our shell belong to? if they are diff,the shell sends itself SIGTTIN
            kill(-shell_pgid, SIGTTIN); //SIGTTIN is a signal that stops a bg process grp when it tries to read from the terminal.the shell waits until it is running in the foreground.
        }

        setup_signals(1);

        shell_pgid = getpid();

        if (setpgid(shell_pgid, shell_pgid) < 0 &&
            errno != EACCES && errno != EPERM) {
            perror("setpgid");
        }

        shell_pgid = getpgrp();

        tcsetpgrp(shell_terminal, shell_pgid);
        tcgetattr(shell_terminal, &shell_tmodes);
    } else {
        setup_signals(0);
    }
}

// Check whether normal job control is enabled.
int job_control_active(void) {
    return !in_subshell;
}

int terminal_control_active(void) {
    return shell_interactive && !in_subshell;
}

// Wait until a job finishes or stops.
int wait_for_job(Job *job) {
    while (job->state == JOB_RUNNING) {
        int status;
        pid_t pid = waitpid(-1, &status, in_subshell ? 0 : WUNTRACED);

        if (pid > 0) {
            mark_process_status(pid, status);
        } else if (pid < 0 && errno == EINTR) {
            continue;
        } else {
            for (int i = 0; i < job->nprocs; i++) {
                job->procs[i].completed = 1;
            }

            update_job_state(job);
        }
    }

    return job->state;
}

// Display a message when a process terminates because of a signal.
static void report_signal_death(const Job *job) {
    if (job->nprocs == 0) {
        return;
    }

    int status = job->procs[job->nprocs - 1].status;

    if (!WIFSIGNALED(status)) {
        return;
    }

    int signal = WTERMSIG(status);

    if (signal == SIGINT) {
        if (shell_interactive) {
            fputc('\n', stderr);
        }
    } else if (signal != SIGPIPE) {
        fprintf(stderr, "%s%s\n",
                strsignal(signal),
                WCOREDUMP(status) ? " (core dumped)" : "");
    }
}

// Move a job to the foreground and wait for it.
int put_job_in_foreground(Job *job, int cont) {
    int job_id = job->job_id;
    int use_terminal = terminal_control_active();

    job->background = 0;

    if (use_terminal) {
        tcsetpgrp(shell_terminal, job->pgid);
    }

    if (cont) {
        if (use_terminal && job->has_tmodes) {
            tcsetattr(shell_terminal, TCSADRAIN, &job->tmodes);
        }

        if (kill(-job->pgid, SIGCONT) < 0) {
            perror("kill (SIGCONT)");
        }
    }

    wait_for_job(job);

    if (use_terminal) {
        tcsetpgrp(shell_terminal, shell_pgid);

        if (job->state == JOB_STOPPED) {
            tcgetattr(shell_terminal, &job->tmodes);
            job->has_tmodes = 1;
        }

        tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
    }

    int status;

    if (job->state == JOB_STOPPED) {
        status = 128 + job_stop_signal(job);
        job->notified = 1;
        job->background = 1;

        printf("\n");
        print_job_line(job, 0);
        fflush(stdout);
    } else {
        status = job_exit_status(job);
        report_signal_death(job);
        remove_job(job_id);
    }

    return status;
}

// Keep a job running in the background.
void put_job_in_background(Job *job, int cont) {
    job->background = 1;

    if (cont && kill(-job->pgid, SIGCONT) < 0) {
        perror("kill (SIGCONT)");
    }
}

// Resume a stopped job in the foreground or background.
int continue_job(Job *job, int foreground) {
    for (int i = 0; i < job->nprocs; i++) {
        if (!job->procs[i].completed) {
            job->procs[i].stopped = 0;
        }
    }

    update_job_state(job);
    job->notified = 0;

    if (foreground) {
        return put_job_in_foreground(job, 1);
    }

    put_job_in_background(job, 1);
    return 0;
}

// Display information about background and stopped jobs.
int builtin_jobs(int argc, char **argv) {
    int show_pids = 0;
    int filter = JOBS_ALL;
    int i = 1;

    for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }

        for (const char *f = argv[i] + 1; *f; f++) {
            switch (*f) {
                case 'l':
                    show_pids = 1;
                    break;

                case 'r':
                    filter = JOBS_ONLY_RUNNING;
                    break;

                case 's':
                    filter = JOBS_ONLY_STOPPED;
                    break;

                default:
                    fprintf(stderr,
                            "jobs: -%c: invalid option\n"
                            "jobs: usage: jobs [-lrs] [jobspec ...]\n", *f);
                    return 2;
            }
        }
    }

    update_jobs();

    if (i < argc) {
        int status = 0;

        for (; i < argc; i++) {
            Job *job = job_from_spec(argv[i]);

            if (job == NULL) {
                fprintf(stderr, "jobs: %s: no such job\n", argv[i]);
                status = 1;
            } else {
                print_job_line(job, show_pids);
            }
        }

        return status;
    }

    list_jobs(show_pids, filter);
    return 0;
}

// Parse job-related arguments for built-in commands.
static Job *job_for_builtin(const char *name, int argc, char **argv) {
    const char *spec = argc > 1 ? argv[1] : NULL;
    Job *job = job_from_spec(spec);

    if (job == NULL) {
        if (spec == NULL) {
            fprintf(stderr, "%s: no current job\n", name);
        } else {
            fprintf(stderr, "%s: %s: no such job\n", name, spec);
        }
    }

    return job;
}

// Bring a job to the foreground.
int builtin_fg(int argc, char **argv) {
    Job *job = job_for_builtin("fg", argc, argv);

    if (job == NULL) {
        return 1;
    }

    printf("%s\n", job->command);
    fflush(stdout);

    return continue_job(job, 1);
}

// Resume a stopped job in the background.
int builtin_bg(int argc, char **argv) {
    Job *job = job_for_builtin("bg", argc, argv);

    if (job == NULL) {
        return 1;
    }

    if (job->state == JOB_RUNNING) {
        fprintf(stderr, "bg: job %d already in background\n", job->job_id);
        return 0;
    }

    printf("[%d]+ %s &\n", job->job_id, job->command);
    fflush(stdout);

    return continue_job(job, 0);
}

// Supported signal names for the kill command.
static const struct {
    const char *name;
    int number;
} signal_names[] = {
    {"HUP", SIGHUP}, {"INT", SIGINT}, {"QUIT", SIGQUIT},
    {"ILL", SIGILL}, {"ABRT", SIGABRT}, {"FPE", SIGFPE},
    {"KILL", SIGKILL}, {"SEGV", SIGSEGV}, {"PIPE", SIGPIPE},
    {"ALRM", SIGALRM}, {"TERM", SIGTERM}, {"USR1", SIGUSR1},
    {"USR2", SIGUSR2}, {"CHLD", SIGCHLD}, {"CONT", SIGCONT},
    {"STOP", SIGSTOP}, {"TSTP", SIGTSTP}, {"TTIN", SIGTTIN},
    {"TTOU", SIGTTOU}
};

#define NUM_SIGNAL_NAMES \
    (sizeof(signal_names) / sizeof(signal_names[0]))

// Convert a signal name or number into its signal value.
static int parse_signal(const char *text) {
    if (isdigit((unsigned char)*text)) {
        char *end;
        long number = strtol(text, &end, 10);

        return (*end == '\0' && number >= 0 && number < 65)
                   ? (int)number : -1;
    }

    if (strncasecmp(text, "SIG", 3) == 0) {
        text += 3;
    }

    for (size_t i = 0; i < NUM_SIGNAL_NAMES; i++) {
        if (strcasecmp(text, signal_names[i].name) == 0) {
            return signal_names[i].number;
        }
    }

    return -1;
}

// Send signals to processes or jobs.
int builtin_kill(int argc, char **argv) {
    int signal = SIGTERM;
    int i = 1;

    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        for (size_t k = 0; k < NUM_SIGNAL_NAMES; k++) {
            printf("%2d) SIG%s\n",
                   signal_names[k].number,
                   signal_names[k].name);
        }

        return 0;
    }

    if (argc > 2 && strcmp(argv[1], "-s") == 0) {
        signal = parse_signal(argv[2]);

        if (signal < 0) {
            fprintf(stderr, "kill: %s: invalid signal specification\n",
                    argv[2]);
            return 1;
        }

        i = 3;
    } else if (argc > 1 && argv[1][0] == '-' &&
               argv[1][1] != '\0' && strcmp(argv[1], "--") != 0) {
        signal = parse_signal(argv[1] + 1);

        if (signal < 0) {
            fprintf(stderr, "kill: %s: invalid signal specification\n",
                    argv[1] + 1);
            return 1;
        }

        i = 2;
    }

    if (i < argc && strcmp(argv[i], "--") == 0) {
        i++;
    }

    if (i >= argc) {
        fprintf(stderr,
                "kill: usage: kill [-s sigspec | -sigspec] pid | %%job ...\n");
        return 1;
    }

    int status = 0;

    for (; i < argc; i++) {
        const char *target = argv[i];

        if (target[0] == '%') {
            Job *job = job_from_spec(target);

            if (job == NULL) {
                fprintf(stderr, "kill: %s: no such job\n", target);
                status = 1;
                continue;
            }

            if (kill(-job->pgid, signal) < 0) {
                fprintf(stderr, "kill: %s: %s\n",
                        target, strerror(errno));
                status = 1;
                continue;
            }

            if (job->state == JOB_STOPPED &&
                (signal == SIGTERM || signal == SIGHUP ||
                 signal == SIGINT)) {
                kill(-job->pgid, SIGCONT);
            }
        } else {
            char *end;
            long pid = strtol(target, &end, 10);

            if (*target == '\0' || *end != '\0' || pid <= 0) {
                fprintf(stderr,
                        "kill: %s: arguments must be process or job IDs\n",
                        target);
                status = 1;
                continue;
            }

            if (kill((pid_t)pid, signal) < 0) {
                fprintf(stderr, "kill: (%ld) - %s\n",
                        pid, strerror(errno));
                status = 1;
            }
        }
    }

    return status;
}