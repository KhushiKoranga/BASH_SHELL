/*
 * execution.c - Executes parsed commands and pipelines.
 */

#include "shell.h"


/* ---------- Child process ---------- */

static void run_child_process(const ExpandedCommand *ec, int pipes[][2],
                              int index, int stages, pid_t pgid, int background) {
    if (job_control_active()) {
        pid_t self = getpid();

        if (pgid == 0)
            pgid = self;

        setpgid(self, pgid);

        if (!background && terminal_control_active())
            tcsetpgrp(shell_terminal, pgid);
    }

    reset_child_signals();

    connect_pipeline_stage(pipes, index, stages);

    if (apply_redirections(ec->redirs, ec->nredirs) < 0)
        _exit(1);

    if (ec->argc == 0)
        _exit(0);

    /* Builtins inside pipelines must run in the child. */
    if (is_builtin(ec->argv[0])) {
        int status = run_builtin(ec->argc, ec->argv);
        fflush(NULL);
        _exit(status);
    }

    execvp(ec->argv[0], ec->argv);

    int saved_errno = errno;

    if (saved_errno == ENOENT && strchr(ec->argv[0], '/') == NULL) {
        fprintf(stderr, "%s: %s: command not found\n",
                SHELL_NAME, ec->argv[0]);
    } else {
        fprintf(stderr, "%s: %s: %s\n",
                SHELL_NAME, ec->argv[0], strerror(saved_errno));
    }

    _exit(saved_errno == ENOENT ? 127 : 126);
}

/* ---------- Commands executed in the shell ---------- */

static int all_assignments(const ExpandedCommand *ec) {
    if (ec->argc == 0)
        return 0;

    for (int i = 0; i < ec->argc; i++) {
        if (!is_assignment_word(ec->argv[i]))
            return 0;
    }

    return 1;
}

/*
 * Handles assignments, redirections and builtins that can run
 * directly inside the shell.
 */
static int run_in_shell(const ExpandedCommand *ec, int *status) {
    int assignments = all_assignments(ec);
    int builtin = ec->argc > 0 && is_builtin(ec->argv[0]);

    if (ec->argc != 0 && !assignments && !builtin)
        return 0;

    int saved[3];

    fflush(NULL);
    save_std_fds(saved);

    if (apply_redirections(ec->redirs, ec->nredirs) < 0) {
        *status = 1;
    } else if (ec->argc == 0) {
        *status = 0;
    } else if (assignments) {
        *status = run_assignments(ec->argc, ec->argv);
    } else {
        *status = run_builtin(ec->argc, ec->argv);
    }

    fflush(NULL);
    restore_std_fds(saved);

    return 1;
}

/* ---------- Pipeline execution ---------- */

static void abort_launch(Job *job) {
    if (job == NULL)
        return;

    kill(-job->pgid, SIGKILL);
    kill(-job->pgid, SIGCONT);

    wait_for_job(job);
    remove_job(job->job_id);
}

static int launch_pipeline(const Pipeline *pl,
                           const ExpandedCommand *cmds,
                           int background) {
    int stages = pl->ncmds;
    int pipes[MAX_PROCS_PER_JOB][2];

    Job *job = NULL;
    pid_t pgid = 0;
    pid_t last_pid = 0;

    for (int i = 0; i < MAX_PROCS_PER_JOB; i++) {
        pipes[i][0] = -1;
        pipes[i][1] = -1;
    }

    if (jobs_table_full()) {
        fprintf(stderr, "%s: job table is full\n", SHELL_NAME);
        return 1;
    }

    if (create_pipes(pipes, stages - 1) < 0) {
        perror("pipe");
        return 1;
    }

    fflush(NULL);

    for (int i = 0; i < stages; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            close_all_pipes(pipes, stages - 1);
            abort_launch(job);
            return 1;
        }

        if (pid == 0) {
            run_child_process(&cmds[i], pipes, i, stages, pgid, background);
        }

        /* Parent process. */
        if (pgid == 0)
            pgid = pid;

        if (job_control_active())
            setpgid(pid, pgid);

        if (job == NULL) {
            int id = add_job(pgid, pl->text, JOB_RUNNING);
            job = (id >= 0) ? find_job(id) : NULL;

            if (job == NULL) {
                kill(-pgid, SIGKILL);
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
                close_all_pipes(pipes, stages - 1);
                return 1;
            }
        }

        job_add_process(job, pid);
        last_pid = pid;

        /* Close pipe ends that the parent no longer needs. */
        if (i > 0 && pipes[i - 1][0] >= 0) {
            close(pipes[i - 1][0]);
            pipes[i - 1][0] = -1;
        }

        if (i < stages - 1 && pipes[i][1] >= 0) {
            close(pipes[i][1]);
            pipes[i][1] = -1;
        }
    }

    if (background) {
        put_job_in_background(job, 0);

        if (!in_subshell) {
            printf("[%d] %d\n", job->job_id, (int)last_pid);
            fflush(stdout);
        }

        return 0;
    }

    return put_job_in_foreground(job, 0);
}

/* ---------- Public pipeline function ---------- */

int execute_pipeline(Pipeline *pl, int background) {
    int stages = pl->ncmds;
    int status = 1;

    if (stages > MAX_PROCS_PER_JOB) {
        fprintf(stderr, "%s: pipeline too long (maximum %d commands)\n",
                SHELL_NAME, MAX_PROCS_PER_JOB);
        return 1;
    }

    ExpandedCommand *cmds = xcalloc((size_t)stages, sizeof *cmds);
    int expanded = 0;

    for (int i = 0; i < stages; i++) {
        if (expand_command(&pl->cmds[i], &cmds[i]) < 0)
            goto done;

        expanded++;
    }

    /* A single foreground builtin should run in the parent shell. */
    if (stages == 1 && !background &&
        run_in_shell(&cmds[0], &status)) {
        goto done;
    }

    status = launch_pipeline(pl, cmds, background);

done:
    for (int i = 0; i < expanded; i++)
        free_expanded_command(&cmds[i]);

    free(cmds);
    return status;
}

/* ---------- Command lists ---------- */

/* AND/OR chains are evaluated from left to right. */
static int execute_and_or(AndOrList *ao) {
    int status = 0;

    for (int i = 0; i < ao->npipelines; i++) {
        if (i > 0) {
            Connector connector = ao->connectors[i - 1];

            if ((connector == CONN_AND && status != 0) ||
                (connector == CONN_OR && status == 0)) {
                continue;
            }
        }

        status = execute_pipeline(&ao->pipelines[i], 0);
        last_exit_status = status;

        if (!shell_running)
            break;
    }

    return status;
}

/* Runs an entire AND/OR chain in a background subshell. */
static int run_background_chain(AndOrList *ao) {
    if (jobs_table_full()) {
        fprintf(stderr, "%s: job table is full\n", SHELL_NAME);
        return 1;
    }

    fflush(NULL);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        in_subshell = 1;
        setpgid(0, 0);
        reset_child_signals();
        clear_jobs();

        int status = execute_and_or(ao);

        fflush(NULL);
        _exit(status);
    }

    setpgid(pid, pid);

    int id = add_job(pid, ao->text, JOB_RUNNING);
    Job *job = (id >= 0) ? find_job(id) : NULL;

    if (job == NULL) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return 1;
    }

    job_add_process(job, pid);
    put_job_in_background(job, 0);

    printf("[%d] %d\n", job->job_id, (int)pid);
    fflush(stdout);

    return 0;
}

/* ---------- Public command-list function ---------- */

int execute_command_list(CommandList *list) {
    for (int i = 0; i < list->nitems && shell_running; i++) {
        AndOrList *item = &list->items[i];
        int status;

        if (item->background) {
            if (item->npipelines == 1)
                status = execute_pipeline(&item->pipelines[0], 1);
            else
                status = run_background_chain(item);
        } else {
            status = execute_and_or(item);
        }

        last_exit_status = status;
    }

    return last_exit_status;
}