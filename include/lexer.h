#ifndef LEXER_H
#define LEXER_H

#define INITIAL_TOKS_ARR_CAP 32
#define BUF_GROWTH_FACTOR 2

#include "alias_ht.h"
#include "arena.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum e_token_type {

  TOKEN_SIMPLE = 0,
  TOKEN_TRUNC,
  TOKEN_APPEND,
  TOKEN_INPUT,
  TOKEN_HEREDOC,
  TOKEN_OPEN_PAR,
  TOKEN_CLOSE_PAR,
  TOKEN_PIPE,
  TOKEN_OR,
  TOKEN_AND,
  TOKEN_BG,
  TOKEN_SEQ,
  TOKEN_IF,
  TOKEN_THEN,
  TOKEN_ELSE,
  TOKEN_ELIF,
  TOKEN_FI,
  TOKEN_WHILE,
  TOKEN_FOR,
  TOKEN_IN,
  TOKEN_DO,
  TOKEN_DONE,
  TOKEN_NEWLINE
} t_token_type;

typedef struct s_token {

  char *start;
  size_t len;
  t_token_type type;
} t_token;

typedef struct s_token_stream {

  t_token *tokens;
  size_t tokens_arr_cap;
  size_t tokens_arr_len;
} t_token_stream;

int init_token_stream(t_token_stream *token_stream, t_arena *a);
int lex_command_line(char **cmd_buf, t_token_stream *tokens,
                     t_alias_hashtable *aliases, int depth, t_arena *a);

#endif // ! LEXER_H
