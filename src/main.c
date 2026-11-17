#include "shell.h"

int main(int argc, char *argv[]) {
    const char *command = NULL;

    // Handle the -c option to execute a single command.
    if (argc >= 2 && strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            fprintf(stderr, "%s: -c requires a command\n", SHELL_NAME);
            return 2;
        }

        command = argv[2];
    }
    // Read commands from a script file.
    else if (argc >= 2) {
        if (freopen(argv[1], "r", stdin) == NULL) {
            fprintf(stderr, "%s: %s: %s\n",
                    SHELL_NAME, argv[1], strerror(errno));
            return 127;
        }
    }

    init_shell();

    // Execute the command passed with -c.
    if (command != NULL) {
        process_line(command);
        return shell_finish(0);
    }

    // Start the interactive shell or read commands from stdin.
    shell_loop();

    return shell_finish(1);
}
