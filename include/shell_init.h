#ifndef SHELL_INIT_H
#define SHELL_INIT_H

#include "ast_init.h"
#include "builtins.h"
#include "builtins_ht.h"
#include "shell.h"
#include "sigtable_init.h"
#include "terminal_control.h"
#include "userinp.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

/**
 * @file shell_init.h
 *
 * This module is used to define hardcoded limits, environ, and an initializer
 * for the shell struct to avoid recursive inclusion.
 *
 */

/**
 * @def PATH_MAX
 * @brief limit for the maximum length of a path/directory.
 */
#define PATH_MAX 4096

/**
 * @def FILE_NAME_MAX
 * @brief limit for the maximum length of a filename.
 */
#define FILE_NAME_MAX 255

/**
 * @def INITIAL_PID_ARR_LENGTH
 * @brief limit for the initial length of the array of PIDs
 */
#define INITIAL_PID_ARR_LENGTH 32

/**
 * @def INITIAL_JOB_TABLE_LENGTH
 * @brief limit for the initial length of the array of pointers to job structs
 * (job table)
 */
#define INITIAL_JOB_TABLE_LENGTH 32

/**
 * @def INITIAL_SH_NAME_LENGTH
 * @brief limit for the initial length of the shell name.
 */
#define INITIAL_SH_NAME_LENGTH 32

/**
 * @def BUF_GROWTH_FACTOR
 * @brief limit for the growth factor of any buffers.
 */
#define BUF_GROWTH_FACTOR 2

/**
 * @def environ
 * @brief extern to copy environment variables into shell struct
 */
extern char **environ;

void get_shell_prompt(t_shell *shell);
/**
 * @def init_shell_state(t_shell* shell)
 * @param shell pointer to shell struct
 * @brief initializes values of shell struct.
 *
 * This function initializes values to null, or mallocs and checks for errors
 * during initializes if error occurs, returns -1 and program dies.
 *
 * @note Driver calls this function prior to main loop.
 */
int init_shell_state(t_shell *shell);

#endif // ! SHELL_INIT_H
