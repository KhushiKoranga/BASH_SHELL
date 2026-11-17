#ifndef SHELL_H
#define SHELL_H
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>

/* Shell settings. */
#define SHELL_NAME "myshell"
#define PROMPT "myshell> "
#define MAX_LINE 4096

/* Job table settings. */
#define MAX_JOBS 100
#define MAX_PROCS_PER_JOB 32
#define MAX_JOB_COMMAND 256

#define JOBS_ALL 0
#define JOBS_ONLY_RUNNING 1
#define JOBS_ONLY_STOPPED 2

#define HISTORY_MAX 1000

/* Job and process information. */
typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobState;

typedef struct {
    pid_t pid;
    int status;
    int completed;
    int stopped;
} Process;

typedef struct {
    int job_id;
    pid_t pgid;
    char command[MAX_JOB_COMMAND];
    JobState state;
    int active;
    int background;
    int notified;
    int nprocs;
    Process procs[MAX_PROCS_PER_JOB];
    struct termios tmodes;
    int has_tmodes;
} Job;

/* Command parsing structures. */
typedef enum {
    REDIR_IN,
    REDIR_OUT,
    REDIR_APPEND,
    REDIR_ERR,
    REDIR_ERR_APPEND,
    REDIR_ERR_TO_OUT,
    REDIR_OUT_TO_ERR
} RedirType;

typedef struct {
    RedirType type;
    char *target;
} Redirection;

typedef struct {
    char **words;
    int nwords;
    Redirection *redirs;
    int nredirs;
} Command;

typedef struct {
    Command *cmds;
    int ncmds;
    char *text;
} Pipeline;

typedef enum {
    CONN_AND,
    CONN_OR
} Connector;

typedef struct {
    Pipeline *pipelines;
    Connector *connectors;
    int npipelines;
    int background;
    char *text;
} AndOrList;

typedef struct {
    AndOrList *items;
    int nitems;
} CommandList;

typedef struct {
    RedirType type;
    char *path;
} ExpandedRedir;

typedef struct {
    char **argv;
    int argc;
    ExpandedRedir *redirs;
    int nredirs;
} ExpandedCommand;

/* Shared shell state. */
extern pid_t shell_pgid;
extern int shell_terminal;
extern struct termios shell_tmodes;

extern int shell_interactive;
extern int in_subshell;
extern int last_exit_status;
extern int shell_running;
extern int shell_exit_code;

extern volatile sig_atomic_t got_sigchld;
extern volatile sig_atomic_t got_sigcont;

extern Job job_table[MAX_JOBS];

/* Memory helpers. */
void *xmalloc(size_t size);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* Shell setup and main loop. */
void init_shell(void);
int process_line(const char *line);
void shell_loop(void);
int shell_finish(int save_history);

/* Command parsing and expansion. */
int parse_line(const char *line, CommandList *list, char *err, size_t errsize);
void free_command_list(CommandList *list);
int expand_command(const Command *cmd, ExpandedCommand *out);
void free_expanded_command(ExpandedCommand *ec);

/* Command execution, pipes and redirection. */
int execute_command_list(CommandList *list);
int execute_pipeline(Pipeline *pipeline, int background);
int apply_redirections(const ExpandedRedir *redirs, int count);
int save_std_fds(int saved[3]);
void restore_std_fds(const int saved[3]);
int create_pipes(int pipes[][2], int count);
void close_all_pipes(int pipes[][2], int count);
void connect_pipeline_stage(int pipes[][2], int index, int stages);

/* Job table and process status. */
int add_job(pid_t pgid, const char *command, JobState state);
int job_add_process(Job *job, pid_t pid);
Job *find_job(int job_id);
Job *find_job_by_pid(pid_t pid);
void remove_job(int job_id);
void clear_jobs(void);
int jobs_table_full(void);
int count_jobs_in_state(JobState state);
void mark_process_status(pid_t pid, int status);
void update_job_state(Job *job);
void update_jobs(void);
int job_exit_status(const Job *job);
int job_stop_signal(const Job *job);
void job_status_text(const Job *job, char *buf, size_t size);
void print_job_line(const Job *job, int show_pids);
void list_jobs(int show_pids, int filter);
void reap_jobs(void);
void terminate_all_jobs(void);
Job *job_current(void);
Job *job_previous(void);
Job *job_from_spec(const char *spec);

/* Terminal and foreground/background job control. */
void setup_shell(void);
int terminal_control_active(void);
int job_control_active(void);
int wait_for_job(Job *job);
int put_job_in_foreground(Job *job, int cont);
void put_job_in_background(Job *job, int cont);
int continue_job(Job *job, int foreground);

/* Signal handling. */
void setup_signals(int interactive);
void reset_child_signals(void);

/* Built-in commands. */
int is_builtin(const char *name);
int run_builtin(int argc, char **argv);
int is_assignment_word(const char *word);
int run_assignments(int argc, char **argv);
int builtin_cd(int argc, char **argv);
int builtin_pwd(int argc, char **argv);
int builtin_echo(int argc, char **argv);
int builtin_exit(int argc, char **argv);
int builtin_history(int argc, char **argv);
int builtin_export(int argc, char **argv);
int builtin_unset(int argc, char **argv);
int builtin_jobs(int argc, char **argv);
int builtin_fg(int argc, char **argv);
int builtin_bg(int argc, char **argv);
int builtin_kill(int argc, char **argv);

/* Command history and interactive input. */
void history_add(const char *line);
int history_count(void);
const char *history_get(int index);
void history_clear(void);
void history_load(const char *path);
void history_save(const char *path);
const char *history_default_path(char *buf, size_t size);
char *history_expand(const char *line, int *error);
char *read_input_line(const char *prompt);

#endif
