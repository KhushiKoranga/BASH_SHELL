
#include "shell.h"

volatile sig_atomic_t got_sigchld = 0;
volatile sig_atomic_t got_sigcont = 0;

// SIGCHLD handler: notify the shell that a child process changed state.
static void handle_sigchld(int sig) {
    (void)sig;
    got_sigchld = 1;
}

// SIGCONT handler: notify the shell that it has been continued.
static void handle_sigcont(int sig) {
    (void)sig;
    got_sigcont = 1;
}

// Set up a signal handler or ignore a signal.
static void set_handler(int sig, void (*handler)(int), int flags) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;

    sigaction(sig, &sa, NULL);
}

// Configure signal handling for the shell.
void setup_signals(int interactive) {
    if (interactive) {
        set_handler(SIGINT, SIG_IGN, 0);
        set_handler(SIGQUIT, SIG_IGN, 0);
        set_handler(SIGTSTP, SIG_IGN, 0);
    }

    set_handler(SIGTTIN, SIG_IGN, 0);
    set_handler(SIGTTOU, SIG_IGN, 0);

    // Restart system calls for non-interactive input.
    int flags = interactive ? 0 : SA_RESTART;

    set_handler(SIGCHLD, handle_sigchld, flags);
    set_handler(SIGCONT, handle_sigcont, flags);
}

// Restore default signal handling in child processes.
void reset_child_signals(void) {
    set_handler(SIGINT, SIG_DFL, 0);
    set_handler(SIGQUIT, SIG_DFL, 0);
    set_handler(SIGTSTP, SIG_DFL, 0);
    set_handler(SIGTTIN, SIG_DFL, 0);
    set_handler(SIGTTOU, SIG_DFL, 0);
    set_handler(SIGCHLD, SIG_DFL, 0);
    set_handler(SIGCONT, SIG_DFL, 0);
}
