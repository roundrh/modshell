#include "userinp.h"
#include <unistd.h>

/**
 * @file userinp.c
 * @brief contains implementation of functions to read user input
 */

int handle_write_fail(int fd, const char *buf, size_t len, char *buffer_ptr) {

  ssize_t write_ret;

  while ((write_ret = write(fd, buf, len)) == -1) {
    if (errno == EINTR)
      continue;

    if (errno == EPIPE)
      return -1;

    perror("readinp fail: write fatal");
    if (buffer_ptr) {
      free(buffer_ptr);
    }
    return -1;
  }
  return 0;
}

void get_term_size(int *rows, int *cols) {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  *rows = w.ws_row;
  *cols = w.ws_col;
}
/**
 * @brief handle reallocation of line buffer
 * @return -1 on fail 0 on success.
 * @param buf pointer to array of characters line buffer
 * @param buf_cap capacity of line buffer
 * @param buf_len length of line buffer
 *
 * function is called in the loop to check if the buffer needs to be reallocated
 * to handle a large capacity of input.
 */
static int handle_realloc_buf(char **buf, size_t *buf_cap, size_t *buf_len) {

  if (!buf || !(*buf) || !buf_cap || !buf_len || *buf_cap <= 0)
    return -1;

  if (*buf_len < *buf_cap - 1)
    return 0; // no realloc needed

  size_t new_size = (*buf_cap) * BUF_GROWTH_FACTOR;
  if (new_size >= MAX_COMMAND_LENGTH) {
    fprintf(stderr, "\ncmd max length reached");
    free(*buf);
    *buf = NULL;
    return -1;
  }

  char *new_buf = realloc(*buf, new_size);
  if (!new_buf) {
    perror("fail to realloc new_buf");
    free(*buf);
    *buf = NULL;
    return -1;
  }

  *buf = new_buf;
  *buf_cap = new_size;
  new_buf = NULL;

  return 1;
}

static void rndr_sgst(size_t cmd_len, size_t cmd_idx, t_dllnode *sgst) {
  if (!sgst || cmd_idx != cmd_len)
    return;

  char *s = sgst->strbg + cmd_len;
  printf("\033[s");
  printf("%s", COLOR_GRAY);
  printf("%s", s);
  printf("%s", COLOR_RESET);
  printf("\033[u");
}

static void clr_sgst(size_t cmd_len, size_t cmd_idx, size_t sgst_len) {
  char seq[32] = {0};
  size_t cnt_shift = cmd_len - cmd_idx;
  if (cnt_shift > 0) {
    int n = snprintf(seq, sizeof(seq), "\x1b[%luD", cnt_shift);
    HANDLE_WRITE_FAIL_FATAL(0, seq, n, NULL);
  }
  HANDLE_WRITE_FAIL_FATAL(0, "\033[0J", 3, NULL);

  if (cnt_shift > 0) {
    int n = snprintf(seq, sizeof(seq), "\x1b[%luC", cnt_shift);
    HANDLE_WRITE_FAIL_FATAL(0, seq, n, NULL);
  }
}

static t_dllnode *search_history(t_shell *shell, char *cmd, size_t cmd_len,
                                 size_t cmd_idx, t_dllnode *pn) {

  if (cmd_len == 0)
    return NULL;
  else if (cmd_idx != cmd_len)
    return pn;

  t_dllnode *h = shell->history.head;
  while (h) {
    if (strncmp(h->strbg, cmd, strlen(cmd)) == 0)
      return h;
    h = h->next;
  }
  return NULL;
}

void redraw_cmd(t_shell *shell, char *cmd, size_t cmd_len, size_t cmd_idx,
                t_dllnode **suggestion) {
  if (suggestion) {
    *suggestion = search_history(shell, cmd, cmd_len, cmd_idx, *suggestion);
  }
  int rows, cols;
  get_term_size(&rows, &cols);

  static size_t last_rows_drawn = 0;
  size_t old_rows = last_rows_drawn;

  char seq[32];
  if (old_rows > 0) {
    int n = snprintf(seq, sizeof(seq), "\x1b[%luA", old_rows);
    HANDLE_WRITE_FAIL_FATAL(STDOUT_FILENO, seq, n, NULL);
  }
  HANDLE_WRITE_FAIL_FATAL(STDOUT_FILENO, "\r\x1b[J", 4, NULL);

  HANDLE_WRITE_FAIL_FATAL(STDOUT_FILENO, shell->prompt, strlen(shell->prompt),
                          NULL);
  if (cmd_len > 0) {
    HANDLE_WRITE_FAIL_FATAL(STDOUT_FILENO, cmd, cmd_len, cmd);
  }
  if (suggestion && *suggestion) {
    clr_sgst(cmd_len, cmd_idx, strlen((*suggestion)->strbg));
    rndr_sgst(cmd_len, cmd_idx, *suggestion);
  }

  for (int i = cmd_idx; i < cmd_len; i++)
    HANDLE_WRITE_FAIL_FATAL(STDOUT_FILENO, "\033[D", 3, NULL);
  size_t total_len = shell->prompt_len + cmd_len;
  last_rows_drawn = (total_len > 0) ? (total_len - 1) / cols : 0;
}

/**
 * @brief force reallocation of line buffer
 * @return -1 on fail 0 on success.
 *
 * @param buf pointer to array of characters line buffer
 * @param buf_cap capacity of line buffer
 * @param new_buf_cap new capacity of line buffer
 *
 * function is called to force the reallocation of the line buffer to a new
 * buffer capacity.
 */
static int force_realloc_buf(char **buf, size_t *buf_cap, size_t *new_buf_cap) {

  if (!buf || !(*buf) || !buf_cap || !new_buf_cap || *new_buf_cap == 0 ||
      *buf_cap == 0)
    return 0; // bad pass

  if (*new_buf_cap >= MAX_COMMAND_LENGTH) {
    fprintf(stderr, "how did you even get here");
    free(*buf);
    *buf = NULL;
    return -1;
  }

  char *new_buf = realloc(*buf, *new_buf_cap);
  if (!new_buf) {
    perror("bad realloc");
    free(*buf);
    *buf = NULL;
    return -1;
  }

  *buf = new_buf;
  *buf_cap = *new_buf_cap;

  return 1;
}

/**
 * @brief switches termios to VMIN 0, VTIME 1
 * @return -1 on fail 0 on success.
 *
 * @param shell pointer to shell struct
 *
 * Switches VMIN to 0 and VTIME to 1 to allow for 3 byte sequence to arrive for
 * arrow key parsing.
 *
 * @note function used to read arrow key input.
 */
static int temp_switch_termios(t_shell *shell) {

  struct termios temptio = shell->term_ctrl.new_term_settings;

  temptio.c_cc[VMIN] = 0;
  temptio.c_cc[VTIME] = 1;

  return tcsetattr(STDIN_FILENO, TCSANOW, &temptio);
}

static bool firstwrd(char *cmd, size_t cmd_idx) {
  for (size_t i = 0; i < cmd_idx; i++) {
    if (isspace(cmd[i]))
      return false;
  }
  return true;
}

static int realloc_matches(char ***matches, size_t *matches_cap) {
  size_t new_cap = *matches_cap * BUF_GROWTH_FACTOR;

  char **new_matches = realloc(*matches, new_cap * sizeof(char *));
  if (!new_matches) {
    perror("realloc");
    return -1;
  }

  for (size_t i = *matches_cap; i < new_cap; i++) {
    new_matches[i] = NULL;
  }
  *matches = new_matches;
  *matches_cap = new_cap;
  return 0;
}
static void freematches(char **matches) {
  for (size_t i = 0; matches[i]; i++) {
    free(matches[i]);
  }
  free(matches);
}
static void printmatches(char **matches, size_t matches_len) {
  int rows, cols;
  get_term_size(&rows, &cols);

  if (matches_len >= (size_t)rows - 1) {
    char ans = '\0';
    printf("\nmsh: list %lu matches? (y/n) ", matches_len);
    fflush(stdout);
    while (ans != 'y' && ans != 'n') {
      if (read(STDIN_FILENO, &ans, 1) < 0) {
        if (errno == EINTR)
          continue;
        return;
      }
    }
    if (ans == 'n')
      return;
  }

  size_t max_len = 0;
  for (size_t i = 0; matches[i]; i++) {
    size_t len = strlen(matches[i]);
    if (len > max_len)
      max_len = len;
  }

  int col_width = (int)max_len + 3;

  if (col_width > cols)
    col_width = cols;

  int num_cols = cols / col_width;
  if (num_cols == 0)
    num_cols = 1;

  printf("\n");
  for (size_t i = 0; matches[i]; i++) {
    printf("%-*s", col_width, matches[i]);
    if ((i + 1) % num_cols == 0) {
      printf("\n");
    }
  }
  if (matches_len % num_cols != 0) {
    printf("\n");
  }
}
static void tab_sngl(char *cmd, size_t *cmd_len, size_t *cmd_idx,
                     t_shell *shell) {

  if (*cmd_len == 0 || *cmd_idx == 0) {
    return;
  }
  DIR *d;
  struct dirent *ent;
  bool first = firstwrd(cmd, *cmd_idx);

  size_t sfx_len = 0;
  char *match_str = NULL;

  if (first) {
    char *p = getenv("PATH");
    if (!p)
      return;
    char *path = strdup(p);
    if (!path)
      exit(1);

    char **matches = (char **)calloc(sizeof(char *), 16);
    size_t matches_len = 0;
    size_t matches_cap = 16;
    char *t = strtok(path, ":");
    while (t) {
      d = opendir(t);
      if (!d) {
        t = strtok(NULL, ":");
        continue;
      }
      while ((ent = readdir(d)) != NULL) {
        if (strncmp(cmd, ent->d_name, *cmd_idx) == 0) {
          if (matches_len + 1 >= matches_cap &&
              realloc_matches(&matches, &matches_cap) == -1) {
            perror("realloc");
            exit(1);
          }
          matches[matches_len++] = strdup(ent->d_name);
          matches[matches_len] = NULL;
        }
      }
      closedir(d);
      t = strtok(NULL, ":");
    }
    if (matches_len == 1) {
      match_str = matches[0];
      sfx_len = strlen(match_str) - *cmd_idx;

      size_t required_cap = *cmd_len + sfx_len + 1;
      if (required_cap < MAX_COMMAND_LENGTH) {
        memmove(cmd + *cmd_idx + sfx_len, cmd + *cmd_idx,
                *cmd_len - *cmd_idx + 1);
        memcpy(cmd + *cmd_idx, match_str + *cmd_idx, sfx_len);
        *cmd_idx += sfx_len;
        *cmd_len += sfx_len;
      }
    } else if (matches_len > 1) {
      printmatches(matches, matches_len);
    }
    freematches(matches);
    free(path);
  } else {
    size_t lst = 0;
    for (size_t i = 0; i < *cmd_idx; i++) {
      if (cmd[i] == ' ') {
        lst = i;
      }
    }
    bool cwd = true;
    for (size_t i = lst + 1; i < *cmd_idx; i++) {
      if (cmd[i] == '/' && (i == 0 || cmd[i - 1] != '\\')) {
        cwd = false;
        break;
      }
    }
    if (cwd) {
      char *p = &(cmd[lst + 1]);
      size_t p_len = *cmd_idx - (lst + 1);
      char **matches = (char **)calloc(sizeof(char *), 16);
      size_t matches_len = 0;
      size_t matches_cap = 16;
      d = opendir(".");
      if (d) {
        while ((ent = readdir(d)) != NULL) {
          if (strncmp(p, ent->d_name, p_len) == 0) {
            if (matches_len + 1 >= matches_cap &&
                realloc_matches(&matches, &matches_cap) == -1) {
              perror("realloc");
              exit(1);
            }
            matches[matches_len++] = strdup(ent->d_name);
            matches[matches_len] = NULL;
          }
        }
        closedir(d);
      }
      if (matches_len == 1) {
        match_str = matches[0];
        sfx_len = strlen(match_str) - p_len;

        if (*cmd_len + sfx_len + 1 < MAX_COMMAND_LENGTH) {
          memmove(cmd + *cmd_idx + sfx_len, cmd + *cmd_idx,
                  *cmd_len - *cmd_idx + 1);
          memcpy(cmd + *cmd_idx, match_str + p_len, sfx_len);
          *cmd_idx += sfx_len;
          *cmd_len += sfx_len;
        }
      } else if (matches_len > 1) {
        printmatches(matches, matches_len);
      }
      freematches(matches);
    } else {
      char *ptr = &cmd[lst + 1];
      ssize_t k = -1;
      for (size_t i = lst + 1; i < *cmd_idx; i++) {
        if (cmd[i] == '/' && (i == 0 || cmd[i - 1] != '\\'))
          k = i;
      }
      if (k == -1)
        return;

      char *dir_path = strndup(ptr, (k - (lst + 1)) + 1);
      d = opendir(dir_path);
      if (!d) {
        free(dir_path);
        return;
      }
      char *prfx = &cmd[k + 1];
      size_t prfx_len = *cmd_idx - (k + 1);
      char **matches = (char **)calloc(sizeof(char *), 16);
      size_t matches_len = 0;
      size_t matches_cap = 16;
      while ((ent = readdir(d)) != NULL) {
        if (strncmp(prfx, ent->d_name, prfx_len) == 0) {
          if (matches_len + 1 >= matches_cap &&
              realloc_matches(&matches, &matches_cap) == -1) {
            perror("realloc");
            exit(1);
          }
          matches[matches_len++] = strdup(ent->d_name);
          matches[matches_len] = NULL;
        }
      }
      closedir(d);
      if (matches_len == 1) {
        match_str = matches[0];
        sfx_len = strlen(match_str) - prfx_len;

        if (*cmd_len + sfx_len + 1 < MAX_COMMAND_LENGTH) {
          memmove(cmd + *cmd_idx + sfx_len, cmd + *cmd_idx,
                  *cmd_len - *cmd_idx + 1);
          memcpy(cmd + *cmd_idx, match_str + prfx_len, sfx_len);
          *cmd_idx += sfx_len;
          *cmd_len += sfx_len;
        }
      } else if (matches_len > 1) {
        printmatches(matches, matches_len);
      }
      freematches(matches);
      free(dir_path);
    }
  }
}

/**
 * @brief reads user input into cmd
 * @return pointer to array of char, NULL on fail.
 *
 * @param shell pointer to shell struct
 *
 * Function handles backspace logic, arrow keys for history switching, and
 * shifting of characters as needed with proper indexing of charactrs.
 */
char *read_user_inp(t_shell *shell) {

  t_dllnode *ptr = shell->history.head;
  t_dllnode *suggestion_node = NULL;

  size_t cmd_cap = INITIAL_COMMAND_LENGTH;
  char *cmd = (char *)malloc(sizeof(char) * INITIAL_COMMAND_LENGTH);
  if (!cmd) {
    perror("malloc fail: buf read user inp");
    return NULL;
  }
  cmd[0] = '\0';

  size_t cmd_len = 0;
  size_t cmd_idx = 0;

  bool tab = false;
  while (1) {

    redraw_cmd(shell, cmd, cmd_len, cmd_idx, &suggestion_node);

    if (handle_realloc_buf(&cmd, &cmd_cap, &cmd_len) == -1)
      return NULL;
    if (sigwinch_flag) {
      sigwinch_flag = 0;
      continue;
    }

    char c = '\0';
    while (read(0, &c, 1) < 0) {
      if (errno == EINTR) {
        if (sigwinch_flag) {
          sigwinch_flag = 0;
          continue;
        }
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      } else {
        free(cmd);
        return NULL;
      }
    }

    if (c == '\t' && !tab) {
      tab_sngl(cmd, &cmd_len, &cmd_idx, shell);
      tab = true;
      continue;
    } else if (c == '\t') {
      // tab_dbl(cmd, cmd_idx, shell);
      tab = false;
      continue;
    }

    if (c == '\x1b') {
      temp_switch_termios(shell);
      char seq_end[2] = {0};

      while (read(0, &seq_end, 2) < 0) {
        if (errno == EINTR) {
          if (sigwinch_flag) {
            sigwinch_flag = 0;
            continue;
          }
          continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;
        } else {
          free(cmd);
          return NULL;
        }
      }

      if (tcsetattr(STDIN_FILENO, TCSANOW,
                    &(shell->term_ctrl.new_term_settings)) == -1) {
        perror("fail to reset terminal");
        free(cmd);
        cmd = NULL;
        return cmd;
      }

      if (seq_end[0] == '[') {
        switch (seq_end[1]) {
        case 'A': {

          if (!ptr)
            ptr = shell->history.head;
          else if (ptr->next)
            ptr = ptr->next;
          else
            continue;

          size_t n_cap = strlen(ptr->strbg) + 1;
          if (n_cap > cmd_cap) {
            if (force_realloc_buf(&cmd, &cmd_cap, &n_cap) == -1)
              return NULL;
          }

          strcpy(cmd, ptr->strbg);
          cmd_idx = cmd_len = n_cap - 1;
          continue;
        }
        case 'B': {

          if (!ptr)
            ptr = shell->history.head;
          else if (ptr->prev)
            ptr = ptr->prev;
          else
            continue;

          size_t n_cap = strlen(ptr->strbg) + 1;
          if (n_cap > cmd_cap) {
            if (force_realloc_buf(&cmd, &cmd_cap, &n_cap) == -1)
              return NULL;
          }

          strcpy(cmd, ptr->strbg);
          cmd_idx = cmd_len = n_cap - 1;
          continue;
        }
        case 'C': {
          if (cmd_idx == cmd_len && suggestion_node) {
            size_t slen = strlen(suggestion_node->strbg);

            if (cmd_cap < slen + 1) {
              size_t new_cap = slen + 1;
              if (force_realloc_buf(&cmd, &cmd_cap, &new_cap) == -1)
                return NULL;
            }

            memcpy(cmd, suggestion_node->strbg, slen + 1);
            cmd_idx = slen;
            cmd_len = slen;
          } else if (cmd_idx < cmd_len) {
            cmd_idx++;
          }

          continue;
        }
        case 'D': {
          if (cmd_idx > 0) {
            cmd_idx--;
          }

          continue;
        }
        default:
          continue;
        }
      } else {
        continue;
      }
    }

    if (c == '\b' || c == 127) {

      if (cmd_idx == 0)
        continue;
      else if (cmd_idx < cmd_len && cmd_idx != 0) {
        size_t count_shift_back = cmd_len - cmd_idx;
        memmove(cmd + cmd_idx - 1, cmd + cmd_idx, count_shift_back);
      }
      cmd_len--;
      cmd_idx--;
      cmd[cmd_len] = '\0';
    }

    if (c == '\n' || c == '\r') {
      for (size_t i = cmd_idx; i <= cmd_len; i++)
        printf("\033[C");
      printf("\033[0J");
      fflush(stdout);
      break;
    }
    if (cmd_cap < MAX_COMMAND_LENGTH - 1 && isprint(c)) {
      size_t count_shift = cmd_len - cmd_idx;
      if (cmd_cap <= cmd_len + 1) {
        size_t n_cap = cmd_cap * BUF_GROWTH_FACTOR;
        if (force_realloc_buf(&cmd, &cmd_cap, &n_cap) == -1)
          return NULL;
      }

      memmove(cmd + cmd_idx + 1, cmd + cmd_idx, count_shift);
      cmd[cmd_idx++] = c;
      cmd_len++;
      cmd[cmd_len] = '\0';
      tab = false;
    }
  }

  return cmd;
}
