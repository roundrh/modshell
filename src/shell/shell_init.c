#include "shell_init.h"
#include "builtins.h"
#include "executor.h"
#include "shell.h"
#include "var_exp.h"
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
        dprintf(fd, "export MSH_RENDER_AUTOSGST=1\n\n");
        dprintf(fd, "export FUNCNEST=10\n\n");
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
  if (!fp) {
    fprintf(stderr, "msh: ~/.msh_history not found: file created\n");
    return;
  }

  char *line = NULL;
  size_t len = 0;
  while (getline(&line, &len, fp) != -1) {
    line[strcspn(line, "\n")] = 0;
    push_front_dll(line, &shell->history);
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
typedef struct s_builtin_def {
  const char *name;
  int (*fn)(t_ast_n *, t_shell *, char **);
} t_builtin_def;

static int push_built_ins(t_shell *shell) {

  static const t_builtin_def builtins[] = {{"exit", exit_builtin},
                                           {"cd", cd_builtin},
                                           {"alias", alias_builtin},
                                           {"unalias", unalias_builtin},
                                           {"fg", fg_builtin},
                                           {"bg", bg_builtin},
                                           {"jobs", jobs_builtin},
                                           {"kill", kill_builtin},
                                           {"export", export_builtin},
                                           {"unset", unset_builtin},
                                           {"clear", clear_builtin},
                                           {"env", env_builtin},
                                           {"history", history_builtin},
                                           {"v", v_builtin},
                                           {"[", test_builtin},
                                           {"true", true_builtin},
                                           {"false", false_builtin},
                                           {"echo", echo_builtin},
                                           {"exec", exec_builtin},
                                           {"source", source_builtin},
                                           {".", source_builtin},
                                           {"read", read_builtin},
                                           {"pwd", pwd_builtin},
                                           {"builtin", builtin_builtin},
                                           {"rehash", rehash_builtin},
                                           {":", nop_builtin},
                                           {"set", set_builtin},
                                           {"local", local_builtin},
                                           {"break", break_builtin},
                                           {"continue", continue_builtin},
                                           {"return", return_builtin},
                                           {"type", type_builtin},
                                           {"shopt", shopt_builtin},
                                           {"eval", eval_builtin},
                                           {"readonly", readonly_builtin},
                                           {"command", command_builtin},
                                           {"hash", hash_builtin},
                                           {"times", times_builtin},
                                           {"wait", wait_builtin},
                                           {"trap", trap_builtin},
                                           {"shift", shift_builtin}};

  for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
    if (!insert_builtin(&shell->builtins, builtins[i].name, builtins[i].fn))
      return -1;
  }

  return 0;
}

int replace_home_dir(char **buf, const char *home) {
  if (!home)
    return 0;
  char *pos = strstr(*buf, home);
  if (!pos)
    return -1;

  size_t home_len = strlen(home);

  memmove(pos + 1, pos + home_len, strlen(pos + home_len) + 1);

  *pos = '~';

  return 0;
}

size_t visible_len(const char *s, int cols, int *rows) {
  size_t len = 0;
  size_t col = 0;

  if (rows)
    *rows = 1;

  while (*s) {
    if (*s == '\033' && *(s + 1) == '[') {
      s += 2;
      while (*s && !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')))
        s++;
      if (*s)
        s++;
      continue;
    }

    if (*s == '\n') {
      if (cols > 0 && col)
        len += cols - col;

      col = 0;
      if (rows)
        (*rows)++;
      s++;
      continue;
    }

    unsigned char c = (unsigned char)*s;

    if ((c & 0xC0) != 0x80) {
      len++;
      col++;

      if (cols > 0 && col == (size_t)cols) {
        col = 0;
        if (rows)
          (*rows)++;
      }
    }

    s++;
  }

  return len;
}

char *parse_prompt(const char *src) {
  size_t len = strlen(src);
  char *dst = malloc(len + 1);
  if (!dst)
    return NULL;

  const char *s = src;
  char *d = dst;

  while (*s) {
    if (*s != '\\') {
      *d++ = *s++;
      continue;
    }

    s++;

    switch (*s) {
    case 'a':
      *d++ = '\a';
      s++;
      break;
    case 'b':
      *d++ = '\b';
      s++;
      break;
    case 'e':
      *d++ = '\033';
      s++;
      break;
    case 'f':
      *d++ = '\f';
      s++;
      break;
    case 'n':
      *d++ = '\n';
      s++;
      break;
    case 'r':
      *d++ = '\r';
      s++;
      break;
    case 't':
      *d++ = '\t';
      s++;
      break;
    case 'v':
      *d++ = '\v';
      s++;
      break;
    case '\\':
      *d++ = '\\';
      s++;
      break;
    case '\'':
      *d++ = '\'';
      s++;
      break;
    case '"':
      *d++ = '"';
      s++;
      break;

    case '0': {
      int value = 0;
      int count = 0;

      while (count < 3 && *s >= '0' && *s <= '7') {
        value = (value << 3) + (*s - '0');
        s++;
        count++;
      }

      *d++ = (char)value;
      break;
    }

    default:
      *d++ = '\\';
      if (*s)
        *d++ = *s++;
      break;
    }
  }

  *d = '\0';
  return dst;
}

int get_shell_prompt(t_shell *shell) {
  if (getenv_local_ref(&shell->env, "PS1"))
    return 0;

  if (shell->prompt)
    free(shell->prompt);

  char hostname[256];
  if (gethostname(hostname, sizeof(hostname)) == -1) {
    perror("gethostname");
    return -1;
  } else {
    hostname[sizeof(hostname) - 1] = '\0';
    add_to_env(shell, "HOST", hostname, false, 0);
  }

  char *dir = getcwd(NULL, 0);
  if (!dir) {
    perror("getcwd");
    return -1;
  }

  ;
  char *user = getenv("USER");
  size_t prompt_cap = strlen(dir) + strlen(user) + strlen(hostname) + 64;
  shell->prompt = (char *)malloc(prompt_cap);
  if (!shell->prompt) {
    perror("malloc");
    return -1;
  }
  (shell->prompt)[0] = '\0';
  snprintf(shell->prompt, prompt_cap,
           "\033[1;37m%s@%s %s\033[0m:\033[0;37m%s\033[0m\033[1;37m$ \033[0m",
           user, hostname, dir, shell->sh_name);
  free(dir);
  add_to_env(shell, "PS1", shell->prompt, false, 0);
  add_to_env(shell, "PS2", "> ", false, 0);

  replace_home_dir(&shell->prompt, getenv("HOME"));

  shell->prompt_len =
      visible_len(shell->prompt, shell->cols, &shell->prompt_rows);

  return 0;
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

int init_env(t_shell *shell) {

  ht_init(&shell->env);

  for (size_t i = 0; environ[i]; i++) {
    char *entry = strdup(environ[i]);
    if (!entry) {
      perror("strdup");
      return -1;
    }
    char *eq = strchr(entry, '=');
    if (eq) {
      *eq = '\0';
      if (add_to_env(shell, entry, eq + 1, false, 0) == -1)
        return -1;
      t_ht_node *hh = ht_find(&shell->env, entry);
      t_env_entry *a = (t_env_entry *)hh->value;
      a->flags |= ENV_EXPORTED;
    }

    free(entry);
  }

  return 0;
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
int init_shell_state(t_shell *shell, bool script) {

  for (size_t i = 0; i < NSIG; i++) {
    shell->traps[i] = NULL;
    sigs[i] = 0;
  }

  shell->exflag = 0;

  arena_init(&shell->arena);
  get_term_size(&shell->rows, &shell->cols);

  shell->prompt = NULL;

  shell->sh_name = (char *)malloc(4);
  strcpy(shell->sh_name, "msh");

  shell->next_job_id = 1;
  shell->job_control_flag = 1;

  if (init_pa_sigtable(&(shell->shell_sigtable)) == -1) {
    fprintf(stderr, "msh: failed sigaction: job control disabled\n");
    shell->job_control_flag = 0;
  }

  shell->fg_job = NULL;
  shell->last_exit_status = 0;

  shell->is_interactive = !script;
  shell->tty_fd = -1;
  shell->job_control_flag = 0;

  if (shell->is_interactive) {
    shell->tty_fd = open("/dev/tty", O_RDWR);
    if (shell->tty_fd == -1) {
      perror("open");
      fprintf(stderr, "msh: job control disabled\n");
      shell->tty_fd = STDIN_FILENO;
    } else {
      shell->job_control_flag = 1;
    }
  }

  if (shell->is_interactive) {
    if (setpgid(0, 0) < 0) {
      if (errno != EPERM && errno != EACCES && errno != ESRCH) {
        perror("setpgid");
        fprintf(stderr, "msh: job control disabled\n");
        shell->job_control_flag = 0;
      }
    }
  }

  shell->pgid = getpgrp();

  if (shell->is_interactive && shell->job_control_flag && shell->tty_fd != -1 &&
      shell->tty_fd != STDIN_FILENO) {
    if (tcsetpgrp(shell->tty_fd, shell->pgid) == -1) {
      if (errno != EINVAL && errno != ENOTTY) {
        perror("tcsetpgrp");
        fprintf(stderr, "msh: job control disabled\n");
        shell->job_control_flag = 0;
      }
    }
  }
  shell->job_table =
      (t_job **)malloc(sizeof(t_job *) * INITIAL_JOB_TABLE_LENGTH);
  if (shell->job_table == NULL) {
    perror("job table fail");
    return -1;
  }
  for (size_t i = 0; i < INITIAL_JOB_TABLE_LENGTH; i++)
    shell->job_table[i] = NULL;

  shell->job_table_cap = INITIAL_JOB_TABLE_LENGTH;
  shell->job_count = 0;

  init_ast(&(shell->ast));

  ht_init(&(shell->builtins));
  ht_init(&(shell->aliases));
  ht_init(&(shell->functions));

  init_dll(&(shell->history));

  if (shell->is_interactive)
    init_s_term_ctrl(shell);

  shell->last_exit_status = 0;

  if (push_built_ins(shell) == -1) {
    perror("failure to initialize");
    return -1;
  }

  if (init_env(shell) == -1) {
    return -1;
  }

  get_shell_prompt(shell);

  shell->path = getenv_local_ref(&shell->env, "PATH");
  if (shell->path) {
    shell->path_len = strlen(shell->path);
  } else {
    shell->path_len = 0;
  }
  init_bins(shell);

  shell->exec_ctx.is_subshell = false;
  shell->exec_ctx.subshell_job = NULL;
  shell->exec_ctx.pipeline = NULL;
  shell->exec_ctx.flow = false;
  shell->exec_ctx.fnest_d = 0;
  shell->exec_ctx.script = script;
  shell->exec_ctx.continue_loop = false;
  shell->exec_ctx.break_loop = false;
  shell->exec_ctx.return_fun = false;
  shell->exec_ctx.pipeline_pids = NULL;
  shell->exec_ctx.pids_len = 0;
  shell->exec_ctx.pids_cap = 0;

  if (shell->is_interactive) {
    load_rc(shell);
    load_history(shell);
    shell->script_fstream = NULL;
  }

  shell->pending_hds = NULL;
  shell->pending_hds_cap = 0;
  shell->pending_hds_len = 0;

  char *rndr = getenv_local(&shell->env, "MSH_RENDER_AUTOSGST", &shell->arena);
  if (!rndr) {
    shell->shopts.render_autosgst = false;
  } else {
    int x = atoi(rndr);
    if (x == 1)
      shell->shopts.render_autosgst = true;
    else
      shell->shopts.render_autosgst = false;
  }

  arena_reset(&shell->arena);

  return 0;
}
