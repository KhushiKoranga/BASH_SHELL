#include "shell.h"
#include <fcntl.h>

/* Open a file and redirect it to the required file descriptor. */
static int redirect_to_file(const char *path, int flags, int target_fd) {
    int fd = open(path, flags, 0644);

    if (fd < 0) {
        fprintf(stderr, "%s: %s: %s\n",SHELL_NAME, path, strerror(errno));
        return -1;
    }

    if (fd != target_fd) {
        if (dup2(fd, target_fd) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);
    }

    return 0;
}

/* Apply all redirections in the order they were written. */
int apply_redirections(const ExpandedRedir *redirs, int count) {
    for (int i = 0; i < count; i++) {
        const ExpandedRedir *r = &redirs[i];
        int rc = 0;

        switch (r->type) {
            case REDIR_IN:
                rc = redirect_to_file(
                    r->path, O_RDONLY, STDIN_FILENO
                );
                break;

            case REDIR_OUT:
                rc = redirect_to_file(
                    r->path,
                    O_WRONLY | O_CREAT | O_TRUNC,
                    STDOUT_FILENO
                );
                break;

            case REDIR_APPEND:
                rc = redirect_to_file(
                    r->path,
                    O_WRONLY | O_CREAT | O_APPEND,
                    STDOUT_FILENO
                );
                break;

            case REDIR_ERR:
                rc = redirect_to_file(
                    r->path,
                    O_WRONLY | O_CREAT | O_TRUNC,
                    STDERR_FILENO
                );
                break;

            case REDIR_ERR_APPEND:
                rc = redirect_to_file(
                    r->path,
                    O_WRONLY | O_CREAT | O_APPEND,
                    STDERR_FILENO
                );
                break;

            case REDIR_ERR_TO_OUT:
                if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
                    perror("dup2");
                    rc = -1;
                }
                break;

            case REDIR_OUT_TO_ERR:
                if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
                    perror("dup2");
                    rc = -1;
                }
                break;

            default:
                fprintf(stderr, "%s: invalid redirection\n", SHELL_NAME);
                rc = -1;
                break;
        }

        if (rc < 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Save stdin, stdout and stderr before running a builtin
 * inside the shell process.
 */
int save_std_fds(int saved[3]) {
    for (int fd = 0; fd < 3; fd++) {
        saved[fd] = fcntl(fd, F_DUPFD_CLOEXEC, 10);

        if (saved[fd] < 0) {
            perror("fcntl");

            /* Close descriptors that were already saved. */
            for (int j = 0; j < fd; j++) {
                close(saved[j]);
                saved[j] = -1;
            }

            return -1;
        }
    }

    return 0;
}

/* Restore the original standard input, output and error. */
void restore_std_fds(const int saved[3]) {
    for (int fd = 0; fd < 3; fd++) {
        if (saved[fd] >= 0) {
            if (dup2(saved[fd], fd) < 0) {
                perror("dup2");
            }

            close(saved[fd]);
        }
    }
}