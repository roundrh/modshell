#ifndef VAR_EXP_H
#define VAR_EXP_H

#include<stdio.h>
#include<stdlib.h>
#include<sys/types.h>
#include<pwd.h>
#include<ctype.h>
#include"limits.h"
#include"shell.h"
#include"ast.h"
#include"lexer.h"
#include<sys/wait.h>

/**
  * @define ARGV_INITIAL_LEN
  * @brief Initial length of argument vector
  */
#define ARGV_INITIAL_LEN 32

/**
 * @define BUF_GROWTH_FACTOR
 * @brief Growth factor for any malloc'd buffer requiring a realloc.
 */
#define BUF_GROWTH_FACTOR 2

/**
 * @define BUFFER_INITIAL_LEN
 * @brief Initial length of a buffer during malloc stage
 */
#define BUFFER_INITIAL_LEN 256

/**
 * @define MAX_IFS_SIZE
 * @brief Maximum size for IFS splitting
 * @note Once surpassed by a field-splitting expansion, will free all fields and 
 * resort to appending a single string
 */
#define MAX_IFS_SIZE 128

/**
 * @define INITIAL_IFS_LEN
 * @brief initial length of the IFS vector
 * @note resized via BUF_GROWTH_FACTOR, maximum of 3 resizes before exiting.
 */
#define INITIAL_IFS_LEN 32

/* Forward declaration of parse_and_execute top level function */
int parse_and_execute(char** cmd_buf, t_shell* shell, t_token_stream* token_stream);

/**
 * @typedef enum e_err_type
 * @brief tracks error type throughout expansion, propagates back where error matters
 */
typedef enum e_err_type{

  err_none = 0,
  err_fatal = -1,
  err_div_zero = -2,
  err_syntax = -3,
  err_unbal_par = -4,
  err_overflow = -5,
  err_bad_sub = -6,
  err_param_unset = -7
}t_err_type;

/**
 * @typedef enum e_param_op
 * @brief tracks expansion type for brace expansions (theres too many of these, need enum).
 */
typedef enum e_param_op {

    PARAM_OP_ERR = -1,
    PARAM_OP_NONE,        // ${VAR}
    PARAM_OP_MINUS,       // ${VAR:-word}
    PARAM_OP_EQUAL,       // ${VAR:=word}
    PARAM_OP_PLUS,        // ${VAR:+word}
    PARAM_OP_QUESTION,    // ${VAR:?word}
    PARAM_OP_HASH,        // ${VAR#pattern}
    PARAM_OP_HASH_HASH,   // ${VAR##pattern}
    PARAM_OP_PERCENT,     // ${VAR%pattern}
    PARAM_OP_PERCENT_PERCENT, // ${VAR%%pattern}
    PARAM_OP_COLON,       // ${VAR:offset} or ${VAR:offset:length}
    PARAM_OP_LEN
} t_param_op;

/* typedef function pointer for expansion handlers - used by dispatcher */
typedef char* (*t_exp_handler)(t_shell* shell, const char* src, size_t* i);

/**
 * @typedef s_exp_map
 * @brief struct contains trigger string of each expansion type, contains pointer
 * to handler, to be returned by the dispatcher
 */
typedef struct s_exp_map {

  const char* trigger; ///< Trigger String
  t_exp_handler handler; /// Function pointer to handler
} t_exp_map;

/**
 * @brief expands "$?" last shell exit status 
 */
char* expand_exit_status(t_shell* shell, const char* src, size_t* i);

/**
 * @brief expands "??" pid of shell (shell->pgid).
 */
char* expand_pid(t_shell* shell, const char* src, size_t* i);

/**
 * @brief expands arithmetic expression syntax $(()).
 */
char* expand_arith(t_shell* shell, const char* src, size_t* i);

/**
 * @brief expands braces ${}.
 */
char* expand_braces(t_shell* shell, const char* src, size_t* i);

/**
 * @brief expands subshell $().
 */
char* expand_subshell(t_shell* shell, const char* src, size_t* i);

/**
 * @brief expands standard $VAR.
 */
char* expand_var(t_shell* shell, const char* src, size_t* i);

/**
 * @brief top-level function to be called by executor.
 */
t_err_type expand_make_argv(t_shell* shell, char*** argv, t_token* start, const size_t segment_len);

#endif // ! VAR_EXP_H
