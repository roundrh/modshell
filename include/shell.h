#ifndef SHELL_H
#define SHELL_H

#include"builtins_ht.h"
#include"lexer.h"
#include"dll.h"
#include"alias_ht.h"
#include"ast.h"
#include"termstruct.h"
#include"sigstruct.h"
#include"jobs.h"

/**
 * @file shell.h
 * 
 * Module declares shell struct.
 * @note mainly to avoid recursive include for include guards.
 */

/**
 * @typedef shell_s t_shell
 * @brief struct encapsulates all information about the shell.
 *
 * Struct encapsulations of all things that are unique to a shell.
 */
typedef struct shell_s{

    int is_interactive;
    int job_control_flag;

    char** env; //special malloc
    size_t env_count;
    size_t env_cap;

    t_token_stream token_stream;

    t_job* fg_job;

    int tty_fd;
    pid_t pgid;

    t_job** job_table; //
    size_t job_table_cap; //
    size_t job_count;

    t_shell_sigtable shell_sigtable; //

    t_term_ctrl term_ctrl; // 

    t_ast ast; //

    t_dll history;  
    t_hashtable builtins; //
    t_alias_hashtable aliases; //

    int last_exit_status; //
    char* sh_name; //

    int std_fd_backup[2]; //
} t_shell;

#endif // ! SHELL_H