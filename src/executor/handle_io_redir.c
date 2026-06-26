#include "handle_io_redir.h"
#include "ast.h"
#include "lexer.h"
#include "shell.h"
#include "var_exp.h"
#include <fcntl.h>
#include <unistd.h>

/**
 * @file handle_io_redir.c
 * @brief implementation of handling of I/O redirection
 */

static int check_realloc_fdsp(t_shell *shell) {
  if (shell->fd_prevs_len < shell->fd_prevs_cap)
    return 0;

  size_t ncap = shell->fd_prevs_cap * BUF_GROWTH_FACTOR;
  if (ncap < shell->fd_prevs_cap)
    return -1;
  t_fd_backup *nptr =
      (t_fd_backup *)arena_alloc(&shell->arena, ncap * sizeof(t_fd_backup));
  if (!nptr)
    return -1;

  memcpy(nptr, shell->fd_prevs, shell->fd_prevs_cap * sizeof(t_fd_backup));

  shell->fd_prevs = nptr;
  shell->fd_prevs_cap = ncap;

  return 0;
}

static int save_fd(int src_fd, t_shell *shell) {
  for (size_t i = 0; i < shell->fd_prevs_len; ++i) {
    if (shell->fd_prevs[i].src_fd == src_fd)
      return 0;
  }

  int dup_fd = dup(src_fd);
  if (dup_fd == -1)
    return -1;

  if (check_realloc_fdsp(shell) == -1)
    return -1;

  shell->fd_prevs[shell->fd_prevs_len].src_fd = src_fd;
  shell->fd_prevs[shell->fd_prevs_len].saved_fd = dup_fd;
  shell->fd_prevs_len++;

  return 0;
}

/**
 * @brief helper function to get redirection flags based on redirection type.
 * @return flags
 * @param node pointer to ast node
 * @param index index of io_redir arr
 */
static int get_redir_flags(t_ast_n *node, int index) {

  int flags = 0;

  t_redir_type typ = node->io_redir[index]->io_redir_type;
  if (typ == IO_TRUNC) {
    flags = O_WRONLY | O_CREAT | O_TRUNC;
  } else if (typ == IO_APPEND || typ == IO_FORCE_OW) {
    flags = O_WRONLY | O_CREAT | O_APPEND;
  } else if (typ == IO_READ_WRITE) {
    flags = O_RDWR | O_CREAT;
  } else if (typ == IO_INPUT) {
    flags = O_RDONLY;
  }

  return flags;
}

/**
 * @brief handles heredoc_io
 * @return 0 success, -1 fail (fatal)
 * @param shell pointer to shell struct
 * @param index index of io_redir arr
 * @param node pointer to ast node
 *
 * Function first calls mkstemp(template) on defined char template[] to create a
 * temp file to read heredoc i/o into fcntl and unlink assure the deletion of
 * the flag after heredoc is read. reading of user input for heredoc handled
 * within this function.
 */
static int heredoc_io(t_shell *shell, int index, t_ast_n *node) {
  char template[] = "/tmp/heredocXXXXXX";
  int heredoc_fd = mkstemp(template);
  if (heredoc_fd == -1) {
    perror("mkstemp fatal error");
    return -1;
  }
  fcntl(heredoc_fd, F_SETFD, FD_CLOEXEC);
  unlink(template);

  char *body = node->io_redir[index]->hd_body;
  if (body) {
    if (write(heredoc_fd, body, strlen(body)) == -1) {
      perror("heredoc_io write");
      close(heredoc_fd);
      return -1;
    }
  }

  if (lseek(heredoc_fd, 0, SEEK_SET) == -1) {
    perror("lseek fatal error");
    close(heredoc_fd);
    return -1;
  }
  return heredoc_fd;
}

int check_realloc_pending_hds(t_shell *shell) {
  if (shell->pending_hds_len < shell->pending_hds_cap)
    return 0;

  if (shell->pending_hds_cap > SIZE_MAX / BUF_GROWTH_FACTOR) {
    fprintf(stderr, "pending hds resize: overflow\n");
    return -1;
  }

  size_t ncap = shell->pending_hds_cap * BUF_GROWTH_FACTOR;

  char **nptr =
      arena_realloc(&shell->arena, shell->pending_hds, sizeof(char *) * ncap,
                    sizeof(char *) * shell->pending_hds_cap);

  if (!nptr)
    return -1;

  memset(nptr + shell->pending_hds_cap, 0,
         sizeof(char *) * (ncap - shell->pending_hds_cap));

  shell->pending_hds = nptr;
  shell->pending_hds_cap = ncap;

  return 0;
}

int read_hd_body(const char *delim, bool strip, t_shell *shell, char **out) {

  FILE *stream = shell->is_interactive ? stdin : shell->script_fstream;

  size_t buf_cap = 512;
  size_t buf_len = 0;
  char *buf = arena_alloc(&shell->arena, buf_cap);
  if (!buf)
    return -1;
  buf[0] = '\0';

  size_t dlen = strlen(delim);
  char *dcpy = arena_alloc(&shell->arena, dlen + 1);
  if (!dcpy) {
    return -1;
  }

  memcpy(dcpy, delim, dlen + 1);

  bool exp = true;
  if (dcpy[0] == '\'' || dcpy[0] == '"')
    exp = false;
  if (!exp)
    strip_quotes(dcpy);

  char *line = NULL;
  size_t line_cap = 0;
  ssize_t nr;

  if (shell->is_interactive)
    fprintf(stderr, "HEREDOC>> ");
  while ((nr = getline(&line, &line_cap, stream)) != -1) {

    if (nr > 0 && line[nr - 1] == '\n')
      line[--nr] = '\0';

    char *eff = line;
    if (strip) {
      while (*eff == '\t')
        eff++;
    }
    if (strcmp(eff, dcpy) == 0)
      break;

    size_t eff_len = strlen(eff);
    char *abuf = arena_alloc(&shell->arena, eff_len + 1);
    if (!abuf)
      break;
    memcpy(abuf, eff, eff_len + 1);

    char *tb = abuf;
    if (exp) {
      t_token_stream vs;
      init_token_stream(&vs, &shell->arena);
      lex_command_line(&abuf, &vs, NULL, 0, &shell->arena, 1);
      size_t tbc = eff_len + 1;
      if (tbc < 128)
        tbc = 128;
      tb = arena_alloc(&shell->arena, tbc);
      make_buf(shell, vs.tokens, vs.tokens_arr_len, &shell->arena, &tb, &tbc,
               1);
    }

    size_t tb_len = strlen(tb);
    size_t needed = buf_len + tb_len + 2;
    if (needed > buf_cap) {
      size_t ncap = buf_cap;

      while (ncap < needed)
        ncap *= 2;

      char *nbuf = arena_realloc(&shell->arena, buf, ncap, buf_cap);

      if (!nbuf) {
        return -1;
      }

      buf = nbuf;
      buf_cap = ncap;
    }
    memcpy(buf + buf_len, tb, tb_len);
    buf_len += tb_len;
    buf[buf_len++] = '\n';
    buf[buf_len] = '\0';

    if (shell->is_interactive)
      fprintf(stderr, "HEREDOC>> ");
  }
  free(line);
  *out = buf;
  return 0;
}

static int collect_stdin_hds_node(t_ast_n *r, size_t *idx, t_shell *shell) {
  if (!r)
    return 0;
  if (r->io_redir) {
    for (size_t i = 0; r->io_redir[i]; i++) {
      t_io_redir *rd = r->io_redir[i];
      if (rd->io_redir_type != IO_HEREDOC &&
          rd->io_redir_type != IO_HEREDOC_STRIP)
        continue;
      if (check_realloc_pending_hds(shell) == -1)
        return -1;
      char *body = NULL;
      bool strip = rd->io_redir_type == IO_HEREDOC_STRIP;
      if (read_hd_body(rd->filename, strip, shell, &body) == -1)
        return -1;
      shell->pending_hds[*idx] = body;
      shell->pending_hds_len++;
      (*idx)++;
    }
  }
  if (r->sub_ast_root &&
      collect_stdin_hds_node(r->sub_ast_root, idx, shell) == -1)
    return -1;
  if (r->left && collect_stdin_hds_node(r->left, idx, shell) == -1)
    return -1;
  if (r->right && collect_stdin_hds_node(r->right, idx, shell) == -1)
    return -1;
  return 0;
}

int collect_stdin_hds(t_shell *shell, t_ast_n *root) {
  size_t idx = 0;
  return collect_stdin_hds_node(root, &idx, shell);
}

static int collect_hd_node(t_io_redir *n_redir, size_t idx, t_shell *shell) {
  char *body = shell->pending_hds[idx];
  n_redir->hd_body = body;
  return 0;
}

int collect_pending_hds(t_ast_n *r, size_t *idx, t_shell *shell) {
  if (!r)
    return 0;

  if (r->io_redir) {
    for (size_t i = 0; r->io_redir[i]; i++) {
      if (r->io_redir[i]->io_redir_type == IO_HEREDOC ||
          r->io_redir[i]->io_redir_type == IO_HEREDOC_STRIP) {
        collect_hd_node(r->io_redir[i], *idx, shell);
        (*idx)++;
      }
    }
  }

  if (r->sub_ast_root)
    collect_pending_hds(r->sub_ast_root, idx, shell);
  if (r->left)
    collect_pending_hds(r->left, idx, shell);
  if (r->right)
    collect_pending_hds(r->right, idx, shell);

  return 0;
}

/**
 * @brief applies a single redirection at index of io_redir array in node
 * @return 0 success, -1 fail (fatal)
 * @param shell pointer to shell struct
 * @param node pointer to ast node
 * @param index index of io_redir arr
 *
 */
static int apply_single_redir(t_shell *shell, t_ast_n *node, int index) {

  t_io_redir *redir = node->io_redir[index];
  t_redir_type typ = redir->io_redir_type;
  int newfd = -1;

  if (typ != IO_DUP_IN && typ != IO_DUP_OUT && redir->filename == NULL) {
    fprintf(stderr, "\nmsh: invalid redir: filename");
    return -1;
  }

  if (typ == IO_DUP_IN || typ == IO_DUP_OUT) {
    if (save_fd(redir->src_fd, shell) == -1)
      return -1;
    if (dup2(redir->target_fd, redir->src_fd) == -1) {
      perror("dup2 fatal error");
      return -1;
    }
    return 0;
  }

  if (typ == IO_HEREDOC || typ == IO_HEREDOC_STRIP) {
    newfd = heredoc_io(shell, index, node);
    if (newfd == -1)
      return -1;
  } else {
    int flags = get_redir_flags(node, index);
    newfd = open(redir->filename, flags, 0644);
    if (newfd == -1) {
      perror("open");
      return -1;
    }
  }

  if (save_fd(redir->src_fd, shell) == -1) {
    close(newfd);
    return -1;
  }

  if (dup2(newfd, redir->src_fd) == -1) {
    perror("dup2");
    close(newfd);
    return -1;
  }

  close(newfd);
  return 0;
}

/**
 * @brief calls apply_single_redir iteratively on all indexes within io_redir
 * array of node
 * @return 0 success, -1 fail (fatal)
 * @param shell pointer to shell struct
 * @param node pointer to ast node
 *
 */
int redirect_io(t_shell *shell, t_ast_n *node) {
  shell->fd_prevs = (t_fd_backup *)arena_alloc(
      &shell->arena, FDS_P_DEF_SIZE * sizeof(t_fd_backup));
  shell->fd_prevs_cap = FDS_P_DEF_SIZE;
  shell->fd_prevs_len = 0;

  for (size_t i = 0; i < shell->fd_prevs_cap; i++) {
    shell->fd_prevs[i].src_fd = -1;
    shell->fd_prevs[i].saved_fd = -1;
  }

  for (int i = 0; node->io_redir[i] != NULL; i++) {

    if (apply_single_redir(shell, node, i) == -1) {
      restore_io(shell);
      return -1;
    }
  }
  return 0;
}

/**
 * @brief restores I/O file descriptors backed up by restore_io into shell
 * struct.
 * @return 0 success, -1 fail (fatal)
 * @param shell pointer to shell struct
 *
 */
int restore_io(t_shell *shell) {
  for (size_t i = 0; i < shell->fd_prevs_len; ++i) {
    int src = shell->fd_prevs[i].src_fd;
    int saved = shell->fd_prevs[i].saved_fd;

    if (dup2(saved, src) == -1)
      perror("dup2 restore");

    close(saved);
  }

  shell->fd_prevs = NULL;
  shell->fd_prevs_len = 0;
  shell->fd_prevs_cap = 0;

  return 0;
}
