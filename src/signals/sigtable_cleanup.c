#include "sigtable_cleanup.h"

/**
 * @file sigtable_cleanup.c
 * @brief implements cleanup function for sigtable
 */

/**
 * @brief cleans up sigtable restoring default handlers
 * @param sigtable pointer to t_shell_sigtable parent struct
 * @return 0 on success, -1 on fail
 *
 * Function resets all signals called in initializer to default handlers and
 * flags in oldact.
 */
int cleanup_sigtable(t_shell_sigtable *sigtable) {

  if (sigtable == NULL)
    return -1;

  if (sigaction(SIGINT, &sigtable->sigtable[SIGINT].oldact, NULL) == -1) {
    perror("sigaction cleanup SIGINT");
    return -1;
  }
  if (sigaction(SIGQUIT, &sigtable->sigtable[SIGQUIT].oldact, NULL) == -1) {
    perror("sigaction cleanup SIGQUIT");
    return -1;
  }
  if (sigaction(SIGCHLD, &sigtable->sigtable[SIGCHLD].oldact, NULL) == -1) {
    perror("sigaction cleanup SIGCHLD");
    return -1;
  }
  if (sigaction(SIGTSTP, &sigtable->sigtable[SIGTSTP].oldact, NULL) == -1) {
    perror("sigaction cleanup SIGTSTP");
    return -1;
  }
  if (sigaction(SIGTTOU, &sigtable->sigtable[SIGTTOU].oldact, NULL) == -1) {
    perror("sigaction cleanup SIGTTOU");
    return -1;
  }
  if (sigaction(SIGTTIN, &sigtable->sigtable[SIGTTIN].oldact, NULL) == -1) {
    perror("sigaction cleanup SIGTTIN");
    return -1;
  }

  return 0;
}
