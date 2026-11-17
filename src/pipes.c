/* pipes.c - Helper functions for pipeline execution. For N commands, we need N-1 pipes.
 * Example:   cmd1 | cmd2 | cmd3     cmd1 -> pipe1 -> cmd2 -> pipe2 -> cmd3
*/
#include "shell.h"

/* Create the required number of pipes. */
int create_pipes(int pipes[][2], int count) {
    for (int i = 0; i < count; i++) {
        if (pipe(pipes[i]) < 0) {
            close_all_pipes(pipes, i);
            return -1;
        }
    }

    return 0;
}

/* Close all pipe file descriptors. */
void close_all_pipes(int pipes[][2], int count) {
    for (int i = 0; i < count; i++) {
        if (pipes[i][0] >= 0) {
            close(pipes[i][0]);
            pipes[i][0] = -1;
        }

        if (pipes[i][1] >= 0) {
            close(pipes[i][1]);
            pipes[i][1] = -1;
        }
    }
}

/*
 * Connect a child process to the correct pipeline stage.
 *
 * index = 0       -> first command
 * index = 1       -> middle command
 * index = stages-1 -> last command
 */
//dup2() is a Linux system call used to duplicate one file descriptor onto another file descriptor.
void connect_pipeline_stage(int pipes[][2], int index, int stages) {
    if (index > 0) {
        if (dup2(pipes[index - 1][0], STDIN_FILENO) < 0) {  //dup2(oldfd,newfd): oldfd->existing file discriptor that we want to use.
            perror("dup2: stdin");
            _exit(1);
        }
    }

    if (index < stages - 1) {
        if (dup2(pipes[index][1], STDOUT_FILENO) < 0) {
            perror("dup2: stdout");
            _exit(1);
        }
    }

    /* Close original pipe descriptors after connecting them. */
    close_all_pipes(pipes, stages - 1);
}