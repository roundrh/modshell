#include "shell_init.h"
#include <sys/sysmacros.h>

/**
 * @file shell_init.c
 *
 * @brief Module contains implementations of functions in shell_init.h
 *
 */

t_ht_node *insert_builtin(t_hashtable *ht, const char *name,
                          t_builtin_func fn) {
  t_builtin *b = malloc(sizeof(*b));
  if (!b) {
    perror("malloc");
    exit(12);
  }

  b->fn = fn;
  return ht_insert(ht, name, b);
}

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
  if (!insert_builtin(&shell->builtins, "exit", exit_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "stty", stty_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "cd", cd_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "alias", alias_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "unalias", unalias_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "fg", fg_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "bg", bg_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "jobs", jobs_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "kill", kill_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "export", export_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "unset", unset_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "clear", clear_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "env", env_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "history", history_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "v", v_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "[", test_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "true", true_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "false", false_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "echo", echo_builtin))
    return -1;

  return 0;
}

static int push_def_aliases(t_hashtable *ht) {
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
    perror("115: strdup");
    return -1;
  }

  char *bufbuf = malloc(PATH_MAX);
  if (!bufbuf) {
    perror("121: malloc");
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

static void hash_directory(t_hashtable *bins, char *dir_path) {
  DIR *dir = opendir(dir_path);
  struct dirent *entry;
  char full_path[4096];

  if (!dir)
    return;

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
    if (access(full_path, X_OK) == 0) {
      ht_insert(bins, entry->d_name, strdup(full_path));
    }
  }
  closedir(dir);
}
void refresh_path_bins(t_hashtable *bins) {
  char *path_env = getenv("PATH");
  if (!path_env)
    return;

  ht_flush(bins, free);

  char *path_copy = strdup(path_env);
  char *dir = strtok(path_copy, ":");

  while (dir) {
    hash_directory(bins, dir);
    dir = strtok(NULL, ":");
  }

  free(path_copy);
}

void init_bins(t_hashtable *bins) {
  ht_init(bins);
  refresh_path_bins(bins);
}

void init_env(t_shell *shell) {
  extern char **environ;

  ht_init(&shell->env);

  for (int i = 0; environ[i]; i++) {
    char *entry = strdup(environ[i]);
    char *eq = strchr(entry, '=');
    if (eq) {
      *eq = '\0';
      add_to_env(shell, entry, eq + 1);
    }
    free(entry);
  }
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
    perror("malloc");
    return -1;
  }

  shell->prompt = NULL;

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
        perror("warning: setpgid fail");
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

  ht_init(&(shell->builtins));
  ht_init(&(shell->aliases));
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

  init_env(shell);
  init_bins(&shell->bins);

  return 0;
}
