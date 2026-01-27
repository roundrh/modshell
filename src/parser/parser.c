#include "parser.h"

/**
 * @file parser.c
 * @brief implementation of parser functions
 */

static t_ast_n *
parse_pipeline(t_ast *ast, t_token_stream *ts, int start,
               int end); ///< Forward declaration of parse pipeline
static t_ast_n *
parse_conditionals(t_ast *ast, t_token_stream *ts, int start,
                   int end); ///< Forward declaration of parse conditionals
static t_ast_n *parse_subshells(t_ast *ast, t_token_stream *ts, int start,
                                int end); ///< Forward declaration

/**
 * @brief handles fail when creating io_redir array
 * @param cmd_buf pointer to cmd line buffer
 * @param command pointer to command struct
 *
 * Tokenizes command line buffer cmd_buf into command argv
 */
static int handle_fail_err_clear(t_ast_n *node, int count_redir) {

  for (int i = 0; i < count_redir; i++) {

    if (node->io_redir[i]->filename != NULL)
      free(node->io_redir[i]->filename);

    if (node->io_redir[i] != NULL)
      free(node->io_redir[i]);
  }

  free(node->io_redir);
  node->io_redir = NULL;

  return -1;
}
static int find_at_depth(t_token_stream *ts, int start, int end,
                         t_token_type wanted) {
  int depth = 0;
  for (int i = start; i <= end; i++) {
    t_token_type t = ts->tokens[i].type;

    if (depth == 0 && t == wanted)
      return i;

    if (t == TOKEN_IF || t == TOKEN_WHILE || t == TOKEN_OPEN_PAR ||
        t == TOKEN_FOR)
      depth++;
    if (depth == 1 && t == wanted)
      return i;

    if (t == TOKEN_FI || t == TOKEN_DONE || t == TOKEN_CLOSE_PAR)
      depth--;

    if (depth < 0)
      return -1;
  }
  return -1;
}

static int handle_realloc_io_redir(t_ast_n *node, size_t *capacity) {

  size_t new_cap = *capacity * BUF_GROWTH_FACTOR;
  if (new_cap >= MAX_REDIR_LEN) {
    fprintf(stderr, "Too many redirections\n");
    return -1;
  }

  t_io_redir **new_redir =
      realloc(node->io_redir, sizeof(t_io_redir *) * new_cap);
  if (!new_redir) {
    perror("fatal realloc error");
    return -1;
  }

  node->io_redir = new_redir;

  for (size_t i = *capacity; i < new_cap; i++) {
    node->io_redir[i] = NULL;
  }

  *capacity = new_cap;

  return 0;
}

/**
 * @brief parses all redirections within a command's argv into node
 * @param node pointer to ast node
 * @param command pointer to command struct
 * @param start start index of command
 * @param end end index of command
 *
 * Finds all redirection tokens within command argv from start to end parsing
 * them into a node. start and end defined by function parse_pipeline, which
 * calls recursively.
 */
static int scan_redirections(t_ast_n *node, t_token_stream *token_stream,
                             int start, int end) {

  node->io_redir =
      (t_io_redir **)malloc(sizeof(t_io_redir *) * INITIAL_REDIR_LEN);
  if (!node->io_redir) {
    perror("fatal malloc error");
    cleanup_ast(node);
    return -1;
  }

  for (int i = 0; i < INITIAL_REDIR_LEN; ++i)
    node->io_redir[i] = NULL;

  size_t capacity = INITIAL_REDIR_LEN;

  int count_redir = 0;
  int filename_prober = 0;

  t_redir_type pending = -1;

  for (int i = start; i <= end; i++) {

    if (count_redir >= capacity - 2) {
      if (handle_realloc_io_redir(node, &capacity) == -1) {
        fprintf(stderr, "\nCommand exceeds maximum redirections.");
        return -1;
      }
    }

    if (filename_prober && token_stream->tokens[i].type == TOKEN_NEWLINE)
      continue;

    if (filename_prober && token_stream->tokens[i].type == TOKEN_SIMPLE) {

      char buf[FILE_NAME_MAX];

      filename_prober = 0;
      node->io_redir[count_redir] = (t_io_redir *)malloc(sizeof(t_io_redir));
      node->io_redir[count_redir]->filename = NULL;
      if (!node->io_redir[count_redir]) {
        perror("fatal malloc");
        return handle_fail_err_clear(node, count_redir);
      }
      if (token_stream->tokens[i].len >= FILE_NAME_MAX) {
        fprintf(stderr, "\nInvalid filename");
        return handle_fail_err_clear(node, count_redir);
      }

      strncpy(buf, token_stream->tokens[i].start, token_stream->tokens[i].len);
      buf[token_stream->tokens[i].len] = '\0';

      node->io_redir[count_redir]->filename = strdup(buf);
      if (!node->io_redir[count_redir]->filename) {
        perror("fatal strdup error filename");
        return handle_fail_err_clear(node, count_redir);
      }

      node->io_redir[count_redir]->io_redir_type = pending;
      count_redir++;

      continue;
    }

    if (token_stream->tokens[i].type == TOKEN_INPUT) {
      filename_prober = 1;
      pending = IO_INPUT;
      node->redir_bool = 1;
    } else if (token_stream->tokens[i].type == TOKEN_TRUNC) {
      filename_prober = 1;
      pending = IO_TRUNC;
      node->redir_bool = 1;
    } else if (token_stream->tokens[i].type == TOKEN_HEREDOC) {
      filename_prober = 1;
      pending = IO_HEREDOC;
      node->redir_bool = 1;
    } else if (token_stream->tokens[i].type == TOKEN_APPEND) {
      filename_prober = 1;
      pending = IO_APPEND;
      node->redir_bool = 1;
    }
  }

  if (count_redir == 0) {
    free(node->io_redir);
    node->io_redir = NULL;
  } else {
    node->redir_bool = 1;
    node->io_redir[count_redir] = NULL;
  }

  return count_redir;
}
/**
 * @brief parses simple command including redirections
 * @param ast pointer to ast to parse into
 * @param command pointer to command struct
 * @param start start index of command argv
 * @param end end index of command argv
 *
 * @return ast node on success, NULL on fail.
 *
 * Calls scan redirections to parse redirection tokens
 * then parses the remaining command struct argv (start to end) into node argv,
 * returning node.
 */
static t_ast_n *parse_command(t_ast *ast, t_token_stream *ts, int start,
                              int end) {

  if (start > end)
    return NULL;

  t_ast_n *node = (t_ast_n *)malloc(sizeof(t_ast_n));
  if (!node) {
    perror("fatal malloc error");
    return NULL;
  }
  init_ast_node(node);
  node->op_type = OP_SIMPLE;

  int redir_count = scan_redirections(node, ts, start, end);
  if (redir_count == -1) {
    perror("fatal err");
    cleanup_ast_node(node);
    return NULL;
  }

  size_t count = (end - start) + 1;
  node->tok_start = &(ts->tokens[start]);
  node->tok_segment_len = count;

  return node;
}

static int next_terminator_index(t_token_stream *ts, int start, int end) {
  int depth = 0;

  for (int i = start; i <= end; i++) {
    t_token_type type = ts->tokens[i].type;

    if (type == TOKEN_IF || type == TOKEN_WHILE || type == TOKEN_OPEN_PAR ||
        type == TOKEN_FOR) {
      depth++;
    }
    if (type == TOKEN_FI || type == TOKEN_DONE || type == TOKEN_CLOSE_PAR) {
      depth--;
    }

    if (depth < 0)
      return -2;

    if (depth == 0 &&
        (type == TOKEN_SEQ || type == TOKEN_BG || type == TOKEN_NEWLINE)) {
      return i;
    }
  }
  return (depth == 0) ? -1 : -2;
}

/**
 * @brief parsers terminators in command line buf
 * @param ast pointer to ast
 * @param command pointer to command struct
 * @param start start index within command argv
 * @param end end index within command argv
 *
 * @return NULL on fail, root node on success
 * Background terminator & parsed as left tree, ; operator parsed as root node.
 */
static t_ast_n *parse_terminators(t_ast *ast, t_token_stream *ts, int start,
                                  int end) {

  int i = start;
  t_ast_n *seq = NULL;

  while (i <= end) {

    int term = next_terminator_index(ts, i, end);
    if (term == -2) {
      fprintf(stderr, "msh: syntax error: unbalanced token\n");
      return NULL;
    }

    int cmd_start = i;
    int cmd_end = (term == -1) ? end : term - 1;

    while (cmd_end >= cmd_start &&
           (ts->tokens[cmd_end].type == TOKEN_SEQ ||
            ts->tokens[cmd_end].type == TOKEN_NEWLINE)) {
      cmd_end--;
    }

    if (cmd_start > cmd_end) {
      i = term + 1;
      continue;
    }

    t_ast_n *cmd = parse_conditionals(ast, ts, cmd_start, cmd_end);
    if (!cmd) {
      if (seq)
        cleanup_ast(seq);
      return NULL;
    }

    if (term != -1 && ts->tokens[term].type == TOKEN_BG)
      cmd->background = 1;

    if (!seq)
      seq = cmd;
    else {
      t_ast_n *node = (t_ast_n *)malloc(sizeof(t_ast_n));
      if (!node) {
        perror("268: fatal malloc terminators");
        cleanup_ast(seq);
        return NULL;
      }
      init_ast_node(node);

      node->op_type = OP_SEQ;
      node->left = seq;
      node->right = cmd;

      seq = node;
    }

    if (term == -1)
      break;

    i = term + 1;
  }

  return seq;
}

/**
 * @brief parses conditional operators && and ||
 * @return subtree node on success, null on fail
 * @param ast pointer to ast struct
 * @param command pointer to command struct
 * @param start start index of command struct argv
 * @param end end index of command struct argv
 *
 * Searches command argv range start to end accordingly, looking and parsing
 * conditional operators and breaking them apart into subtrees returned to
 * parse_terminators to attach to AST.
 */
static t_ast_n *parse_conditionals(t_ast *ast, t_token_stream *ts, int start,
                                   int end) {

  if (start > end)
    return NULL;

  t_ast_n *node = NULL;

  int last_conditional_index = -1;
  int cond_flag = -1;
  int par_depth = 0;
  int flow_depth = 0;

  for (int i = start; i <= end; i++) {
    t_token_type type = ts->tokens[i].type;

    if (type == TOKEN_OPEN_PAR)
      par_depth++;
    else if (type == TOKEN_CLOSE_PAR)
      par_depth--;

    if (type == TOKEN_IF || type == TOKEN_WHILE)
      flow_depth++;
    else if (type == TOKEN_FI || type == TOKEN_DONE)
      flow_depth--;

    if (par_depth == 0 && flow_depth == 0) {
      if (type == TOKEN_AND) {
        last_conditional_index = i;
        cond_flag = OP_AND;
      } else if (type == TOKEN_OR) {
        last_conditional_index = i;
        cond_flag = OP_OR;
      }
    }
  }

  if (par_depth != 0) {
    fprintf(stderr, "\nmsh: syntax error unbalanced parantheses");
    return NULL;
  }

  if (last_conditional_index == -1) {
    return parse_pipeline(ast, ts, start, end);
  } else {

    node = (t_ast_n *)malloc(sizeof(t_ast_n));
    if (!node) {
      perror("node malloc fatal fail");
      return NULL;
    }
    init_ast_node(node);
    node->op_type = cond_flag;

    node->left = parse_conditionals(ast, ts, start, last_conditional_index - 1);
    node->right = parse_conditionals(ast, ts, last_conditional_index + 1, end);

    if (!node->left || !node->right) {

      if (node->op_type == OP_AND)
        fprintf(stderr, "\nmesh: syntax error near token '&&'\n");
      if (node->op_type == OP_OR)
        fprintf(stderr, "\nmesh: syntax error near token '||'\n");

      if (node->left)
        cleanup_ast(node->left);
      if (node->right)
        cleanup_ast(node->right);

      cleanup_ast_node(node);

      return NULL;
    }
    return node;
  }

  return node;
}

/**
 * @brief recursively parses pipeline tokens and their respective left and right
 * children
 * @param ast pointer to ast to parse into
 * @param command pointer to command struct
 * @param start start index of command argv
 * @param end end index of command argv
 *
 * @return ast root node on success, NULL on fail.
 *
 * Main hierarchy here is:
 *  - Call parse_pipeline from last pipe index
 *  - Parse left and right subtrees recursively via parse_command (which calls
 * scan_redirections) This creates a left-associative AST to be given to the
 * executor.
 *
 */
static t_ast_n *parse_pipeline(t_ast *ast, t_token_stream *ts, int start,
                               int end) {

  if (start > end)
    return NULL;

  t_ast_n *node = NULL;

  int last_pipe_index = -1;
  int par_depth = 0;
  int flow_depth = 0;

  for (int i = start; i <= end; i++) {
    t_token_type type = ts->tokens[i].type;

    if (type == TOKEN_OPEN_PAR)
      par_depth++;
    else if (type == TOKEN_CLOSE_PAR)
      par_depth--;

    if (type == TOKEN_IF || type == TOKEN_WHILE || type == TOKEN_FOR)
      flow_depth++;
    else if (type == TOKEN_FI || type == TOKEN_DONE)
      flow_depth--;

    if (par_depth == 0 && flow_depth == 0 && type == TOKEN_PIPE) {
      last_pipe_index = i;
    }
  }

  if (par_depth != 0) {
    fprintf(stderr, "\nmsh: syntax error unbalanced parantheses");
    return NULL;
  }

  if (last_pipe_index == -1) {
    return parse_subshells(ast, ts, start, end);
  } else {

    node = (t_ast_n *)malloc(sizeof(t_ast_n));
    if (!node) {
      perror("node malloc fatal fail");
      return NULL;
    }
    init_ast_node(node);
    node->op_type = OP_PIPE;

    node->left = parse_pipeline(ast, ts, start, last_pipe_index - 1);
    node->right = parse_pipeline(ast, ts, last_pipe_index + 1, end);

    if (!node->left || !node->right) {

      fprintf(stderr, "\nmesh: syntax error near token '|'\n");
      if (node->left)
        cleanup_ast(node->left);
      if (node->right)
        cleanup_ast(node->right);

      cleanup_ast_node(node);

      return NULL;
    }
    return node;
  }

  return node;
}

static t_ast_n *parse_if_chain(t_ast *ast, t_token_stream *ts, int start,
                               int end) {
  t_token_type type = ts->tokens[start].type;

  if (type == TOKEN_ELIF) {
    int then_idx = -1;
    int depth = 0;
    for (int i = start; i < end; i++) {
      if (ts->tokens[i].type == TOKEN_IF)
        depth++;
      else if (ts->tokens[i].type == TOKEN_FI)
        depth--;
      if (depth == 0 && ts->tokens[i].type == TOKEN_THEN) {
        then_idx = i;
        break;
      }
    }

    if (then_idx == -1) {
      fprintf(stderr, "msh: syntax error: expected 'then' after elif\n");
      return NULL;
    }

    int next_ctrl = end;
    depth = 0;
    for (int i = start; i < end; i++) {
      t_token_type t = ts->tokens[i].type;
      if (t == TOKEN_IF)
        depth++;
      else if (t == TOKEN_FI)
        depth--;

      if (depth == 0 && i > then_idx && (t == TOKEN_ELIF || t == TOKEN_ELSE)) {
        next_ctrl = i;
        break;
      }
    }

    t_ast_n *node = malloc(sizeof(t_ast_n));
    if (!node)
      return NULL;
    init_ast_node(node);
    node->op_type = OP_IF;

    node->left = parse_terminators(ast, ts, start + 1, then_idx - 1);
    node->right = parse_terminators(ast, ts, then_idx + 1, next_ctrl - 1);
    node->sub_ast_root = NULL;

    if (!node->left || !node->right)
      goto fail;

    if (next_ctrl != end) {
      node->sub_ast_root = parse_if_chain(ast, ts, next_ctrl, end);
      if (!node->sub_ast_root)
        goto fail;
    }
    return node;

  fail:
    if (node->left)
      cleanup_ast(node->left);
    if (node->right)
      cleanup_ast(node->right);
    free(node);
    return NULL;
  }

  if (type == TOKEN_ELSE) {
    return parse_terminators(ast, ts, start + 1, end - 1);
  }

  return NULL;
}
static t_ast_n *parse_if(t_ast *ast, t_token_stream *ts, int start, int end) {

  int fi_idx = find_at_depth(ts, start, end, TOKEN_FI);
  if (fi_idx == -1) {
    fprintf(stderr, "msh: syntax error: missing 'fi'\n");
    return NULL;
  }

  int then_idx = find_at_depth(ts, start, fi_idx, TOKEN_THEN);
  if (then_idx == -1) {
    fprintf(stderr, "msh: syntax error: expected 'then'\n");
    return NULL;
  }

  int next_ctrl = fi_idx;
  int depth = 0;
  for (int i = start; i < fi_idx; i++) {
    t_token_type t = ts->tokens[i].type;
    if (t == TOKEN_IF)
      depth++;
    else if (t == TOKEN_FI)
      depth--;

    if (depth == 1 && i > then_idx && (t == TOKEN_ELIF || t == TOKEN_ELSE)) {
      next_ctrl = i;
      break;
    }
  }

  t_ast_n *node = malloc(sizeof(t_ast_n));
  if (!node)
    return NULL;
  init_ast_node(node);
  node->op_type = OP_IF;

  node->left = parse_terminators(ast, ts, start + 1, then_idx - 1);
  node->right = parse_terminators(ast, ts, then_idx + 1, next_ctrl - 1);
  node->sub_ast_root = NULL;

  if (!node->left || !node->right)
    goto fail;

  if (ts->tokens[next_ctrl].type == TOKEN_ELIF ||
      ts->tokens[next_ctrl].type == TOKEN_ELSE) {
    node->sub_ast_root = parse_if_chain(ast, ts, next_ctrl, fi_idx);
    if (!node->sub_ast_root)
      goto fail;
  }

  return node;

fail:
  if (node->left)
    cleanup_ast(node->left);
  if (node->right)
    cleanup_ast(node->right);
  if (node->sub_ast_root)
    cleanup_ast(node->sub_ast_root);
  free(node);
  return NULL;
}

static t_ast_n *parse_while(t_ast *ast, t_token_stream *ts, int start,
                            int end) {
  int do_idx = -1;
  int done_idx = -1;
  int depth = 0;

  for (int i = start; i <= end; i++) {
    t_token_type type = ts->tokens[i].type;
    if (type == TOKEN_WHILE || type == TOKEN_FOR || type == TOKEN_OPEN_PAR ||
        type == TOKEN_IF)
      depth++;
    if (depth == 1) {
      if (type == TOKEN_DO)
        do_idx = i;
      if (type == TOKEN_DONE)
        done_idx = i;
    }
    if (type == TOKEN_DONE || type == TOKEN_CLOSE_PAR || type == TOKEN_FI)
      depth--;
  }
  if (do_idx == -1 || done_idx == -1) {
    fprintf(stderr, "msh: syntax error: expected 'do' or 'done'\n");
    return NULL;
  }

  t_ast_n *node = (t_ast_n *)malloc(sizeof(t_ast_n));
  if (!node)
    return NULL;
  init_ast_node(node);
  node->op_type = OP_WHILE;

  node->left = parse_terminators(ast, ts, start + 1, do_idx - 1);
  node->right = parse_terminators(ast, ts, do_idx + 1, done_idx - 1);

  if (scan_redirections(node, ts, done_idx + 1, end) == -1) {
    cleanup_ast_node(node);
    return NULL;
  }

  return node;
}

static t_ast_n *parse_for(t_ast *ast, t_token_stream *ts, int start, int end) {
  if (start > end) {
    fprintf(stderr, "msh: syntax error: incomplete for loop\n");
    return NULL;
  }

  t_ast_n *node = malloc(sizeof(t_ast_n));
  if (!node)
    return NULL;
  init_ast_node(node);
  node->op_type = OP_FOR;

  node->for_var = &(ts->tokens[start + 1]);
  if (node->for_var->type != TOKEN_SIMPLE) {
    fprintf(stderr, "msh: syntax error: expected variable name after 'for'\n");
    goto fail;
  }

  if (ts->tokens[start + 2].type != TOKEN_IN) {
    fprintf(stderr, "msh: syntax error: expected 'in' after 'for %.*s'\n",
            (int)node->for_var->len, node->for_var->start);
    goto fail;
  }

  int items_start = start + 3;
  int do_idx = -1;
  int done_idx = -1;
  int depth = 0;
  for (int i = start; i <= end; i++) {
    t_token_type t = ts->tokens[i].type;

    if (t == TOKEN_IF || t == TOKEN_FOR || t == TOKEN_WHILE ||
        t == TOKEN_OPEN_PAR)
      depth++;
    if (t == TOKEN_FI || t == TOKEN_DONE || t == TOKEN_CLOSE_PAR)
      depth--;

    if (t == TOKEN_DO && depth == 1 && do_idx == -1) {
      do_idx = i;
    }
    if (t == TOKEN_DONE && depth == 0) {
      done_idx = i;
      break;
    }
  }
  if (do_idx == -1 || done_idx == -1) {
    fprintf(stderr, "msh: syntax error: missing 'do' or 'done'\n");
    goto fail;
  }

  node->for_items = &(ts->tokens[items_start]);
  node->items_len = do_idx - items_start;

  if (node->items_len > 0) {
    t_token_type last_item = ts->tokens[do_idx - 1].type;
    if (last_item == TOKEN_SEQ || last_item == TOKEN_NEWLINE) {
      node->items_len--;
    }
  }

  node->sub_ast_root = parse_terminators(ast, ts, do_idx + 1, done_idx - 1);
  if (!node->sub_ast_root)
    goto fail;

  return node;

fail:
  cleanup_ast_node(node);
  return NULL;
}

static t_ast_n *parse_flow_control(t_ast *ast, t_token_stream *ts, int start,
                                   int end) {

  if (start > end)
    return NULL;

  t_token_type type = ts->tokens[start].type;

  if (type == TOKEN_IF) {
    return parse_if(ast, ts, start, end);
  } else if (type == TOKEN_WHILE) {
    return parse_while(ast, ts, start, end);
  } else if (type == TOKEN_FOR) {
    return parse_for(ast, ts, start, end);
  }
  return parse_command(ast, ts, start, end);
}

static t_ast_n *parse_subshells(t_ast *ast, t_token_stream *ts, int start,
                                int end) {

  if (start > end)
    return NULL;
  if (ts->tokens[start].type != TOKEN_OPEN_PAR) {
    return parse_flow_control(ast, ts, start, end);
  }

  t_ast_n *node = NULL;

  int first_open_par_idx = -1;
  int last_close_par_idx = -1;

  for (int i = start; i <= end; i++) {

    if (first_open_par_idx == -1 && ts->tokens[i].type == TOKEN_OPEN_PAR) {
      first_open_par_idx = i;
    }
    if (first_open_par_idx != -1 && ts->tokens[i].type == TOKEN_CLOSE_PAR) {
      last_close_par_idx = i;
    }
  }

  if (first_open_par_idx == -1 || last_close_par_idx == -1) {
    return parse_flow_control(ast, ts, start, end);
  } else {

    node = (t_ast_n *)malloc(sizeof(t_ast_n));
    if (!node) {
      perror("575: node malloc fail");
      return NULL;
    }
    init_ast_node(node);
    int redir_count = scan_redirections(node, ts, last_close_par_idx + 1, end);
    if (redir_count == -1) {
      perror("redir subsh");
      cleanup_ast_node(node);
      return NULL;
    }
    node->op_type = OP_SUBSHELL;

    node->sub_ast_root = parse_terminators(ast, ts, first_open_par_idx + 1,
                                           last_close_par_idx - 1);
    if (!node->sub_ast_root) {
      perror("582: fail to parse subast");
      cleanup_ast_node(node);
      return NULL;
    }

    node->left = NULL;
    node->right = NULL;
  }

  return node;
}

// static const char* get_op_name(t_op_type type) {
//     switch (type) {
//         case OP_SIMPLE:     return "SIMPLE";
//         case OP_PIPE:       return "PIPE";
//         case OP_AND:        return "AND";
//         case OP_OR:         return "OR";
//         case OP_SEQ:        return "SEQ";
//         case OP_SUBSHELL:   return "SUBSHELL";
//         case OP_WHILE:      return "WHILE";
//         case OP_IF:         return "IF";
//         default:            return "UNKNOWN";
//     }
// }

// static void print_token(const t_token *tok) {
//     if (!tok || !tok->start || tok->len == 0)
//         return;

//     printf("'");
//     fwrite(tok->start, 1, tok->len, stdout);
//     printf("'");
// }
// static void print_ast(t_ast_n *node, int depth) {
//     if (!node)
//         return;

//     for (int i = 0; i < depth; i++)
//         printf("  ");

//     printf("├── [%s]", get_op_name(node->op_type));

//     if (node->tok_start && node->tok_segment_len > 0) {
//         printf(" ARGS: ");
//         for (size_t i = 0; i < node->tok_segment_len; i++) {
//             print_token(&node->tok_start[i]);
//             if (i + 1 < node->tok_segment_len) printf(" ");
//         }
//     }

//     if (node->background) printf(" [BG]");
//     printf("\n");

//     if (node->io_redir) {
//         for (int i = 0; node->io_redir[i]; i++) {
//             for (int j = 0; j <= depth; j++) printf("  ");
//             printf("↳ REDIR ");
//             switch (node->io_redir[i]->io_redir_type) {
//                 case IO_INPUT:   printf("< "); break;
//                 case IO_TRUNC:   printf("> "); break;
//                 case IO_APPEND:  printf(">> "); break;
//                 case IO_HEREDOC: printf("<< "); break;
//                 default:         printf("? "); break;
//             }
//             printf("'%s'\n", node->io_redir[i]->filename);
//         }
//     }

//     if (node->op_type == OP_IF) {
//         for (int i = 0; i <= depth; i++) printf("  ");
//         printf("↳ IF-CONDITION:\n");
//         print_ast(node->left, depth + 2);

//         for (int i = 0; i <= depth; i++) printf("  ");
//         printf("↳ IF-BODY:\n");
//         print_ast(node->right, depth + 2);

//         if (node->sub_ast_root) {
//             for (int i = 0; i <= depth; i++) printf("  ");
//             printf("↳ NEXT-BRANCH (ELIF/ELSE):\n");
//             print_ast(node->sub_ast_root, depth + 2);
//         }
//     }
//     else if (node->op_type == OP_WHILE) {
//         for (int i = 0; i <= depth; i++) printf("  ");
//         printf("↳ WHILE-CONDITION:\n");
//         print_ast(node->left, depth + 2);

//         for (int i = 0; i <= depth; i++) printf("  ");
//         printf("↳ WHILE-BODY:\n");
//         print_ast(node->right, depth + 2);
//     }
//     else if (node->op_type == OP_SUBSHELL) {
//         for (int i = 0; i <= depth; i++) printf("  ");
//         printf("↳ SUBSHELL-ROOT:\n");
//         print_ast(node->sub_ast_root, depth + 2);
//     }
//     else {
//         if (node->left) print_ast(node->left, depth + 1);
//         if (node->right) print_ast(node->right, depth + 1);
//     }
// }

/**
 * @brief function to be called by the parse_and_execute function defined in
 * executor.h
 * @param ast pointer to ast to parse into
 * @param command pointer to command struct
 *
 * @return ast root node on success, NULL on fail.
 *
 */
t_ast_n *build_ast(t_ast *ast, t_token_stream *token_stream) {

  init_ast(ast);
  if ((ast->root = parse_terminators(
           ast, token_stream, 0, token_stream->tokens_arr_len - 1)) == NULL) {
    return NULL;
  }

  // printf("--- AST Structure ---\n");
  // print_ast(ast->root, 0);
  // printf("---------------------\n");

  return ast->root;
}
