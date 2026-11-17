
#include "shell.h"
#include <ctype.h>

Job job_table[MAX_JOBS];

// Find the next available job ID.
static int next_free_job_id(void) {
    int max_id = 0;

    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].active && job_table[i].job_id > max_id) {
            max_id = job_table[i].job_id;
        }
    }

    return max_id + 1;
}

// Add a new job to the table.
int add_job(pid_t pgid, const char *command, JobState state) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_table[i].active) {
            Job *job = &job_table[i];

            memset(job, 0, sizeof(*job));

            job->job_id = next_free_job_id();
            job->pgid = pgid;
            snprintf(job->command, sizeof(job->command), "%s", command);

            job->state = state;
            job->active = 1;

            return job->job_id;
        }
    }

    fprintf(stderr, "%s: job table is full\n", SHELL_NAME);
    return -1;
}

// Add a process to an existing job.
int job_add_process(Job *job, pid_t pid) {
    if (job->nprocs >= MAX_PROCS_PER_JOB) {
        return -1;
    }

    Process *process = &job->procs[job->nprocs++];

    process->pid = pid;
    process->status = 0;
    process->completed = 0;
    process->stopped = 0;

    return 0;
}

// Find a job using its job ID.
Job *find_job(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].active && job_table[i].job_id == job_id) {
            return &job_table[i];
        }
    }

    return NULL;
}

// Find which job owns a particular process.
Job *find_job_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_table[i].active) {
            continue;
        }

        for (int j = 0; j < job_table[i].nprocs; j++) {
            if (job_table[i].procs[j].pid == pid) {
                return &job_table[i];
            }
        }
    }

    return NULL;
}

// Remove a job from the table.
void remove_job(int job_id) {
    Job *job = find_job(job_id);

    if (job != NULL) {
        job->active = 0;
    }
}

// Clear the complete job table.
void clear_jobs(void) {
    memset(job_table, 0, sizeof(job_table));
}

// Check whether the job table is full.
int jobs_table_full(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_table[i].active) {
            return 0;
        }
    }

    return 1;
}

// Count jobs having a particular state.
int count_jobs_in_state(JobState state) {
    int count = 0;

    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].active && job_table[i].state == state) {
            count++;
        }
    }

    return count;
}

// Find the next job with an ID greater than the given ID.
static Job *next_job_after(int id) {
    Job *next = NULL;

    for (int i = 0; i < MAX_JOBS; i++) {
        Job *job = &job_table[i];

        if (job->active && job->job_id > id) {
            if (next == NULL || job->job_id < next->job_id) {
                next = job;
            }
        }
    }

    return next;
}

// Update a job's state using the states of its processes.
void update_job_state(Job *job) {
    int all_completed = 1;
    int any_running = 0;

    for (int i = 0; i < job->nprocs; i++) {
        if (!job->procs[i].completed) {
            all_completed = 0;

            if (!job->procs[i].stopped) {
                any_running = 1;
            }
        }
    }

    if (all_completed) {
        job->state = JOB_DONE;
    } else if (!any_running) {
        job->state = JOB_STOPPED;
    } else {
        job->state = JOB_RUNNING;
    }
}


void mark_process_status(pid_t pid, int status) {
    Job *job = find_job_by_pid(pid);

    if (job == NULL) {
        return;
    }

    for (int i = 0; i < job->nprocs; i++) {
        Process *process = &job->procs[i];

        if (process->pid != pid) {
            continue;
        }

        if (WIFSTOPPED(status)) { //it checks if process has stopped
            process->stopped = 1;
            process->status = status;
        } else if (WIFCONTINUED(status)) { //if process has resumed
            process->stopped = 0;
        } else {
            process->completed = 1;
            process->stopped = 0;
            process->status = status;
        }

        break;
    }

    update_job_state(job);
}

void update_jobs(void) { 
    while (1) {  
        int status; 
        pid_t pid = waitpid(-1, &status,WNOHANG | WUNTRACED | WCONTINUED); 
       
        if (pid > 0) {  
            mark_process_status(pid, status);
        } else if (pid < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

//job_exit_status() returns the exit code of the last process in a job, handling both normal and signal-based termination.
int job_exit_status(const Job *job) {
    if (job->nprocs == 0) {
        return 0;
    }//Exit code is a number returned by a program or process when it finishes execution. It tells the operating system or shell whether the program executed successfully or encountered an error.

    const Process *last = &job->procs[job->nprocs - 1];

    if (WIFEXITED(last->status)) {  //checks whethere a process exited normally
        return WEXITSTATUS(last->status);//extracts its exit code
    }

    if (WIFSIGNALED(last->status)) { //checks whether a process terminated due to a signal
        return 128 + WTERMSIG(last->status); //extracts the signal number
    }

    return 1;
}

//SIGTTIN-stops a background process that tries to read from the terminal
int job_stop_signal(const Job *job) {
    for (int i = 0; i < job->nprocs; i++) {
        Process *process = (Process *)&job->procs[i];

        if (process->stopped && WIFSTOPPED(process->status)) {
            return WSTOPSIG(process->status);
        }
    }

    return SIGTSTP;
}

// Convert a job state into readable text.
void job_status_text(const Job *job, char *buf, size_t size) { /
    switch (job->state) {
        case JOB_RUNNING:
            snprintf(buf, size, "Running");

        case JOB_STOPPED:
            snprintf(buf, size, "Stopped");
            break;

        case JOB_DONE:
        default:
            if (job->nprocs > 0) { //checks whether the job contains atleast one process .Why? because we need to examine the last process,s exit status
                int status = job->procs[job->nprocs - 1].status;

                if (WIFSIGNALED(status)) {/
                    snprintf(buf, size, "%s",strsignal(WTERMSIG(status)));//Extracts the signal number responsible for termination.
                    break; //strsignal Converts the signal number into a human-readable description.
                }

                if (WIFEXITED(status) && WEXITSTATUS(status) != 0) { /
                    snprintf(buf, size, "Exit %d",WEXITSTATUS(status));
                    break;
                } 
            }

            snprintf(buf, size, "Done");
            break;
    }
}

// Get the current or previous job.
//rank=0 current job(most recently created active job).
//rank=1 previous job(second most recently created active job).
//it returns a pointer to the selected Job
static Job *job_by_rank(int rank, int include_done) { //static this function can only be accessed inside jobs.c
    Job *first = NULL;
    Job *second = NULL;

    for (int i = 0; i < MAX_JOBS; i++) {
        Job *job = &job_table[i];

        if (!job->active || (!include_done && job->state == JOB_DONE)) {
            continue;
        }

        if (first == NULL || job->job_id > first->job_id) {
            second = first;
            first = job;
        } else if (second == NULL || job->job_id > second->job_id) {
            second = job;
        }
    }

    return rank == 0 ? first : second;
}

Job *job_current(void) {
    return job_by_rank(0, 0);
}

Job *job_previous(void) {
    return job_by_rank(1, 0);
}

// Find a job using its job specification.
Job *job_from_spec(const char *spec) {
    if (spec == NULL || *spec == '\0') {
        return job_current();
    }

    const char *name = spec;
    int has_percent = (*name == '%');

    if (has_percent) {
        name++;
    }

    if (*name == '\0' || strcmp(name, "%") == 0 ||strcmp(name, "+") == 0) {
        return job_current();
    }

    if (strcmp(name, "-") == 0) {
        return job_previous();
    }

    if (isdigit((unsigned char)*name)) {
        char *end;
        long id = strtol(name, &end, 10);

        if (*end != '\0' || id > 1000000 || id < 1) {
            return NULL;
        }

        Job *job = find_job((int)id);

        return (job != NULL && job->state != JOB_DONE) ? job : NULL;
    }

    if (has_percent) {
        int contains = (*name == '?');

        if (contains) {
            name++;
        }

        if (*name == '\0') {
            return NULL;
        }

        Job *match = NULL;

        for (int i = 0; i < MAX_JOBS; i++) {
            Job *job = &job_table[i];

            if (!job->active || job->state == JOB_DONE) {
                continue;
            }

            int found = contains? strstr(job->command, name) != NULL: strncmp(job->command, name, strlen(name)) == 0;

            if (found && (match == NULL || job->job_id > match->job_id)) {
                match = job;
            }
        }

        return match;
    }

    return NULL;
}

// Print information about a job.
void print_job_line(const Job *job, int show_pids) {
    char status[64];
    job_status_text(job, status, sizeof(status));

    char marker = ' ';

    if (job == job_by_rank(0, 1)) {
        marker = '+';
    } else if (job == job_by_rank(1, 1)) {
        marker = '-';
    }

    const char *background =
        (job->background && job->state == JOB_RUNNING) ? " &" : "";

    if (show_pids && job->nprocs > 0) {
        printf("[%d]%c %6d %-12s %s%s\n",
               job->job_id, marker,
               (int)job->procs[0].pid,
               status, job->command, background);

        for (int i = 1; i < job->nprocs; i++) {
            printf("     %6d\n", (int)job->procs[i].pid);
        }
    } else {
        printf("[%d]%c  %-12s %s%s\n",
               job->job_id, marker,
               status, job->command, background);
    }
}

// Display jobs, optionally filtering by state.
void list_jobs(int show_pids, int filter) {
    Job *job = next_job_after(0);

    while (job != NULL) {
        int id = job->job_id;
        int show = 1;

        if (filter == JOBS_ONLY_RUNNING && job->state != JOB_RUNNING) {
            show = 0;
        }

        if (filter == JOBS_ONLY_STOPPED && job->state != JOB_STOPPED) {
            show = 0;
        }

        if (show) {
            print_job_line(job, show_pids);

            if (job->state == JOB_DONE) {
                remove_job(id);
            } else if (job->state == JOB_STOPPED) {
                job->notified = 1;
            }
        }

        job = next_job_after(id);
    }
}

// Check for finished or stopped background jobs.
void reap_jobs(void) {
    got_sigchld = 0;
    update_jobs();

    Job *job = next_job_after(0);

    while (job != NULL) {
        int id = job->job_id;

        if (job->state == JOB_DONE) {
            if (job->background) {
                print_job_line(job, 0);
            }

            remove_job(id);
        } else if (job->state == JOB_STOPPED && !job->notified) {
            print_job_line(job, 0);
            job->notified = 1;
        }

        job = next_job_after(id);
    }

    fflush(stdout);
}

// Send termination signals to remaining jobs when the shell exits.
void terminate_all_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *job = &job_table[i];

        if (!job->active || job->state == JOB_DONE) {
            continue;
        }

        kill(-job->pgid, SIGHUP);
        kill(-job->pgid, SIGCONT);
    }
}
