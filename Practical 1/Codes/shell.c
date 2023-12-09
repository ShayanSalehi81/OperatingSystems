#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h> 

#define FALSE 0
#define TRUE 1
#define INPUT_STRING_SIZE 80

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

/* Command Lookup table */
typedef int cmd_fun_t (tok_t args[]); /* cmd functions take token array and return int */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

int cmd_help(tok_t arg[]);
int cmd_quit(tok_t arg[]);
int cmd_pwd(tok_t arg[]);
int cmd_cd(tok_t arg[]);
int cmd_wait(tok_t arg[]);

fun_desc_t cmd_table[] = {
	{cmd_help, "?", "show this help menu"},
	{cmd_quit, "quit", "quit the command shell"},
	{cmd_pwd, "pwd", "get directory"},
	{cmd_cd, "cd", "go to path"},
	{cmd_wait, "wait", "wait for all processes to finish"},
};


process *create_process(tok_t *arg, int background);

int cmd_help(tok_t arg[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    printf("%s - %s\n",cmd_table[i].cmd, cmd_table[i].doc);
  }
  return 1;
}

int cmd_quit(tok_t arg[]) {
  printf("Bye\n");
  exit(0);
  return 1;
}

int cmd_pwd(tok_t arg[]) {
    char buffer[PATH_MAX];

    if (getcwd(buffer, sizeof(buffer)) == NULL) {
        perror("getting current directory failed!");
        // free(buffer);
        return -1;
    }

    printf("%s\n", buffer);
    // free(buffer);
    return 1;
}

int cmd_cd(tok_t arg[]) {
    if (arg == NULL || arg[0] == NULL) {
        fprintf(stderr, "No path specified for cd command\n");
        return -1;
    }

    int result = chdir(arg[0]);

    if (result != 0) {
        perror("cd failed");
        return -1;
    }

    return 0;
}

int cmd_wait(tok_t arg[]) {
    int status;
    pid_t child_pid;

    do {
        child_pid = waitpid(-1, &status, WNOHANG);

        if (child_pid > 0) {
            if (WIFEXITED(status)) {
                printf("Process %d terminated with exit status %d\n", child_pid, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("Process %d terminated due to signal %d\n", child_pid, WTERMSIG(status));
            }
        }
    } while (child_pid > 0 || (child_pid == -1 && errno == EINTR));

    if (child_pid == -1 && errno != ECHILD) {
        perror("waitpid");
        return -1;
    }

    return 1;
}

void execute_external_command(tok_t cmd, tok_t args[], int read, tok_t read_file, int write, tok_t write_file, int background) {
    process *proc;
    int fdIn, fdOut;
    pid_t cpid, current_pid;

    current_pid = getpid();
    proc = create_process(args, background);
    if (!proc) return;

    add_process(proc);

    cpid = fork();
    if (cpid < 0) {
        perror("Error in fork");
        return;
    }

    if (cpid == 0) { // Child process
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        proc->pid = getpid();

        if (write) {
            fdOut = open(write_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fdOut < 0) {
                perror("Error opening output file");
                exit(EXIT_FAILURE);
            }
            dup2(fdOut, STDOUT_FILENO);
            close(fdOut);
        }

        if (read) {
            fdIn = open(read_file, O_RDONLY);
            if (fdIn < 0) {
                perror("Error opening input file");
                exit(EXIT_FAILURE);
            }
            dup2(fdIn, STDIN_FILENO);
            close(fdIn);
        }

        exec_command(cmd, args);
        exit(EXIT_FAILURE); // If exec_command returns, it failed
    } else { // Parent process
        proc->pid = cpid;

        if (!background) {
            int status;
            do {
                waitpid(cpid, &status, WUNTRACED);
            } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        }
    }
}

void exec_command(tok_t cmd, tok_t args[]) {
    if (execv(cmd, args) == -1) {
        search_and_exec_command(cmd, args);
    }
}

void search_and_exec_command(tok_t cmd, tok_t args[]) {
    char *path = getenv("PATH");
    char *path_env = (char*) malloc(1024);
    strncpy(path_env, path, 1024);
    char fullPath[1024];
    int found = 0;

    if (path_env != NULL) {
        char *token;
        for (token = strtok(path_env, ":"); token != NULL; token = strtok(NULL, ":")) {
            snprintf(fullPath, sizeof(fullPath), "%s/%s", token, cmd);
            if (execv(fullPath, args) != -1) {
                found = 1;
                break;
            }
        }
        if (!found) {
            printf("Command not found: %s\n", cmd);
        }
    }
}

int lookup(char cmd[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  }
  return -1;
}

void init_shell()
{
  /* Check if we are running interactively */
  shell_terminal = STDIN_FILENO;

  /** Note that we cannot take control of the terminal if the shell
      is not interactive */
  shell_is_interactive = isatty(shell_terminal);

  if(shell_is_interactive){

    /* force into foreground */
    while(tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp()))
      kill( - shell_pgid, SIGTTIN);

    shell_pgid = getpid();
    /* Put shell in its own process group */
    if(setpgid(shell_pgid, shell_pgid) < 0){
      perror("Couldn't put the shell in its own process group");
      exit(1);
    }

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);
    tcgetattr(shell_terminal, &shell_tmodes);

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
  }

  initialize_process(&first_process, getpid(), NULL, 0);

}

/**
 * Add a process to our process list
 */
void add_process(process* new_proc)
{
   if (!first_process) {
        first_process = new_proc;
        return;
    }

    process *current = first_process;
    while (current->next != NULL) {
        current = current->next;
    }

    current->next = new_proc;
    new_proc->prev = current;
}

/**
 * Creates a process given the inputString from stdin
 */
process* create_process(tok_t *args, int is_background)
{
  if (!args || !args[0]) {
        return NULL;
    }

    process *new_proc = (process *)malloc(sizeof(process));
    initialize_process(new_proc, -1, args, is_background);
    return new_proc;
}

void initialize_process(process *proc, pid_t process_id, tok_t args[], int is_background) {
    proc->pid = (process_id == -1) ? getpid() : process_id;
    proc->stopped = 0;
    proc->completed = 0;
    proc->background = is_background;
    proc->stdin = STDIN_FILENO;
    proc->stdout = STDOUT_FILENO;
    proc->stderr = STDERR_FILENO;
    proc->prev = NULL;
    proc->next = NULL;
    proc->argv = args;
}

void refresh_process_status() {
    int process_status;
    pid_t completed_pid;

    while (1) {
        completed_pid = waitpid(-1, &process_status, WNOHANG); // Non-blocking wait

        if (completed_pid <= 0) {
            break;
        }

        if (!update_individual_process_status(completed_pid, process_status)) {
            break;
        }
        
        if (WIFEXITED(process_status) || WIFSIGNALED(process_status)) {
            mark_process_as_completed(completed_pid);
        }
    }
}

void mark_process_as_completed(pid_t pid) {
    process* current = first_process;
    while (current != NULL) {
        if (current->pid == pid) {
            current->completed = TRUE;
            break;
        }
        current = current->next;
    }
}

int shell (int argc, char *argv[]) {
  char *s = malloc(INPUT_STRING_SIZE+1);			/* user input string */
  tok_t *t;			                                /* tokens parsed from input */
  int lineNum = 0;
  int fundex = -1;
  pid_t pid = getpid();		                        /* get current processes PID */
  pid_t ppid = getppid();	                        /* get parents PID */
  pid_t cpid, tcpid, cpgid;

  init_shell();

  // printf("%s running as PID %d under %d\n",argv[0],pid,ppid);

  lineNum=0;
  // fprintf(stdout, "%d: ", lineNum);
  while ((s = freadln(stdin))){
    t = getToks(s); /* break the line into tokens */
    fundex = lookup(t[0]); /* Is first token a shell literal */
    if(fundex >= 0) cmd_table[fundex].fun(&t[1]);
    else {
      handle_non_builtin_commands(t);
    }
    // fprintf(stdout, "%d: ", lineNum);
    refresh_process_status();
  }
  return 0;
}

void handle_non_builtin_commands(tok_t *tokens) {
  char *write_file = NULL, *read_file = NULL;
  int is_write = 0, is_read = 0, is_background = 0;

  for (int i = 0; tokens[i] != NULL; i++) {
    if (strcmp(tokens[i], ">") == 0 && tokens[i + 1] != NULL) {
      is_write = 1;
      write_file = tokens[i + 1];
      tokens[i] = NULL;
    } else if (strcmp(tokens[i], "<") == 0 && tokens[i + 1] != NULL) {
      is_read = 1;
      read_file = tokens[i + 1];
      tokens[i] = NULL;
    } else if (strcmp(tokens[i], "&") == 0) {
      is_background = 1;
      tokens[i] = NULL;
    }
  }

  if (is_write || is_read || is_background) {
    execute_external_command(tokens[0], tokens, is_read, read_file, is_write, write_file, is_background);
  } else {
    execute_external_command(tokens[0], tokens, 0, NULL, 0, NULL, 0);
  }
}
