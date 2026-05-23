#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "ast.h"

typedef struct s_function {
  t_ast *ast_arr; // body
  size_t ast_arr_cap;
  size_t ast_arr_size;
} t_function;

#endif // ! H_FUNCTIONS
