# BASH_SHELL – Custom Unix Shell

A custom Unix-like command-line shell implemented in C, inspired by the functionality of the Bash shell. This project demonstrates core Operating System concepts such as process creation, inter-process communication, job control, signal handling, and input/output redirection.

## About the Project

BASH_SHELL is a simplified command-line interpreter that allows users to execute commands and interact with the Linux operating system.

The shell reads user input, parses commands, executes built-in and external commands, and manages processes. It also supports pipes, redirection, foreground and background execution, and job management.

The main objective of this project is to understand how a shell works internally and how operating system concepts are implemented using C and Linux system calls.

## Features

- **Command Execution:** Execute external Linux commands.
- **Built-in Commands:** Support for commands such as `cd`, `pwd`, `echo`, `history`, and `exit`.
- **Pipes:** Connect multiple commands using the pipe (`|`) operator.
- **Input/Output Redirection:** Support input (`<`), output (`>`), and append (`>>`) redirection.
- **Error Redirection:** Redirect standard error using operators such as `2>` and `2>>`.
- **Job Control:** Manage foreground and background processes.
- **Job Table:** Maintain information about active jobs, their process IDs, and execution states.
- **Signal Handling:** Handle signals such as `SIGINT`, `SIGTSTP`, and `SIGCHLD`.
- **Command History:** Maintain and display previously executed commands.

## Technologies Used

- **Programming Language:** C
- **Operating System:** Linux (Ubuntu / WSL)
- **System Calls and APIs:** `fork()`, `execvp()`, `waitpid()`, `pipe()`, `dup2()`, `open()`, `close()`, `kill()`
- **Build Tool:** GNU Make

## Project Structure

```text
BASH_SHELL/
│
├── include/
│   └── shell.h
│
├── src/
│   ├── main.c
│   ├── shell.c
│   ├── parsing.c
│   ├── execution.c
│   ├── builtins.c
│   ├── pipes.c
│   ├── redirection.c
│   ├── jobs.c
│   ├── control.c
│   ├── signals.c
│   └── history.c
│
├── Makefile
├── .gitignore
└── README.md
```

## Installation and Execution

### 1. Clone the Repository

```bash
git clone https://github.com/KhushiKoranga/BASH_SHELL.git
```

### 2. Navigate to the Project Directory

```bash
cd BASH_SHELL
```

### 3. Compile the Project

```bash
make
```

### 4. Run the Shell

```bash
./myshell
```

*Note: Use the executable name specified in your Makefile if it differs from `myshell`.*

## Example Commands

```bash
pwd
ls
echo Hello World
cd ..
history
```

### Pipe Example

```bash
ls | wc -l
```

This lists the entries in the current directory and counts the output lines.

### Redirection Examples

```bash
echo Hello World > output.txt
cat < output.txt
echo New Line >> output.txt
```

### Job Control Examples

```bash
sleep 100 &
jobs
fg %1
```

## Operating System Concepts Implemented

This project demonstrates the practical implementation of several Operating System concepts:

1. Process Creation and Management
2. Parent-Child Process Relationships
3. Inter-Process Communication (IPC)
4. Anonymous Pipes
5. File Descriptors
6. Input/Output Redirection
7. Foreground and Background Processes
8. Process Groups and Job Control
9. Signal Handling
10. Process Synchronization

## Learning Outcomes

- Understanding the internal working of a command-line shell.
- Learning process creation and execution using Linux system calls.
- Understanding communication between processes using pipes.
- Implementing input and output redirection using file descriptors.
- Managing foreground and background jobs.
- Understanding signal handling and process synchronization.

## Author

**Khushi Koranga**

GitHub: [KhushiKoranga](https://github.com/KhushiKoranga)

---

*This project was developed as an academic project to explore Linux system programming and Operating System concepts using C.*