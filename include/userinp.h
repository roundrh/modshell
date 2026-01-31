#ifndef USERINP_H
#define USERINP_H

#include "dirent.h"
#include "shell.h"
#include "sigtable_init.h"
#include "terminal_control.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/**
 * @file userinp.h
 * @brief Raw termios user input handling and function declarations
 *
 * This module provides macros to safely handle errors in reading user input
 * from stdin in the terminal and declares function to read user input.
 */

/**
 * @def INITIAL_COMMAND_LENGTH
 * @brief initial length of line buffer before any realloc calls
 */
#define INITIAL_COMMAND_LENGTH 1024
#define COLOR_GRAY "\x1b[90m"
#define COLOR_RESET "\x1b[0m"

/**
 * @def MAX_COMMAND_LENGTH
 * @brief maximum length line buffer reaches before fail
 */
#define MAX_COMMAND_LENGTH 32769

/**
 * @def BUF_GROWTH_FACTOR
 * @brief growth factor of line buffer for realloc call
 */
#define BUF_GROWTH_FACTOR 2

#define WRITE_LITERAL(fd, buf)                                                 \
  HANDLE_WRITE_FAIL_FATAL(fd, buf, sizeof(buf) - 1, NULL)

#define HANDLE_WRITE_FAIL_FATAL(fd, buf, len, bufptr)                          \
  if (handle_write_fail(fd, buf, len, bufptr) == -1) {                         \
    exit(EXIT_FAILURE);                                                        \
  }
/**
 * @def HANDLE_SNPRINTF_FAIL_FATAL(fd, str, len, buffer_ptr)
 * @brief Handles snprintf failures as fatal errors
 * @param src string to print to
 * @param fmt size of string
 * @param arg arguments of how to write to string
 * @param buffer_ptr pointer to buffer to free on failure
 *
 * If snprintf fails, prints error, frees buffer if provided,
 * sets errno to EIO, and returns NULL.
 */

void get_term_size(int *rows, int *cols);

int handle_write_fail(int fd, const char *buf, size_t len, char *buffer_ptr);
/**
 * @brief reads user input from terminal
 * @param shell pointer to shell struct
 * @return dynamically allocd string containing user input, NULL on error
 *
 * Function reads user input with buffer management
 * Buffer starts at INITIAL_COMMAND_LENGTH and grows by BUF_GROWTH_FACTOR
 * up to MAX_COMMAND_LENGTH. Handles terminal control and error conditions.
 *
 * @note Caller must free the returned string
 * @warning Buffer resizes maximum of 2 times before failing
 */
char *read_user_inp(t_shell *shell);

#endif // ! USERINP_H
