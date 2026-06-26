#ifndef HANDLE_IO_REDIR_H
#define HANDLE_IO_REDIR_H

#include "shell.h"
#include "string.h"
#include "userinp.h"
#include "var_exp.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#define BUF_GROWTH_FACTOR 2
#define FDS_P_DEF_SIZE 8

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
int redirect_io(t_shell *shell, t_ast_n *node);

int collect_pending_hds(t_ast_n *r, size_t *idx, t_shell *shell);

int check_realloc_pending_hds(t_shell *shell);

int read_hd_body(const char *delim, bool strip, t_shell *shell, char **out);

int collect_stdin_hds(t_shell *shell, t_ast_n *root);
/**
 * @brief restores standard i/o of process
 * @param shell pointer to shell struct
 * @return 0 success, -1 fail
 *
 * Restores the backed up i/o fds for the process; backups found in
 * std_fd_backup[2] in shell struct.
 * @note called by executor
 */
int restore_io(t_shell *shell);

#endif // ! HANDLE_IO_REDIR_H
