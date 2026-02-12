#ifndef SHELL_H
#define SHELL_H

#include "arena.h"
#include "ast.h"
#include "dll.h"
#include "hashtable.h"
#include "jobs.h"
#include "lexer.h"
#include "sigstruct.h"
#include "termstruct.h"

/**
 * @file shell.h
 *
 * Module declares shell struct.
 */

#define ENV_EXPORTED (1 << 0)
#define ENV_READONLY (1 << 1)
#define ENV_HAS_VINT (1 << 2)

typedef struct s_env_entry {
  char *name;
  char *val;
  long long vint;
  unsigned char flags;
} t_env_entry;

/**
 * @typedef shell_s t_shell
 * @brief struct encapsulates all information about the shell.
 *
 * Struct encapsulations of all things that are unique to a shell.
 */
typedef struct shell_s {

  t_arena arena;

  char **argv;
  int argc;

  int is_interactive;
  int job_control_flag;

  t_hashtable bins;
  t_hashtable env;

  t_token_stream token_stream;

  t_job *fg_job;

  int tty_fd;
  pid_t pgid;

  char *prompt;
  size_t prompt_len;

  t_job **job_table;    //
  size_t job_table_cap; //
  size_t job_count;

  t_shell_sigtable shell_sigtable; //

  t_term_ctrl term_ctrl; //

  t_ast ast; //

  t_dll history;
  t_hashtable builtins; //
  t_hashtable aliases;  //

  int last_exit_status; //
  char *sh_name;        //

  int next_job_id;

  int std_fd_backup[2]; //

  int rows;
  int cols;

} t_shell;

#endif // ! SHELL_H
