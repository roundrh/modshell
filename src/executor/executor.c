#include "executor.h"
#include "builtins.h"
#include "sigtable_init.h"
#include <signal.h>
#include <stdlib.h>

typedef enum e_pgrp {

  PGPR_NONE = 0,
  PGRP_FATAL = -1,
  PGRP_INVAL = -2
} t_pgrp;

/**
 * @file executor.c
 * @brief implementation of functions used to execute AST.
 */

int reap_sigchld_jobs(t_shell *shell) {

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
      print_job_info(job);
      del_job(shell, job->job_id, false);
    } else if (job && is_job_stopped(job)) {
      job->state = S_STOPPED;
      print_job_info(job);
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
    shell->job_count = 0;
  }
  return 0;
}

static t_shell *g_shell_ptr = NULL;
void set_global_shell_ptr_chld(t_shell *ptr) { g_shell_ptr = ptr; }

void cleanup_global_cmd_buf_ptr(void); ///< defined in shell_driver.c

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

static void cleanup_argv(char **argv) {

  if (argv == NULL)
    return;

  int i = 0;
  while (argv[i] != NULL) {

    free(argv[i]);
    argv[i] = NULL;

    i++;
  }

  free(argv);
  argv = NULL;
}

static void cleanup_flattened_ast(t_ast_n *node) {

  if (!node)
    return;

  t_ast_n *p = node;
  while (p) {
    t_ast_n *next = p->right;

    free(p);
    p = next;
  }
}

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
      job->last_exit_status = WEXITSTATUS(status);

      if (pid == job->last_pid)
        shell->last_exit_status = job->last_exit_status;

    } else if (WIFSTOPPED(status)) {

      process->stopped = 1;
      process->completed = 0;
      process->running = 0;
      job->state = S_STOPPED;
      job->position = P_BACKGROUND;

      job->last_exit_status = WSTOPSIG(status);
      shell->last_exit_status = job->last_exit_status;

      ret_status = WAIT_STOPPED;
      break;

    } else if (WIFSIGNALED(status)) {

      process->completed = 1;
      process->stopped = 0;
      process->running = 0;

      int sig = WTERMSIG(status);

      process->exit_status = 128 + sig;
      job->last_exit_status = 128 + sig;

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

    execvp(argv[0], argv);
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
    job->last_pid = pid;
    if (add_process_to_job(job, process) == -1) {
      perror("fail to add process to job");
      return -1;
    }
  }

  return pid;
}

static pid_t exec_bg_builtin(t_ast_n *node, t_shell *shell, t_job *job,
                             t_ast_n *pipeline, t_ht_node *builtin_ptr,
                             char **argv) {

  if (!argv) {
    perror("argv prop err");
    return -1;
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork fail exec_extern");
    return -1;
  } // fork error.

  if (pid == 0) {

    init_ch_sigtable(&(shell->shell_sigtable));
    set_global_shell_ptr_chld(shell);

    int exit_status = builtin_ptr->builtin_ptr(node, shell, argv);
    cleanup_argv(argv);
    _exit(exit_status);
  } else {
    int status;
    waitpid(pid, &status, 0);
    job->last_pid = pid;
    shell->last_exit_status = WEXITSTATUS(status);
  }

  return pid;
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

  char **argv = NULL;
  t_err_type err_ret =
      expand_make_argv(shell, &argv, node->tok_start, node->tok_segment_len);
  if (err_ret == err_fatal) {
    perror("fatal err expanding argv");
    exit_builtin(node, shell, NULL);
  } else if (argv == NULL || argv[0] == NULL) {
    cleanup_argv(argv);
    return 0;
  }
  job->command = strdup(argv[0]);

  t_ht_node *builtin_imp = hash_find_builtin(&shell->builtins, argv[0]);
  if (builtin_imp == NULL) {
    pid_t ret_pid = exec_extern_cmd(shell, node, job, is_pipeline_child,
                                    is_subshell, pipeline, argv);
    cleanup_argv(argv);
    return ret_pid;
  }

  if (job->position == P_FOREGROUND) {

    job->last_exit_status = builtin_imp->builtin_ptr(node, shell, argv);
    cleanup_argv(argv);
    shell->last_exit_status = WEXITSTATUS(job->last_exit_status);

    return 0;
  } else {
    exec_bg_builtin(node, shell, job, pipeline, builtin_imp, argv);
    cleanup_argv(argv);
    return 0;
  }

  /* pid 0 on built in execution -- denotes no fork -- shell last exit status
   * set */
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

    int exit_status = exec_list(NULL, node->sub_ast_root, shell, 1, job, flow);

    int status = 0;
    while (waitpid(-job->pgid, &status, 0) > 0)
      ;

    _exit(exit_status);
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

static t_ast_n *flatten_ast(t_ast_n *node) {

  if (!node)
    return NULL;

  if (node->op_type == OP_PIPE) {
    t_ast_n *left_list = flatten_ast(node->left);
    t_ast_n *right_list = flatten_ast(node->right);

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
    t_ast_n *new_node = (t_ast_n *)malloc(sizeof(t_ast_n));
    if (!new_node)
      return NULL;

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

  t_ast_n *pipeline = flatten_ast(node);
  if (!pipeline) {
    perror("fail to flatten");

    /* fail to malloc fatal*/
    exit_builtin(node, shell, NULL);
  }

  t_ast_n *head = pipeline;
  int count_cmd = 0;
  while (head) {
    count_cmd++;
    head = head->right;
  }

  if (count_cmd < 1)
    return -1;

  int (*pipes)[2] = malloc(sizeof(int[2]) * (count_cmd - 1));
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
      cleanup_flattened_ast(pipeline);
      pipeline = NULL;
      perror("pipe fail");
      free(pipes);
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

  if (pipeline) {
    cleanup_flattened_ast(pipeline);
    pipeline = NULL;
  }

  for (int j = 0; j < count_cmd - 1; j++) {
    close(pipes[j][0]);
    close(pipes[j][1]);
  }

  free(pipes);

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
    int status = 0;

    /* builtins set shell exit status - return pid 0 */
    if (lpid != 0) {
      waitpid(lpid, &status, WUNTRACED);
      shell->last_exit_status = WEXITSTATUS(status);
      if (errno == EINTR)
        shell->last_exit_status = WTERMSIG(status) + 128;
      if (!subshell) {
        del_job(shell, job->job_id, true);
        job = NULL;
      }
      return WAIT_FINISHED;
    }

    while (waitpid(-1, NULL, WUNTRACED) > 0)
      ;

    if (!subshell) {
      del_job(shell, job->job_id, true);
      job = NULL;
    }
  }

  return WAIT_FINISHED;
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
    exec_list(cmd_buf, node->left, shell, subshell, subshell_job, flow);
    if (shell->last_exit_status == 0)
      exec_list(cmd_buf, node->right, shell, subshell, subshell_job, flow);
    return shell->last_exit_status;
  case OP_OR:
    exec_list(cmd_buf, node->left, shell, subshell, subshell_job, flow);
    if (shell->last_exit_status != 0)
      exec_list(cmd_buf, node->right, shell, subshell, subshell_job, flow);
    return shell->last_exit_status;
  case OP_IF:
    exec_list(cmd_buf, node->left, shell, subshell, subshell_job, flow);
    if (shell->last_exit_status == 0) {
      return exec_list(cmd_buf, node->right, shell, subshell, subshell_job,
                       flow);
    } else if (node->sub_ast_root) {
      return exec_list(cmd_buf, node->sub_ast_root, shell, subshell,
                       subshell_job, flow);
    }
    return 0;
  case OP_WHILE: {

    while (!sigint_flag && !sigtstp_flag) {
      /* force reap when some larp decides to spam the job table */
      if (is_job_table_full(shell)) {
        reap_sigchld_jobs(shell);
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
      if (shell->last_exit_status != 0)
        break;
    }

    reap_sigchld_jobs(shell);
    return shell->last_exit_status;
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

  t_alias_hashtable *aliases = &(shell->aliases);
  if (lex_command_line(cmd_buf, token_stream, aliases, 0) == -1) {
    return -1;
  }

  t_ast_n *root;
  if ((root = build_ast(&(shell->ast), token_stream)) == NULL) {
    return -1;
  }

  struct sigaction sa_int, osa_int;
  struct sigaction sa_tstp, osa_tstp;
  sigemptyset(&sa_int.sa_mask);
  sigemptyset(&sa_tstp.sa_mask);
  sa_int.sa_handler = sigint_handler;
  sa_tstp.sa_handler = sigtstp_handler;
  sa_int.sa_flags = 0;
  sa_tstp.sa_flags = 0;
  sigaction(SIGINT, &sa_int, &osa_int);
  sigaction(SIGTSTP, &sa_tstp, &osa_tstp);

  sigset_t old, new;
  sigemptyset(&new);
  sigaddset(&new, SIGCHLD);
  sigprocmask(SIG_BLOCK, &new, &old);

  exec_list(*cmd_buf, root, shell, 0, NULL, script);

  sigprocmask(SIG_SETMASK, &old, NULL);
  sigaction(SIGINT, &osa_int, NULL);
  sigaction(SIGTSTP, &osa_tstp, NULL);

  cleanup_ast(root);
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
