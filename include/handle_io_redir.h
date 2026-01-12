#ifndef HANDLE_IO_REDIR_H
#define HANDLE_IO_REDIR_H

#include"shell.h"
#include"userinp.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#define BUF_GROWTH_FACTOR 2

/**
 * @file handle_io_redir.h
 * 
 * This module declares functions to handle io redirections.
 */

/**
 * @brief sets up required i/o redirections
 * @param shell pointer to shell struct
 * @param node pointer to ast node
 * @return 0 success, -1 fail
 * 
 * Function sets up all I/O redirections within the ast node.
 * @note called by executor
 */
int redirect_io(t_shell* shell, t_ast_n* node);

/**
 * @brief restores standard i/o of process
 * @param shell pointer to shell struct
 * @return 0 success, -1 fail
 * 
 * Restores the backed up i/o fds for the process; backups found in std_fd_backup[2] in shell struct.
 * @note called by executor
 */
int restore_io(t_shell* shell);

#endif // ! HANDLE_IO_REDIR_H