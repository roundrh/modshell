#include "shell_init.h"
#include "executor.h"
#include "shell.h"
#include <sys/sysmacros.h>
#include <unistd.h>

extern char **environ;

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
  return ht_insert(ht, name, b, NULL);
}

static void load_rc(t_shell *shell) {
  const char *home = getenv_local_ref(&shell->env, "HOME");
  if (!home)
    return;

  char rc_path[PATH_MAX];
  snprintf(rc_path, sizeof(rc_path), "%s/.mshrc", home);

  if (access(rc_path, F_OK) == 0) {
    exec_script(shell, rc_path);
  } else if (errno == ENOENT) {
    char a;
    fprintf(stderr, "\nmsh: no rc file found at ~/.mshrc:\n");
    printf("y: Create default rc file\n");
    printf(
        "n: skip rc file creation (this will display next time msh is ran)\n");

    while (1) {
      printf("Enter (y/n): ");
      fflush(stdout);
      while (read(STDIN_FILENO, &a, 1) < 0) {
        if (errno != EINTR)
          exit(1);
      }
      if (a == 'y' || a == 'n')
        break;
    }

    if (a == 'y') {
      int fd = open(rc_path, O_CREAT | O_WRONLY | O_EXCL, 0644);
      if (fd != -1) {
        dprintf(fd, "# msh config file\n\n");
        dprintf(fd, "# aliases\n");
        dprintf(fd, "alias l='ls -lAh'\n");
        dprintf(fd, "alias ls='ls --color=tty'\n");
        dprintf(fd, "alias grep='grep --color=auto'\n");
        dprintf(fd, "alias vim='nvim'\n");
        dprintf(fd, "alias ll='ls -lh'\n");
        dprintf(fd, "alias nf='neofetch'\n");
        dprintf(fd, "alias fetch='neofetch'\n");
        dprintf(fd, "alias ..='cd ..'\n");
        dprintf(fd, "alias _='sudo'\n");

        close(fd);
        printf("msh: default .mshrc created at %s\n", rc_path);
        exec_script(shell, rc_path);
      } else {
        perror("\nmsh: failed to create rc file");
      }
    } else {
      printf("msh: skipping rc creation.\n");
    }
  }

  printf("\033[J");
}

static void load_history(t_shell *shell) {
  const char *home = getenv_local_ref(&shell->env, "HOME");
  if (!home)
    return;

  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "%s/.msh_history", home);
  FILE *fp = fopen(path, "r");
  if (!fp)
    return;

  char *line = NULL;
  size_t len = 0;
  while (getline(&line, &len, fp) != -1) {
    line[strcspn(line, "\n")] = 0;
    push_back_dll(line, &shell->history);
  }
  free(line);
  fclose(fp);
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
  if (!insert_builtin(&shell->builtins, "exec", exec_builtin))
    return -1;
  if (!insert_builtin(&shell->builtins, "source", source_builtin))
    return -1;
  if (!insert_builtin(&shell->builtins, ".", source_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "read", read_builtin))
    return -1;

  if (!insert_builtin(&shell->builtins, "pwd", pwd_builtin))
    return -1;
  if (!insert_builtin(&shell->builtins, "builtin", builtin_builtin))
    return -1;

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
      ht_insert(bins, entry->d_name, strdup(full_path), free);
    }
  }
  closedir(dir);
}
void refresh_path_bins(t_shell *shell) {
  if (!shell->path)
    return;

  ht_flush(&shell->bins, free);

  char *path_copy = arena_alloc(&shell->arena, shell->path_len + 1);
  memcpy(path_copy, shell->path, shell->path_len);
  path_copy[shell->path_len] = '\0';

  char *dir = strtok(path_copy, ":");

  while (dir) {
    hash_directory(&shell->bins, dir);
    dir = strtok(NULL, ":");
  }
}

void init_bins(t_shell *shell) {
  ht_init(&shell->bins);
  refresh_path_bins(shell);
}

void init_env(t_shell *shell) {

  ht_init(&shell->env);

  for (int i = 0; environ[i]; i++) {
    char *entry = strdup(environ[i]);
    char *eq = strchr(entry, '=');
    if (eq) {
      *eq = '\0';
      add_to_env(shell, entry, eq + 1);
      t_ht_node *hh = ht_find(&shell->env, entry);
      t_env_entry *a = (t_env_entry *)hh->value;
      a->flags |= ENV_EXPORTED;
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
int init_shell_state(t_shell *shell, int script) {

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
  shell->path = getenv_local_ref(&shell->env, "PATH");
  if (shell->path) {
    shell->path_len = strlen(shell->path);
  } else {
    shell->path_len = 0;
  }
  init_bins(shell);

  if (!script) {
    load_rc(shell);
    load_history(shell);
  }

  return 0;
}
