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

  t_hashtable bins;
  t_hashtable env;
  t_hashtable builtins;
  t_hashtable aliases;

  t_job **job_table;
  t_job *fg_job;

  t_token_stream token_stream;
  t_ast ast;

  t_dll history;

  char **argv;
  const char *path;
  char *prompt;
  char *sh_name;

  t_arena arena;
  t_shell_sigtable shell_sigtable;
  t_term_ctrl term_ctrl;

  size_t path_len;
  size_t prompt_len;
  size_t job_table_cap;
  size_t job_count;

  pid_t pgid;
  int argc;
  int last_exit_status;
  int next_job_id;

  int tty_fd;
  int std_fd_backup[2];

  unsigned int is_interactive : 1;
  unsigned int job_control_flag : 1;

  int rows;
  int cols;

} t_shell;

#endif // ! SHELL_H
