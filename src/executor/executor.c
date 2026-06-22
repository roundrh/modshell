#include "executor.h"
#include "ast.h"
#include "hashtable.h"
#include "jobs.h"
#include "shell.h"

typedef enum e_pgrp {

  PGPR_NONE = 0,
  PGRP_FATAL = -1,
  PGRP_INVAL = -2
} t_pgrp;

/**
 * @file executor.c
 * @brief implementation of functions used to execute AST.
 */

static void del_local_depth(size_t depth, t_hashtable *env) {
  for (size_t i = 0; i < env->count; ++i) {
    t_ht_node *h = env->buckets[i];
    while (h) {
      t_ht_node *n = h->next;
      t_env_entry *v = (t_env_entry *)h->value;
      if (v && (v->flags & ENV_LOCAL) && v->local_depth == depth) {
        remove_from_env(env, h->key);
      }
      h = n;
    }
  }
}

static char *append_script_line(char *old_buf, const char *new_line,
                                t_arena *a) {

  size_t old_len = old_buf ? strlen(old_buf) : 0;
  size_t new_len = strlen(new_line);

  char *new_buf = arena_realloc(a, old_buf, old_len + new_len + 1, old_len);
  memcpy(new_buf + old_len, new_line, new_len + 1);
  return new_buf;
}

static void print_err(t_err_code last_err, size_t lc, bool is_script) {

  const char *msg;
  switch (last_err) {
  case ERR_ALIAS_DEPTH:
    msg = "alias expansion exceeded maximum depth";
    break;
  case ERR_UNBALANCED_QUOTES:
    msg = "unbalanced quotes";
    break;
  case ERR_UNBALANCED_PARENS:
    if (!is_script)
      msg = "unbalanced parentheses";
    else
      return;
    break;
  case ERR_UNBALANCED_BRACES:
    if (!is_script)
      msg = "unbalanced braces";
    else
      return;
    break;
  case ERR_UNBALANCED_TOKEN:
    if (!is_script)
      msg = "missing terminator";
    else
      return;
    break;
  case ERR_REDIR_MAX:
    msg = "too many redirections";
    break;
  case ERR_REDIR_FILENAME:
    msg = "missing filename for redirection";
    break;
  case ERR_MISSING_FI:
    if (!is_script)
      msg = "missing 'fi'";
    else
      return;
    break;
  case ERR_MISSING_THEN:
    if (!is_script)
      msg = "missing 'then'";
    else
      return;
    break;
  case ERR_MISSING_DO:
    if (!is_script)
      msg = "missing 'do'";
    else
      return;
    break;
  case ERR_MISSING_DONE:
    if (!is_script)
      msg = "missing 'done'";
    else
      return;
    break;
  case ERR_MISSING_FOR_VAR:
    msg = "missing loop variable in 'for'";
    break;
  case ERR_MISSING_IN:
    if (!is_script)
      msg = "missing 'in' in 'for'";
    else
      return;
    break;
  case ERR_EMPTY_LOOP:
    msg = "empty loop body";
    break;
  case ERR_MALFORMED_FUN:
    msg = "malformed function definition";
    break;
  case ERR_NEAR_PIPE:
    msg = "syntax error near unexpected token '|'";
    break;

  case ERR_NEAR_AND:
    msg = "syntax error near unexpected token '&&'";
    break;

  case ERR_NEAR_OR:
    msg = "syntax error near unexpected token '||'";
    break;
  case ERR_ALLOC:
    msg = "memory allocation failure";
    break;
  default:
    if (last_err == -1)
      return;
    msg = "unknown parse error";
    break;
  }

  if (is_script)
    fprintf(stderr, "msh: %zu: %s\n", lc, msg);
  else
    fprintf(stderr, "msh: %s\n", msg);
}

int exec_script(t_shell *shell, const char *path) {
  FILE *script = fopen(path, "r");
  shell->script_fstream = script;
  if (!script) {
    errno = EINVAL;
    perror("msh: open");
    return -1;
  }

  char *line = NULL;
  size_t lc = 0;
  size_t cap = 0;
  char *total_buf = NULL;
  while (getline(&line, &cap, script) != -1) {
    char *p = line;
    while (*p && isspace(*p))
      p++;
    lc++;
    if (*p == '#' || *p == '\0')
      continue;

    total_buf = append_script_line(total_buf, line, &shell->arena);

    t_err_code last_err;
    if (parse_and_execute(&total_buf, shell, &shell->token_stream, true,
                          &last_err) == 0) {
      total_buf = NULL;
      arena_reset(&shell->arena);
    } else {
      print_err(last_err, lc, true);
      last_err = 0;
    }
  }
  if (total_buf != NULL) {
    fprintf(stderr, "msh: unexpected EOF while looking for matching token\n");
  }

  free(line);
  fclose(script);
  shell->script_fstream = NULL;
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
      if (job->depth > 0)
        del_local_depth(job->depth, &shell->env);
    }
  }

  del_completed_jobs(shell);

  return reaped;
}

static pid_t exec_pipe(t_ast_n *node, t_shell *shell, t_job *job);
static pid_t exec_command(t_ast_n *node, t_shell *shell, t_job *job);
static pid_t exec_simple_command(t_ast_n *node, t_shell *shell, t_job *job);
static int exec_list(char *cmd_buf, t_ast_n *node, t_shell *shell);
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
      if (pid == job->last_pid) {
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
      if (pid == job->last_pid) {
        job->last_exit_status = 128 + sig;
        shell->last_exit_status = job->last_exit_status;
      }
      if (pid == job->last_pid)
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

static pid_t exec_bg_fun(t_shell *shell, t_ast_n *node, t_job *job,
                         t_ht_node *fn, char **argv) {

  t_exec_ctx *ctx = &shell->exec_ctx;

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    return -1;
  }
  if (job->pgid == -1)
    job->pgid = pid;

  if (pid == 0) {

    if (!ctx->is_subshell) {
      if (setpgid(0, job->pgid) < 0) {
        if (errno != EPERM && errno != EACCES && errno != ESRCH) {
          perror("280: setpgid");
          _exit(EXIT_FAILURE);
        }
      }
    }

    shell->job_control_flag = 0;
    init_ch_sigtable(&(shell->shell_sigtable));

    ctx->subshell_job = job;
    ctx->flow = false;
    exec_list(NULL, (t_ast_n *)fn->value, shell);
    while (waitpid(-job->pgid, NULL, 0) > 0)
      ;

    _exit(shell->last_exit_status);
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

static pid_t exec_extern_cmd(t_shell *shell, t_ast_n *node, t_job *job,
                             char **argv) {

  t_exec_ctx *ctx = &shell->exec_ctx;

  if (ctx->pipeline) {
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
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork fail exec_extern");
    return -1;
  }

  if (job->pgid == -1)
    job->pgid = pid;

  if (pid == 0) {

    if (!ctx->is_subshell) {
      if (setpgid(0, job->pgid) < 0) {
        if (errno != EPERM && errno != EACCES && errno != ESRCH) {
          perror("280: setpgid");
          _exit(EXIT_FAILURE);
        }
      }
    }

    init_ch_sigtable(&(shell->shell_sigtable));

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
                             t_ht_node *builtin_ptr, char **argv) {
  t_exec_ctx *ctx = &shell->exec_ctx;

  if (!argv) {
    perror("argv prop err");
    return -1;
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork fail exec_extern");
    return -1;
  }

  if (job->pgid == -1)
    job->pgid = pid;

  if (pid == 0) {

    init_ch_sigtable(&(shell->shell_sigtable));

    if (!ctx->is_subshell) {
      if (setpgid(0, job->pgid) < 0) {
        if (errno != EPERM && errno != EACCES && errno != ESRCH) {
          perror("280: setpgid");
          _exit(EXIT_FAILURE);
        }
      }
    }

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
static pid_t exec_simple_command(t_ast_n *node, t_shell *shell, t_job *job) {

  t_exec_ctx *ctx = &shell->exec_ctx;
  if (!node)
    return -1;

  t_region *p = NULL;
  size_t off = 0;
  if (!ctx->pipeline)
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

  t_ht_node *fn_node = ht_find(&shell->functions, argv[0]);

  const char *vn = "FUNCNEST";
  const char *fun_nest = getenv_local(&shell->env, vn, &shell->arena);
  int fnestmax = 10;
  if (fun_nest)
    fnestmax = atoi(fun_nest);

  if (fn_node) {
    if (ctx->fnest_d >= fnestmax) {
      fprintf(stderr,
              "msh: maximum nested function calls, increase FUNCNEST "
              "via export FUNCNEST\nFUNCNEST=%d",
              fnestmax);
      job->last_exit_status = shell->last_exit_status = 1;
      del_local_depth(ctx->fnest_d, &shell->env);
      return 0;
    }
    if (job->position == P_FOREGROUND) {
      ctx->fnest_d++;
      char **curr_argv = shell->argv;
      shell->argv = argv;
      size_t curr_argc = shell->argc;
      for (shell->argc = 0; argv[shell->argc]; shell->argc++)
        ;
      int status = exec_list(NULL, fn_node->value, shell);
      if (status)
        shell->last_exit_status = status;

      // this can never be == 0 here but guarding to be safe as to not delete
      // every not exported variable
      if (ctx->fnest_d > 0)
        del_local_depth(ctx->fnest_d, &shell->env);
      ctx->fnest_d--;
      shell->argv = curr_argv;
      shell->argc = curr_argc;
      return status;
    } else if (job->position == P_BACKGROUND) {
      job->depth = ctx->fnest_d;
      return exec_bg_fun(shell, node, job, fn_node, argv);
    }
  }

  t_ht_node *builtin_imp = ht_find(&shell->builtins, argv[0]);
  if (builtin_imp && strcmp(builtin_imp->key, "exit") != 0)
    shell->exflag = 0;
  if (builtin_imp && strcmp(builtin_imp->key, "v") == 0)
    builtin_imp = NULL;
  else if (!builtin_imp && is_set_var(argv)) {
    builtin_imp = ht_find(&shell->builtins, "v");
  }

  if (builtin_imp == NULL) {
    pid_t ret_pid = exec_extern_cmd(shell, node, job, argv);
    arena_rollback(&shell->arena, p, off);
    return ret_pid;
  } else if (job->position == P_FOREGROUND) {
    t_builtin *b = (t_builtin *)builtin_imp->value;
    job->last_exit_status = b->fn(node, shell, argv);
    shell->last_exit_status = job->last_exit_status;
  } else {
    return exec_bg_builtin(node, shell, job, builtin_imp, argv);
  }

  if (!ctx->pipeline)
    arena_rollback(&shell->arena, p, off);

  fflush(stdout);
  fflush(stderr);

  /* pid 0 on built in execution -- denotes no fork -- shell last exit status
   * set */
  return 0;
}

static pid_t exec_subshell(t_ast_n *node, t_shell *shell, t_job *job) {

  t_exec_ctx *ctx = &shell->exec_ctx;

  pid_t pid = fork();
  if (pid < 0) {
    perror("241: fork fail");
    return -1;
  }

  if (!ctx->is_subshell && job->pgid == -1)
    job->pgid = pid;

  if (pid == 0) {
    if (!ctx->is_subshell) {
      if (setpgid(0, job->pgid) < 0) {
        if (errno != EPERM && errno != EACCES && errno != ESRCH) {
          perror("379: setpgid");
          _exit(EXIT_FAILURE);
        }
      }
    }

    shell->job_control_flag = 0;
    init_ch_sigtable(&(shell->shell_sigtable));

    ctx->is_subshell = true;
    ctx->subshell_job = job;

    exec_list(NULL, node->sub_ast_root, shell);

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
static pid_t exec_command(t_ast_n *node, t_shell *shell, t_job *job) {
  t_exec_ctx *ctx = &shell->exec_ctx;

  if (!node)
    return -1;

  bool restore_io_flag = 0;
  if (node->redir_bool) {
    if (redirect_io(shell, node) == -1) {
      fprintf(stderr, "msh: redirect io\n");
      return -1;
    }
    if (!ctx->flow)
      node->redir_bool = false;
    restore_io_flag = 1;
  }

  pid_t pid = -1;
  if (node->op_type == OP_PIPE) {
    pid = exec_pipe(node, shell, job);
  } else if (node->op_type == OP_SIMPLE) {
    pid = exec_simple_command(node, shell, job);
  } else if (node->op_type == OP_SUBSHELL) {
    pid = exec_subshell(node, shell, job);
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
    if (!new_node) {
      return NULL;
    }

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
static pid_t exec_pipe(t_ast_n *node, t_shell *shell, t_job *job) {

  t_exec_ctx *ctx = &shell->exec_ctx;

  pid_t last_pid = -1;

  t_ast_n *pipeline = flatten_ast(node, &shell->arena);
  if (!pipeline)
    return -1;

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
  ctx->pipeline = pipeline;
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

        if (!ctx->is_subshell) {
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

        exec_command(exec, shell, job);

        _exit(shell->last_exit_status);
      } else if (i == count_cmd - 1) {

        if (!ctx->is_subshell) {
          if (setpgid(0, job->pgid) < 0) {
            if (errno != EPERM && errno != EACCES && errno != ESRCH) {
              perror("280: setpgid");
              _exit(EXIT_FAILURE);
            }
          }
        }

        if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
          perror("dup2");
        }
        for (int j = 0; j < count_cmd - 1; j++) {
          close(pipes[j][0]);
          close(pipes[j][1]);
        }

        init_ch_sigtable(&shell->shell_sigtable);

        exec_command(exec, shell, job);

        _exit(shell->last_exit_status);
      } else {

        if (!ctx->is_subshell) {
          if (setpgid(0, job->pgid) < 0) {
            if (errno != EPERM && errno != EACCES && errno != ESRCH) {
              perror("280: setpgid");
              _exit(EXIT_FAILURE);
            }
          }
        }

        if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
          perror("dup2");
        }
        if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
          perror("dup2");
        }
        for (int j = 0; j < count_cmd - 1; j++) {
          close(pipes[j][0]);
          close(pipes[j][1]);
        }

        init_ch_sigtable(&shell->shell_sigtable);

        exec_command(exec, shell, job);

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
      if (i == count_cmd - 1) {
        job->last_pid = pid;
        last_pid = pid;
      }

      add_process_to_job(job, process);
    }

    exec = exec->right;
    i++;
  }

  for (int j = 0; j < count_cmd - 1; j++) {
    close(pipes[j][0]);
    close(pipes[j][1]);
  }

  ctx->pipeline = NULL;
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

static int exec_job(char *cmd_buf, t_ast_n *node, t_shell *shell) {

  t_exec_ctx *ctx = &shell->exec_ctx;
  t_job *job = NULL;
  if (!ctx->is_subshell)
    job = make_job(shell, cmd_buf, S_RUNNING, -1,
                   node->background ? P_BACKGROUND : P_FOREGROUND);
  else
    job = ctx->subshell_job;
  if (!job) {
    return -1;
  }

  ctx->pipeline = NULL;
  pid_t lpid = exec_command(node, shell, job);
  if (shell->job_control_flag && job->position == P_FOREGROUND) {

    t_pgrp tc;
    if ((tc = handle_tcsetpgrp(shell, job->pgid)) == PGRP_FATAL) {
      perror("720: tcsetpgrp reclaim");
      exit(1);
    }

    t_wait_status job_status = wait_for_foreground_job(job, shell);

    if ((tc = handle_tcsetpgrp(shell, shell->pgid)) == PGRP_FATAL) {
      perror("720: tcsetpgrp reclaim");
      exit(1);
    }
    tcgetattr(shell->tty_fd, &shell->term_ctrl.curr_settings);

    if (job_status == WAIT_FINISHED) {
      del_job(shell, job->job_id, ctx->flow);
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
      exit(1);
    }

  } else if (shell->job_control_flag && job->pgid != -1) {
    if (!ctx->is_subshell)
      print_job_info(job);
    return WAIT_FINISHED;
  } else if (!shell->job_control_flag) {

    /* builtins set shell exit status - return pid 0 */
    if (lpid > 0) {
      int status;
      pid_t p;
      while ((p = waitpid(-1, &status, 0)) > 0) {
        if (p == lpid) {
          if (WIFEXITED(status))
            shell->last_exit_status = WEXITSTATUS(status);
          else if (WIFSIGNALED(status))
            shell->last_exit_status = WTERMSIG(status) + 128;
        }
      }
    }

    if (!ctx->is_subshell) {
      del_job(shell, job->job_id, true);
      job = NULL;
    }
  }

  return WAIT_FINISHED;
}

static int exec_cond_bg(char *cmd_buf, t_ast_n *node, t_shell *shell) {

  t_exec_ctx *ctx = &shell->exec_ctx;

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

    node->background = 0;

    ctx->is_subshell = 1;
    ctx->subshell_job = job;

    exec_list(cmd_buf, node, shell);
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

static int exec_list(char *cmd_buf, t_ast_n *node, t_shell *shell) {
  if (!node)
    return 0;
  if (sigint_flag || sigtstp_flag)
    return WAIT_INTERRUPTED;

  t_exec_ctx *ctx = &shell->exec_ctx;

  switch (node->op_type) {
  case OP_FUN: {
    t_ast_n *clone = clone_heap_ast(node->sub_ast_root);
    if (!clone) {
      perror("clone");
      return -1;
    }
    size_t len = node->tok_start->len - 2;
    char *buf = arena_alloc(&shell->arena, len + 1);
    memcpy(buf, node->tok_start->start, len);
    buf[len] = '\0';
    if (!ht_insert(&shell->functions, buf, clone, free_heap_ast)) {
      free_heap_ast(clone);
      fprintf(stderr, "\nmsh: failed to insert function");
      return -1;
    }
    return 0;
  }
  case OP_SEQ:
    exec_list(cmd_buf, node->left, shell);
    return exec_list(cmd_buf, node->right, shell);
  case OP_AND:
    if (node->background && !(ctx->is_subshell)) {
      return exec_cond_bg(cmd_buf, node, shell);
    }
    exec_list(cmd_buf, node->left, shell);
    if (shell->last_exit_status == 0)
      exec_list(cmd_buf, node->right, shell);
    return shell->last_exit_status;

  case OP_OR:
    if (node->background && !(ctx->is_subshell)) {
      return exec_cond_bg(cmd_buf, node, shell);
    }
    exec_list(cmd_buf, node->left, shell);
    if (shell->last_exit_status != 0)
      exec_list(cmd_buf, node->right, shell);
    return shell->last_exit_status;
  case OP_IF:
    exec_list(cmd_buf, node->left, shell);
    if (shell->last_exit_status == 0) {
      exec_list(cmd_buf, node->right, shell);
    } else if (node->sub_ast_root) {
      exec_list(cmd_buf, node->sub_ast_root, shell);
    }
    return (ctx->is_subshell) ? shell->last_exit_status : WAIT_FINISHED;
  case OP_WHILE: {

    bool prev_flow = ctx->flow;
    ctx->flow = true;
    while (!sigint_flag && !sigtstp_flag) {
      /* force reap when some larp decides to spam the job table */
      if (is_job_table_full(shell)) {
        wait_for_job_slot(shell);
      }

      t_wait_status wait;
      wait = exec_list(cmd_buf, node->left, shell);
      if (sigint_flag || sigtstp_flag || wait == WAIT_INTERRUPTED ||
          wait == WAIT_STOPPED)
        break;
      if (shell->last_exit_status != 0)
        break;

      wait = exec_list(cmd_buf, node->right, shell);
      if (sigint_flag || sigtstp_flag || wait == WAIT_INTERRUPTED ||
          wait == WAIT_STOPPED)
        break;
    }
    ctx->flow = prev_flow;

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

    bool prev_flow = ctx->flow;
    ctx->flow = true;
    for (int i = 0; expanded_items[i] != NULL; i++) {
      // see OP_WHILE
      if (is_job_table_full(shell)) {
        wait_for_job_slot(shell);
      }

      if (sigint_flag || sigtstp_flag)
        break;

      add_to_env(shell, var_name, expanded_items[i], false, 0);
      t_wait_status wait = exec_list(cmd_buf, node->sub_ast_root, shell);
      if (wait == WAIT_INTERRUPTED || wait == WAIT_STOPPED)
        break;
    }
    ctx->flow = prev_flow;

    return (ctx->is_subshell) ? shell->last_exit_status : WAIT_FINISHED;
  }
  default:
    return exec_job(cmd_buf, node, shell);
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
                      t_token_stream *token_stream, bool script,
                      t_err_code *last_err) {

  *last_err = -1;

  init_token_stream(token_stream, &shell->arena);
  t_hashtable *aliases = &(shell->aliases);
  if (lex_command_line(cmd_buf, token_stream, aliases, 0, &shell->arena, 0) ==
      -1) {
    return -1;
  }

  t_ast_n *root;
  if ((root = build_ast(&(shell->ast), token_stream, &shell->arena,
                        last_err)) == NULL) {
    if (!script)
      print_err(*last_err, 0, script);
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
  exec_list(*cmd_buf, root, shell);
  if (!script) {
    sigprocmask(SIG_SETMASK, &old, NULL);
    sigaction(SIGINT, &osa_int, NULL);
    sigaction(SIGTSTP, &osa_tstp, NULL);
  }
  shell->ast.root = NULL;

  sigint_flag = 0;
  sigtstp_flag = 0;
  return 0;
}
