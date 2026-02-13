#include "shell_cleanup.h"
#include "alias.h"
#include "builtins.h"

/**
 * @file shell_cleanup.c
 * @brief implementations of cleanup for shell struct
 */

/**
 * @brief cleans up shell struct
 * @param shell pointer to shell struct
 *
 * Flushes malloc'd data members, does nothing with static data members
 * refer to shell.h.
 */
void cleanup_shell(t_shell *shell, int is_chld) {

  ht_flush(&shell->aliases, free_alias);
  ht_flush(&shell->builtins, free_builtin);
  ht_flush(&shell->env, free_env_entry);
  ht_flush(&shell->bins, free);

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

  free_dll(&(shell->history));

  if (shell->sh_name) {
    free(shell->sh_name);
    shell->sh_name = NULL;
  }

  if (isatty(shell->tty_fd) && shell->is_interactive && !is_chld) {
    if (reset_terminal_mode(shell) == -1) {
      perror("terminal fail");
    }
  }

  if (isatty(shell->tty_fd) && shell->is_interactive && !is_chld)
    close(shell->tty_fd);
}
