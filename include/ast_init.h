#ifndef AST_INIT_H
#define AST_INIT_H

#include"ast.h"

/**
 * @file ast_init.h
 * @brief initializer for ast
 */


/**
 * @brief initializes ast node
 * @param ast_node pointer to ast node
 * @return 0 on success, -1 on fail
 */
int init_ast_node(t_ast_n* ast_node);

/**
 * @brief initializes ast recursively calling init_ast_node
 * @param ast pointer to ast
 * @return 0 on success, -1 on fail
 */
int init_ast(t_ast* ast);

#endif // ! AST_INIT_H
