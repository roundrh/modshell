#ifndef CLEANUP_AST_H
#define CLEANUP_AST_H

#include"ast.h"

/**
 * @file cleanup_ast.h
 *
 * This module contains function declerations for cleaning up the AST built by the parser.
 */

 /**
  * @param node pointer to ast node.
  *
  * @brief cleans up all encapsulated data within ast node "node"
  */
int cleanup_ast_node(t_ast_n* node);

 /**
  * @param node pointer to ast node.
  *
  * @brief recursively cleans up ast by calling cleanup_ast_node on entire tree.
  */
int cleanup_ast(t_ast_n* node);

#endif // ! CLEANUP_AST_H