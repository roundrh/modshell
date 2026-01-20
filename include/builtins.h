
/**
 * @file builtins.h
 * @brief refer to builtins.c for comments on functions
 */

#ifndef BUILTINS_H
#define BUILTINS_H
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<stdbool.h>
#include<ctype.h>
#include<errno.h>
#include<string.h>
#include<sys/utsname.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<time.h>
#include<sys/wait.h>
#include<pwd.h>
#include<grp.h>
#include<sys/wait.h>
#include<string.h>
#include<fcntl.h>
#include<dirent.h>
#include"shell.h"
#include"jobs.h"
#include"job_handler.h"
#include"terminal_control.h"
#include"userinp.h"

int help_builtin(t_ast_n* node, t_shell* shell, char** argv);
int cd_builtin(t_ast_n* node, t_shell* shell, char** argv);
int jobs_builtin(t_ast_n* node, t_shell* shell, char** argv);
int fg_builtin(t_ast_n* node, t_shell* shell, char** argv);
int bg_builtin(t_ast_n* node, t_shell* shell, char** argv);
int export_builtin(t_ast_n* node, t_shell* shell, char** argv);
int unset_builtin(t_ast_n* node, t_shell* shell, char** argv);
int clear_builtin(t_ast_n* node, t_shell* shell, char** argv);
int alias_builtin(t_ast_n* node, t_shell* shell, char** argv);
int unalias_builtin(t_ast_n* node, t_shell* shell, char** argv);
int exit_builtin(t_ast_n* node, t_shell* shell, char** argv);
int env_builtin(t_ast_n* node, t_shell* shell, char** argv);
int history_builtin(t_ast_n* node, t_shell* shell, char** argv);
int stty_builtin(t_ast_n* node, t_shell* shell, char** argv);
int kill_builtin(t_ast_n* node, t_shell* shell, char** argv);
int set_builtin(t_ast_n* node, t_shell* shell, char** argv);
int cond_builtin(t_ast_n* node, t_shell* shell, char** argv);

#endif // BUILTINS_H