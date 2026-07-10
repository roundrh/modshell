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
  if (cleanup_sigtable(&(shell->shell_sigtable)) == -1) {
    perror("shell sigtable");
  }

  if (isatty(shell->tty_fd) && shell->is_interactive && !is_chld) {
    if (reset_terminal_mode(shell) == -1) {
      perror("terminal fail");
    }
  }
}
