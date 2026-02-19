/**
 * @file builtins.h
 * @brief refer to builtins.c for comments on functions
 */

#ifndef BUILTINS_H
#define BUILTINS_H
#include "job_handler.h"
#include "jobs.h"
#include "shell.h"
#include "terminal_control.h"
#include "userinp.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* t_builtin_func */
typedef int (*t_builtin_func)(t_ast_n *node, t_shell *shell, char **argv);

typedef struct s_builtin {
  t_builtin_func fn;
} t_builtin;

void free_builtin(void *value);
void free_env_entry(void *value);

int cd_builtin(t_ast_n *node, t_shell *shell, char **argv);
int jobs_builtin(t_ast_n *node, t_shell *shell, char **argv);
int fg_builtin(t_ast_n *node, t_shell *shell, char **argv);
int bg_builtin(t_ast_n *node, t_shell *shell, char **argv);
int export_builtin(t_ast_n *node, t_shell *shell, char **argv);
int unset_builtin(t_ast_n *node, t_shell *shell, char **argv);
int clear_builtin(t_ast_n *node, t_shell *shell, char **argv);
int alias_builtin(t_ast_n *node, t_shell *shell, char **argv);
int unalias_builtin(t_ast_n *node, t_shell *shell, char **argv);
int exit_builtin(t_ast_n *node, t_shell *shell, char **argv);
int env_builtin(t_ast_n *node, t_shell *shell, char **argv);
int history_builtin(t_ast_n *node, t_shell *shell, char **argv);
int stty_builtin(t_ast_n *node, t_shell *shell, char **argv);
int kill_builtin(t_ast_n *node, t_shell *shell, char **argv);
int v_builtin(t_ast_n *node, t_shell *shell, char **argv);
int test_builtin(t_ast_n *node, t_shell *shell, char **argv);
int builtin_builtin(t_ast_n *node, t_shell *shell, char **argv);
int true_builtin(t_ast_n *node, t_shell *shell, char **argv);
int false_builtin(t_ast_n *node, t_shell *shell, char **argv);
int echo_builtin(t_ast_n *node, t_shell *shell, char **argv);
int exec_builtin(t_ast_n *node, t_shell *shell, char **argv);
int source_builtin(t_ast_n *node, t_shell *shell, char **argv);
int pwd_builtin(t_ast_n *node, t_shell *shell, char **argv);
int read_builtin(t_ast_n *node, t_shell *shell, char **argv);
int rehash_builtin(t_ast_n *node, t_shell *shell, char **argv);

#endif // BUILTINS_H
