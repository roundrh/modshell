#include "executor.h"
#include "shell.h"
#include "shell_cleanup.h"
#include "shell_init.h"
#include "userinp.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static t_shell *g_shell_ptr = NULL;

void set_global_shell_ptr(t_shell *ptr) { g_shell_ptr = ptr; }

void cleanup_global_shell_ptr(void) {
  cleanup_shell(g_shell_ptr, 0);
  g_shell_ptr = NULL;
}

static int add_hist_entry(t_shell *shell, const char *line) {
  t_dll *hist = &shell->history;

  if (!push_front_dll(line, hist)) {
    perror("push_front_dll");
    return -1;
  }

  while (hist->size > HIST_MAX) {
    t_dllnode *old = hist->tail;
    if (!old)
      break;

    hist->tail = old->prev;
    if (hist->tail)
      hist->tail->next = NULL;
    else
      hist->head = NULL;

    free(old->strbg);
    free(old);
    hist->size--;
  }

  const char *home = getenv_local_ref(&shell->env, "HOME");
  if (home) {
    char hist_path[PATH_MAX];
    snprintf(hist_path, sizeof(hist_path), "%s/.msh_history", home);

    FILE *fp = fopen(hist_path, "a");
    if (fp) {
      fprintf(fp, "%s\n", line);
      fclose(fp);
    }
  }

  return 0;
}

void make_argl(t_shell *shell, int argc, char **argv) {

  if (argc <= 1) {
    shell->argc = 0;
    shell->argv = NULL;
    return;
  }

  shell->argc = argc - 1;

  shell->argv = malloc(sizeof(char *) * (shell->argc + 1));
  if (!shell->argv) {
    perror("msh: empty argv malloc fail");
    return;
  }

  for (int i = 0; i < shell->argc; i++) {
    shell->argv[i] = argv[i + 1];
  }
  shell->argv[shell->argc] = NULL;
}

static int ign_sigint(t_shell_sigtable *sigtable) {
  INIT_SIG(sigtable, sigint, SIG_IGN, 0, SIGINT);
  return 0;
}
static int unign_sigint(t_shell_sigtable *sigtable) {
  INIT_SIG(sigtable, sigint, sigint_handler, 0, SIGINT);
  return 0;
}

int main(int argc, char **argv) {

  t_shell shell_state;
  char *cmd_line_buf = NULL;

  set_global_shell_ptr(&shell_state);
  atexit(cleanup_global_shell_ptr);

  bool script = (argc > 1) ? true : false;
  if (init_shell_state(&shell_state, script) == -1) {
    fprintf(stderr, "msh: fail to initialize\n");
    return -1;
  }

  make_argl(&shell_state, argc, argv);

  if (argc > 1) {

    init_ch_sigtable(&shell_state.shell_sigtable);
    // init shell already disables this
    shell_state.job_control_flag = 0;

    if (argc > 2) {
      if (strcmp(argv[1], "-c") == 0) {
        t_err_code last_err;
        parse_and_execute(&(argv[2]), &shell_state, &shell_state.token_stream,
                          script, &last_err);
        exit(shell_state.last_exit_status);
      }
    }

    if (exec_script(&shell_state, argv[1]) == -1) {
      exit(1);
    }
    exit(shell_state.last_exit_status);
  }

  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  while (1) {
    if (shell_state.is_interactive) {

      if (shell_state.job_control_flag && sigchld_flag) {
        sigchld_flag = 0;
        reap_sigchld_jobs(&shell_state);
      }

      if (shell_state.job_control_flag &&
          tcgetpgrp(shell_state.tty_fd) != shell_state.pgid) {
        tcsetpgrp(shell_state.tty_fd, shell_state.pgid);
      }

      unrawify(&shell_state);
      HANDLE_WRITE_FAIL_FATAL(shell_state.tty_fd, "\033[?25h", 6, cmd_line_buf);
      HANDLE_WRITE_FAIL_FATAL(shell_state.tty_fd, "\033[5 q", 5, cmd_line_buf);

      get_shell_prompt(&shell_state);
      printf("%s", shell_state.prompt);

      rawify(&shell_state);
      unign_sigint(&shell_state.shell_sigtable);
      cmd_line_buf = read_user_inp(&shell_state);
      ign_sigint(&shell_state.shell_sigtable);
      unrawify(&shell_state);
    } else {
      size_t cap = 0;
      ssize_t n = getline(&cmd_line_buf, &cap, stdin);
      if (n == -1) {
        exit(shell_state.last_exit_status);
      }
    }

    if (!cmd_line_buf || *cmd_line_buf == '\0' || *cmd_line_buf == '\n' ||
        *cmd_line_buf == '\r') {

      if (shell_state.is_interactive)
        HANDLE_WRITE_FAIL_FATAL(shell_state.tty_fd, "\n", 1, cmd_line_buf);
      reap_sigchld_jobs(&shell_state);
      continue;
    }

    cmd_line_buf[strcspn(cmd_line_buf, "\n")] = '\0';
    cmd_line_buf[strcspn(cmd_line_buf, "\r")] = '\0';

    if (add_hist_entry(&shell_state, cmd_line_buf) == -1) {
      perror("add_hist_entry");
      return -1;
    }

    if (shell_state.is_interactive)
      HANDLE_WRITE_FAIL_FATAL(shell_state.tty_fd, "\n", 1, cmd_line_buf);

    t_err_code last_err;
    parse_and_execute(&cmd_line_buf, &shell_state, &shell_state.token_stream,
                      false, &last_err);
    arena_reset(&shell_state.arena);
  }

  return 0;
}
