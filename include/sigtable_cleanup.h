#ifndef SIGTABLE_CLEANUP_H
#define SIGTABLE_CLEANUP_H

#include"sigstruct.h"
#include<stdio.h>

/**
 * @file sigtable_cleanup.h
 *
 * This module declares functions that handle the cleanup of the sigtable struct
 */


/**
 * @def cleanup_sigtable(t_shell_sigtable* sigtable)
 * @brief restors default sig disposition table
 * @param sigtable pointer to shell_sigtable struct
 * @return -1 on fail, 0 on success.
 *
 * Restores stored oldact for each signal handler changed to non-default.
 */
int cleanup_sigtable(t_shell_sigtable* sigtable);

#endif // ! SIGTABLE_CLEANUP_H