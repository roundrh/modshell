#include "shell_cleanup.h"

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
int cleanup_shell(t_shell *shell, int is_chld) {

  cleanup_token_stream(&shell->token_stream);

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
    return -1;
  }

  flush_builtin_ht(&(shell->builtins));
  flush_alias_ht(&(shell->aliases));

  shell->last_exit_status = 0;

  free_dll(&(shell->history));

  cleanup_ast(shell->ast.root);

  if (shell->sh_name) {
    free(shell->sh_name);
    shell->sh_name = NULL;
  }

  if (shell->env) {

    char **env_ptr = shell->env;

    for (size_t i = 0; i < shell->env_count; i++) {
      free(env_ptr[i]);
    }
    free(shell->env);
    shell->env = NULL;
  }

  if (isatty(shell->tty_fd) && shell->is_interactive && !is_chld) {
    if (reset_terminal_mode(shell) == -1) {
      perror("terminal fail");
      return -1;
    }
  }

  if (isatty(shell->tty_fd) && shell->is_interactive && !is_chld)
    close(shell->tty_fd);

  return 0;
}
