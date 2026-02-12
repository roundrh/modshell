#include "lexer.h"
#include "arena.h"

int init_token_stream(t_token_stream *token_stream, t_arena *a) {

  token_stream->tokens =
      (t_token *)arena_alloc(a, INITIAL_TOKS_ARR_CAP * sizeof(t_token));

  token_stream->tokens_arr_cap = INITIAL_TOKS_ARR_CAP;
  for (size_t i = 0; i < INITIAL_TOKS_ARR_CAP; i++) {
    token_stream->tokens[i].type = -1;
    token_stream->tokens[i].start = NULL;
    token_stream->tokens[i].len = 0;
  }

  token_stream->tokens_arr_len = 0;
  return 0;
}

t_token_type get_token_type(const char *c, size_t *len) {

  if (!c || !len)
    return -1;

  if (c[0] == '|' && c[1] == '|') {
    *len = 2;
    return TOKEN_OR;
  }
  if (c[0] == '&' && c[1] == '&') {
    *len = 2;
    return TOKEN_AND;
  }
  if (c[0] == '>' && c[1] == '>') {
    *len = 2;
    return TOKEN_APPEND;
  }
  if (c[0] == '<' && c[1] == '<') {
    *len = 2;
    return TOKEN_HEREDOC;
  }

  if (c[0] == '|') {
    *len = 1;
    return TOKEN_PIPE;
  }
  if (c[0] == '&') {
    *len = 1;
    return TOKEN_BG;
  }
  if (c[0] == '>') {
    *len = 1;
    return TOKEN_TRUNC;
  }
  if (c[0] == '<') {
    *len = 1;
    return TOKEN_INPUT;
  }
  if (c[0] == '(') {
    *len = 1;
    return TOKEN_OPEN_PAR;
  }
  if (c[0] == ')') {
    *len = 1;
    return TOKEN_CLOSE_PAR;
  }
  if (c[0] == ';') {
    *len = 1;
    return TOKEN_SEQ;
  }
  if (c[0] == '\n') {
    *len = 1;
    return TOKEN_NEWLINE;
  }

  return TOKEN_SIMPLE;
}

t_token_type check_reserved_word(const char *start, size_t len) {
  if (len == 2 && strncmp(start, "if", 2) == 0)
    return TOKEN_IF;
  if (len == 2 && strncmp(start, "fi", 2) == 0)
    return TOKEN_FI;
  if (len == 2 && strncmp(start, "do", 2) == 0)
    return TOKEN_DO;
  if (len == 4 && strncmp(start, "then", 4) == 0)
    return TOKEN_THEN;
  if (len == 4 && strncmp(start, "else", 4) == 0)
    return TOKEN_ELSE;
  if (len == 4 && strncmp(start, "elif", 4) == 0)
    return TOKEN_ELIF;
  if (len == 5 && strncmp(start, "while", 5) == 0)
    return TOKEN_WHILE;
  if (len == 4 && strncmp(start, "done", 4) == 0)
    return TOKEN_DONE;
  if (len == 3 && strncmp(start, "for", 3) == 0)
    return TOKEN_FOR;
  if (len == 2 && strncmp(start, "in", 2) == 0)
    return TOKEN_IN;
  return TOKEN_SIMPLE;
}

static int check_realloc_toks_arr(t_token_stream *ts, size_t tok_count,
                                  t_arena *a) {

  if (tok_count + 2 < ts->tokens_arr_cap)
    return 0;

  size_t ocap = (ts->tokens_arr_cap);
  size_t ncap = (ts->tokens_arr_cap) * BUF_GROWTH_FACTOR;
  ts->tokens = (t_token *)arena_realloc(a, ts->tokens, ncap * sizeof(t_token),
                                        ocap * sizeof(t_token));

  for (size_t i = ts->tokens_arr_cap; i < ncap; i++) {
    ts->tokens[i].len = 0;
    ts->tokens[i].type = -1;
    ts->tokens[i].start = NULL;
  }

  ts->tokens_arr_cap = ncap;

  return 0;
}

static void flush_word(t_token_stream *ts, char **tok_start, size_t *tok_len,
                       bool *tokenized, size_t *tok_count, bool quoted) {
  if (*tokenized == false)
    return;

  ts->tokens[*tok_count].start = *tok_start;
  ts->tokens[*tok_count].len = *tok_len;

  if (!quoted) {
    ts->tokens[*tok_count].type = check_reserved_word(*tok_start, *tok_len);
  } else {
    ts->tokens[*tok_count].type = TOKEN_SIMPLE;
  }

  (*tok_count)++;
  *tok_start = NULL;
  *tok_len = 0;
  *tokenized = false;
}

static bool should_alias(t_token_stream *ts, size_t i) {
  if (i == 0)
    return true;
  t_token_type prev = ts->tokens[i - 1].type;
  return (prev == TOKEN_PIPE || prev == TOKEN_SEQ || prev == TOKEN_AND ||
          prev == TOKEN_OR || prev == TOKEN_OPEN_PAR);
}

static int expand_alias_token(char **cmd_line_buf, t_hashtable *aliases,
                              t_token_stream *ts, t_arena *a) {
  if (!cmd_line_buf || !*cmd_line_buf || ts->tokens_arr_len == 0 || !aliases)
    return 0;

  for (size_t i = 0; i < ts->tokens_arr_len; i++) {

    if (ts->tokens[i].type != TOKEN_SIMPLE || !should_alias(ts, i))
      continue;

    char *name = strndup(ts->tokens[i].start, ts->tokens[i].len);
    if (name[0] == '\\') {
      free(name);
      continue;
    }
    char *alias_val = NULL;
    t_ht_node *av = ht_find(aliases, name);

    if (av) {
      t_alias *al = av->value;
      if (al && al->cmd)
        alias_val = strdup(al->cmd);
    }

    if (alias_val && strcmp(name, alias_val) == 0) {
      free(name);
      name = NULL;
      continue;
    }

    if (alias_val) {
      size_t first_word_len = 0;
      while (alias_val[first_word_len] && !isspace(alias_val[first_word_len]))
        first_word_len++;

      bool circular = (strlen(name) == first_word_len &&
                       strncmp(name, alias_val, first_word_len) == 0);

      size_t prefix_len = ts->tokens[i].start - *cmd_line_buf;
      size_t alias_len = strlen(alias_val);
      size_t suffix_len = strlen(ts->tokens[i].start + ts->tokens[i].len);

      char *new_buf = arena_alloc(a, prefix_len + alias_len + (size_t)circular +
                                         suffix_len + 1);

      memcpy(new_buf, *cmd_line_buf, prefix_len);
      if (circular) {
        new_buf[prefix_len] = '\\';
        memcpy(new_buf + prefix_len + 1, alias_val, alias_len);
      } else {
        memcpy(new_buf + prefix_len, alias_val, alias_len);
      }
      strcpy(new_buf + prefix_len + (size_t)circular + alias_len,
             ts->tokens[i].start + ts->tokens[i].len);

      *cmd_line_buf = new_buf;

      free(name);
      free(alias_val);
      return 1;
    }
    free(name);
    free(alias_val);
  }
  return 0;
}

/*buffer safe because userinp.c null-terminates buffer. paired with while loop
 * cond cmd_buf[i+1] can be '\0' but never UB*/
int lex_command_line(char **cmd_line_buf, t_token_stream *token_stream,
                     t_hashtable *aliases, int depth, t_arena *a) {

  /* alias depth */
  if (depth > 10) {
    fprintf(stderr, "\nmsh: alias depth limit reached.");
    return -1;
  }

  bool in_single_quote = false;
  bool in_double_quote = false;
  bool tokenized = false;
  char *tok_start = NULL;
  size_t word_len = 0;
  size_t op_len = 0;
  int i = 0;
  size_t token_count = 0;
  char *cmd_buf = *cmd_line_buf;
  while (cmd_buf[i] != '\0') {

    check_realloc_toks_arr(token_stream, token_count, a);

    if (!in_single_quote && !in_double_quote && cmd_buf[i] == '\'') {
      in_single_quote = true;
      if (!tokenized) {
        tok_start = &cmd_buf[i];
        tokenized = true;
      }
    } else if (!in_single_quote && !in_double_quote && cmd_buf[i] == '"') {
      in_double_quote = true;
      if (!tokenized) {
        tok_start = &cmd_buf[i];
        tokenized = true;
      }
    } else if (!in_single_quote && !in_double_quote) {

      char seq[3] = {cmd_buf[i], cmd_buf[i + 1], '\0'};
      t_token_type type = get_token_type(seq, &op_len);

      if (cmd_buf[i] == ' ' || cmd_buf[i] == '\t') {
        flush_word(token_stream, &tok_start, &word_len, &tokenized,
                   &token_count, in_single_quote || in_double_quote);
        i++;
        continue;
      } else if (type != TOKEN_SIMPLE) {

        flush_word(token_stream, &tok_start, &word_len, &tokenized,
                   &token_count, in_single_quote || in_double_quote);
        token_stream->tokens[token_count].start = &cmd_buf[i];
        token_stream->tokens[token_count].len = op_len;
        token_stream->tokens[token_count].type = type;
        token_count++;
        tokenized = false;

        i += op_len;
        continue;
      } else if (!tokenized) {
        tok_start = &cmd_buf[i];
        tokenized = true;
        word_len++;
        i++;
        continue;
      }
    } else {

      if (in_single_quote && cmd_buf[i] == '\'') {
        in_single_quote = false;
      }
      if (in_double_quote && cmd_buf[i] == '"') {
        in_double_quote = false;
      }
    }

    if (tokenized) {
      word_len++;
    }
    i++;
  }

  flush_word(token_stream, &tok_start, &word_len, &tokenized, &token_count,
             in_single_quote || in_double_quote);

  /* state should never still be in quotes */
  if (in_single_quote || in_double_quote) {
    fprintf(stderr, "\nmsh: syntax err unbalanced quotes");
    return -1;
  }
  token_stream->tokens_arr_len = token_count;

  int expanded = expand_alias_token(cmd_line_buf, aliases, token_stream, a);
  if (expanded < 0)
    return -1;

  if (expanded == 1) {
    init_token_stream(token_stream, a);
    return lex_command_line(cmd_line_buf, token_stream, aliases, depth + 1, a);
  }

  return 0;
}
