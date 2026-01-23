#include "sigtable_init.h"

/**
 * @file sigtable_init.c
 * @brief Implementations of functions to initialize t_shell_sigtable struct in
 * sigtable.h
 */

volatile sig_atomic_t sigchld_flag = 0;
volatile sig_atomic_t sigint_flag = 0;
volatile sig_atomic_t sigtstp_flag = 0;

void sigchld_handler(int sig) {
  (void)sig;
  sigchld_flag = 1;
}
void sigint_handler(int sig) {
  (void)sig;
  sigint_flag = 1;
}
void sigtstp_handler(int sig) {
  (void)sig;
  sigtstp_flag = 1;
}

int init_pa_sigtable(t_shell_sigtable *sigtable) {

  if (sigtable == NULL) {
    return -1;
  }

  INIT_SIG(sigtable, sigint, SIG_IGN, 0, SIGINT);
  INIT_SIG(sigtable, sigquit, SIG_IGN, 0, SIGQUIT);
  INIT_SIG(sigtable, sigtstp, SIG_IGN, 0, SIGTSTP);
  INIT_SIG(sigtable, sigttou, SIG_IGN, 0, SIGTTOU);
  INIT_SIG(sigtable, sigttin, SIG_IGN, 0, SIGTTIN);
  INIT_SIG(sigtable, sigchld, sigchld_handler, SA_RESTART | SA_NOCLDSTOP,
           SIGCHLD);

  return 0;
}

int init_ch_sigtable(t_shell_sigtable *sigtable) {

  if (sigtable == NULL) {
    return -1;
  }

  INIT_SIG(sigtable, sigint, SIG_DFL, 0, SIGINT);
  INIT_SIG(sigtable, sigquit, SIG_DFL, 0, SIGQUIT);
  INIT_SIG(sigtable, sigtstp, SIG_DFL, 0, SIGTSTP);
  INIT_SIG(sigtable, sigttou, SIG_DFL, 0, SIGTTOU);
  INIT_SIG(sigtable, sigttin, SIG_DFL, 0, SIGTTIN);
  INIT_SIG(sigtable, sigchld, SIG_DFL, 0, SIGCHLD);

  return 0;
}
