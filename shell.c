#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include <fcntl.h>
#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_id(struct tokens *tokens);
int cmd_run(struct tokens *tokens);
int cmd_du(struct tokens *tokens);

/* Built-in command functions take token array and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_id, "id", "display the user-id, the primary group-id and the groups the user is part of"},
  {cmd_run, "run", "run the executable"},
  {cmd_du, "du", "support for output redirection"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
  exit(0);
}

/* Display the user-id, the primary group-id and the groups the user is part of */
int cmd_id(unused struct tokens *tokens){
  struct passwd *p = getpwuid(getuid());
  printf("User name: %s\n", p->pw_name);
  printf("User ID is %d\n", getuid());
  printf("Group ID is %d\n", getgid());
  int ngroups, i;
  gid_t groups[NGROUPS_MAX];
  ngroups = NGROUPS_MAX;
  if ( getgrouplist( getlogin(), getegid(), groups, &ngroups) == -1) {
   printf ("Groups array is too small: %d\n", ngroups);
  }
  printf ("%s belongs to these groups: %d", getlogin(), getegid());
  for (i=0; i < ngroups; i++) {
    printf (", %d", groups[i]);
  }
  printf ("\n");
  return 0;
}

/* Function for running executable file */
int cmd_run(unused struct tokens *tokens){
  char *add=tokens_get_token(tokens,0);
  pid_t pid=fork();
  if (pid==0) { 
      static char *argv[]={"",NULL};
      execv(add,argv);
      exit(127); 
  }
  else { 
      waitpid(pid,0,0); 
  }
  return 0;
}

/* Function for output redirection */
int cmd_du(unused struct tokens *tokens){  
  pid_t pid=fork();
  if (pid==0) { 
      char *file=tokens_get_token(tokens,2);
      int fd = open(file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
      dup2(fd, 1);  
      dup2(fd, 2); 
      cmd_run(tokens);
      close(fd); 
      exit(127); 
  }
  else { 
      waitpid(pid,0,0); 
  }
  return 0;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[], size_t tokens_size) {
  if (tokens_size == 3) {
      return 4;
  }
  if (cmd[0]=='/')
      return 3;
  
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++) {
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  }
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);
    
    // printf("%zu ", tokens_get_length(tokens));
    size_t tokens_size = tokens_get_length(tokens);
    
    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0),tokens_size);
    
    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      fprintf(stdout, "This shell doesn't know how to run programs.\n");
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
