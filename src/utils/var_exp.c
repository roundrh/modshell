#include "var_exp.h"
#include <stdlib.h>

static const t_exp_map g_jump_table[] = {
    {"?", expand_exit_status}, // $?
    {"$", expand_pid},         // $$
    {"((", expand_arith},      // $((
    {"(", expand_subshell},    // $(
    {"{", expand_braces},      // ${
    {"@", expand_args},        // $@
    {"*", expand_args},        // @*
    {"#", expand_args},        // $#
    {0, NULL}                  // Null terminator
};

/* Forward declerations */
static long long parse_arith_primary(const char **p, t_err_type *err);
static long long parse_arith_unary(const char **p, t_err_type *err);
static long long parse_arith_muldiv(const char **p, t_err_type *err);
static long long parse_arith_addsub(const char **p, t_err_type *err);
static long long parse_arith_exprsh(const char **p, t_err_type *err);
static long long parse_arith_number(const char **p, t_err_type *err);

static char **expand_fields(t_shell *shell, char **src_str, int *depth);

char *getenv_local(char **env, const char *var_name) {

  if (!env || !var_name || !*var_name)
    return NULL;

  char *result = NULL;
  size_t key_len = strlen(var_name);

  for (int i = 0; env[i]; i++) {
    if (strncmp(env[i], var_name, key_len) == 0 && env[i][key_len] == '=') {
      return (result = strdup(env[i] + key_len + 1));
    }
  }
  return result;
}
int getenv_local_idx(char **env, const char *var_name) {

  if (!env || !var_name || !*var_name)
    return -1;

  int result = -1;
  size_t key_len = strlen(var_name);

  for (int i = 0; env[i]; i++) {
    if (strncmp(env[i], var_name, key_len) == 0 && env[i][key_len] == '=') {
      return i;
    }
  }
  return result;
}

static int realloc_argv(char ***argv, size_t *argv_cap) {

  size_t new_cap = *argv_cap * BUF_GROWTH_FACTOR;

  char **new_argv = realloc(*argv, new_cap * sizeof(char *));
  if (!new_argv)
    return -1;

  for (int i = *argv_cap; i < new_cap; i++)
    new_argv[i] = NULL;

  *argv = new_argv;
  *argv_cap = new_cap;

  return 0;
}

int add_to_env(t_shell *shell, char *var, char *val) {

  if (!shell || !var || !val)
    return -1;

  if (shell->env_count + 1 >= shell->env_cap) {
    if (realloc_argv(&shell->env, &shell->env_cap) == -1) {
      perror("realloc");
      return -1;
    }
  }

  size_t size_env_var = strlen(var) + strlen(val) + 1 + 1; ///< +1 '=' & +1 '\0'
  char *env_var = (char *)malloc(sizeof(char) * size_env_var);
  if (!env_var) {
    perror("41: fatal malloc env_var");
    return -1;
  }

  snprintf(env_var, size_env_var, "%s=%s", var, val);
  int idx = -1;
  if ((idx = getenv_local_idx(shell->env, var)) == -1) {

    if (shell->env_count - 1 >= shell->env_cap) {
      if (realloc_argv(&shell->env, &shell->env_cap) == -1) {
        perror("realloc");
        exit(EXIT_FAILURE);
      }
    }
    shell->env[shell->env_count++] = env_var;
    shell->env[shell->env_count] = NULL;
  } else {
    free(shell->env[idx]);
    shell->env[idx] = env_var;
  }
  return 0;
}

static int realloc_buf(char **buffer, size_t *cap) {

  size_t new_cap = *cap * BUF_GROWTH_FACTOR;
  char *new_buf = realloc(*buffer, new_cap);
  if (!new_buf) {
    perror("realloc buf");
    return -1;
  }

  *cap = new_cap;
  *buffer = new_buf;
  return 0;
}

static void cleanup_argv(char **argv) {

  if (argv == NULL)
    return;

  int i = 0;
  while (argv[i] != NULL) {

    free(argv[i]);
    argv[i] = NULL;

    i++;
  }

  free(argv);
}
static void cleanup_splits(char **argv) {
  if (argv == NULL)
    return;

  if (argv[0] != NULL)
    free(argv[0]);
  free(argv);
}

static char *get_ifs(t_shell *shell) {

  char *ifs = getenv_local(shell->env, "IFS");
  if (!ifs && (ifs = getenv("IFS")) == NULL) {
    char ifs_loc[] = {' ', '\t', '\n', '\0'};
    return strdup(ifs_loc);
  }
  return ifs;
}

static int is_ifs_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n';
}
static size_t count_sep(const char *src, unsigned char *sep) {
  size_t count = 0;
  const char *p = src;

  while (*p && sep[(unsigned char)*p] && is_ifs_whitespace(*p))
    p++;

  if (!*p)
    return 0;

  while (*p) {
    count++;
    while (*p && !sep[(unsigned char)*p])
      p++;
    if (!*p)
      break;

    if (!is_ifs_whitespace(*p)) {
      p++;
      if (*p == '\0' || sep[(unsigned char)*p])
        count++;
    } else {
      while (*p && sep[(unsigned char)*p] && is_ifs_whitespace(*p))
        p++;
    }
  }
  return count;
}
static char **separate_ifs(t_shell *shell, const char *src) {

  char *ifs = get_ifs(shell);
  unsigned char sep[256] = {0};
  for (int i = 0; ifs[i]; i++)
    sep[(unsigned char)ifs[i]] = 1;

  size_t m = count_sep(src, sep);
  char **argv = malloc(sizeof(char *) * (m + 1));
  if (!argv)
    return NULL;

  char *sc = strdup(src);
  char *p = sc;
  size_t argc = 0;

  while (*p && sep[(unsigned char)*p] && is_ifs_whitespace(*p))
    p++;

  while (*p) {

    argv[argc++] = p;
    while (*p && !sep[(unsigned char)*p])
      p++;

    if (!*p)
      break;
    char delimiter = *p;
    *p = '\0';
    p++;

    if (is_ifs_whitespace(delimiter)) {
      while (*p && sep[(unsigned char)*p] && is_ifs_whitespace(*p))
        p++;
    } else {
      if (!*p || sep[(unsigned char)*p]) {
        argv[argc++] = p;
      }
    }
  }

  argv[argc] = NULL;
  free(ifs);
  return argv;
}
static char *strip_quotes(const char *src) {
  if (!src)
    return NULL;

  size_t len = strlen(src);
  char *dst = malloc(len + 1);
  if (!dst)
    return NULL;

  size_t di = 0;
  bool in_single = false;
  bool in_double = false;

  for (size_t si = 0; si < len; si++) {
    if (src[si] == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (src[si] == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (src[si] == '\\' && !in_single) {
      if (si + 1 < len) {
        si++;
      }
    }

    dst[di++] = src[si];
  }

  dst[di] = '\0';
  return dst;
}

static char *make_word(const t_token *tokens, size_t *idx,
                       const size_t segment_len) {
  char *buf = calloc(1, BUFFER_INITIAL_LEN);
  if (!buf)
    return NULL;

  size_t buf_cap = BUFFER_INITIAL_LEN;
  size_t buf_len = 0;

  bool paren = false;
  int paren_depth = 0;

  bool in_brace = false;
  int brace_depth = 0;

  bool space = false;

  while (*idx < segment_len) {
    const t_token *crtok = &tokens[*idx];

    if (!*(crtok->start))
      break;

    while (buf_len + crtok->len + 2 > buf_cap) {
      if (realloc_buf(&buf, &buf_cap) == -1) {
        free(buf);
        return NULL;
      }
    }

    if (crtok->len == 1 && crtok->start[0] == '(') {
      paren_depth++;
      paren = true;
      space = false;
    } else if (crtok->len == 1 && crtok->start[0] == ')') {
      paren_depth--;
      if (paren_depth == 0) {
        paren = false;
      }
      space = false;
    }

    for (size_t i = 0; i < crtok->len; i++) {
      char c = crtok->start[i];

      if (c == '{') {
        brace_depth++;
        in_brace = true;
        space = false;
      } else if (c == '}') {
        brace_depth--;
        if (brace_depth == 0) {
          in_brace = false;
        }
        space = false;
      }

      if ((in_brace || paren) && space) {
        buf[buf_len++] = ' ';
        space = false;
      }

      buf[buf_len++] = c;
    }
    buf[buf_len] = '\0';

    char *p = crtok->start + crtok->len;

    if (!paren && !in_brace &&
        (*p == ' ' || *p == '\t' || *p == '\n' || *p == '|' || *p == '&' ||
         *p == ';' || *p == '<' || *p == '>' || *p == '\0')) {
      (*idx)++;
      break;
    }

    if (paren || in_brace) {
      if (*(crtok->start + crtok->len) == ' ' ||
          *(crtok->start + crtok->len) == '\t' ||
          *(crtok->start + crtok->len) == '\n') {
        space = true;
      }
    }

    (*idx)++;
  }

  return buf;
}

static t_exp_handler find_handler(const char *c) {

  if (isdigit(*c))
    return expand_args;

  for (int i = 0; g_jump_table[i].trigger; i++) {
    size_t tlen = strlen(g_jump_table[i].trigger);
    if (strncmp(g_jump_table[i].trigger, c, tlen) == 0)
      return g_jump_table[i].handler;
  }
  return expand_var;
}

char *expand_exit_status(t_shell *shell, const char *src, size_t *i) {

  (void)src;
  (*i)++;

  char status[32];
  snprintf(status, sizeof(status), "%d", shell->last_exit_status);
  char *res = strdup(status);
  if (!res) {
    perror("strdup fail");
    return NULL;
  }

  return res;
}

static int expand_glob(char ***argv) {

  size_t cap = ARGV_INITIAL_LEN, argc = 0;
  char **new_argv = calloc(cap, sizeof(char *));
  if (!new_argv)
    return -1;

  for (int i = 0; (*argv)[i]; i++) {

    char **add = NULL;
    size_t len = 0;
    bool mallcd = false;

    if (strpbrk((*argv)[i], "*?[]")) {
      glob_t g;
      if (glob((*argv)[i], GLOB_NOCHECK, NULL, &g) == 0) {
        len = g.gl_pathc;
        add = malloc(len * sizeof(char *));
        for (size_t j = 0; j < g.gl_pathc; j++)
          add[j] = strdup(g.gl_pathv[j]);
        globfree(&g);
      }
      mallcd = true;
      free((*argv)[i]);
    }

    if (!add) {
      add = &(*argv)[i];
      len = 1;
    }

    if (argc + len >= cap) {
      while (argc + len >= cap)
        cap *= 2;
      new_argv = realloc(new_argv, cap * sizeof(char *));
    }

    for (size_t j = 0; j < len; j++)
      new_argv[argc++] = add[j];

    if (mallcd) {
      free(add);
      add = NULL;
    }
  }

  new_argv[argc] = NULL;
  free(*argv);
  *argv = new_argv;
  return 0;
}

char *expand_args(t_shell *shell, const char *src, size_t *i) {

  if (isdigit(src[*i])) {
    char r[32];
    int m = 0;

    while (isdigit(src[*i]) && m < 31) {
      r[m++] = src[(*i)++];
    }
    r[m] = '\0';

    int s = atoi(r);
    if (s >= 1 && s <= shell->argc)
      return strdup(shell->argv[s - 1]);

    return NULL;
  }

  char *r;
  size_t n;
  int k;

  (*i)++;

  switch (src[*i - 1]) {
  case '*':
    n = 0;
    for (k = 0; k < shell->argc; k++)
      n += strlen(shell->argv[k]) + 1;

    r = malloc(n + 3);
    if (!r)
      return NULL;

    char *p = r;
    *p++ = '"';

    for (k = 0; k < shell->argc; k++) {
      size_t len = strlen(shell->argv[k]);
      memcpy(p, shell->argv[k], len);
      p += len;
      *p++ = ' ';
    }

    if (*(p - 1) == ' ')
      p--;

    *p++ = '"';
    *p = '\0';
    return r;

  case '@':
    n = 0;
    for (k = 0; k < shell->argc; k++)
      n += strlen(shell->argv[k]) + 1;

    r = malloc(n + 1);
    if (!r)
      return NULL;

    r[0] = '\0';
    for (k = 0; k < shell->argc; k++) {
      strcat(r, shell->argv[k]);
      strcat(r, " ");
    }

    if (n && r[n - 1] == ' ')
      r[n - 1] = '\0';

    return r;

  case '#':
    r = malloc(32);
    if (!r)
      return NULL;

    snprintf(r, 32, "%d", shell->argc);
    return r;
  }

  return NULL;
}

char *expand_pid(t_shell *shell, const char *src, size_t *i) {

  (void)src;
  (*i)++;

  char pid[32];
  snprintf(pid, sizeof(pid), "%d", shell->pgid);
  char *res = strdup(pid);
  if (!res) {
    perror("strdup fail");
    return NULL;
  }

  return res;
}

static void skip_spaces(const char **p) {
  while (**p && (**p == ' ' || **p == '\t'))
    (*p)++;
}

static long long parse_arith_pow(const char **p, t_err_type *err) {

  long long left = parse_arith_unary(p, err);
  if (*err != err_none)
    return 0;

  skip_spaces(p);

  if ((*p)[0] == '*' && (*p)[1] == '*') {
    (*p) += 2;

    long long right = parse_arith_pow(p, err);
    if (*err != err_none)
      return 0;

    if (right < 0) {
      return 0;
    }

    long long res = 1;
    for (long long i = 0; i < right; i++) {
      res *= left;
    }
    return res;
  }

  return left;
}

static long long parse_arith_primary(const char **p, t_err_type *err) {

  skip_spaces(p);

  if (**p == '(') {
    (*p)++;
    long long val = parse_arith_exprsh(p, err);
    skip_spaces(p);
    if (**p != ')') {
      *err = err_syntax;
      return 0;
    }
    (*p)++;
    return val;
  }

  if (isdigit(**p)) {
    return parse_arith_number(p, err);
  }

  *err = err_syntax;
  return 0;
}
static long long parse_arith_unary(const char **p, t_err_type *err) {
  skip_spaces(p);
  if (**p == '+') {
    (*p)++;
    return parse_arith_unary(p, err);
  }
  if (**p == '-') {
    (*p)++;
    return -parse_arith_unary(p, err);
  }
  return parse_arith_primary(p, err);
}
static long long parse_arith_muldiv(const char **p, t_err_type *err) {

  long long left = parse_arith_pow(p, err);
  if (*err != err_none)
    return 0;

  skip_spaces(p);
  while (**p == '*' || **p == '/' || **p == '%') {
    char op = **p;
    (*p)++;

    long long right = parse_arith_primary(p, err);
    if (*err != err_none)
      return 0;

    if (op == '*') {
      left *= right;
    } else {
      if (right == 0) {
        *err = err_div_zero;
        return 0;
      }
      if (op == '/')
        left /= right;
      else
        left %= right;
    }
    skip_spaces(p);
  }
  return left;
}
static long long parse_arith_addsub(const char **p, t_err_type *err) {

  long long left = parse_arith_muldiv(p, err);
  if (*err != err_none)
    return 0;

  skip_spaces(p);
  while (**p == '+' || **p == '-') {
    char op = **p;
    (*p)++;
    long long right = parse_arith_muldiv(p, err);

    if (op == '+')
      left += right;
    else
      left -= right;

    skip_spaces(p);
  }
  return left;
}
static long long parse_arith_exprsh(const char **p, t_err_type *err) {

  if (!p || !*p || !**p)
    return 0;

  long long result = parse_arith_addsub(p, err);

  skip_spaces(p);
  if (**p != '\0' && **p != ')' && *err == err_none) {
    *err = err_syntax;
  }

  return result;
}
static long long parse_arith_number(const char **p, t_err_type *err) {

  char *endptr;
  long long val = strtoll(*p, &endptr, 10);

  if (*p == endptr) {
    *err = err_syntax;
    return 0;
  }

  *p = endptr;
  return val;
}

static bool is_valid_var_char(char c, bool first_char) {
  if (first_char) {
    return isalpha(c) || c == '_';
  } else {
    return isalnum(c) || c == '_';
  }
}

static char *extract_arith(const char *src, size_t *i, t_err_type *err) {
  size_t idx = *i + 1;
  int depth = 1;
  size_t len = 0;

  while (src[idx] && depth > 0) {
    if (src[idx] == '(' && src[idx + 1] == '(') {
      depth++;
      idx += 2;
      len += 2;
    } else if (src[idx] == ')' && src[idx + 1] == ')') {
      depth--;
      if (depth == 0) {
        break;
      }
      idx += 2;
      len += 2;
    } else {
      idx++;
      len++;
    }
  }

  if (depth != 0) {
    *err = err_syntax;
    return NULL;
  }

  char *eqn_buf = strndup(&src[*i + 1], len);

  *i = idx + 2;
  return eqn_buf;
}

char *expand_arith(t_shell *shell, const char *src, size_t *i) {

  t_err_type err = err_none;
  char *raw_eqn_buf = extract_arith(src, i, &err);
  if (err == err_syntax) {
    fprintf(stderr, "\nmsh: invalid arith syntax\n");
    return strdup("");
  }
  if (!raw_eqn_buf) {
    perror("malloc fail");
    return NULL;
  }
  if (strcmp(raw_eqn_buf, "") == 0)
    return raw_eqn_buf;

  int f_depth = 1;
  char **fields = expand_fields(shell, &raw_eqn_buf, &f_depth);
  free(raw_eqn_buf);

  char *expanded = fields && fields[0] ? strdup(fields[0]) : strdup("");
  cleanup_argv(fields);

  /* evaluate arithmetic */
  const char *p = expanded;
  long long rs = parse_arith_exprsh(&p, &err);

  char *result = NULL;
  if (err == err_syntax) {
    fprintf(stderr, "\nmsh: invalid arith syntax\n");
    result = strdup("");
  } else if (err == err_div_zero) {
    fprintf(stderr, "\nmsh: div by zero\n");
    result = strdup("0");
  } else if (err == err_overflow) {
    fprintf(stderr, "\nmsh: longlong overflow\n");
    result = strdup("0");
  } else {
    result = malloc(32);
    if (result)
      snprintf(result, 32, "%lld", rs);
  }

  free(expanded);
  return result;
}

static size_t skip_alnum_us(const char **p) {

  size_t len = 0;
  while ((isalnum(**p) || **p == '_') && **p != '\0') {
    (*p)++;
    len++;
  }
  return len;
}

static t_param_op get_param_op(t_shell *shell, const char *src) {

  const char *op = src;
  if (*op == '#')
    return PARAM_OP_LEN;
  size_t comparator = skip_alnum_us(&op);
  size_t len = strlen(src);
  if (comparator == len) {
    return PARAM_OP_NONE;
  } else {
    if (*op == ':' && *(op + 1) == '-')
      return PARAM_OP_MINUS;
    if (*op == ':' && *(op + 1) == '=')
      return PARAM_OP_EQUAL;
    if (*op == ':' && *(op + 1) == '+')
      return PARAM_OP_PLUS;
    if (*op == ':' && *(op + 1) == '?')
      return PARAM_OP_QUESTION;
    if (*op == '#' && *(op + 1) == '#')
      return PARAM_OP_HASH_HASH;
    if (*op == '%' && *(op + 1) == '%')
      return PARAM_OP_PERCENT_PERCENT;
    if (*op == '#')
      return PARAM_OP_HASH;
    if (*op == ':')
      return PARAM_OP_COLON;
    if (*op == '%')
      return PARAM_OP_PERCENT;
  }
  return PARAM_OP_ERR;
}

static char *expand_tilde(char *src) {

  if (src[1] == '\0' || src[1] == '/') {

    const char *home = getenv("HOME");
    if (!home)
      return NULL;

    size_t len = strlen(home) + strlen(src);
    char *buf = malloc(len + 1);
    if (!buf) {
      perror("malloc fatal");
      return NULL;
    }
    strcpy(buf, home);
    strcat(buf, src + 1);

    return buf;
  }

  char *p = src + 1;
  char uname[LOGIN_NAME_MAX];
  size_t i = 0;
  while (isalnum(*p) || *p == '_' || *p == '.' || *p == '-')
    uname[i++] = *p++;
  uname[i] = '\0';

  struct passwd *pw = getpwnam(uname);
  if (!pw || !pw->pw_dir)
    return NULL;

  const char *hdir = pw->pw_dir;
  if (!hdir)
    return NULL;

  size_t buf_len = strlen(hdir) + strlen(p);
  char *buf = (char *)malloc(buf_len + 1);
  if (!buf) {
    perror("malloc fatal");
    return NULL;
  }
  buf[0] = '\0';

  strcat(buf, hdir);
  strcat(buf, p);

  return buf;
}

static bool is_op_char(const char *c) {
  switch (*c) {
  case ':':
  case '#':
  case '%':
  case '=':
  case '-':
  case '?':
  case '+':
    return true;
  default:
    return false;
  }
}

static char *extract_var_and_alt(const char *src, char **alt_ptr,
                                 t_err_type *err) {

  if (!src || src[0] == '\0') {
    return NULL;
  }
  size_t slen = strlen(src);
  const size_t INITIAL_VAR_VAL_LEN = 128;

  char *var = malloc(INITIAL_VAR_VAL_LEN);
  if (!var) {
    perror("malloc fatal");
    return NULL;
  }

  char *alt = malloc(INITIAL_VAR_VAL_LEN);
  if (!alt) {
    free(var);
    perror("malloc fatal");
    return NULL;
  }

  size_t var_cap = INITIAL_VAR_VAL_LEN, var_len = 0;
  size_t alt_cap = INITIAL_VAR_VAL_LEN, alt_len = 0;
  var[0] = '\0';
  alt[0] = '\0';

  bool parsing_alt = false;
  size_t idx = 0;

  while (idx < slen && src[idx] != '\0') {

    if (!parsing_alt && is_op_char(src + idx)) {
      if (idx + 2 < slen && is_op_char(src + idx + 1))
        idx += 2;
      else
        idx += 1;
      parsing_alt = true;
      continue;
    }

    if (!parsing_alt) {
      if (!is_valid_var_char(src[idx], idx == 0)) {
        free(var);
        free(alt);
        *err = err_bad_sub;
        return NULL;
      }

      if (var_len + 1 >= var_cap && realloc_buf(&var, &var_cap) == -1) {
        free(var);
        free(alt);
        return NULL;
      }

      var[var_len++] = src[idx];
      var[var_len] = '\0';
    } else {
      if (alt_len + 1 >= alt_cap && realloc_buf(&alt, &alt_cap) == -1) {
        free(var);
        free(alt);
        return NULL;
      }

      alt[alt_len++] = src[idx];
      alt[alt_len] = '\0';
    }
    idx++;
  }

  if (var_len == 0) {
    free(var);
    free(alt);
    *err = err_bad_sub;
    return NULL;
  }

  if (alt[0] == '~') {
    char *new_alt = expand_tilde(alt);
    if (new_alt) {
      free(alt);
      *alt_ptr = new_alt;
      alt = *alt_ptr;
      new_alt = NULL;
    }
  }

  *alt_ptr = alt;
  return var;
}

char *remove_leading_pattern(const char *src, const char *pattern,
                             bool longest) {

  char *rs = NULL;
  if (!pattern || pattern[0] == '\0') {
    rs = strdup(src);
    if (!rs) {
      perror("736: strdup fatal");
      return NULL;
    }
    return rs;
  }

  const char *rem = NULL;
  size_t src_len = strlen(src);

  for (size_t si = 0; si <= src_len; si++) {
    size_t pi = 0;
    size_t sj = si;

    while (pattern[pi]) {
      if (pattern[pi] == '*') {
        while (pattern[pi] == '*')
          pi++;

        if (!pattern[pi]) {
          sj = src_len;
          break;
        }

        while (src[sj] != '\0' && src[sj] != pattern[pi])
          sj++;
      } else {
        if (src[sj] != pattern[pi])
          break;
        sj++;
        pi++;
      }
    }

    if (!pattern[pi]) {
      if (!longest) {
        rs = strdup(src + sj);
        if (!rs) {
          perror("770: strdup fatal");
          return NULL;
        }
        return rs;
      }
      rem = src + sj;
    }
  }

  if (!rem)
    rs = strdup("");
  else
    rs = strdup(rem);
  if (!rs) {
    perror("785: fatal strdup");
    return NULL;
  }

  return rs;
}

static char *remove_trailing_pattern(const char *src, const char *pattern,
                                     bool longest) {

  char *rs = NULL;
  if (!pattern || pattern[0] == '\0') {
    rs = strdup(src);
    if (!rs) {
      perror("796: strdup fatal");
      return NULL;
    }
    return rs;
  }

  const char *rem = NULL;
  size_t src_len = strlen(src);

  for (ssize_t si = src_len; si >= 0; si--) {

    ssize_t pi = strlen(pattern) - 1;
    ssize_t sj = si - 1;
    while (pi >= 0) {

      if (pattern[pi] == '*') {

        while (pi >= 0 && pattern[pi] == '*')
          pi--;

        if (pi < 0) {
          sj = -1;
          break;
        }
        while (sj >= 0 && src[sj] != pattern[pi])
          sj--;
      } else {
        if (sj < 0 || src[sj] != pattern[pi])
          break;
        sj--;
        pi--;
      }
    }

    if (pi < 0) {
      if (!longest) {
        rs = strndup(src, sj + 1 + 1);
        if (!rs) {
          perror("831: strndup fatal");
          return NULL;
        }
        return rs;
      }
      rem = src + sj + 1 + 1;
    }
  }

  if (!rem)
    rs = strdup(src);
  else
    rs = strndup(src, rem - src);
  if (!rs) {
    perror("843: strndup fatal");
    return NULL;
  }

  return rs;
}

static char *expand_param_op(t_shell *shell, const char *src, t_param_op op,
                             t_err_type *err) {

  char *alt = NULL;
  char *var = extract_var_and_alt(src, &alt, err);
  if (!var || !alt) {
    if (*err == err_fatal)
      exit(EXIT_FAILURE);
    else {
      if (*err == err_bad_sub)
        fprintf(stderr, "\nmsh: bad substitution");
      return NULL;
    }
  }

  if (*err != err_none) {
    return NULL;
  }

  char *val = getenv_local(shell->env, var);
  if (!val) {
    const char *sys_val = getenv(var);
    if (sys_val) {
      val = strdup(sys_val);
    }
  }

  char *result = NULL;

  switch (op) {
  case PARAM_OP_MINUS:
    if (!val || val[0] == '\0') {
      result = strdup(alt);
    } else {
      result = strdup(val);
    }
    break;

  case PARAM_OP_EQUAL:
    if (!val || val[0] == '\0') {
      result = strdup(alt);
      add_to_env(shell, var, alt);
      setenv(var, alt, 1);
    } else {
      result = strdup(val);
    }
    break;

  case PARAM_OP_PLUS:
    if (val && val[0] != '\0') {
      result = strdup(alt);
    } else {
      result = strdup("");
    }
    break;

  case PARAM_OP_QUESTION:
    if (!val || val[0] == '\0') {
      if (alt && alt[0] != '\0') {
        fprintf(stderr, "msh: %s: %s\n", var, alt);
      } else {
        fprintf(stderr, "msh: %s: parameter not set\n", var);
        *err = err_param_unset;
      }
      result = NULL;
    } else {
      result = strdup(val);
    }
    break;
  case PARAM_OP_HASH:
    if (val) {
      result = remove_leading_pattern(val, alt, false);
    } else {
      result = strdup("");
    }
    break;

  case PARAM_OP_HASH_HASH:
    if (val) {
      result = remove_leading_pattern(val, alt, true);
    } else {
      result = strdup("");
    }
    break;
  case PARAM_OP_PERCENT:
    if (val) {
      result = remove_trailing_pattern(val, alt, false);
    } else {
      result = strdup("");
    }
    break;
  case PARAM_OP_PERCENT_PERCENT:
    if (val) {
      result = remove_trailing_pattern(val, alt, true);
    } else {
      result = strdup("");
    }
    break;
  default:
    result = NULL;
    *err = err_bad_sub;
    break;
  }

  if (val)
    free(val);
  if (var)
    free(var);
  if (alt)
    free(alt);

  return result;
}

/**
 * @brief Expands param of type brace
 *
 * @param shell shell struct
 * @param src src string
 * @param err err type
 * @return char* expanded param to expand_fields
 */
static char *expand_param(t_shell *shell, const char *src, t_err_type *err) {

  t_param_op op = get_param_op(shell, src);
  if (op == PARAM_OP_ERR) {
    *err = err_bad_sub;
    return NULL;
  }

  if (op == PARAM_OP_NONE) {

    size_t i_ph = 0;
    return expand_var(shell, src, &i_ph);
  } else if (op == PARAM_OP_LEN) {

    char buf[32];

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)strlen(src));
    char *rs = strdup(buf);
    if (!rs) {
      perror("982: strdup fatal");
      if (getpid() == shell->pgid) {
        exit(1);
      } else {
        _exit(1);
      }
    }
    return rs;
  }

  char *result = expand_param_op(shell, src, op, err);
  if (*err != err_none) {
    if (result)
      free(result);
    return NULL;
  }

  return result;
}

char *expand_braces(t_shell *shell, const char *src, size_t *i) {

  int depth = 0;
  size_t idx = *i;
  char *buf = malloc(BUFFER_INITIAL_LEN);
  if (!buf)
    return NULL;
  buf[0] = '\0';
  size_t w = 0;
  size_t buf_cap = BUFFER_INITIAL_LEN;

  while (src && src[idx]) {
    if (src[idx] == '{') {
      depth++;
      if (depth == 1) {
        idx++;
        continue;
      }
    }

    if (src[idx] == '}') {
      depth--;
      if (depth == 0) {
        idx++;
        break;
      }
    }
    if (depth >= 1) {
      if (w + 1 >= buf_cap) {
        size_t new_cap = buf_cap * BUF_GROWTH_FACTOR;
        char *new_buf = realloc(buf, new_cap);
        if (!new_buf) {
          free(buf);
          return NULL;
        }
        buf = new_buf;
        buf_cap = new_cap;
      }
      buf[w++] = src[idx];
      buf[w] = '\0';
    }

    idx++;
  }
  if (depth != 0) {
    free(buf);
    *i = idx;
    return NULL;
  }

  char *str = NULL;
  int f_depth = 1;
  char **fields = expand_fields(shell, &buf, &f_depth);
  if (fields && fields[0]) {
    str = strdup(fields[0]);
    cleanup_argv(fields);
  }

  free(buf);

  if (!str) {
    *i = idx;
    return NULL;
  }

  t_err_type err = err_none;
  char *result = expand_param(shell, str, &err);
  free(str);
  *i = idx;

  if (err != err_none) {
    if (result)
      free(result);
    return NULL;
  }

  return result;
}

char *expand_subshell(t_shell *shell, const char *src, size_t *i) {

  size_t slen = strlen(src);
  size_t idx = *i;
  char *cmd_line = (char *)malloc(BUFFER_INITIAL_LEN);
  if (!cmd_line) {
    return NULL;
  }
  size_t l_cap = BUFFER_INITIAL_LEN;
  cmd_line[0] = '\0';

  int paren_depth = 1;
  idx++;
  size_t w = 0;
  while (paren_depth > 0 && idx < slen) {
    if (w + 1 >= l_cap) {
      if (realloc_buf(&cmd_line, &l_cap) == -1) {
        free(cmd_line);
        return NULL;
      }
    }

    if (src[idx] == '(') {
      paren_depth++;
    } else if (src[idx] == ')') {
      paren_depth--;
      if (paren_depth == 0)
        break;
    }

    cmd_line[w++] = src[idx++];
  }
  cmd_line[w] = '\0';

  int fds[2] = {-1, -1};
  if (pipe(fds) == -1) {
    perror("pipe fatal");
    free(cmd_line);
    return NULL;
  }
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    free(cmd_line);
    return NULL;
  }

  if (pid == 0) {

    t_token_stream ts;
    init_token_stream(&ts);

    setpgid(0, 0);

    shell->job_control_flag = 0;

    close(fds[0]);
    dup2(fds[1], STDOUT_FILENO);
    close(fds[1]);
    parse_and_execute(&cmd_line, shell, &ts, false);

    fflush(stdout);

    _exit(shell->last_exit_status);
  } else {

    setpgid(pid, pid);

    close(fds[1]);
    char *buf = (char *)malloc(BUFFER_INITIAL_LEN);
    if (!buf) {
      perror("buf fatal");
      free(cmd_line);
    }
    size_t buf_cap = BUFFER_INITIAL_LEN, buf_size = 0;
    char tmp[8192];
    ssize_t n;

    while ((n = read(fds[0], tmp, sizeof(tmp))) > 0) {
      if (buf_size + n + 1 > buf_cap) {
        while (buf_size + n + 1 > buf_cap)
          buf_cap *= 2;
        char *nbuf = realloc(buf, buf_cap);
        if (!nbuf) {
          free(buf);
          free(cmd_line);
          kill(-pid, SIGKILL);
          while (waitpid(-pid, NULL, 0) > 0)
            ;
          return NULL;
        }
        buf = nbuf;
      }

      memcpy(buf + buf_size, tmp, n);
      buf_size += n;
    }

    buf[buf_size] = '\0';
    close(fds[0]);

    while (waitpid(-pid, NULL, 0) > 0)
      ;
    free(cmd_line);

    while (buf_size > 0 && buf[buf_size - 1] == '\n')
      buf[--buf_size] = '\0';

    *i = ++idx;
    return buf;
  }

  return NULL;
}

char *expand_var(t_shell *shell, const char *src, size_t *i) {

  size_t start = *i;

  while (src[*i] != '\0' && (isalnum(src[*i]) || src[*i] == '_')) {
    (*i)++;
  }

  size_t len = *i - start;
  if (len == 0)
    return strdup("");

  char *name = strndup(src + start, len);
  if (!name) {
    perror("strdup fail");
    return NULL;
  }
  char *value = getenv_local(shell->env, name);
  free(name);

  return value ? value : strdup("");
}

static int push_field(char ***fields, size_t *f_argc, size_t *f_cap,
                      char **str) {

  if (*f_argc + 1 >= *f_cap) {

    size_t new_cap = *f_cap * BUF_GROWTH_FACTOR;
    char **new_fields = realloc(*fields, new_cap * sizeof(char *));
    if (!new_fields) {
      perror("realloc");
      return -1;
    }
    for (size_t i = *f_cap; i < new_cap; i++) {
      new_fields[i] = NULL;
    }

    *fields = new_fields;
    *f_cap = new_cap;
  }

  (*fields)[(*f_argc)++] = *str;
  (*fields)[*f_argc] = NULL;
  *str = NULL;

  return 0;
}

static char **expand_fields(t_shell *shell, char **src_str, int *depth) {

  if (*depth >= 32) {
    fprintf(stderr, "msh: max nested depth");
    return NULL;
  }
  (*depth)++;

  char *src = *src_str;
  size_t len = strlen(src);

  size_t fields_cap = ARGV_INITIAL_LEN, fields_argc = 0;
  char **fields = calloc(sizeof(char *), fields_cap);
  if (!fields) {
    perror("fields malloc");
    return NULL;
  }

  char *buffer = malloc(BUFFER_INITIAL_LEN);
  if (!buffer) {
    perror("buf malloc");
    cleanup_argv(fields);
    return NULL;
  }
  buffer[0] = '\0';

  if (src[0] == '~') {
    char *new_src = expand_tilde(src);
    if (new_src) {
      free(*src_str);
      *src_str = new_src;
      src = *src_str;
      new_src = NULL;
    }
  }

  size_t buffer_cap = BUFFER_INITIAL_LEN, buffer_size = 0, i = 0;
  bool single_q = false, double_q = false;
  while (src && src[i] != '\0') {

    if (buffer_size + 2 > buffer_cap) {
      if (realloc_buf(&buffer, &buffer_cap) == -1) {
        perror("realloc buf");
        free(buffer);
        cleanup_argv(fields);
        return NULL;
      }
    }

    if (src[i] == '\'' && !double_q) {
      single_q = !single_q;
      buffer[buffer_size++] = src[i++];
      buffer[buffer_size] = '\0';
      continue;
    }
    if (!single_q && src[i] == '"') {
      double_q = !double_q;
      buffer[buffer_size++] = src[i++];
      buffer[buffer_size] = '\0';
      continue;
    }
    if (src[i] == '\\' && !single_q) {
      buffer[buffer_size++] = src[i++];
      if (src[i]) {
        buffer[buffer_size++] = src[i++];
      }
      buffer[buffer_size] = '\0';
      continue;
    }

    if (!single_q && src[i] == '$') {

      char c[3];
      if (i + 3 <= len && src[i + 2] == '(') {
        c[0] = src[i + 1];
        c[1] = src[i + 2];
        c[2] = '\0';
      } else {
        c[0] = src[i + 1];
        c[1] = '\0';
        c[2] = '\0';
      }
      t_exp_handler handler = find_handler(c);
      i++;
      if (handler == expand_arith)
        i++;

      char *val = handler(shell, src, &i);
      if (!val) {
        free(buffer);
        cleanup_argv(fields);
        return NULL;
      }

      if (double_q || *depth > 1) {

        size_t val_len = strlen(val);
        while (buffer_size + val_len + 1 > buffer_cap) {
          if (realloc_buf(&buffer, &buffer_cap) == -1) {
            perror("realloc buf");
            free(buffer);
            cleanup_argv(fields);
          }
        }

        memcpy(buffer + buffer_size, val, val_len);
        buffer_size += val_len;
        buffer[buffer_size] = '\0';
        free(val);
      } else {

        char **splits = separate_ifs(shell, val);
        free(val);

        if (splits && splits[0]) {

          size_t s0_len = strlen(splits[0]);
          while (buffer_size + s0_len + 1 > buffer_cap) {
            if (realloc_buf(&buffer, &buffer_cap) == -1) {
              perror("realloc buf s0");
              cleanup_splits(splits);
              cleanup_argv(fields);
              free(buffer);
              return NULL;
            }
          }

          memcpy(buffer + buffer_size, splits[0], s0_len);
          buffer_size += s0_len;
          buffer[buffer_size] = '\0';

          for (int j = 1; splits[j]; j++) {

            push_field(&fields, &fields_argc, &fields_cap, &buffer);

            buffer = strdup(splits[j]);
            if (!buffer) {
              perror("391: realloc buf");
              cleanup_argv(fields);
              cleanup_splits(splits);
              return NULL;
            }

            buffer_cap = strlen(buffer) + 1;
            buffer_size = buffer_cap - 1;
          }
        }
        cleanup_splits(splits);
      }
    } else {
      buffer[buffer_size++] = src[i++];
    }
    buffer[buffer_size] = '\0';
  }
  if (buffer_size > 0 || fields_argc == 0)
    push_field(&fields, &fields_argc, &fields_cap, &buffer);
  else
    free(buffer);

  return fields;
}

static int find_brace_pair(const char *s, int *l, int *r) {

  int depth = 0;
  bool in_single = false, in_double = false;
  for (int i = 0; s[i]; i++) {

    if (s[i] == '\'' && !in_double)
      in_single = !in_single;
    else if (s[i] == '"' && !in_single)
      in_double = !in_double;

    if (in_single)
      continue;

    if (s[i] == '{') {
      if (i - 1 >= 0 && s[i - 1] == '$')
        continue;
      if (depth == 0)
        *l = i;
      depth++;
    } else if (s[i] == '}') {
      depth--;
      if (depth == 0) {
        *r = i;
        return 0;
      }
    }
  }
  return -1;
}

static char **split_brace_options(const char *s) {
  size_t cap = ARGV_INITIAL_LEN;
  size_t argc = 0;
  char **out = calloc(cap, sizeof(char *));

  int depth = 0;
  bool in_single = false, in_double = false;

  const char *start = s;

  for (const char *p = s;; p++) {
    char c = *p;

    if (c == '\'' && !in_double)
      in_single = !in_single;
    else if (c == '"' && !in_single)
      in_double = !in_double;

    if (!in_single && !in_double) {
      if (c == '{')
        depth++;
      else if (c == '}')
        depth--;
    }

    if ((c == ',' && depth == 0 && !in_single && !in_double) || c == '\0') {
      char *part = strndup(start, p - start);
      push_field(&out, &argc, &cap, &part);

      if (c == '\0')
        break;

      start = p + 1;
    }
  }

  return out;
}

static char **expand_brace_range(const char *s) {

  const char *dots = strstr(s, "..");
  if (!dots)
    return NULL;

  char *lhs = strndup(s, dots - s);
  char *rhs = strdup(dots + 2);

  if (!*lhs || !*rhs) {
    free(lhs);
    free(rhs);
    return NULL;
  }

  size_t cap = ARGV_INITIAL_LEN;
  size_t argc = 0;
  char **out = calloc(cap, sizeof(char *));

  /* numeric range */
  if ((isdigit(lhs[0]) || lhs[0] == '-') &&
      (isdigit(rhs[0]) || rhs[0] == '-')) {

    int start = atoi(lhs);
    int end = atoi(rhs);

    int step = (start <= end) ? 1 : -1;

    int width = 0;
    if (lhs[0] == '0' || rhs[0] == '0')
      width = MAX(strlen(lhs), strlen(rhs));

    for (int i = start;; i += step) {

      char buf[64];

      if (width)
        snprintf(buf, sizeof(buf), "%0*d", width, i);
      else
        snprintf(buf, sizeof(buf), "%d", i);

      char *tmp = strdup(buf);
      push_field(&out, &argc, &cap, &tmp);

      if (i == end)
        break;
    }
  } else if (strlen(lhs) == 1 && strlen(rhs) == 1 && isalpha(lhs[0]) &&
             isalpha(rhs[0])) {

    char start = lhs[0];
    char endc = rhs[0];

    int step = (start <= endc) ? 1 : -1;

    for (char c = start;; c += step) {
      char buf[2] = {c, 0};
      char *tmp = strdup(buf);
      push_field(&out, &argc, &cap, &tmp);

      if (c == endc)
        break;
    }
  } else {
    free(lhs);
    free(rhs);
    cleanup_argv(out);
    return NULL;
  }

  free(lhs);
  free(rhs);
  return out;
}

char **expand_word_braces(const char *word) {

  int l = -1, r = -1;
  if (find_brace_pair(word, &l, &r) == -1) {
    char **out = calloc(2, sizeof(char *));
    out[0] = strdup(word);
    return out;
  }

  char *prefix = strndup(word, l);
  char *inside = strndup(word + l + 1, r - l - 1);
  char *suffix = strdup(word + r + 1);

  char **options = expand_brace_range(inside);
  if (!options)
    options = split_brace_options(inside);

  size_t res_cap = ARGV_INITIAL_LEN;
  size_t res_argc = 0;
  char **result = calloc(res_cap, sizeof(char *));

  for (int i = 0; options[i]; i++) {

    size_t sz = strlen(prefix) + strlen(options[i]) + strlen(suffix) + 1;
    char *combined = malloc(sz);
    snprintf(combined, sz, "%s%s%s", prefix, options[i], suffix);

    char **rec = expand_word_braces(combined);
    free(combined);

    for (int j = 0; rec[j]; j++) {
      char *s = rec[j];
      push_field(&result, &res_argc, &res_cap, &s);
    }

    free(rec);
    free(options[i]);
  }

  free(options);
  free(prefix);
  free(inside);
  free(suffix);

  return result;
}

static int redir_tok_found(t_token *tok) {

  if (!tok || tok->type == -1)
    return 0;

  if (tok->type == TOKEN_APPEND)
    return 1;
  if (tok->type == TOKEN_HEREDOC)
    return 1;
  if (tok->type == TOKEN_TRUNC)
    return 1;
  if (tok->type == TOKEN_INPUT)
    return 1;

  return 0;
}

t_err_type expand_make_argv(t_shell *shell, char ***argv, t_token *tokens,
                            const size_t segment_len) {

  size_t argv_cap = ARGV_INITIAL_LEN;
  char **argv_local = calloc(argv_cap, sizeof(char *));
  if (!argv_local)
    return err_fatal;

  size_t argc = 0;
  size_t idx = 0;

  while (idx < segment_len) {

    if (redir_tok_found(&tokens[idx])) {
      idx += 2;
      continue;
    }

    char *str = make_word(tokens, &idx, segment_len);
    if (!str) {
      cleanup_argv(argv_local);
      return err_fatal;
    }

    char **brace_words = expand_word_braces(str);
    free(str);

    for (int b = 0; brace_words[b]; b++) {

      int f_depth = 0;
      char *tmp = strdup(brace_words[b]);
      char **expanded_fields = expand_fields(shell, &tmp, &f_depth);
      free(tmp);

      if (!expanded_fields)
        continue;

      for (int i = 0; expanded_fields[i]; i++) {
        if (argc >= argv_cap - 1) {
          if (realloc_argv(&argv_local, &argv_cap) == -1) {
            cleanup_argv(expanded_fields);
            cleanup_argv(argv_local);
            return err_fatal;
          }
        }
        expand_glob(&expanded_fields);
        argv_local[argc++] = strip_quotes(expanded_fields[i]);
        argv_local[argc] = NULL;
      }
      cleanup_argv(expanded_fields);
    }
    cleanup_argv(brace_words);
  }

  *argv = argv_local;
  return err_none;
}
