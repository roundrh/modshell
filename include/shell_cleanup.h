#include "jobs_cleanup.h"
#include "shell.h"
#include "sigtable_cleanup.h"
#include "terminal_control.h"

/**
 * @file shell_cleanup.h
 *
 * Module cleans up shell struct.
 */

/**
 * @def cleanup_shell(t_shell* shell)
 * @param shell pointer shell struct
 * @brief cleans up shell struct.
 *
 * Function cleans up allocd shell struct data, and initializes values to null,
 * -1, etc. across the shell struct.
 *
 * @note this must be called by the driver.
 * @warning not calling this causes major memory leaks (obviously).
 */
void cleanup_shell(t_shell *shell, int is_chld);
