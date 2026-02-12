#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "builtins.h"
#include "handle_io_redir.h"
#include "job_handler.h"
#include "jobs_init.h"
#include "parser.h"
#include "shell.h"
#include "shell_cleanup.h"
#include "sigtable_init.h"
#include "userinp.h"
#include "var_exp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/**
 * @file executor.h
 *
 * This module declares the functions called by the driver to parse and execute
 * accordingly.
 */

/**
 * @brief function parses cmd_buf to command then builds ast in shell struct and
 * sends to executor.
 * @param cmd_buf pointer to the cmd line buffer
 * @param shell pointer to shell struct
 * @param command pointer to command struct
 *
 * Function parses and sets parse the command line into a command struct
 * then tokenize, build the AST in shell struct, and finally pass to executor.
 *
 * @note called by driver.
 */

typedef enum e_wait_status {
  WAIT_FINISHED = 0,
  WAIT_STOPPED = 1,
  WAIT_ERROR = -1,
  WAIT_INTERRUPTED = -2
} t_wait_status;

int parse_and_execute(char **cmd_buf, t_shell *shell,
                      t_token_stream *token_stream, bool script);

int reap_sigchld_jobs(t_shell *shell);

#endif // ! EXECUTOR_H
