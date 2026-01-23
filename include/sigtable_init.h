#ifndef SIGTABLE_INIT_H
#define SIGTABLE_INIT_H

#include "sigstruct.h"
#include <stdio.h>
#include <string.h>

extern volatile sig_atomic_t sigchld_flag;
extern volatile sig_atomic_t sigint_flag;
extern volatile sig_atomic_t sigtstp_flag;

/**
 * @file sigtable_init.h
 * @brief Module handles the initialization of sigtable struct.
 */

/**
 * @brief macro to handle initializing t_sigtable in t_shell_sigtable safely.
 * @param sigtable t_shell_sigtable parent struct
 * @param sig t_sigtable child struct
 * @param handler handler function to point to
 * @param flags flags to pass into sa_flags
 * @param sig_c sig constant defined by signal.h
 *
 * sigtable's sig initialized with zero vals via memset then sigemptyset to
 * safely init an empty set for sa_mask sigaction to set sig handler for newact
 * in child struct (sigstruct.h for more info) saves default into child struct
 * oldact
 *
 */
#define INIT_SIG(sigtable, sig, handler, flags, sig_c)                         \
  do {                                                                         \
    memset(&(sigtable->sig.newact), 0, sizeof(sigtable->sig.newact));          \
    sigtable->sig.newact.sa_flags = flags;                                     \
    sigemptyset(&(sigtable->sig.newact.sa_mask));                              \
    sigtable->sig.newact.sa_handler = handler;                                 \
                                                                               \
    if (sigaction(sig_c, &(sigtable->sig.newact), &(sigtable->sig.oldact)) ==  \
        -1) {                                                                  \
      perror("sigaction");                                                     \
      return -1;                                                               \
    }                                                                          \
  } while (0)

/**
 * @brief initializes sigtable parent struct with values matching specific
 * signals.
 * @param sigtable pointer to shell sigtable parent struct
 *
 * @return 0 on success, -1 on err.
 *
 * Function calls above macro (INIT_SIG) on all signals whose handlers are set
 * to non-default handlers.
 */
int init_pa_sigtable(t_shell_sigtable *sigtable);

int init_ch_sigtable(t_shell_sigtable *sigtable);

void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

#endif // ! SIGTABLE_INIT_H
