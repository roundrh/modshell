#include "ast.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief initializes ast node
 * @return 0 on success, -1 on fail
 */
int init_ast_node(t_ast_n *ast_node) {

  if (!ast_node) {
    return -1;
  }

  ast_node->tok_start = NULL;
  ast_node->tok_segment_len = 0;
  ast_node->background = 0;
  ast_node->sub_ast_root = NULL;

  ast_node->redir_bool = 0;

  ast_node->op_type = -1;

  ast_node->left = NULL;
  ast_node->right = NULL;

  ast_node->io_redir = NULL;

  ast_node->for_var = NULL;
  ast_node->for_items = NULL;
  ast_node->items_len = 0;

  return 0;
}

/**
 * @brief initializes ast recursively calling init_ast_node
 * @return 0 on success, -1 on fail
 */
int init_ast(t_ast *ast) {
  if (!ast)
    return -1;

  ast->root = NULL;

  return 0;
}

static t_io_redir **clone_io_redir(t_io_redir **src) {
  if (!src)
    return NULL;

  size_t n = 0;
  while (src[n])
    n++;

  t_io_redir **dst = calloc(n + 1, sizeof(t_io_redir *));
  if (!dst) {
    perror("calloc");
    return NULL;
  }

  for (size_t i = 0; i < n; i++) {
    dst[i] = malloc(sizeof(t_io_redir));
    if (!dst[i])
      goto fail;

    dst[i]->io_redir_type = src[i]->io_redir_type;
    dst[i]->filename = src[i]->filename ? strdup(src[i]->filename) : NULL;
    dst[i]->src_fd = src[i]->src_fd;
    dst[i]->target_fd = src[i]->target_fd;
  }
  dst[n] = NULL;
  return dst;

fail:
  for (size_t i = 0; dst[i]; i++) {
    free(dst[i]->filename);
    free(dst[i]);
  }
  free(dst);
  return NULL;
}

static size_t measure_lpr(const t_ast_n *node) {
  if (!node)
    return 0;

  size_t total = 0;

  total += measure_lpr(node->left);

  if (node->tok_start && node->tok_segment_len > 0) {
    for (size_t i = 0; i < node->tok_segment_len; i++)
      total += node->tok_start[i].len;
    for (size_t i = 0; i < node->tok_segment_len; i++)
      if (node->tok_start[i].trailing_delim)
        total += 1;
  }

  if (node->op_type == OP_FOR) {
    if (node->for_var) {
      total += node->for_var->len;
      if (node->for_var->trailing_delim)
        total += 1;
    }

    if (node->for_items && node->items_len > 0) {
      for (size_t i = 0; i < node->items_len; i++)
        total += node->for_items[i].len;
      for (size_t i = 0; i < node->items_len; i++)
        if (node->for_items[i].trailing_delim)
          total += 1;
    }
  }

  total += measure_lpr(node->right);
  total += measure_lpr(node->sub_ast_root);

  return total;
}

typedef struct {
  char *buf;
  size_t pos;
  size_t cap;
} t_copy_ctx;

static t_token *copy_token_segment(const t_token *base, size_t len,
                                   t_copy_ctx *ctx) {
  if (!base || len == 0)
    return NULL;

  t_token *arr = calloc(len, sizeof(t_token));
  if (!arr)
    return NULL;

  for (size_t i = 0; i < len; i++) {
    arr[i].start = ctx->buf + ctx->pos;
    arr[i].len = base[i].len;
    arr[i].type = base[i].type;
    arr[i].trailing_delim = base[i].trailing_delim;

    memcpy(ctx->buf + ctx->pos, base[i].start, base[i].len);
    ctx->pos += base[i].len;

    if (base[i].trailing_delim)
      ctx->buf[ctx->pos++] = base[i].trailing_delim;
  }

  return arr;
}

static t_ast_n *copy_lpr(const t_ast_n *src, t_copy_ctx *ctx) {
  if (!src)
    return NULL;

  t_ast_n *dst = calloc(1, sizeof(t_ast_n));
  if (!dst)
    return NULL;

  dst->op_type = src->op_type;
  dst->background = src->background;
  dst->redir_bool = src->redir_bool;
  dst->tok_segment_len = src->tok_segment_len;
  dst->items_len = src->items_len;

  dst->left = copy_lpr(src->left, ctx);

  if (src->tok_start && src->tok_segment_len > 0) {
    dst->tok_start =
        copy_token_segment(src->tok_start, src->tok_segment_len, ctx);
    if (!dst->tok_start)
      goto fail;
  }

  if (src->op_type == OP_FOR) {
    if (src->for_var) {
      dst->for_var = copy_token_segment(src->for_var, 1, ctx);
      if (!dst->for_var)
        goto fail;
    }

    if (src->for_items && src->items_len > 0) {
      dst->for_items = copy_token_segment(src->for_items, src->items_len, ctx);
      if (!dst->for_items)
        goto fail;
    }
  }

  if (src->io_redir) {
    dst->io_redir = clone_io_redir(src->io_redir);
    if (!dst->io_redir)
      goto fail;
  }

  dst->right = copy_lpr(src->right, ctx);
  dst->sub_ast_root = copy_lpr(src->sub_ast_root, ctx);

  return dst;

fail:
  free(dst->tok_start);
  free(dst->for_var);
  free(dst->for_items);
  free(dst);
  return NULL;
}

t_ast_n *clone_heap_ast(const t_ast_n *src) {
  if (!src)
    return NULL;

  size_t needed = measure_lpr(src);

  char *buf = malloc(needed + 1);
  if (!buf)
    return NULL;
  buf[needed] = '\0';

  t_copy_ctx ctx = {.buf = buf, .pos = 0, .cap = needed + 1};

  t_ast_n *root = copy_lpr(src, &ctx);
  if (!root) {
    free(buf);
    return NULL;
  }

  return root;
}

static void free_io_redir(t_io_redir **redir) {
  if (!redir)
    return;
  for (size_t i = 0; redir[i]; i++) {
    free(redir[i]->filename);
    free(redir[i]);
  }
  free(redir);
}

static char *find_buf_base(const t_ast_n *node, char *cur_min) {
  if (!node)
    return cur_min;

  if (node->tok_start && node->tok_segment_len > 0) {
    char *c = node->tok_start[0].start;
    if (!cur_min || c < cur_min)
      cur_min = c;
  }
  if (node->for_var) {
    char *c = node->for_var->start;
    if (!cur_min || c < cur_min)
      cur_min = c;
  }
  if (node->for_items && node->items_len > 0) {
    char *c = node->for_items[0].start;
    if (!cur_min || c < cur_min)
      cur_min = c;
  }

  cur_min = find_buf_base(node->left, cur_min);
  cur_min = find_buf_base(node->right, cur_min);
  cur_min = find_buf_base(node->sub_ast_root, cur_min);

  return cur_min;
}

static void free_nodes(t_ast_n *node) {
  if (!node)
    return;
  free_nodes(node->left);
  free_nodes(node->right);
  free_nodes(node->sub_ast_root);
  free(node->tok_start);
  free(node->for_items);
  free(node->for_var);
  free_io_redir(node->io_redir);
  free(node);
}

void free_heap_ast(void *value) {
  if (!value)
    return;

  t_ast_n *root = (t_ast_n *)value;

  char *buf_base = find_buf_base(root, NULL);
  if (buf_base)
    free(buf_base);

  free_nodes(root);
}
