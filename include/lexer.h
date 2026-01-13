#ifndef LEXER_H
#define LEXER_H

#define INITIAL_TOKS_ARR_CAP 32
#define BUF_GROWTH_FACTOR 2

#include<stdio.h>
#include<stdlib.h>
#include<stdbool.h>
#include<string.h>
#include"alias_ht.h"

typedef enum e_token_type{

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
    TOKEN_EQUAL,
    TOKEN_OPEN_BRACE,
    TOKEN_CLOSE_BRACE
} t_token_type;

typedef struct s_token{

    char* start;
    size_t len;
    t_token_type type;
} t_token;

typedef struct s_token_stream{

    t_token* tokens;
    size_t tokens_arr_cap;
    size_t tokens_arr_len;
} t_token_stream;

int init_token_stream(t_token_stream* token_stream);
int lex_command_line(char **cmd_buf, t_token_stream *tokens, t_alias_hashtable* aliases, int depth);
int cleanup_token_stream(t_token_stream* token_stream);

#endif // ! LEXER_H