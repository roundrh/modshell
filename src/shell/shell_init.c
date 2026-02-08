#include "shell_init.h"
#include "lexer.h"

/**
 * @file shell_init.c
 *
 * @brief Module contains implementations of functions in shell_init.h
 *
 */

/**
 * @brief populate built ins hashtable with functions
 * @param shell pointer to shell struct
 *
 * @returns 0 success, -1 on fail
 *
 * built ins are split into forkable and unforkable, main use is to not run any
 * unforkable commands in a pipe.
 */
static int push_built_ins(t_shell *shell) {

  if (insert_builtin(&(shell->builtins), "exit", exit_builtin, 0) == NULL) {
    perror("FATAL: Failed to insert 'exit' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "stty", stty_builtin, 0) == NULL) {
    perror("FATAL: Failed to insert 'exit' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "cd", cd_builtin, 0) == NULL) {
    perror("FATAL: Failed to insert 'cd' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "alias", alias_builtin, 0) == NULL) {
    perror("FATAL: Failed to insert 'alias' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "fg", fg_builtin, 0) == NULL) {
    perror("FATAL: Failed to insert 'alias' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "bg", bg_builtin, 0) == NULL) {
    perror("FATAL: Failed to insert 'alias' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "jobs", jobs_builtin, 0) == NULL) {
    perror("FATAL: Failed to insert 'alias' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "kill", kill_builtin, 0) == NULL) {
    perror("FATAL: Failed to insert 'alias' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "unalias", unalias_builtin, 0) ==
      NULL) {
    perror("FATAL: Failed to insert 'unalias' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "export", export_builtin, 0) == NULL) {
    perror("FATAL: Failed to insert 'export' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "unset", unset_builtin, 0) == NULL) {
    perror("FATAL: Failed to insert 'unset' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "clear", clear_builtin, 1) == NULL) {
    perror("FATAL: Failed to insert 'help' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "env", env_builtin, 1) == NULL) {
    perror("FATAL: Failed to insert 'env' builtin");
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "history", history_builtin, 1) ==
      NULL) {
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "help", help_builtin, 1) == NULL) {
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "set", set_builtin, 1) == NULL) {
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }
  if (insert_builtin(&(shell->builtins), "[", cond_builtin, 1) == NULL) {
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }
  if (insert_builtin(&(shell->builtins), "true", true_builtin, 1) == NULL) {
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }
  if (insert_builtin(&(shell->builtins), "false", false_builtin, 1) == NULL) {
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  if (insert_builtin(&(shell->builtins), "echo", echo_builtin, 1) == NULL) {
    flush_builtin_ht(&(shell->builtins));
    return -1;
  }

  return 0;
}

/**
 * @brief help function to copy environ to shell->env
 * @param shell pointer to shell struct
 *
 * Deep copies extern char** environ to env in shell state struct
 *
 * @note mallocd env freed in shell_cleanup.h, called atexit()
 */
static int copy_environ_to_env(t_shell *shell) {

  size_t count = 0;
  while (environ[count] != NULL) {
    count++;
  }

  shell->env_count = count;
  shell->env_cap = count * BUF_GROWTH_FACTOR;

  shell->env = (char **)malloc(sizeof(char *) * (shell->env_cap + 1));
  if (!shell->env) {
    perror("malloc shell->env fail");
    return -1;
  }

  for (size_t i = 0; i < shell->env_cap; i++)
    shell->env[i] = NULL;

  for (size_t i = 0; i < count; i++) {

    shell->env[i] = strdup(environ[i]);
    if (!shell->env[i]) {
      perror("strdup fail copying environ");
      for (int j = 0; j < i; j++) {
        free(shell->env[j]);
      }
      free(shell->env);
      return -1;
    }
  }
  shell->env[count] = NULL;

  return 0;
}

static int push_def_aliases(t_alias_hashtable *ht) {
  insert_alias(ht, "l", "ls -lAh");
  insert_alias(ht, "ls", "ls --color=tty");
  insert_alias(ht, "fetch", "neofetch");
  insert_alias(ht, "grep", "grep --color=auto");
  insert_alias(ht, "ll", "ls -lh");
  insert_alias(ht, "vim", "nvim");
  insert_alias(ht, "ff", "fastfetch");
  insert_alias(ht, "nf", "neofetch");
  return 0;
}

static int replace_home_dir(char **buf) {

  if (strncmp(*buf, "/home/", 6) != 0)
    return -1;

  char *replacement = strdup(*buf);
  if (!replacement) {
    perror("115: strdup malloc error");
    return -1;
  }

  char *bufbuf = malloc(PATH_MAX);
  if (!bufbuf) {
    perror("121: bufbuf malloc error");
    free(replacement);
    return -1;
  }
  bufbuf[0] = '~';
  bufbuf[1] = '\0';

  char *part = strtok(replacement, "/"); // ""
  part = strtok(NULL, "/");              // home

  while ((part = strtok(NULL, "/")) != NULL) {
    strcat(bufbuf, "/");
    strcat(bufbuf, part);
  }
  for (int i = 0; i < strlen(*buf); i++)
    (*buf)[i] = '\0'; ///< Clear buf
  free(*buf);
  *buf = strdup(bufbuf);
  free(bufbuf);
  free(replacement);

  return 0;
}

size_t visible_len(const char *s) {
  size_t len = 0;
  const char *p = s;
  while (*p) {
    if (*p == '\033' && *(p + 1) == '[') {
      p += 2;
      while (*p && !((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')))
        p++;
      if (*p)
        p++;
    } else {
      len++;
      p++;
    }
  }
  return len;
}
void get_shell_prompt(t_shell *shell) {

  if (shell->prompt)
    free(shell->prompt);

  char hostname[256];
  if (gethostname(hostname, sizeof(hostname)) == -1) {
    perror("gethostname");
  } else {
    hostname[sizeof(hostname) - 1] = '\0';
  }
  char *dir = getcwd(NULL, 0);
  if (!dir) {
    perror("getcwd");
    exit(1);
  }
  replace_home_dir(&dir);
  char *user = getenv("USER");
  size_t prompt_cap = strlen(dir) + strlen(user) + strlen(hostname) + 64;
  shell->prompt = (char *)malloc(prompt_cap);
  if (!shell->prompt) {
    perror("malloc");
    exit(1);
  }
  (shell->prompt)[0] = '\0';
  snprintf(shell->prompt, prompt_cap,
           "\033[1;37m%s@%s %s\033[0m:\033[0;37m%s\033[0m\033[1;37m$ \033[0m",
           user, hostname, dir, shell->sh_name);
  free(dir);

  shell->prompt_len = visible_len(shell->prompt);
}
/**
 * @brief initialize shell state to null or calloc values
 * @param shell pointer to shell struct
 *
 * Initializes to either malloc of initial lengths, defined within shell_init.h,
 * or NULL / -1 values.
 *
 * @note mallocd env freed in shell_cleanup.h, called atexit()
 */
int init_shell_state(t_shell *shell) {

  arena_init(&shell->arena);
  get_term_size(&shell->rows, &shell->cols);

  shell->sh_name = (char *)malloc(4);
  if (!shell->sh_name) {
    perror("shell init: sh name malloc fail");
    return -1;
  }

  shell->prompt = NULL;

  shell->intr = 0;
  shell->next_job_id = 1;
  shell->job_control_flag = 1;

  if (init_pa_sigtable(&(shell->shell_sigtable)) == -1) {
    perror("shell sigtable init");
    shell->job_control_flag = 0;
  }
  shell->fg_job = NULL;
  shell->last_exit_status = 0;

  shell->is_interactive = isatty(STDIN_FILENO);

  shell->tty_fd = STDIN_FILENO;

  if (shell->is_interactive) {
    shell->tty_fd = open("/dev/tty", O_RDWR);
    if (shell->tty_fd == -1)
      shell->tty_fd = STDIN_FILENO;
  } else {
    shell->tty_fd = -1;
    shell->job_control_flag = 0;
  }

  if (shell->is_interactive) {
    if (setpgid(0, 0) < 0) {
      if (errno != EPERM && errno != EACCES && errno != ESRCH) {
        perror("warning: setpgid failed");
        shell->job_control_flag = 0;
      }
    }
  }
  shell->pgid = getpgrp();

  if (shell->is_interactive && shell->job_control_flag && shell->tty_fd != -1 &&
      shell->tty_fd != STDIN_FILENO) {
    if (tcsetpgrp(shell->tty_fd, shell->pgid) == -1) {
      if (errno != EINVAL && errno != ENOTTY) {
        perror("warning: tcsetpgrp failed");
        shell->job_control_flag = 0;
      }
    }
  }

  shell->job_table =
      (t_job **)malloc(sizeof(t_job *) * INITIAL_JOB_TABLE_LENGTH);
  if (shell->job_table == NULL) {
    perror("fatal job table alloc err");
    return -1;
  }
  for (size_t i = 0; i < INITIAL_JOB_TABLE_LENGTH; i++)
    shell->job_table[i] = NULL;

  shell->job_table_cap = INITIAL_JOB_TABLE_LENGTH;
  shell->job_count = 0;

  if (!shell->is_interactive)
    shell->job_control_flag = 0;

  init_ast(&(shell->ast));

  init_builtin_hashtable(&(shell->builtins));
  init_alias_hashtable(&(shell->aliases));
  push_def_aliases(&(shell->aliases));

  init_dll(&(shell->history));

  if (shell->is_interactive)
    init_s_term_ctrl(shell);

  shell->last_exit_status = 0;

  strcpy(shell->sh_name, "msh");

  shell->std_fd_backup[0] = -1;
  shell->std_fd_backup[1] = -1;

  if (push_built_ins(shell) == -1) {
    perror("failure to initialize");
    return -1;
  }

  shell->env = NULL;

  if (copy_environ_to_env(shell) == -1) {
    perror("error copying environ to shell env");
    shell->env = NULL;
    return -1;
  }

  return 0;
}
