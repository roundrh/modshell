#include "shell_cleanup.h"
#include "alias.h"
#include "builtins.h"
#include "dll.h"
#include "var_exp.h"

/**
 * @file shell_cleanup.c
 * @brief implementations of cleanup for shell struct
 */

static void save_history(t_shell *shell) {
  const char *home = getenv_local_ref(&shell->env, "HOME");
  if (!home)
    return;

  char hist_path[PATH_MAX];
  snprintf(hist_path, sizeof(hist_path), "%s/.msh_history", home);
  FILE *fp = fopen(hist_path, "w");
  if (!fp)
    return;

  t_dllnode *curr = shell->history.head;
  int count = 0;
  while (curr && count < 2000) {
    fprintf(fp, "%s\n", (char *)curr->strbg);
    curr = curr->next;
    count++;
  }
  fclose(fp);
}

/**
 * @brief cleans up shell struct
 * @param shell pointer to shell struct
 *
 * Flushes malloc'd data members, does nothing with static data members
 * refer to shell.h.
 */
void cleanup_shell(t_shell *shell, int is_chld) {

  arena_free(&shell->arena);
  if (shell->prompt)
    free(shell->prompt);

  if (shell->job_table) {

    for (size_t i = 0; i < shell->job_table_cap; i++) {

      if (shell->job_table[i] == NULL)
        continue;

      cleanup_job_struct(shell->job_table[i]);
      free(shell->job_table[i]);

      shell->job_table[i] = NULL;
    }
    free(shell->job_table);
    shell->job_table = NULL;
  }

  if (cleanup_sigtable(&(shell->shell_sigtable)) == -1) {
    perror("shell sigtable");
  }

  shell->last_exit_status = 0;

  save_history(shell);
  free_dll(&(shell->history));

  if (shell->sh_name) {
    free(shell->sh_name);
    shell->sh_name = NULL;
  }

  ht_flush(&shell->aliases, free_alias);
  ht_flush(&shell->builtins, free_builtin);
  ht_flush(&shell->env, free_env_entry);
  ht_flush(&shell->bins, free);

  if (isatty(shell->tty_fd) && shell->is_interactive && !is_chld) {
    if (reset_terminal_mode(shell) == -1) {
      perror("terminal fail");
    }
  }

  if (isatty(shell->tty_fd) && shell->is_interactive && !is_chld)
    close(shell->tty_fd);
}
