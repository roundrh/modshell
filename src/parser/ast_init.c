#include "ast_init.h"

/**
 * @file ast_init.c
 * @brief Implementation of functions to initialize ast
 */

/**
 * @brief initializes ast node
 * @return 0 on success, -1 on fail
 */
int init_ast_node(t_ast_n *ast_node) {

  if (!ast_node) {
    return -1;
  }

  ast_node->tok_start = NULL;
  ast_node->tok_segment_len = 0;
  ast_node->background = 0;
  ast_node->sub_ast_root = NULL;

  ast_node->redir_bool = 0;

  ast_node->op_type = -1;

  ast_node->left = NULL;
  ast_node->right = NULL;

  ast_node->io_redir = NULL;

  return 0;
}

/**
 * @brief initializes ast recursively calling init_ast_node
 * @return 0 on success, -1 on fail
 */
int init_ast(t_ast *ast) {

  if (!ast)
    return -1;

  ast->root = NULL;

  return 0;
}
