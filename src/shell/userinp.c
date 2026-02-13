#include "userinp.h"
#include <unistd.h>

typedef struct s_completions {
  char **matches;
  size_t count;
  size_t prefix_len;
} t_completions;

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

static void tty_write(int fd, char *s) {
  if (s)
    HANDLE_WRITE_FAIL_FATAL(fd, s, strlen(s), NULL);
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
  char a[32];
  snprintf(a, sizeof(a), "\033[s");
  tty_write(STDOUT_FILENO, a);
  snprintf(a, sizeof(a), "%s", COLOR_GRAY);
  tty_write(STDOUT_FILENO, a);
  tty_write(STDOUT_FILENO, s);
  snprintf(a, sizeof(a), "%s", COLOR_RESET);
  tty_write(STDOUT_FILENO, a);
  snprintf(a, sizeof(a), "\033[u");
  tty_write(STDOUT_FILENO, a);
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
    if (*suggestion) {
      size_t slen = strlen((*suggestion)->strbg);
      clr_sgst(cmd_len, cmd_idx, slen);
    }
  }
  int rows, cols;
  get_term_size(&rows, &cols);

  static size_t last_rows_drawn = 0;

  char a[32] = {0};
  if (last_rows_drawn > 0) {
    snprintf(a, sizeof(a), "\033[%zuA", last_rows_drawn);
    tty_write(STDOUT_FILENO, a);
  }
  snprintf(a, sizeof(a), "\r\033[0J");
  tty_write(STDOUT_FILENO, a);

  tty_write(STDOUT_FILENO, shell->prompt);
  tty_write(STDOUT_FILENO, cmd);

  size_t total_len = shell->prompt_len + cmd_len;
  size_t target_pos = shell->prompt_len + cmd_idx;

  size_t current_row = (total_len > 0) ? (total_len - 1) / cols : 0;
  size_t target_row = (target_pos > 0) ? (target_pos - 1) / cols : 0;
  size_t target_col = target_pos % cols;

  if (current_row > target_row) {
    snprintf(a, sizeof(a), "\033[%zuA", current_row - target_row);
    tty_write(STDOUT_FILENO, a);
  }

  snprintf(a, sizeof(a), "\r");
  tty_write(STDOUT_FILENO, a);
  if (target_col > 0) {
    snprintf(a, sizeof(a), "\x1b[%zuC", target_col);
    tty_write(STDOUT_FILENO, a);
  }

  last_rows_drawn = target_row;
  fflush(stdout);

  if (suggestion && *suggestion) {
    rndr_sgst(cmd_len, cmd_idx, *suggestion);
  }
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
 * Switches VMIN to 0 and VTIME to 1 to allow for 3 byte sequence to arrive
 * for arrow key parsing.
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
static size_t printmatches(char **matches, size_t matches_len) {
  int rows, cols;
  get_term_size(&rows, &cols);

  size_t rows_printed = 0;

  if (matches_len >= (size_t)rows - 1) {
    char ans = '\0';
    printf("\nmsh: list %lu matches? (y/n) ", matches_len);
    while (ans != 'y' && ans != 'n') {
      if (read(STDIN_FILENO, &ans, 1) < 0) {
        if (errno == EINTR)
          continue;
        return -1;
      }
    }
    if (ans == 'n') {
      printf("\033[A");
      printf("\r\033[J");
      return 0;
    }
  }
  rows_printed++;

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
  rows_printed++;

  for (size_t i = 0; matches[i]; i++) {
    printf("%-*s", col_width, matches[i]);
    if ((i + 1) % num_cols == 0) {
      rows_printed++;
      printf("\n");
    }
  }
  if (matches_len % num_cols != 0) {
    printf("\n");
    rows_printed++;
  }

  return rows_printed;
}

static t_completions get_matches(char *cmd, size_t cmd_idx) {

  t_completions res = {NULL, 0, 0};
  bool first = firstwrd(cmd, cmd_idx);

  res.matches = calloc(16, sizeof(char *));
  size_t cap = 16;

  if (first) {
    res.prefix_len = cmd_idx;
    char *env_path = getenv("PATH");
    if (!env_path)
      return res;
    char *path_copy = strdup(env_path);
    char *dir = strtok(path_copy, ":");
    while (dir) {
      DIR *d = opendir(dir);
      struct dirent *ent;
      if (d) {
        while ((ent = readdir(d))) {
          if (strncmp(ent->d_name, cmd, cmd_idx) == 0) {
            if (res.count + 1 >= cap)
              realloc_matches(&res.matches, &cap);
            res.matches[res.count++] = strdup(ent->d_name);
          }
        }
        closedir(d);
      }
      dir = strtok(NULL, ":");
    }
    free(path_copy);
  } else {
    bool skip_hidden = true;
    size_t last_space = 0;
    for (size_t i = 0; i < cmd_idx; i++)
      if (cmd[i] == ' ')
        last_space = i + 1;

    char *path_part = strndup(cmd + last_space, cmd_idx - last_space);
    char *slash = strrchr(path_part, '/');

    char *search_dir =
        slash ? strndup(path_part, (slash - path_part) + 1) : strdup(".");
    char *search_prefix = slash ? slash + 1 : path_part;
    res.prefix_len = strlen(search_prefix);
    for (size_t i = 0; i <= res.prefix_len; i++) {
      if (search_prefix[i] == '.') {
        skip_hidden = false;
        break;
      }
    }
    DIR *d = opendir(search_dir);
    if (d) {
      struct dirent *ent;
      while ((ent = readdir(d))) {

        if (ent->d_name[0] == '.' && skip_hidden)
          continue;

        if (strncmp(ent->d_name, search_prefix, res.prefix_len) == 0) {
          if (res.count + 1 >= cap)
            realloc_matches(&res.matches, &cap);
          res.matches[res.count++] = strdup(ent->d_name);
        }
      }
      closedir(d);
    }
    free(path_part);
    free(search_dir);
  }
  return res;
}

static void append_completion(char *cmd, size_t *cmd_len, size_t *cmd_idx,
                              char *match, size_t prefix_len) {

  size_t suffix_len = strlen(match) - prefix_len;
  memmove(cmd + *cmd_idx + suffix_len, cmd + *cmd_idx, *cmd_len - *cmd_idx + 1);
  memcpy(cmd + *cmd_idx, match + prefix_len, suffix_len);

  *cmd_idx += suffix_len;
  *cmd_len += suffix_len;
}
static size_t tab_sngl(char *cmd, size_t *cmd_len, size_t *cmd_idx,
                       t_shell *shell) {
  if (*cmd_len == 0 || *cmd_idx == 0)
    return 0;

  size_t rows_printed = 0;
  t_completions comp = get_matches(cmd, *cmd_idx);

  if (comp.count == 1) {
    append_completion(cmd, cmd_len, cmd_idx, comp.matches[0], comp.prefix_len);
  } else if (comp.count > 1) {
    rows_printed = printmatches(comp.matches, comp.count);
  }
  freematches(comp.matches);

  return rows_printed;
}

static void tab_dbl(char *cmd, size_t *cmd_len, size_t *cmd_idx, t_shell *shell,
                    size_t rws_clr) {

  size_t clr_ofst = *cmd_len / shell->cols;
  if (clr_ofst == 0)
    rws_clr--;
  for (size_t i = 0; i < rws_clr; i++) {
    printf("\033[A");
  }
  printf("\r\033[J");
  fflush(stdout);

  t_completions c = get_matches(cmd, *cmd_idx);
  if (c.count == 0) {
    freematches(c.matches);
    return;
  } else if (c.count == 1) {
    append_completion(cmd, cmd_len, cmd_idx, c.matches[0], c.prefix_len);
    freematches(c.matches);
    return;
  }

  size_t s = 0;
  size_t row_offset = 0;

  while (1) {

    redraw_cmd(shell, cmd, *cmd_len, *cmd_idx, NULL);
    printf("\033[s");

    int rows, cols;
    get_term_size(&rows, &cols);

    size_t v_rws = (rows > 2) ? rows - 2 : 1;

    size_t max_l = 0;
    for (size_t i = 0; i < c.count; i++) {
      size_t l = strlen(c.matches[i]);
      if (l > max_l)
        max_l = l;
    }

    int col_w = (int)max_l + 2;
    int n_cols = cols / col_w;
    if (n_cols == 0)
      n_cols = 1;

    size_t items_per_page = v_rws * n_cols;

    if (row_offset + items_per_page > c.count)
      row_offset = (c.count > items_per_page) ? c.count - items_per_page : 0;

    printf("\n");

    size_t start = row_offset;
    size_t end = row_offset + items_per_page;
    if (end > c.count)
      end = c.count;

    size_t printed = 0;
    for (size_t i = start; i < end; i++) {

      if (i == s) {
        printf("\x1b[7m%-*s\x1b[0m", col_w, c.matches[i]);
      } else {
        printf("%-*s", col_w, c.matches[i]);
      }

      printed++;
      if (printed == (size_t)n_cols) {
        printf("\n");
        printed = 0;
      }
    }

    if (printed != 0)
      printf("\n");

    size_t vis_items = end - start;
    size_t mov_lines = (vis_items + n_cols - 1) / n_cols;

    printf("\x1b[%dA\r", (int)mov_lines + 1);
    for (size_t i = 0; i < shell->prompt_len + *cmd_idx; i++)
      printf("\x1b[C");

    char r;
    while (read(STDIN_FILENO, &r, 1) < 0) {
      if (errno != EINTR)
        exit(1);
    }

    if (r == '\t') {
      s = (s + 1) % c.count;
      if (s >= row_offset + items_per_page)
        row_offset++;
    } else if (r == '\r' || r == '\n') {
      append_completion(cmd, cmd_len, cmd_idx, c.matches[s], c.prefix_len);
      freematches(c.matches);
      tcsetattr(STDIN_FILENO, TCSANOW, &shell->term_ctrl.new_term_settings);
      return;
    } else if (r == '\x1b') {
      temp_switch_termios(shell);
      char seq[2];
      while (read(STDIN_FILENO, seq, 2) < 0) {
        if (errno != EINTR)
          exit(1);
      }
      tcsetattr(STDIN_FILENO, TCSANOW, &shell->term_ctrl.new_term_settings);

      if (seq[0] == '[') {
        switch (seq[1]) {
        case 'A':
          if (s < (size_t)n_cols)
            s = c.count - (c.count % n_cols ? c.count % n_cols : n_cols);
          else
            s -= n_cols;

          if (s < row_offset)
            row_offset = (s >= (size_t)n_cols) ? s - n_cols : 0;
          break;

        case 'B':
          s = (s + n_cols) % c.count;
          if (s >= row_offset + items_per_page)
            row_offset += n_cols;
          break;

        case 'C':
          s = (s + 1) % c.count;
          if (s >= row_offset + items_per_page)
            row_offset++;
          break;

        case 'D':
          s = (s == 0) ? c.count - 1 : s - 1;
          if (s < row_offset)
            row_offset = s;
          break;
        }
      } else {
        break;
      }
    } else {
      break;
    }

    printf("\033[J");
  }

  printf("\033[J");
  freematches(c.matches);
  tcsetattr(STDIN_FILENO, TCSANOW, &shell->term_ctrl.new_term_settings);
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
  size_t tab_rws = 0;

  size_t cmd_cap = INITIAL_COMMAND_LENGTH;
  char *cmd = (char *)arena_alloc(&shell->arena, INITIAL_COMMAND_LENGTH);
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
      tab_rws = tab_sngl(cmd, &cmd_len, &cmd_idx, shell);
      if (tab_rws != -1 && tab_rws == 0) {
        tab = false;
        continue;
      } else {
        tab = true;
        continue;
      }
    } else if (c == '\t') {
      tab_dbl(cmd, &cmd_len, &cmd_idx, shell, tab_rws);
      tab = false;
      tab_rws = 0;
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
          if (!ptr && shell->history.head)
            ptr = shell->history.head;
          else if (!shell->history.head)
            continue;

          size_t n_cap = strlen(ptr->strbg) + 1;
          if (n_cap > cmd_cap) {
            if (force_realloc_buf(&cmd, &cmd_cap, &n_cap) == -1)
              return NULL;
          }

          strcpy(cmd, ptr->strbg);
          cmd_idx = cmd_len = n_cap - 1;

          ptr = ptr->next;
          continue;
        }
        case 'B': {

          if (!ptr && shell->history.tail)
            ptr = shell->history.tail;
          else if (!shell->history.tail)
            continue;

          size_t n_cap = strlen(ptr->strbg) + 1;
          if (n_cap > cmd_cap) {
            if (force_realloc_buf(&cmd, &cmd_cap, &n_cap) == -1)
              return NULL;
          }

          strcpy(cmd, ptr->strbg);
          cmd_idx = cmd_len = n_cap - 1;

          ptr = ptr->prev;
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
      redraw_cmd(shell, cmd, cmd_len, cmd_idx, NULL);
      fflush(stdin);
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
      tab_rws = 0;
    }
  }

  return cmd;
}
