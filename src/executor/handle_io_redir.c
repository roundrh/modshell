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

  bool exp = true;
  char *delim = node->io_redir[index]->filename;

  if (delim[0] == '\'' || delim[0] == '"')
    exp = false;

  char *line = NULL;
  size_t line_cap = 0;
  ssize_t char_read = 0;

  t_arena hdoc_arena;
  arena_init(&hdoc_arena);

  char *dcpy = strdup(delim);
  if (!dcpy) {
    perror("strdup");
    return -1;
  }

  if (!exp)
    strip_quotes(dcpy);

  while (1) {
    fprintf(stderr, "HEREDOC>> ");

    char_read = getline(&line, &line_cap, stdin);
    if (char_read == -1)
      break;

    if (char_read > 0 && line[char_read - 1] == '\n') {
      line[char_read - 1] = '\0';
      char_read--;
    }

    char *eff = line;

    if (node->io_redir[index]->io_redir_type == IO_HEREDOC_STRIP) {
      while (*eff && *eff == '\t')
        eff++;
    }

    if (strcmp(eff, dcpy) == 0)
      break;

    arena_reset(&hdoc_arena);

    size_t eff_len = strlen(eff);
    char *buf = arena_alloc(&hdoc_arena, eff_len + 1);
    if (!buf)
      break;

    memcpy(buf, eff, eff_len + 1);
    char *tb = buf;

    if (exp) {
      t_token_stream vs;
      init_token_stream(&vs, &hdoc_arena);

      lex_command_line(&buf, &vs, NULL, 0, &hdoc_arena, 1);

      size_t tbc = eff_len + 1;
      if (tbc < 128)
        tbc = 128;

      tb = arena_alloc(&hdoc_arena, tbc);

      make_buf(shell, vs.tokens, vs.tokens_arr_len, &hdoc_arena, &tb, &tbc, 1);
    }

    if (write(heredoc_fd, tb, strlen(tb)) == -1) {
      perror("fatal inloop err heredoc");
      break;
    }

    if (write(heredoc_fd, "\n", 1) == -1) {
      perror("fatal inloop err heredoc");
      break;
    }
  }

  free(line);
  free(dcpy);
  arena_free(&hdoc_arena);

  if (lseek(heredoc_fd, 0, SEEK_SET) == -1) {
    perror("lseek fatal error");
    close(heredoc_fd);
    return -1;
  }

  return heredoc_fd;
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
