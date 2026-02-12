/**
 * @file builtins.c
 * @brief Implementation of shell builtin commands
 */

#include "builtins.h"
#include "var_exp.h"

static int update_no_noti_jobs(t_shell *shell) {

  sigset_t block_mask, old_mask;
  int reap_flag = 1;
  if (sigemptyset(&block_mask) == -1) {
    perror("sigemptyset");
    reap_flag = 0;
  }
  if (sigaddset(&block_mask, SIGCHLD) == -1) {
    perror("sigaddset");
    reap_flag = 0;
  }

  if (sigprocmask(SIG_BLOCK, &block_mask, &old_mask) == -1) {
    perror("sigproc");
    reap_flag = 0;
  }

  size_t i = 0;
  t_job *job = NULL;

  if (!reap_flag) {
    return -1;
  }

  while (i < shell->job_table_cap) {
    job = shell->job_table[i];
    if (!job) {
      i++;
      continue;
    }

    int status;
    pid_t pid;

    while ((pid = waitpid(-job->pgid, &status, WNOHANG)) > 0) {

      t_process *process = find_process_in_job(job, pid);
      if (!process)
        break;

      if (WIFEXITED(status) || WIFSIGNALED(status)) {
        process->completed = 1;
        process->stopped = 0;
        process->running = 0;
      } else if (WIFSTOPPED(status)) {
        process->stopped = 1;
        process->completed = 0;
        process->running = 0;
      } else if (WIFCONTINUED(status)) {
        process->running = 1;
        process->stopped = 0;
        process->completed = 0;
      }
    }

    if (job && is_job_completed(job) && job->position == P_BACKGROUND) {
      job->state = S_COMPLETED;
    } else if (job && is_job_stopped(job)) {
      job->state = S_STOPPED;
      job->position = P_BACKGROUND;
    } else if (job && job->position == P_BACKGROUND) {
      job->state = S_RUNNING;
    }

    i++;
  }

  if (sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1) {
    perror("sigproc");
  }

  if (is_job_table_empty(shell)) {
    if (reset_job_table_cap(shell) == -1) {
      exit(EXIT_FAILURE);
    }
    shell->next_job_id = 1;
  }
  return 0;
}

void free_env_entry(void *value) {
  t_env_entry *entry = (t_env_entry *)value;
  if (entry) {
    free(entry->name);
    free(entry->val);
    free(entry);
  }
}

void free_builtin(void *value) {
  if (value)
    free(value);
}

int wait_for_foreground_job(t_job *job, t_shell *shell) {

  int status;
  pid_t pid;
  while (!is_job_completed(job)) {

    pid = waitpid(-job->pgid, &status, WUNTRACED | WCONTINUED);
    if (pid <= 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    t_process *process = find_process_in_job(job, pid);
    if (!process)
      continue;

    if (WIFEXITED(status) == 1 || WIFSIGNALED(status) == 1) {
      process->stopped = 0;
      process->running = 0;
      process->completed = 1;
    } else if (WIFSTOPPED(status) == 1) {
      process->stopped = 1;
      process->running = 0;
      process->completed = 0;
      job->state = S_STOPPED;

      print_job_info(job);
      return -1;
    } else if (WIFCONTINUED(status) == 1) {
      process->stopped = 0;
      process->running = 1;
      process->completed = 0;
    }

    if (is_job_completed(job) == 1) {

      del_job(shell, job->job_id, false);
      return 0;
    }
  }

  return 0;
}

/**
 * @brief Builtin cd command - change directory
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return 0 on success, 1 on failure
 */
int cd_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  if (argv[1] == NULL) {
    char *ptr = getenv("HOME");
    if (!ptr) {
      perror("208: err getting home dir");
      return -1;
    }
    int chdir_status = chdir(ptr);
    if (chdir_status == -1) {
      fprintf(stderr, "chdir fail\n");
      return -1;
    }
  } else {
    char *ptr = argv[1];
    int chdir_status = chdir(ptr);
    if (chdir_status == -1) {
      fprintf(stderr, "msh: cd: file not found\n");
      return -1;
    }
  }

  return 0;
}

int jobs_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  if (shell->job_control_flag == -1) {
    fprintf(stderr, "\nmsh: job control disabled");
    return -1;
  }

  update_no_noti_jobs(shell);

  for (size_t i = 0; i < shell->job_table_cap; i++) {

    if (shell->job_table[i] == NULL)
      continue;
    if (shell->job_table[i]->pgid == -1)
      continue;
    if (shell->job_table[i]->pgid == shell->pgid)
      continue;

    print_job_info(shell->job_table[i]);
  }

  return 0;
}

int fg_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  if (shell->job_control_flag == -1) {
    fprintf(stderr, "\nmsh: job control disabled");
    return -1;
  }

  update_no_noti_jobs(shell);

  int i = 0;
  while (argv[i] != NULL)
    i++;
  int argc = i;

  if (argc < 2 || argc > 2) {
    fprintf(stderr, "\nmsh: fg: syntax error: fg %%<JOB_ID>");
    return -1;
  }

  char *id_str = argv[1];

  if (id_str[0] == '%') {
    id_str++;
  }

  int job_id = atoi(id_str);
  if (job_id <= 0) {
    fprintf(stderr, "\nmsh: invalid job id.");
    return -1;
  }

  t_job *job = find_job(shell, job_id);
  if (!job) {
    fprintf(stderr, "\nmsh: no job with id %d", job_id);
    return -1;
  }

  if (job->position == P_FOREGROUND) {
    return 0;
  }

  if (is_job_stopped(job) == 1) {

    job->state = S_RUNNING;
    t_process *p = job->processes;
    while (p) {
      if (!p->completed) {
        p->running = 1;
        p->stopped = 0;
      }

      p = p->next;
    }
  }

  job->position = P_FOREGROUND;

  if (tcsetpgrp(shell->tty_fd, job->pgid) == -1) {
    if (errno == ESRCH && job)
      del_job(shell, job->job_id, false);
    return -1;
  }

  if (kill(-job->pgid, SIGCONT) < 0) {
    perror("failed to restart stopped job");
    if (tcsetpgrp(shell->tty_fd, shell->pgid) == -1) {
      perror("fg: terminal reclaim failed");
    }
    return -1;
  }

  wait_for_foreground_job(job, shell);

  if (tcsetpgrp(shell->tty_fd, shell->pgid) == -1) {
    perror("fg: terminal reclaim failed");
  }

  return 0;
}

int bg_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  if (shell->job_control_flag == -1) {
    fprintf(stderr, "\nmsh: job control disabled");
    return -1;
  }

  update_no_noti_jobs(shell);

  if (!argv)
    return -1;

  int i = 0;
  while (argv[i] != NULL)
    i++;
  int argc = i;

  if (argc < 2 || argc > 2) {
    fprintf(stderr, "\nIncorrect usage: bg %%<JOB_ID>");
    return -1;
  }

  char *id_str = argv[1];

  if (id_str[0] == '%') {
    id_str++;
  }

  int job_id = atoi(id_str);
  if (job_id <= 0) {
    fprintf(stderr, "\nInvalid job id.");
    return -1;
  }

  t_job *job = find_job(shell, job_id);
  if (!job) {
    fprintf(stderr, "\nmsh: no job with id %d", job_id);
    return -1;
  }

  if (job->position == P_BACKGROUND && !is_job_stopped(job)) {
    fprintf(stderr, "bg: job %d is already running in background\n", job_id);
    return 0;
  }

  job->position = P_BACKGROUND;
  job->state = S_RUNNING;

  if (kill(-job->pgid, SIGCONT) < 0) {
    perror("bg: SIGCONT failed");
    return -1;
  }

  t_process *p = job->processes;
  while (p) {
    if (!p->completed) {
      p->stopped = 0;
      p->running = 1;
    }
    p = p->next;
  }

  print_job_info(job);
  printf(" &\n");

  return 0;
}

int echo_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  if (!argv || !argv[0])
    return 0;

  bool newline = true;
  int i = 1;
  if (argv[i] && strcmp(argv[i], "-n") == 0) {
    newline = false;
    i++;
  }
  for (; argv[i]; i++) {
    fputs(argv[i], stdout);
    if (argv[i + 1])
      putchar(' ');
  }
  if (newline)
    putchar('\n');

  return 0;
}

int stty_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  (void)node;

  if (!isatty(shell->tty_fd)) {
    fprintf(stderr, "stty: not a tty\n");
    return -1;
  }

  struct termios t;
  if (tcgetattr(shell->tty_fd, &t) == -1) {
    perror("stty");
    return -1;
  }

  if (argv[1] == NULL) {
    if (t.c_lflag & TOSTOP)
      printf("tostop\n");
    else
      printf("-tostop\n");

    return 0;
  }

  for (int i = 1; argv[i]; i++) {

    if (strcmp(argv[i], "tostop") == 0) {
      t.c_lflag |= TOSTOP;
    } else if (strcmp(argv[i], "-tostop") == 0) {
      t.c_lflag &= ~TOSTOP;
    } else {
      fprintf(stderr, "stty: unkown option: %s\n", argv[i]);
      return -1;
    }
  }

  shell->term_ctrl.ogl_term_settings = t;

  if (tcsetattr(shell->tty_fd, TCSANOW, &shell->term_ctrl.ogl_term_settings) ==
      -1) {
    perror("stty");
    return -1;
  }

  return 0;
}

int true_builtin(t_ast_n *node, t_shell *shell, char **argv) { return 0; }
int false_builtin(t_ast_n *node, t_shell *shell, char **argv) { return -1; }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int test_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  int argc = 0;
  while (argv[argc])
    argc++;

  if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
    fprintf(stderr, "msh: expected ']'\n");
    return 2;
  }

  if (argc == 2)
    return 1;
  if (argc == 3)
    return (argv[1] && strlen(argv[1]) > 0) ? 0 : 1;

  if (argc == 5) {
    char *left = argv[1];
    char *op = argv[2];
    char *right = argv[3];

    int val1 = atoi(left);
    int val2 = atoi(right);

    if (strcmp(op, "-lt") == 0)
      return (val1 < val2) ? 0 : 1;
    if (strcmp(op, "-gt") == 0)
      return (val1 > val2) ? 0 : 1;
    if (strcmp(op, "-eq") == 0)
      return (val1 == val2) ? 0 : 1;
    if (strcmp(op, "-le") == 0)
      return (val1 <= val2) ? 0 : 1;
    if (strcmp(op, "-ge") == 0)
      return (val1 >= val2) ? 0 : 1;
    if (strcmp(op, "==") == 0)
      return (strcmp(left, right) == 0) ? 0 : 1;
    if (strcmp(op, "!=") == 0)
      return (strcmp(left, right) != 0) ? 0 : 1;
  }

  fprintf(stderr, "msh: test: too many arguments or unknown operator\n");
  return 2;
}

static void print_signals_list(void) {
  printf(" 1. SIGHUP       2. SIGINT       3. SIGQUIT      4. SIGILL\n");
  printf(" 5. SIGTRAP      6. SIGABRT      7. SIGBUS       8. SIGFPE\n");
  printf(" 9. SIGKILL     10. SIGUSR1     11. SIGSEGV     12. SIGUSR2\n");
  printf("13. SIGPIPE     14. SIGALRM     15. SIGTERM     17. SIGCHLD\n");
  printf("18. SIGCONT     19. SIGSTOP     20. SIGTSTP     21. SIGTTIN\n");
  printf("22. SIGTTOU\n");
}
int kill_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  if (argv[1] == NULL) {
    fprintf(stderr, "msh: kill: not enough arguments\n");
    return -1;
  }

  if (strcmp(argv[1], "-l") == 0) {
    print_signals_list();
    return 0;
  }

  if (argv[2] == NULL || argv[3] != NULL) {
    fprintf(stderr, "msh: kill: incorrect syntax\n");
    return -1;
  }

  update_no_noti_jobs(shell);

  char *sig_str = (argv[1][0] == '-') ? argv[1] + 1 : argv[1];
  char *sytx_check = sig_str;
  while (*sytx_check) {
    if (!isdigit((unsigned char)*sytx_check)) {
      fprintf(stderr, "msh: kill: invalid signal: %s\n", sig_str);
      return -1;
    }
    sytx_check++;
  }
  int signum = atoi(sig_str);

  t_job *job = NULL;
  pid_t target = -1;
  int job_id = 0;
  if (argv[2][0] == '%') {
    job = find_job(shell, atoi(argv[2] + 1));
    if (!job) {
      fprintf(stderr, "msh: kill: %s: no such job or pgid\n", argv[2]);
      return -1;
    }
    target = job->pgid;
    job_id = job->job_id;
  } else {
    target = (pid_t)atoi(argv[2]);
    job = find_job_by_pid(shell, target);
    if (job)
      job_id = job->job_id;
    else
      job_id = 0;
  }

  if (kill(-target, signum) < 0) {
    perror("msh: kill");
    return -1;
  }

  if (signum == SIGKILL || signum == SIGTERM || signum == SIGHUP ||
      signum == SIGINT) {
    printf("[%d] Killed - %d\n", job_id, target);
    job->state = S_COMPLETED;
  } else if (signum == SIGSTOP || signum == SIGTSTP || signum == SIGTTIN ||
             signum == SIGTTOU) {
    job->state = S_STOPPED;
    printf("[%d] Stopped - %d\n", job_id, target);
  } else if (signum == SIGCONT) {
    job->state = S_RUNNING;
    printf("[%d] Resumed - %d\n", job_id, target);
  } else {
    printf("[%d] Signaled (%d) - %d\n", job_id, signum, target);
  }

  update_no_noti_jobs(shell);
  return 0;
}

int export_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  if (argv[1] == NULL) {
    return 0;
  }

  char *str = argv[1];
  char *eq_ptr = strchr(str, '=');
  char *var_name = NULL;
  char *var_val = NULL;

  if (eq_ptr == NULL) {
    var_name = strdup(str);
    t_ht_node *nd = ht_find(&shell->env, var_name);
    if (nd) {
      t_env_entry *entry = (t_env_entry *)nd->value;
      var_val = strdup(entry->val);
    } else {
      var_val = strdup("");
    }
  } else {
    size_t name_len = eq_ptr - str;
    var_name = strndup(str, name_len);
    var_val = strdup(eq_ptr + 1);
  }

  if (!var_name || !var_val) {
    free(var_name);
    free(var_val);
    return -1;
  }
  add_to_env(shell, var_name, var_val);
  setenv(var_name, var_val, 1);

  free(var_name);
  free(var_val);
  return 0;
}

/**
 * @brief Builtin unset command - remove environment variable
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return 0 on success, 1 on failure
 */
int unset_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  if (argv[1] == NULL) {
    fprintf(stderr, "msh: usage: unset <VAR>\n");
    return -1;
  }

  remove_from_env(&shell->env, argv[1]);
  unsetenv(argv[1]);

  return 0;
}

int v_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  char *eq_ptr = strchr(argv[0], '=');
  char *var_name = strndup(argv[0], eq_ptr - argv[0]);
  char *var_val = strdup(eq_ptr + 1);

  if (add_to_env(shell, var_name, var_val) == -1) {
    fprintf(stderr, "msh: add_to_env failed\n");
  }

  free(var_name);
  free(var_val);
  return 0;
}

/**
 * @brief Builtin clear command - clear terminal screen
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Always returns 0
 */
int clear_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  HANDLE_WRITE_FAIL_FATAL(0, "\033[2J\033[H", 7, NULL);
  return 0;
}

/**
 * @brief Builtin alias command - manage command aliases
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return 0 on success, -1 on failure
 */
int alias_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  if (argv[1] == NULL) {
    print_aliases(&shell->aliases);
    return 0;
  } else if (argv[2] != NULL) {
    fprintf(stderr, "\nmsh: bad assignment");
  }

  int eq_idx = -1;
  char *str = argv[1];
  for (int i = 0; str[i]; i++) {
    if (str[i] == '=') {
      eq_idx = i;
      break;
    }
  }
  if (eq_idx == -1)
    return -1;

  char *alias = strndup(argv[1], eq_idx);
  size_t alias_len = strlen(argv[1]) - eq_idx - 1;
  char *aliased_cmd = strndup(argv[1] + eq_idx + 1, alias_len);
  t_ht_node *n = insert_alias(&(shell->aliases), alias, aliased_cmd);
  free(aliased_cmd);
  free(alias);
  if (!n) {
    perror("insert alias");
    return -1;
  }

  return 0;
}

/**
 * @brief Builtin unalias command - remove command aliases
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Always returns 0
 */
int unalias_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  if (argv[1] == NULL) {
    printf("\nmsh: unalias <alias>");
    return 0;
  }
  if (strcmp(argv[1], "all") == 0) {
    ht_flush(&shell->aliases, free_alias);
    return 0;
  }
  if (ht_delete(&(shell->aliases), argv[1], NULL) == -1) {
    printf("\nmsh: no such alias '%s'", argv[1]);
  }

  return 0;
}

/**
 * @brief Builtin exit command - terminate shell
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Never returns (calls exit())
 */
int exit_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  int exit_status = 0;
  if (argv && argv[1] != NULL) {
    exit_status = atoi(argv[1]);
  }

  /* perform last sweep reap */
  update_no_noti_jobs(shell);

  exit(exit_status);

  return 0; ///< Suppress err (unreachable)
}

/**
 * @brief Builtin env command - print environment variables
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Always returns 0
 */
int env_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  char **env = flatten_env(&shell->env, &shell->arena);
  int i = 0;
  while (env[i] != NULL) {
    printf("\n%s", env[i]);
    i++;
  }

  return 0;
}

/**
 * @brief Builtin history command - print command history
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Always returns 0
 */
int history_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  if (shell->history.size == 0) {
    return 0;
  }

  int max = shell->history.size;

  if (argv[1] != NULL) {
    max = atoi(argv[1]);
    if (max == 0)
      max = shell->history.size;
  }

  t_dllnode *ptr = shell->history.head->next;
  int i = 0;
  while (ptr && i < max) {
    printf("\n%d      %s", i + 1, ptr->strbg);
    ptr = ptr->next;
    i++;
  }

  return 0;
}
