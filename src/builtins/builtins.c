/**
 * @file builtins.c
 * @brief Implementation of shell builtin commands
 */

#include "builtins.h"
#include "executor.h"
#include "jobs.h"
#include "shell.h"
#include "shell_init.h"
#include "var_exp.h"
#include <linux/limits.h>

static void check_rehash(t_shell *shell, const char *var_name) {
  if (strcmp(var_name, "PATH") == 0) {
    shell->path = getenv_local_ref(&shell->env, "PATH");
    if (shell->path) {
      shell->path_len = strlen(shell->path);
    } else {
      shell->path_len = 0;
    }

    refresh_path_bins(shell);
  }
}

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
  char old_path[PATH_MAX];
  char new_path[PATH_MAX];
  const char *target = NULL;

  if (getcwd(old_path, sizeof(old_path)) == NULL) {
    old_path[0] = '\0';
  }

  if (argv[1] == NULL) {
    target = getenv_local_ref(&shell->env, "HOME");
    if (!target) {
      fprintf(stderr, "msh: cd: HOME not set\n");
      return -1;
    }
  } else {
    target = argv[1];
  }

  if (chdir(target) == -1) {
    perror("msh: cd");
    return -1;
  }

  if (getcwd(new_path, sizeof(new_path)) != NULL) {
    if (old_path[0] != '\0') {
      add_to_env(shell, "OLDPWD", old_path, false, 0);
    }
    add_to_env(shell, "PWD", new_path, false, 0);
  }

  return 0;
}

int local_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  (void)node; // shut up compiler

  if (argv[1] == NULL) {
    print_env(&shell->env, false, true);
    return 0;
  }

  char *eq = strchr(argv[1], '=');
  if (!eq || eq == argv[1]) {
    fprintf(stderr, "msh: local: invalid syntax\n");
    return -1;
  }

  size_t name_len = eq - argv[1];

  char *var_name = arena_alloc(&shell->arena, name_len + 1);
  char *var_val = eq + 1;

  memcpy(var_name, argv[1], name_len);
  var_name[name_len] = '\0';

  size_t depth = shell->exec_ctx.fnest_d;
  if (add_to_env(shell, var_name, var_val, true, depth) == -1) {
    fprintf(stderr, "msh: local: failed to add or change variable\n");
    return -1;
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
    fprintf(stderr, "msh: job control disabled\n");
    return -1;
  }

  update_no_noti_jobs(shell);

  int i = 0;
  while (argv[i] != NULL)
    i++;
  int argc = i;

  if (argc < 2 || argc > 2) {
    fprintf(stderr, "msh: fg: syntax error: fg %%<JOB_ID>\n");
    return -1;
  }

  char *id_str = argv[1];

  if (id_str[0] == '%') {
    id_str++;
  }

  int job_id = atoi(id_str);
  if (job_id <= 0) {
    fprintf(stderr, "msh: invalid job id.\n");
    return -1;
  }

  t_job *job = find_job(shell, job_id);
  if (!job) {
    fprintf(stderr, "msh: no job with id %d\n", job_id);
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
    fprintf(stderr, "msh: job control disabled\n");
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
    fprintf(stderr, "msh: incorrect usage: bg %%<JOB_ID>\n");
    return -1;
  }

  char *id_str = argv[1];

  if (id_str[0] == '%') {
    id_str++;
  }

  int job_id = atoi(id_str);
  if (job_id <= 0) {
    fprintf(stderr, "msh: invalid job id.\n");
    return -1;
  }

  t_job *job = find_job(shell, job_id);
  if (!job) {
    fprintf(stderr, "msh: no job with id %d\n", job_id);
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

static bool is_valid_echo_flag(const char *arg) {
  if (arg[0] != '-' || arg[1] == '\0')
    return false;
  for (int i = 1; arg[i]; i++) {
    if (arg[i] != 'n' && arg[i] != 'e' && arg[i] != 'E')
      return false;
  }
  return true;
}

static void print_escaped(const char *s) {
  for (size_t i = 0; s[i]; i++) {
    if (s[i] == '\\' && s[i + 1]) {
      switch (s[i + 1]) {
      case 'n':
        putchar('\n');
        i++;
        break;
      case 't':
        putchar('\t');
        i++;
        break;
      case 'r':
        putchar('\r');
        i++;
        break;
      case '\\':
        putchar('\\');
        i++;
        break;
      case 'a':
        putchar('\a');
        i++;
        break;
      case 'b':
        putchar('\b');
        i++;
        break;
      case 'v':
        putchar('\v');
        i++;
        break;
      case 'f':
        putchar('\f');
        i++;
        break;
      case '0': {
        int val = 0, n = 0;
        size_t j = i + 2;
        while (n < 3 && s[j] >= '0' && s[j] <= '7') {
          val = val * 8 + (s[j] - '0');
          j++;
          n++;
        }
        putchar(val);
        i = j - 1;
        break;
      }
      default:
        putchar(s[i]);
      }
    } else {
      putchar(s[i]);
    }
  }
}

int echo_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  (void)node;
  (void)shell;
  if (!argv || !argv[0])
    return 0;

  bool newline = true;
  bool interpret_escapes = false;
  int i = 1;

  while (argv[i] && is_valid_echo_flag(argv[i])) {
    for (int j = 1; argv[i][j]; j++) {
      if (argv[i][j] == 'n')
        newline = false;
      else if (argv[i][j] == 'e')
        interpret_escapes = true;
      else if (argv[i][j] == 'E')
        interpret_escapes = false;
    }
    i++;
  }

  for (; argv[i]; i++) {
    if (interpret_escapes)
      print_escaped(argv[i]);
    else
      fputs(argv[i], stdout);
    if (argv[i + 1])
      putchar(' ');
  }

  if (newline)
    putchar('\n');

  return 0;
}

int nop_builtin(t_ast_n *node, t_shell *shell, char **argv) { return 0; }

int builtin_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  if (argv[1] == NULL)
    return 0;

  t_ht_node *bin_node = ht_find(&shell->builtins, argv[1]);
  if (bin_node) {
    t_builtin *b = (t_builtin *)bin_node->value;
    return b->fn(node, shell, &argv[1]);
  }
  fprintf(stderr, "msh: %s: not a shell builtin\n", argv[1]);
  return -1;
}

int true_builtin(t_ast_n *node, t_shell *shell, char **argv) { return 0; }
int false_builtin(t_ast_n *node, t_shell *shell, char **argv) { return 1; }

int set_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  if (argv[1] == NULL) {
    printf("msh: set: expected arguments");
    return -1;
  }

  if (strcmp(argv[1], "-m") == 0) {
    shell->job_control_flag = 1;
  } else if (strcmp(argv[1], "+m") == 0) {
    shell->job_control_flag = 0;
  } else {
    printf("msh: set: unknown option");
    return -1;
  }

  return 0;
}

int test_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  int argc = 0;
  while (argv[argc])
    argc++;

  if (argc < 2 || *(argv[argc - 1]) != ']') {
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

int shopt_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  if (argv[1] == NULL) {
    fprintf(stderr, "shopt: missing operand\n");
    return 1;
  }

  if (strcmp(argv[1], "-s") == 0) {

    if (argv[2] == NULL) {
      fprintf(stderr, "shopt: missing option\n");
      return 1;
    }

    if (strcmp(argv[2], "autosuggest") == 0)
      shell->shopts.render_autosgst = true;
    else {
      fprintf(stderr, "shopt: unknown option\n");
      return 1;
    }
  } else if (strcmp(argv[1], "-u") == 0) {
    if (argv[2] == NULL) {
      fprintf(stderr, "shopt: missing option\n");
      return 1;
    }
    if (strcmp(argv[2], "autosuggest") == 0)
      shell->shopts.render_autosgst = false;
    else {
      fprintf(stderr, "shopt: unknown option\n");
      return 1;
    }
  } else {
    if (strcmp(argv[1], "autosuggest") == 0) {
      shell->shopts.render_autosgst ? printf("autosuggest     on")
                                    : printf("autosuggest     off");
    } else {
      fprintf(stderr, "shopt: unknown option\n");
      return 1;
    }
  }

  return 0;
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
    size_t name_len = strlen(str);
    var_name = arena_alloc(&shell->arena, name_len + 1);
    if (!var_name)
      return -1;
    memcpy(var_name, str, name_len + 1);

    t_ht_node *nd = ht_find(&shell->env, var_name);
    if (nd) {
      t_env_entry *entry = (t_env_entry *)nd->value;
      size_t val_len = strlen(entry->val);
      var_val = arena_alloc(&shell->arena, val_len + 1);
      if (!var_val)
        return -1;
      memcpy(var_val, entry->val, val_len + 1);

      entry->flags |= ENV_EXPORTED;
    } else {
      var_val = arena_alloc(&shell->arena, 1);
      if (!var_val)
        return -1;
      var_val[0] = '\0';
    }
  } else {
    size_t name_len = eq_ptr - str;
    size_t val_len = strlen(eq_ptr + 1);

    var_name = arena_alloc(&shell->arena, name_len + 1);
    var_val = arena_alloc(&shell->arena, val_len + 1);
    if (!var_name || !var_val)
      return -1;

    memcpy(var_name, str, name_len);
    var_name[name_len] = '\0';
    memcpy(var_val, eq_ptr + 1, val_len + 1);
  }

  if (add_to_env(shell, var_name, var_val, false, 0) == -1) {
    return -1;
  }

  t_ht_node *n = ht_find(&shell->env, var_name);
  if (n) {
    t_env_entry *e = (t_env_entry *)n->value;
    e->flags |= ENV_EXPORTED;
  }

  check_rehash(shell, var_name);
  return 0;
}

int type_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  (void)node;
  if (!argv[1]) {
    return 1;
  }

  for (int i = 1; argv[i]; i++) {
    char *name = argv[i];

    t_ht_node *alias_node = ht_find(&shell->aliases, name);
    if (alias_node) {
      t_alias *alias_val = (t_alias *)alias_node->value;
      printf("%s is aliased to `%s`\n", name, alias_val->cmd);
      continue;
    }

    t_ht_node *bin_node = ht_find(&shell->builtins, name);
    if (bin_node) {
      printf("%s is a shell builtin\n", name);
      continue;
    }

    t_ht_node *path_node = ht_find(&shell->bins, name);
    if (path_node && path_node->value) {
      printf("%s is %s\n", name, (char *)path_node->value);
      continue;
    }

    fprintf(stderr, "%s: not found\n", name);
  }

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

  check_rehash(shell, argv[1]);
  return 0;
}
int break_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  if (!shell->exec_ctx.flow) {
    fprintf(stderr, "break: not in for, while, or until, loop\n");
    shell->last_exit_status = 1;
    return -1;
  }

  shell->exec_ctx.break_loop = true;
  shell->last_exit_status = 0;

  return 0;
}
int continue_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  if (!shell->exec_ctx.flow) {
    fprintf(stderr, "continue: not in for, while, or until, loop\n");
    shell->last_exit_status = 1;
    return -1;
  }

  shell->exec_ctx.continue_loop = true;
  shell->last_exit_status = 0;
  return 0;
}
int return_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  shell->exec_ctx.return_fun = true;
  if (argv[1]) {
    return (shell->last_exit_status = atoi(argv[1]));
  }

  return 0;
}

int v_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  char *eq_ptr = strchr(argv[0], '=');
  if (!eq_ptr)
    return -1;

  size_t name_len = eq_ptr - argv[0];
  if (name_len == 0) {
    fprintf(stderr, "msh: empty name\n");
    return 1;
  }
  size_t val_len = strlen(eq_ptr + 1);
  char *var_name = arena_alloc(&shell->arena, name_len + 1);
  char *var_val = arena_alloc(&shell->arena, val_len + 1);

  memcpy(var_name, argv[0], name_len);
  var_name[name_len] = '\0';
  memcpy(var_val, eq_ptr + 1, val_len + 1);

  add_to_env(shell, var_name, var_val, false, 0);

  check_rehash(shell, var_name);
  return 0;
}

int rehash_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  refresh_path_bins(shell);
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

  if (shell->exflag)
    goto exit;

  t_job *job;
  size_t i = 0;
  while (i < shell->job_table_cap) {
    job = shell->job_table[i];
    if (!job) {
      i++;
      continue;
    }
    if (job->state == S_STOPPED && job->position == P_BACKGROUND) {
      shell->exflag = 1;
      fprintf(stderr, "\nmsh: you have running background jobs");
      return -1;
    }

    i++;
  }
exit:
  exit(exit_status);

  return 0;
}

/**
 * @brief Builtin env command - print environment variables
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Always returns 0
 */
int env_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  print_env(&shell->env, true, true);
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

int exec_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  char **argvv = &argv[1];
  if (argv[1] == NULL)
    return -1;

  t_ht_node *bin_node = ht_find(&shell->bins, argv[1]);
  char **env = flatten_env(&shell->env, &shell->arena);

  if (bin_node) {
    execve((char *)bin_node->value, argvv, env);
  } else if (strchr(argv[1], '/')) {
    execve(argv[1], argvv, env);
  }

  _exit(127);
}

static char *append_script_line(char *old_buf, const char *new_line,
                                t_arena *a) {
  size_t old_len = old_buf ? strlen(old_buf) : 0;
  size_t new_len = strlen(new_line);

  char *new_buf = arena_realloc(a, old_buf, old_len + new_len + 1, old_len);

  memcpy(new_buf + old_len, new_line, new_len + 1);
  return new_buf;
}
int source_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  if (argv[1] == NULL) {
    fprintf(stderr, "msh: source: filename argument required\n");
    return -1;
  }

  FILE *script = fopen(argv[1], "r");
  if (!script) {
    perror("msh: source");
    return -1;
  }

  char *line = NULL;
  size_t cap = 0;
  char *total_buf = NULL;

  t_region *p_mark;
  size_t off_mark;

  while (getline(&line, &cap, script) != -1) {
    char *p = line;
    while (*p && isspace((unsigned char)*p))
      p++;
    if (*p == '#' || *p == '\0')
      continue;
    arena_get_mark(&shell->arena, &p_mark, &off_mark);

    total_buf = append_script_line(total_buf, line, &shell->arena);
    t_err_code last_err;
    if (parse_and_execute(&total_buf, shell, &shell->token_stream, true,
                          &last_err) == 0) {
      total_buf = NULL;
      arena_rollback(&shell->arena, p_mark, off_mark);
    }
  }

  if (total_buf != NULL) {
    fprintf(stderr, "msh: source: unexpected EOF\n");
  }

  free(line);
  fclose(script);
  return 0;
}

int pwd_builtin(t_ast_n *node, t_shell *shell, char **argv) {

  const char *r = getenv_local_ref(&shell->env, "PWD");
  if (r) {
    printf("%s\n", r);
    return 0;
  }

  char p[PATH_MAX];
  if (getcwd(p, PATH_MAX) == NULL) {
    perror("msh: pwd");
    return -1;
  }
  printf("%s\n", p);

  return 0;
}

int read_builtin(t_ast_n *node, t_shell *shell, char **argv) {
  if (argv[1] == NULL) {
    fprintf(stderr, "msh: read: missing variable name\n");
    return -1;
  }

  char *input_line = NULL;
  size_t len = 0;
  if (getline(&input_line, &len, stdin) == -1) {
    free(input_line);
    return -1;
  }

  size_t n = strlen(input_line);
  if (n > 0 && input_line[n - 1] == '\n') {
    input_line[n - 1] = '\0';
  }

  if (add_to_env(shell, argv[1], input_line, false, 0) == -1) {
    fprintf(stderr, "msh: read: failed to set variable %s\n", argv[1]);
    free(input_line);
    return -1;
  }

  free(input_line);
  return 0;
}
