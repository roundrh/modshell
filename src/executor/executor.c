#include "executor.h"

typedef enum e_pgrp {

  PGPR_NONE = 0,
  PGRP_FATAL = -1,
  PGRP_INVAL = -2
} t_pgrp;

/**
 * @file executor.c
 * @brief implementation of functions used to execute AST.
 */

static char *append_script_line(char *old_buf, const char *new_line,
                                t_arena *a) {

  size_t old_len = old_buf ? strlen(old_buf) : 0;
  size_t new_len = strlen(new_line);

  char *new_buf = arena_realloc(a, old_buf, old_len + new_len + 1, old_len);

  memcpy(new_buf + old_len, new_line, new_len + 1);
  return new_buf;
}

int exec_script(t_shell *shell, const char *path) {
  FILE *script = fopen(path, "r");
  if (!script) {
    errno = EINVAL;
    perror("msh: open");
    return -1;
  }

  char *line = NULL;
  size_t cap = 0;
  char *total_buf = NULL;
  if (getline(&line, &cap, script) != -1) {
    if (strncmp(line, "#!", 2) != 0)
      total_buf = append_script_line(total_buf, line, &shell->arena);
  }
  while (getline(&line, &cap, script) != -1) {
    char *p = line;
    while (*p && isspace((unsigned char)*p))
      p++;

    if (*p == '#' || *p == '\0')
      continue;

    total_buf = append_script_line(total_buf, line, &shell->arena);

    if (parse_and_execute(&total_buf, shell, &shell->token_stream, true) == 0) {
      total_buf = NULL;
      shell->last_exit_status = 0;
      arena_reset(&shell->arena);
    }
  }
  if (total_buf != NULL) {
    fprintf(stderr, "msh: unexpected EOF while looking for matching token\n");
  }

  free(line);
  fclose(script);
  return 0;
}

static void wait_for_job_slot(t_shell *shell) {
  sigset_t mask, oldmask, emptymask;

  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &mask, &oldmask);

  while (is_job_table_full(shell)) {
    sigemptyset(&emptymask);
    sigsuspend(&emptymask);

    reap_sigchld_jobs(shell);

    if (sigint_flag || sigtstp_flag) {
      break;
    }
  }

  sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

void del_completed_jobs(t_shell *shell) {

  for (int i = 0; i < shell->job_table_cap; i++) {
    t_job *j = shell->job_table[i];
    if (!j)
      continue;

    if (j->state == S_COMPLETED)
      del_job(shell, j->job_id, false);
  }
}

int reap_sigchld_jobs(t_shell *shell) {
  int status;
  pid_t pid;

  int reaped = -1;

  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    reaped = 0;

    t_job *job = find_job_by_pid(shell, pid);
    if (!job) {
      continue;
    }

    t_process *process = find_process_in_job(job, pid);
    if (!process)
      continue;

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
      process->completed = 1;
      process->stopped = 0;
      process->running = 0;
    } else if (WIFSTOPPED(status)) {
      process->stopped = 1;
      process->completed = 0;
      process->running = 0;
      job->state = S_STOPPED;
    } else if (WIFCONTINUED(status)) {
      process->running = 1;
      process->stopped = 0;
      process->completed = 0;
      job->state = S_RUNNING;
    }

    if (is_job_completed(job)) {
      job->state = S_COMPLETED;
      print_job_info(job);
    }
  }

  del_completed_jobs(shell);

  return reaped;
}

static t_shell *g_shell_ptr = NULL;
void set_global_shell_ptr_chld(t_shell *ptr) { g_shell_ptr = ptr; }

static pid_t exec_pipe(t_ast_n *node, t_shell *shell, t_job *job, int subshell,
                       bool flow); ///< Forward declaration of function
static pid_t exec_command(t_ast_n *node, t_shell *shell, t_job *job,
                          int is_pipeline_child, int subshell,
                          t_ast_n *pipeline,
                          bool flow); ///< Forward declaration of function
static pid_t
exec_simple_command(t_ast_n *node, t_shell *shell, t_job *job,
                    int is_pipeline_child, int subshell, t_ast_n *pipeline,
                    bool flow); ///< Forward declaration of function
static int exec_list(char *cmd_buf, t_ast_n *node, t_shell *shell, int subshell,
                     t_job *subshell_job, bool flow);
static inline t_pgrp handle_tcsetpgrp(t_shell *shell, pid_t pgid);

static t_wait_status wait_for_foreground_job(t_job *job, t_shell *shell) {

  pid_t pid = 0;
  int status = 0;

  t_wait_status ret_status = WAIT_FINISHED;

  while (!is_job_completed(job) && !is_job_stopped(job)) {

    pid = waitpid(-job->pgid, &status, WUNTRACED);
    if (pid == -1) {
      if (errno == ECHILD) {
        break;
      } else if (errno == EINTR) {
        continue;
      } else {
        perror("waitpid");
        return WAIT_ERROR;
      }
    }

    t_process *process = find_process_in_job(job, pid);
    if (!process)
      continue;

    if (WIFEXITED(status)) {

      process->completed = 1;
      process->stopped = 0;
      process->running = 0;

      process->exit_status = WEXITSTATUS(status);
      if (pid == job->last_pid || job->process_count == 1) {
        job->last_exit_status = WEXITSTATUS(status);
        shell->last_exit_status = job->last_exit_status;
      }
    } else if (WIFSTOPPED(status)) {

      process->stopped = 1;
      process->completed = 0;
      process->running = 0;
      job->state = S_STOPPED;
      job->position = P_BACKGROUND;

      job->last_exit_status = WSTOPSIG(status);
      if (pid == job->last_pid || job->process_count == 1) {
        shell->last_exit_status = job->last_exit_status;
      }

      ret_status = WAIT_STOPPED;
      break;

    } else if (WIFSIGNALED(status)) {

      process->completed = 1;
      process->stopped = 0;
      process->running = 0;

      int sig = WTERMSIG(status);

      process->exit_status = 128 + sig;
      job->last_exit_status = 128 + sig;
      if (ret_status != WAIT_INTERRUPTED)
        shell->last_exit_status = job->last_exit_status;

      ret_status = WAIT_INTERRUPTED;
    }
  }

  return ret_status;
}

static t_job *make_job(t_shell *shell, char *buf, t_state state, pid_t pgid,
                       t_position pos) {

  t_job *job = NULL;

  job = (t_job *)malloc(sizeof(t_job));
  if (job == NULL) {
    perror("job malloc makejob");
    return NULL;
  }

  init_job_struct(job);

  job->position = pos;
  job->pgid = pgid;
  job->state = state;

  if (add_job(shell, job) == -1) {
    cleanup_job_struct(job);
    free(job);
    return NULL;
  }

  return job;
}

static t_process *make_process(pid_t pid) {

  t_process *process = (t_process *)malloc(sizeof(t_process));
  if (process == NULL) {
    perror("makeprocess malloc fail");
    return NULL;
  }

  process->pid = pid;
  process->exit_status = -1;
  process->completed = 0;
  process->stopped = 0;
  process->running = 0;

  process->next = NULL;

  return process;
}

static pid_t exec_extern_cmd(t_shell *shell, t_ast_n *node, t_job *job,
                             int is_pipeline_child, int subshell,
                             t_ast_n *pipeline, char **argv) {
  if (!argv) {
    perror("argv prop err");
    return -1;
  }

  if (is_pipeline_child) {
    execvp(argv[0], argv);
    fprintf(stderr, "msh: command \"%s\" not found\n", argv[0]);
    _exit(127);
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork fail exec_extern");
    return -1;
  } // fork error.

  if (job->pgid == -1)
    job->pgid = pid;

  if (pid == 0) {

    if (!subshell) {
      if (setpgid(0, job->pgid) < 0) {
        if (errno != EPERM && errno != EACCES && errno != ESRCH) {
          perror("280: setpgid");
          _exit(EXIT_FAILURE);
        }
      }
    }

    init_ch_sigtable(&(shell->shell_sigtable));
    set_global_shell_ptr_chld(shell);

    char **env = flatten_env(&shell->env, &shell->arena);
    if (strchr(argv[0], '/')) {
      execve(argv[0], argv, env);
    } else {
      t_ht_node *bin_node = ht_find(&shell->bins, argv[0]);
      if (bin_node) {
        execve(bin_node->value, argv, env);
      }
    }

    fprintf(stderr, "msh: command \"%s\" not found\n", argv[0]);
    _exit(127);
  } else if (shell->job_control_flag) {

    if (setpgid(pid, job->pgid) == -1) {
      if (errno != EPERM && errno != EACCES && errno != ESRCH) {
        perror("230: parent setpgid");
        return -1;
      }
    }
    t_process *process = make_process(pid);
    if (!process)
      return -1;
    if (add_process_to_job(job, process) == -1) {
      perror("fail to add process to job");
      return -1;
    }
    job->last_pid = pid;
  }

  return pid;
}

static pid_t exec_bg_builtin(t_ast_n *node, t_shell *shell, t_job *job,
                             t_ast_n *pipeline, t_ht_node *builtin_ptr,
                             char **argv, bool subshell) {

  if (!argv) {
    perror("argv prop err");
    return -1;
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork fail exec_extern");
    return -1;
  } // fork error.

  if (job->pgid == -1)
    job->pgid = pid;

  if (pid == 0) {

    init_ch_sigtable(&(shell->shell_sigtable));

    if (!subshell) {
      if (setpgid(0, job->pgid) < 0) {
        if (errno != EPERM && errno != EACCES && errno != ESRCH) {
          perror("280: setpgid");
          _exit(EXIT_FAILURE);
        }
      }
    }

    set_global_shell_ptr_chld(shell);

    t_builtin *b = (t_builtin *)builtin_ptr->value;
    int exit_status = b->fn(node, shell, argv);
    _exit(exit_status);
  } else if (shell->job_control_flag) {

    if (setpgid(pid, job->pgid) == -1) {
      if (errno != EPERM && errno != EACCES && errno != ESRCH) {
        perror("230: parent setpgid");
        return -1;
      }
    }
    t_process *process = make_process(pid);
    if (!process)
      return -1;
    if (add_process_to_job(job, process) == -1) {
      perror("fail to add process to job");
      return -1;
    }
    job->last_pid = pid;
  }

  return pid;
}

int is_set_var(char **argv) {
  if (argv[1] != NULL)
    return 0;
  char *a = argv[0];
  int eqcnt = 0;
  while (a && *a != '\0') {
    if (*a == '=')
      eqcnt++;
    a++;
  }
  if (eqcnt != 1)
    return 0;
  else
    return 1;
}

/**
 * @brief executes simple command in node
 * @param node pointer to ast node
 * @param shell pointer to shell struct
 * @return -1 on fail, 0 on success.
 */
static pid_t exec_simple_command(t_ast_n *node, t_shell *shell, t_job *job,
                                 int is_pipeline_child, int is_subshell,
                                 t_ast_n *pipeline, bool flow) {
  if (!node)
    return -1;

  t_region *p;
  size_t off;
  arena_get_mark(&shell->arena, &p, &off);

  char **argv = NULL;
  t_err_type err_ret = expand_make_argv(shell, &argv, node->tok_start,
                                        node->tok_segment_len, &shell->arena);
  if (err_ret == err_fatal) {
    perror("fatal err expanding argv");
    exit(1);
  } else if (argv == NULL || argv[0] == NULL) {
    arena_rollback(&shell->arena, p, off);
    return 0;
  }
  job->command = strdup(argv[0]);

  t_ht_node *builtin_imp = ht_find(&shell->builtins, argv[0]);
  if (builtin_imp && strcmp(builtin_imp->key, "v") == 0)
    builtin_imp = NULL;
  else if (!builtin_imp && is_set_var(argv)) {
    builtin_imp = ht_find(&shell->builtins, "v");
  }

  if (builtin_imp == NULL) {
    pid_t ret_pid = exec_extern_cmd(shell, node, job, is_pipeline_child,
                                    is_subshell, pipeline, argv);
    arena_rollback(&shell->arena, p, off);
    return ret_pid;
  } else if (job->position == P_FOREGROUND) {
    t_builtin *b = (t_builtin *)builtin_imp->value;
    job->last_exit_status = b->fn(node, shell, argv);
    shell->last_exit_status = job->last_exit_status;
  } else {
    exec_bg_builtin(node, shell, job, pipeline, builtin_imp, argv, is_subshell);
  }

  /* pid 0 on built in execution -- denotes no fork -- shell last exit status
   * set */
  arena_rollback(&shell->arena, p, off);
  return 0;
}

static pid_t exec_subshell(t_ast_n *node, t_shell *shell, t_job *job,
                           int is_pipeline_child, t_ast_n *pipeline,
                           int subshell, bool flow) {

  pid_t pid = fork();
  if (pid < 0) {
    perror("241: fork fail");
    return -1;
  }

  if (!subshell && job->pgid == -1)
    job->pgid = pid;

  if (pid == 0) {

    if (!subshell) {
      if (setpgid(0, job->pgid) < 0) {
        if (errno != EPERM && errno != EACCES && errno != ESRCH) {
          perror("379: setpgid");
          _exit(EXIT_FAILURE);
        }
      }
    }

    shell->job_control_flag = 0;
    init_ch_sigtable(&(shell->shell_sigtable));
    set_global_shell_ptr_chld(shell);

    exec_list(NULL, node->sub_ast_root, shell, 1, job, flow);

    while (waitpid(-job->pgid, NULL, 0) > 0)
      ;

    _exit(shell->last_exit_status);
  } else if (shell->job_control_flag) {

    if (setpgid(pid, job->pgid) < 0) {
      if (errno != EPERM && errno != EACCES && errno != ESRCH)
        perror("setpgid");
    }

    t_process *process = make_process(pid);
    if (!process)
      return -1;
    if (add_process_to_job(job, process) == -1) {
      perror("fail to add process to jod");
      return -1;
    }
    job->last_pid = pid;
  }

  return pid;
}

/**
 * @brief executes command in node based on saved OP_TYPE by parser
 * @param node pointer to ast node
 * @param shell pointer to shell struct
 * @return -1 on fail, 0 on success.
 *
 */
static pid_t exec_command(t_ast_n *node, t_shell *shell, t_job *job,
                          int is_pipeline_child, int subshell,
                          t_ast_n *pipeline, bool flow) {

  if (!node)
    return -1;

  int restore_io_flag = 0;
  if (node->redir_bool) {
    if (redirect_io(shell, node) == -1) {
      fprintf(stderr, "\nErr redir io");
      return -1;
    }
    node->redir_bool = 0;
    restore_io_flag = 1;
  }

  pid_t pid = -1;
  if (node->op_type == OP_PIPE) {
    pid = exec_pipe(node, shell, job, subshell, flow);
  } else if (node->op_type == OP_SIMPLE) {
    pid = exec_simple_command(node, shell, job, is_pipeline_child, subshell,
                              pipeline, flow);
  } else if (node->op_type == OP_SUBSHELL) {
    pid = exec_subshell(node, shell, job, is_pipeline_child, pipeline, subshell,
                        flow);
  }

  if (restore_io_flag)
    restore_io(shell);

  return pid;
}

static t_ast_n *flatten_ast(t_ast_n *node, t_arena *a) {

  if (!node)
    return NULL;

  if (node->op_type == OP_PIPE) {
    t_ast_n *left_list = flatten_ast(node->left, a);
    t_ast_n *right_list = flatten_ast(node->right, a);

    t_ast_n *tail = left_list;
    if (tail) {
      while (tail->right) {
        tail = tail->right;
      }

      tail->right = right_list;

      return left_list;
    } else {
      return right_list;
    }
  } else {
    t_ast_n *new_node = (t_ast_n *)arena_alloc(a, sizeof(t_ast_n));

    init_ast_node(new_node);

    new_node->tok_start = node->tok_start;
    new_node->tok_segment_len = node->tok_segment_len;
    new_node->background = node->background;
    new_node->op_type = node->op_type;
    new_node->sub_ast_root = node->sub_ast_root;
    new_node->io_redir = node->io_redir;

    new_node->redir_bool = node->redir_bool;

    new_node->left = NULL;
    new_node->right = NULL;

    return new_node;
  }
}
/**
 * @brief executes pipe command on flattened list
 * @param node pointer to ast node
 * @param shell pointer to shell struct
 * @return -1 on fail, 0 on success.
 *
 * @note double fork caused bad race condition, fixed
 * @note 428 pipes fails with EMFILE: too many open files errno
 *     cleans up flattened ast returns -1 propagates back for conditional
 * commands.
 * @note _exit(shell->last_exit_status) in children is irrelevant
 * each child forks and the parent reaps the final exit status of the exec'd
 * command for builtins in pipes that do reach _exit() and have set
 * shell->last_exit_status for the forked child to _exit with
 */
static pid_t exec_pipe(t_ast_n *node, t_shell *shell, t_job *job, int subshell,
                       bool flow) {

  pid_t last_pid = -1;

  t_ast_n *pipeline = flatten_ast(node, &shell->arena);
  t_ast_n *head = pipeline;
  int count_cmd = 0;
  while (head) {
    count_cmd++;
    head = head->right;
  }

  if (count_cmd < 1)
    return -1;

  int (*pipes)[2] =
      (int (*)[2])arena_alloc(&shell->arena, sizeof(int[2]) * (count_cmd - 1));
  for (int i = 0; i < count_cmd - 1; i++) {
    pipes[i][0] = -1;
    pipes[i][1] = -1;
  }

  for (int i = 0; i < count_cmd - 1; i++) {
    if (pipe(pipes[i]) == -1) {
      for (int j = 0; j < i; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      pipeline = NULL;
      perror("pipe");
      return -1;
    }
  }

  t_ast_n *exec = pipeline;
  int i = 0;
  while (exec && i < count_cmd) {

    pid_t pid = fork();
    if (pid == -1) {
      return -1;
    }

    if (job->pgid == -1 && i == 0) {
      job->pgid = pid;
    }

    if (pid == 0) {

      if (i == 0) {

        if (!subshell) {
          if (setpgid(0, job->pgid) < 0) {
            if (errno != EPERM && errno != EACCES && errno != ESRCH) {
              perror("280: setpgid");
              _exit(EXIT_FAILURE);
            }
          }
        }

        if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
          perror("dup2");
        }

        for (int j = 0; j < count_cmd - 1; j++) {
          close(pipes[j][0]);
          close(pipes[j][1]);
        }

        init_ch_sigtable(&shell->shell_sigtable);
        set_global_shell_ptr_chld(shell);

        exec_command(exec, shell, job, 1, subshell, pipeline, flow);

        _exit(shell->last_exit_status);
      } else if (i == count_cmd - 1) {

        if (!subshell) {
          if (setpgid(0, job->pgid) < 0) {
            if (errno != EPERM && errno != EACCES && errno != ESRCH) {
              perror("280: setpgid");
              _exit(EXIT_FAILURE);
            }
          }
        }

        dup2(pipes[i - 1][0], STDIN_FILENO);
        for (int j = 0; j < count_cmd - 1; j++) {
          close(pipes[j][0]);
          close(pipes[j][1]);
        }

        init_ch_sigtable(&shell->shell_sigtable);
        set_global_shell_ptr_chld(shell);

        exec_command(exec, shell, job, 1, subshell, pipeline, flow);

        _exit(shell->last_exit_status);
      } else {

        if (!subshell) {
          if (setpgid(0, job->pgid) < 0) {
            if (errno != EPERM && errno != EACCES && errno != ESRCH) {
              perror("280: setpgid");
              _exit(EXIT_FAILURE);
            }
          }
        }

        dup2(pipes[i - 1][0], STDIN_FILENO);
        dup2(pipes[i][1], STDOUT_FILENO);
        for (int j = 0; j < count_cmd - 1; j++) {
          close(pipes[j][0]);
          close(pipes[j][1]);
        }

        init_ch_sigtable(&shell->shell_sigtable);
        set_global_shell_ptr_chld(shell);

        exec_command(exec, shell, job, 1, subshell, pipeline, flow);

        _exit(shell->last_exit_status);
      }
    } else if (shell->job_control_flag) {

      if (setpgid(pid, job->pgid) < 0) {
        if (errno != EPERM && errno != EACCES && errno != ESRCH) {
          perror("setpgid fail");
          return -1;
        }
      }

      t_process *process = make_process(pid);
      if (!process) {
        perror("process make fail");
        cleanup_job_struct(job);
        return -1;
      }
      if (i == count_cmd - 1)
        job->last_pid = pid;

      add_process_to_job(job, process);
    }

    last_pid = pid;

    exec = exec->right;
    i++;
  }

  for (int j = 0; j < count_cmd - 1; j++) {
    close(pipes[j][0]);
    close(pipes[j][1]);
  }

  return last_pid;
}

static inline t_pgrp handle_tcsetpgrp(t_shell *shell, pid_t pgid) {

  if (pgid == -1) {
    return PGRP_INVAL;
  }

  while (tcsetpgrp(shell->tty_fd, pgid) == -1) {
    if (errno == EINTR) {
      continue;
    } else if (errno == EPERM || errno == EINVAL)
      return PGRP_INVAL;
    else {
      perror("630: tcsetpgrp");
      return PGRP_FATAL;
    }
  }

  return PGPR_NONE;
}

static int exec_job(char *cmd_buf, t_ast_n *node, t_shell *shell, int subshell,
                    t_job *subshell_job, bool flow) {

  t_job *job = NULL;
  if (!subshell)
    job = make_job(shell, cmd_buf, S_RUNNING, -1,
                   node->background ? P_BACKGROUND : P_FOREGROUND);
  else
    job = subshell_job;
  if (!job) {
    return -1;
  }

  pid_t lpid = exec_command(node, shell, job, 0, subshell, NULL, flow);

  if (shell->job_control_flag && job->position == P_FOREGROUND) {

    t_pgrp tc;
    if ((tc = handle_tcsetpgrp(shell, job->pgid)) == PGRP_FATAL) {
      perror("720: tcsetpgrp reclaim");
      exit_builtin(node, shell, NULL);
    }

    t_wait_status job_status = wait_for_foreground_job(job, shell);

    if ((tc = handle_tcsetpgrp(shell, shell->pgid)) == PGRP_FATAL) {
      perror("720: tcsetpgrp reclaim");
      exit_builtin(node, shell, NULL);
    }

    if (job_status == WAIT_FINISHED) {
      del_job(shell, job->job_id, flow);
      if (lpid == 0) {
        if (shell->next_job_id > 1)
          shell->next_job_id--;
      }
      return WAIT_FINISHED;

    } else if (job_status == WAIT_STOPPED) {
      print_job_info(job);
      return WAIT_STOPPED;
    } else if (job_status == WAIT_INTERRUPTED) {
      del_job(shell, job->job_id, true);
      return WAIT_INTERRUPTED;
    }

    if (job_status == WAIT_ERROR) {
      perror("687: failed wait for fg");
      exit_builtin(NULL, shell, NULL);
    }

  } else if (shell->job_control_flag && job->pgid != -1) {
    if (!subshell)
      print_job_info(job);
    return WAIT_FINISHED;
  } else if (!shell->job_control_flag) {
    /* builtins set shell exit status - return pid 0 */
    if (lpid > 0) {
      int status;
      pid_t p;
      while ((p = waitpid(-1, &status, WUNTRACED)) > 0) {
        if (p == lpid) {
          shell->last_exit_status = WEXITSTATUS(status);
          if (WIFSIGNALED(status))
            shell->last_exit_status = WTERMSIG(status) + 128;
        }
      }
    }

    if (!subshell) {
      del_job(shell, job->job_id, true);
      job = NULL;
    }
  }

  return WAIT_FINISHED;
}

static int exec_cond_bg(char *cmd_buf, t_ast_n *node, t_shell *shell,
                        bool flow) {

  t_job *job = make_job(shell, cmd_buf, S_RUNNING, -1, P_BACKGROUND);
  if (!job)
    return -1;
  job->command = strdup(cmd_buf);

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return -1;
  }

  if (pid == 0) {

    setpgid(0, 0);

    shell->job_control_flag = 0;
    init_ch_sigtable(&(shell->shell_sigtable));
    set_global_shell_ptr_chld(shell);

    node->background = 0;

    exec_list(cmd_buf, node, shell, 1, job, flow);
    while (waitpid(-job->pgid, NULL, 0) > 0)
      ;

    _exit(shell->last_exit_status);
  } else {
    if (job->pgid == -1)
      job->pgid = pid;

    t_process *process = make_process(pid);
    if (process) {
      add_process_to_job(job, process);
      job->last_pid = pid;
    }

    if (shell->job_control_flag) {
      setpgid(pid, job->pgid);
      print_job_info(job);
    }

    return WAIT_FINISHED;
  }
}

static int exec_list(char *cmd_buf, t_ast_n *node, t_shell *shell, int subshell,
                     t_job *subshell_job, bool flow) {
  if (!node)
    return 0;
  if (sigint_flag || sigtstp_flag)
    return WAIT_INTERRUPTED;

  switch (node->op_type) {
  case OP_SEQ:
    exec_list(cmd_buf, node->left, shell, subshell, subshell_job, flow);
    return exec_list(cmd_buf, node->right, shell, subshell, subshell_job, flow);
  case OP_AND:
    if (node->background && !subshell) {
      return exec_cond_bg(cmd_buf, node, shell, flow);
    }
    exec_list(cmd_buf, node->left, shell, subshell, subshell_job, flow);
    if (shell->last_exit_status == 0)
      exec_list(cmd_buf, node->right, shell, subshell, subshell_job, flow);
    return shell->last_exit_status;

  case OP_OR:
    if (node->background && !subshell) {
      return exec_cond_bg(cmd_buf, node, shell, flow);
    }
    exec_list(cmd_buf, node->left, shell, subshell, subshell_job, flow);
    if (shell->last_exit_status != 0)
      exec_list(cmd_buf, node->right, shell, subshell, subshell_job, flow);
    return shell->last_exit_status;
  case OP_IF:
    exec_list(cmd_buf, node->left, shell, subshell, subshell_job, flow);
    if (shell->last_exit_status == 0) {
      exec_list(cmd_buf, node->right, shell, subshell, subshell_job, flow);
    } else if (node->sub_ast_root) {
      exec_list(cmd_buf, node->sub_ast_root, shell, subshell, subshell_job,
                flow);
    }
    return (subshell) ? shell->last_exit_status : WAIT_FINISHED;
  case OP_WHILE: {

    while (!sigint_flag && !sigtstp_flag) {
      /* force reap when some larp decides to spam the job table */
      if (is_job_table_full(shell)) {
        wait_for_job_slot(shell);
      }

      t_wait_status wait;
      wait =
          exec_list(cmd_buf, node->left, shell, subshell, subshell_job, true);
      if (sigint_flag || sigtstp_flag || wait == WAIT_INTERRUPTED ||
          wait == WAIT_STOPPED)
        break;
      if (shell->last_exit_status != 0)
        break;

      wait =
          exec_list(cmd_buf, node->right, shell, subshell, subshell_job, true);
      if (sigint_flag || sigtstp_flag || wait == WAIT_INTERRUPTED ||
          wait == WAIT_STOPPED)
        break;
    }

    return WAIT_FINISHED;
  }
  case OP_FOR: {

    char **expanded_items = NULL;
    t_err_type err = expand_make_argv(shell, &expanded_items, node->for_items,
                                      node->items_len, &shell->arena);
    if (err == err_fatal) {
      exit_builtin(NULL, shell, NULL);
    }
    if (!expanded_items)
      return WAIT_ERROR;

    char var_name[256];
    snprintf(var_name, sizeof(var_name), "%.*s", (int)node->for_var->len,
             node->for_var->start);

    for (int i = 0; expanded_items[i] != NULL; i++) {

      if (is_job_table_full(shell)) {
        wait_for_job_slot(shell);
      }

      if (sigint_flag || sigtstp_flag)
        break;

      add_to_env(shell, var_name, expanded_items[i]);
      t_wait_status wait = exec_list(cmd_buf, node->sub_ast_root, shell,
                                     subshell, subshell_job, true);

      if (wait == WAIT_INTERRUPTED || wait == WAIT_STOPPED)
        break;
    }

    return (subshell) ? shell->last_exit_status : WAIT_FINISHED;
  }
  default:
    return exec_job(cmd_buf, node, shell, subshell, subshell_job, flow);
  }
}
/**
 * @brief called by driver to build ast and execute the ast via recursive
 * descent.
 * @param cmd_buf pointer to cmd line buf
 * @param shell pointer to shell struct
 * @param command pointer to command struct
 * @return -1 on fail, 0 on success.
 *
 */
int parse_and_execute(char **cmd_buf, t_shell *shell,
                      t_token_stream *token_stream, bool script) {
  int s_fd = -1;
  if (script) {
    s_fd = dup(STDERR_FILENO);
    int n_fd = open("/dev/null", O_WRONLY);
    dup2(n_fd, STDERR_FILENO);
    close(n_fd);
  }

  init_token_stream(token_stream, &shell->arena);
  t_hashtable *aliases = &(shell->aliases);
  if (lex_command_line(cmd_buf, token_stream, aliases, 0, &shell->arena) ==
      -1) {
    return -1;
  }

  t_ast_n *root;
  if ((root = build_ast(&(shell->ast), token_stream, &shell->arena)) == NULL) {
    return -1;
  }

  struct sigaction sa_int, osa_int;
  struct sigaction sa_tstp, osa_tstp;
  sigset_t old, new;

  if (!script) {
    sigemptyset(&sa_int.sa_mask);
    sigemptyset(&sa_tstp.sa_mask);
    sa_int.sa_handler = sigint_handler;
    sa_tstp.sa_handler = sigtstp_handler;
    sa_int.sa_flags = 0;
    sa_tstp.sa_flags = 0;
    sigaction(SIGINT, &sa_int, &osa_int);
    sigaction(SIGTSTP, &sa_tstp, &osa_tstp);

    sigemptyset(&new);
    sigaddset(&new, SIGCHLD);
    sigprocmask(SIG_BLOCK, &new, &old);
  }
  exec_list(*cmd_buf, root, shell, 0, NULL, script);
  if (!script) {
    sigprocmask(SIG_SETMASK, &old, NULL);
    sigaction(SIGINT, &osa_int, NULL);
    sigaction(SIGTSTP, &osa_tstp, NULL);
  }
  restore_io(shell);
  shell->ast.root = NULL;

  if (script) {
    dup2(s_fd, STDERR_FILENO);
    close(s_fd);
  }

  sigint_flag = 0;
  sigtstp_flag = 0;
  return 0;
}
