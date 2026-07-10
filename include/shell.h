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
#include <stdint.h>

/**
 * @file shell.h
 *
 * Module declares shell struct.
 */

#define ENV_EXPORTED (1 << 0)
#define ENV_READONLY (1 << 1)
#define ENV_HAS_VINT (1 << 2)
#define ENV_LOCAL (1 << 3)

typedef enum e_err_code {
  ERR_ALIAS_DEPTH = 2,
  ERR_UNBALANCED_QUOTES,
  ERR_UNBALANCED_PARENS,
  ERR_UNBALANCED_BRACES,
  ERR_UNBALANCED_TOKEN,
  ERR_REDIR_MAX,
  ERR_REDIR_FILENAME,
  ERR_MISSING_FI,
  ERR_MISSING_THEN,
  ERR_MISSING_DO,
  ERR_MISSING_DONE,
  ERR_MISSING_FOR_VAR,
  ERR_MISSING_IN,
  ERR_EMPTY_LOOP,
  ERR_MALFORMED_FUN,
  ERR_NEAR_PIPE,
  ERR_NEAR_AND,
  ERR_NEAR_OR,
  ERR_ALLOC,
} t_err_code;

typedef struct s_shopts {
  bool render_autosgst;
} t_shopt;

typedef struct s_env_entry {
  char *name;
  char *val;
  long long vint;
  unsigned char flags;
  int local_depth;
} t_env_entry;

typedef struct s_exec_ctx {
  t_job *subshell_job;
  bool pipeline;
  pid_t *pipeline_pids;
  size_t pids_len;
  size_t pids_cap;
  int fnest_d;
  bool flow;
  bool script;
  bool return_fun;
  bool break_loop;
  bool continue_loop;
  bool is_subshell;
} t_exec_ctx;

typedef struct s_fd_backup {
  int src_fd;
  int saved_fd;
} t_fd_backup;

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
  t_hashtable functions;

  t_job **job_table;
  t_job *fg_job;

  t_token_stream token_stream;
  t_ast ast;

  t_dll history;

  char **argv;
  const char *path;
  char *prompt;
  char *sh_name;

  bool exflag;

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

  t_fd_backup *fd_prevs;
  size_t fd_prevs_len;
  size_t fd_prevs_cap;

  unsigned int is_interactive : 1;
  unsigned int job_control_flag : 1;

  int rows;
  int cols;

  FILE *script_fstream;

  char **pending_hds;
  size_t pending_hds_cap;
  size_t pending_hds_len;

  t_shopt shopts;

  char *traps[NSIG];

  t_exec_ctx exec_ctx;
} t_shell;

#endif // ! SHELL_H
