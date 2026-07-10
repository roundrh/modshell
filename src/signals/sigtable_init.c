#include "sigtable_init.h"
#include "sigstruct.h"

#include <errno.h>
#include <signal.h>
#include <unistd.h>

/**
 * @file sigtable_init.c
 * @brief Implementations of functions to initialize t_shell_sigtable struct in
 * sigtable.h
 */

volatile sig_atomic_t sigs[NSIG];

void sig_handler(int sig) { sigs[sig] = 1; }

int init_pa_sigtable(t_shell_sigtable *sigtable) {
  INIT_SIG(sigtable, SIGINT, SIG_IGN, 0, SIGINT);
  INIT_SIG(sigtable, SIGQUIT, SIG_IGN, 0, SIGQUIT);
  INIT_SIG(sigtable, SIGTSTP, SIG_IGN, 0, SIGTSTP);
  INIT_SIG(sigtable, SIGTTOU, SIG_IGN, 0, SIGTTOU);
  INIT_SIG(sigtable, SIGTTIN, SIG_IGN, 0, SIGTTIN);
  INIT_SIG(sigtable, SIGCHLD, sig_handler, SA_RESTART | SA_NOCLDSTOP, SIGCHLD);
  INIT_SIG(sigtable, SIGWINCH, sig_handler, 0, SIGWINCH);

  return 0;
}

int init_ch_sigtable(t_shell_sigtable *sigtable) {

  if (sigtable == NULL) {
    return -1;
  }

  INIT_SIG(sigtable, SIGINT, SIG_DFL, 0, SIGINT);
  INIT_SIG(sigtable, SIGQUIT, SIG_DFL, 0, SIGQUIT);
  INIT_SIG(sigtable, SIGTSTP, SIG_DFL, 0, SIGTSTP);
  INIT_SIG(sigtable, SIGTTOU, SIG_DFL, 0, SIGTTOU);
  INIT_SIG(sigtable, SIGTTIN, SIG_DFL, 0, SIGTTIN);
  INIT_SIG(sigtable, SIGCHLD, SIG_DFL, 0, SIGCHLD);

  return 0;
}
