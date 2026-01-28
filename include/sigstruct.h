#ifndef SIGSTRUCT_H
#define SIGSTRUCT_H

/**
 * @file sigstruct.h
 * @brief declares sigtable structs (child structs and parent structs)
 */

#include <signal.h>

/**
 * @typedef struct sigtable_s t_sigtable
 * @brief child struct for single signal
 *
 * Struct saves oldact while setting appropriate handlers for newact
 */
typedef struct sigtable_s {

  struct sigaction oldact;
  struct sigaction newact;

} t_sigtable;

/**
 * @typedef struct shell_sigtable_s t_shell_sigtable
 * @brief parent struct of t_sigtables
 *
 * Struct contains all signals whose handlers will be set to non-default
 * functions along with flags, masks, etc. made by sigaction
 */
typedef struct shell_sigtable_s {

  t_sigtable sigint;
  t_sigtable sigtstp;
  t_sigtable sigchld;
  t_sigtable sigttou;
  t_sigtable sigttin;
  t_sigtable sigquit;
  t_sigtable sigwinch;
} t_shell_sigtable;

#endif // ! SIGSTRUCT_H
