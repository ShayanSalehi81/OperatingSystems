#include "process.h"
#include "shell.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>

/**
 * Executes the process p.
 * If the shell is in interactive mode and the process is a foreground process,
 * then p should take control of the terminal.
 */
void launch_process(process *p)
{
   if (shell_is_interactive && !p->background) {
        setpgid(p->pid, p->pid);

        tcsetpgrp(STDIN_FILENO, p->pid);

        if (p->stopped) {
            kill(-p->pid, SIGCONT);
        }
    }
}

/* Put a process in the foreground. This function assumes that the shell
 * is in interactive mode. If the cont argument is true, send the process
 * group a SIGCONT signal to wake it up.
 */
void put_process_in_foreground (process *proc) {
  int proc_status;
  pid_t shell_pgid = first_process->pid;

  if (tcsetpgrp(STDIN_FILENO, proc->pid) != 0) {
      perror("tcsetpgrp error");
      return;
  }

  if (waitpid(proc->pid, &proc_status, WUNTRACED) < 0) {
      perror("waitpid error");
      return;
  }

  tcsetpgrp(STDIN_FILENO, shell_pgid);
}

/* Put a process in the background. If the cont argument is true, send
 * the process group a SIGCONT signal to wake it up. */
void put_process_in_background (process *p, int cont)
{
  if (cont && p->stopped) {
      kill(-p->pid, SIGCONT);
  }
}

int update_individual_process_status(pid_t pid, int new_status)
{
  process *current_process;
  for (current_process = first_process; current_process; current_process = current_process->next) {
    if (current_process->pid == pid) {
      current_process->status = new_status;
      current_process->completed = 1;
      return 0;
    }
  }
  return -1;
}
