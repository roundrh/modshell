#ifndef PARSER_H
#define PARSER_H

#include"ast.h"
#include"ast_init.h"
#include"shell_init.h"
#include"cleanup_ast.h"
#include<string.h>

/** 
 * @file parser.h
 * 
 * Module declares functions for parser.
 */

/**
 * @def build_ast(t_ast* ast, t_command* command)
 * @param ast pointer to ast struct
 * @param command pointer to command struct
 * @brief builds ast to send to executor
 *
 * @return pointer to ast root node success, NULL fail
 * This function takes the tokenized argv within command struct and builds a left-associative ast to pass to an executor.
 */
t_ast_n* build_ast(t_ast* ast, t_token_stream* token_stream);

#endif // ! PARSER_H