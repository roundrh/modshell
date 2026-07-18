#ifndef LEXER_H
#define LEXER_H

#define INITIAL_TOKS_ARR_CAP 32
#define BUF_GROWTH_FACTOR 2

#include "alias.h"
#include "arena.h"
#include "hashtable.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef enum e_token_type {

  TOKEN_SIMPLE = 0,
  TOKEN_TRUNC,
  TOKEN_APPEND,
  TOKEN_INPUT,
  TOKEN_READ_WRITE,
  TOKEN_DUP_OUT,
  TOKEN_DUP_IN,
  TOKEN_FORCE_OW,
  TOKEN_HEREDOC,
  TOKEN_HEREDOC_STRIP,
  TOKEN_OPEN_PAR,
  TOKEN_CLOSE_PAR,
  TOKEN_LBRACE,
  TOKEN_RBRACE,
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
  TOKEN_UNTIL,
  TOKEN_FOR,
  TOKEN_IN,
  TOKEN_DO,
  TOKEN_DONE,
  TOKEN_FUN,
  TOKEN_NEWLINE
} t_token_type;

typedef struct s_token {

  char *start;
  size_t len;
  t_token_type type;
  char trailing_delim;
} t_token;

typedef struct s_token_stream {

  t_token *tokens;
  size_t tokens_arr_cap;
  size_t tokens_arr_len;
} t_token_stream;

int init_token_stream(t_token_stream *token_stream, t_arena *a);
int lex_command_line(char **cmd_buf, t_token_stream *tokens,
                     t_hashtable *aliases, int depth, t_arena *a, bool hd,
                     t_err_code *last_err);

#endif // ! LEXER_H
