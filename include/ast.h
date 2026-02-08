#ifndef AST_H
#define AST_H

#include "lexer.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @file ast.h
 * @brief decleration of AST
 */

/**
 * @def INITIAL_REDIR_LEN
 * @brief Initial length of redirection array io_redir within ast node (s_ast_n)
 */
#define INITIAL_REDIR_LEN 8

/**
 * @def BUF_GROWTH_FACTOR
 * @brief Factor to grow buffer for realloc call
 */
#define BUF_GROWTH_FACTOR 2

/**
 * @def MAX_REDIR_LEN
 * @brief Maximum amount of I/O redirections per full command
 */
#define MAX_REDIR_LEN 256

/**
 * @typedef enum e_op_types t_op_type
 * @brief op types as enum for readability
 */
typedef enum e_op_types {

  OP_PIPE = 0, ///< Pipe Operation (|) 0
  OP_SIMPLE,   ///< Simple command (Equivelant to OP_NONE) 2
  OP_SEQ,      ///< Terminator sequential command (;) 3
  OP_AND,      ///< Conditional and command (&&) 5
  OP_OR,       ///< Conditional or command (||) 6
  OP_SUBSHELL,
  OP_IF,
  OP_WHILE,
  OP_FOR
} t_op_type;

/**
 * @typedef enum e_redir_types t_redir_type
 * @brief redir types as enum for readability
 */
typedef enum e_redir_types {

  IO_NONE = 0,

  IO_APPEND,  ///< Append Redirection (>>)
  IO_TRUNC,   ///< Truncate Redirection (>)
  IO_HEREDOC, ///< Heredoc Redirection (<<)
  IO_INPUT    ///< Input Redirection (<)

} t_redir_type;

/**
 * @typedef struct s_io_redir t_io_redir
 * @brief struct contain redirection type and filename contained within ast node
 * s_ast_n
 */
typedef struct s_io_redir {
  t_redir_type io_redir_type;
  char *filename; // filename is delim in case of heredoc
} t_io_redir;

/**
 * @typedef struct s_ast_n t_ast_n
 * @brief struct containing data of ast node.
 */
typedef struct s_ast_n {

  t_token *tok_start;
  size_t tok_segment_len;

  int background;
  t_op_type op_type;

  t_io_redir **io_redir;
  int redir_bool;

  struct s_ast_n *left;
  struct s_ast_n *right;

  struct s_ast_n *sub_ast_root;

  t_token *for_var;
  t_token *for_items;
  size_t items_len;
} t_ast_n;

/**
 * @typedef struct s_ast t_ast
 * @brief struct containing data of ast.
 *
 * @note this may be removed later as theres no real need for it,
 but it adds readability so im personally against removing it
 */
typedef struct s_ast {

  t_ast_n *root;
} t_ast;

#endif // ! AST_H
