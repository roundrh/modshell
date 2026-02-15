#include "var_exp.h"
#include "builtins.h"
#include "hashtable.h"
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
static long long parse_arith_primary(const char **p, t_err_type *err,
                                     t_shell *shell);
static long long parse_arith_unary(const char **p, t_err_type *err,
                                   t_shell *shell);
static long long parse_arith_muldiv(const char **p, t_err_type *err,
                                    t_shell *shell);
static long long parse_arith_addsub(const char **p, t_err_type *err,
                                    t_shell *shell);
static long long parse_arith_exprsh(const char **p, t_err_type *err,
                                    t_shell *shell);
static long long parse_arith_number(const char **p, t_err_type *err,
                                    t_shell *shell);

static char *expand_recursive(t_shell *shell, char *str, t_arena *a, int depth);

char *getenv_local(t_hashtable *env, const char *var_name, t_arena *a) {
  t_ht_node *node = ht_find(env, var_name);
  if (!node)
    return NULL;

  t_env_entry *entry = (t_env_entry *)node->value;
  if (!entry || !entry->val)
    return NULL;

  size_t len = strlen(entry->val);
  char *copy = arena_alloc(a, len + 1);
  if (copy) {
    memcpy(copy, entry->val, len + 1);
  }
  return copy;
}

const char *getenv_local_ref(t_hashtable *env, const char *var_name) {
  t_ht_node *node = ht_find(env, var_name);
  if (!node)
    return NULL;

  t_env_entry *entry = (t_env_entry *)node->value;
  if (!entry || !entry->val)
    return NULL;

  return entry->val;
}

char **flatten_env(t_hashtable *env, t_arena *a) {
  char **envp = (char **)arena_alloc(a, sizeof(char *) * (env->count + 1));
  size_t env_idx = 0;

  for (int i = 0; i < HT_DEFSIZE; i++) {
    t_ht_node *node = env->buckets[i];

    while (node) {
      t_env_entry *entry = (t_env_entry *)node->value;

      if (entry && entry->val && (entry->flags & ENV_EXPORTED)) {
        size_t k_len = strlen(node->key);
        size_t v_len = strlen(entry->val);
        size_t total_len = k_len + v_len + 2;

        char *str = (char *)arena_alloc(a, total_len);
        if (str) {
          memcpy(str, node->key, k_len);
          str[k_len] = '=';
          memcpy(str + k_len + 1, entry->val, v_len);
          str[total_len - 1] = '\0';

          envp[env_idx++] = str;
        }
      }
      node = node->next;
    }
  }
  envp[env_idx] = NULL;

  return envp;
}

void remove_from_env(t_hashtable *env, const char *var_name) {
  ht_delete(env, var_name, free_env_entry);
}

void print_env(t_hashtable *env, bool exported_only) {
  if (!env)
    return;

  for (size_t i = 0; i < HT_DEFSIZE; i++) {
    t_ht_node *node = env->buckets[i];

    while (node) {
      t_env_entry *entry = (t_env_entry *)node->value;

      if (entry && entry->val) {
        if (!exported_only || (entry->flags & ENV_EXPORTED)) {
          printf("%s=%s\n", node->key, entry->val);
        }
      }
      node = node->next;
    }
  }
}

int add_to_env(t_shell *shell, const char *var, const char *val) {
  if (!var || !val)
    return -1;

  t_ht_node *existing = ht_find(&shell->env, var);
  t_env_entry *entry;

  if (existing) {
    entry = (t_env_entry *)existing->value;
    free(entry->val);
  } else {
    entry = (t_env_entry *)malloc(sizeof(t_env_entry));
    if (!entry)
      return -1;
    entry->name = strdup(var);
    entry->flags = 0;
    ht_insert(&shell->env, var, entry, free_env_entry);
  }

  entry->val = strdup(val);

  char *endptr;
  long long res = strtoll(entry->val, &endptr, 10);

  if (*val != '\0' && *endptr == '\0') {
    entry->vint = res;
    entry->flags |= ENV_HAS_VINT;
  } else {
    entry->flags &= ~ENV_HAS_VINT;
  }

  return 0;
}

static char *get_ifs(t_shell *shell, t_arena *a) {

  char *ifs = getenv_local(&shell->env, "IFS", a);
  if (!ifs) {
    char ifs_def[] = {' ', '\t', '\n', '\0'};
    ifs = (char *)arena_alloc(a, sizeof(ifs_def));
    memcpy(ifs, ifs_def, sizeof(ifs_def));
    return ifs;
  }
  return ifs;
}
static int is_ifs_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n';
}

static bool is_valid_var_char(char c, bool first_char) {
  if (first_char) {
    return isalpha(c) || c == '_';
  } else {
    return isalnum(c) || c == '_';
  }
}
static t_exp_handler find_handler(const char *c) {
  if (isdigit(*c))
    return expand_args;
  if (*c == '(') {
    if (*(c + 1) == '(')
      return expand_arith;
    return expand_subshell;
  }
  for (int i = 0; g_jump_table[i].trigger; i++) {
    if (*c == g_jump_table[i].trigger[0])
      return g_jump_table[i].handler;
  }
  return expand_var;
}

static void skip_spaces(const char **p) {
  while (**p && (**p == ' ' || **p == '\t'))
    (*p)++;
}

static long long parse_arith_pow(const char **p, t_err_type *err,
                                 t_shell *shell) {

  long long left = parse_arith_unary(p, err, shell);
  if (*err != err_none)
    return 0;

  skip_spaces(p);

  if ((*p)[0] == '*' && (*p)[1] == '*') {
    (*p) += 2;

    long long right = parse_arith_pow(p, err, shell);
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

static long long parse_arith_primary(const char **p, t_err_type *err,
                                     t_shell *shell) {
  skip_spaces(p);

  if (**p == '(') {
    (*p)++;
    long long val = parse_arith_exprsh(p, err, shell);
    skip_spaces(p);
    if (**p != ')') {
      *err = err_syntax;
      return 0;
    }
    (*p)++;
    return val;
  }

  if (isdigit(**p)) {
    return parse_arith_number(p, err, shell);
  }

  if (is_valid_var_char(**p, true)) {
    const char *start = *p;
    while (is_valid_var_char(**p, false)) {
      (*p)++;
    }

    int len = *p - start;
    char var_name[256];
    if (len >= 256) {
      *err = err_fatal;
      return 0;
    }

    memcpy(var_name, start, len);
    var_name[len] = '\0';

    t_ht_node *node = ht_find(&shell->env, var_name);
    if (node) {
      t_env_entry *entry = (t_env_entry *)node->value;
      if (entry->flags & ENV_HAS_VINT) {
        return entry->vint;
      }
      if (entry->val) {
        return strtoll(entry->val, NULL, 10);
      }
    }
    return 0;
  }

  *err = err_syntax;
  return 0;
}
static long long parse_arith_unary(const char **p, t_err_type *err,
                                   t_shell *shell) {
  skip_spaces(p);
  if (**p == '+') {
    (*p)++;
    return parse_arith_unary(p, err, shell);
  }
  if (**p == '-') {
    (*p)++;
    return -parse_arith_unary(p, err, shell);
  }
  return parse_arith_primary(p, err, shell);
}
static long long parse_arith_muldiv(const char **p, t_err_type *err,
                                    t_shell *shell) {

  long long left = parse_arith_pow(p, err, shell);
  if (*err != err_none)
    return 0;

  skip_spaces(p);
  while (**p == '*' || **p == '/' || **p == '%') {
    char op = **p;
    (*p)++;

    long long right = parse_arith_primary(p, err, shell);
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
static long long parse_arith_addsub(const char **p, t_err_type *err,
                                    t_shell *shell) {

  long long left = parse_arith_muldiv(p, err, shell);
  if (*err != err_none)
    return 0;

  skip_spaces(p);
  while (**p == '+' || **p == '-') {
    char op = **p;
    (*p)++;
    long long right = parse_arith_muldiv(p, err, shell);

    if (op == '+')
      left += right;
    else
      left -= right;

    skip_spaces(p);
  }
  return left;
}
static long long parse_arith_exprsh(const char **p, t_err_type *err,
                                    t_shell *shell) {

  if (!p || !*p || !**p)
    return 0;

  long long result = parse_arith_addsub(p, err, shell);

  skip_spaces(p);
  if (**p != '\0' && **p != ')' && *err == err_none) {
    *err = err_syntax;
  }

  return result;
}
static long long parse_arith_number(const char **p, t_err_type *err,
                                    t_shell *shell) {

  char *endptr;
  long long val = strtoll(*p, &endptr, 10);

  if (*p == endptr) {
    *err = err_syntax;
    return 0;
  }

  *p = endptr;
  return val;
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
  size_t len = strlen(src) - 1;
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

static void append_to_buf(char **buf, size_t *buf_cap, size_t *k,
                          const char *str, t_arena *a) {
  if (!str)
    return;
  size_t len = strlen(str);

  while (*k + len + 1 >= *buf_cap) {
    size_t n = *buf_cap * BUF_GROWTH_FACTOR;
    *buf = arena_realloc(a, *buf, n, *buf_cap);
    *buf_cap = n;
  }

  memcpy(*buf + *k, str, len);
  *k += len;
  (*buf)[*k] = '\0';
}

t_err_type expand_args(t_shell *shell, char **buf, size_t *cap, const char **p,
                       size_t *k, t_arena *a) {
  char *val = NULL;
  if (isdigit(**p)) {
    int idx = **p - '0';
    if (idx < shell->argc)
      val = shell->argv[idx];
    (*p)++;
  } else if (**p == '*' || **p == '@') {
    for (int i = 1; i < shell->argc; i++) {
      append_to_buf(buf, cap, k, shell->argv[i], a);
      if (i < shell->argc - 1)
        append_to_buf(buf, cap, k, " ", a);
    }
    (*p)++;
  }
  if (val)
    append_to_buf(buf, cap, k, val, a);
  return err_none;
}

int expand_glob_from(t_shell *shell, char ***argv, int start, t_arena *a) {
  char **old = *argv;

  size_t cap = 32;
  size_t argc = 0;
  char **newv = arena_alloc(a, sizeof(char *) * cap);

  if (!newv)
    return -1;

  for (int i = 0; i < start; i++) {
    newv[argc++] = old[i];
  }

  for (int i = start; old[i]; i++) {
    char *arg = old[i];

    if (strpbrk(arg, "*?[]")) {
      glob_t g;
      if (glob(arg, GLOB_NOCHECK | GLOB_TILDE, NULL, &g) == 0) {
        for (size_t j = 0; j < g.gl_pathc; j++) {
          if (argc + 1 >= cap) {
            cap *= 2;
            newv = arena_realloc(a, newv, cap * sizeof(char *),
                                 (cap / 2) * sizeof(char *));
          }

          size_t len = strlen(g.gl_pathv[j]);
          char *p = arena_alloc(a, len + 1);
          memcpy(p, g.gl_pathv[j], len + 1);
          newv[argc++] = p;
        }
        globfree(&g);
        continue;
      }
    }

    newv[argc++] = arg;
  }

  newv[argc] = NULL;
  *argv = newv;
  return 0;
}

t_err_type expand_arith(t_shell *shell, char **buf, size_t *buf_cap,
                        const char **p, size_t *k, t_arena *a) {
  (*p) += 2;

  int ad = 2;
  size_t len = 0;
  while ((*p)[len] != '\0') {
    if ((*p)[len] == '(') {
      ad++;
    } else if ((*p)[len] == ')') {
      ad--;
      if (ad == 0)
        break;
    }

    len++;
  }
  if (ad != 0 || (*p)[len] == '\0')
    return err_syntax;

  char *w = arena_alloc(a, len + 1);
  memcpy(w, *p, len);
  w[len] = '\0';

  *p += len + 2;

  const char *eval_ptr;
  if (strchr(w, '$')) {
    static int depth = 0;
    eval_ptr = expand_recursive(shell, w, a, ++depth);
    depth--;
  } else {
    eval_ptr = w;
  }
  if (!eval_ptr)
    return err_fatal;

  t_err_type err = err_none;
  long long result = parse_arith_exprsh(&eval_ptr, &err, shell);
  if (err != err_none)
    return err;

  char res_str[32];
  snprintf(res_str, sizeof(res_str), "%lld", result);
  append_to_buf(buf, buf_cap, k, res_str, a);

  return err_none;
}

t_err_type expand_exit_status(t_shell *shell, char **buf, size_t *cap,
                              const char **p, size_t *k, t_arena *a) {
  char status_str[12];
  snprintf(status_str, sizeof(status_str), "%d", shell->last_exit_status);
  append_to_buf(buf, cap, k, status_str, a);
  (*p)++;

  return err_none;
}

t_err_type expand_pid(t_shell *shell, char **buf, size_t *cap, const char **p,
                      size_t *k, t_arena *a) {
  char pid_str[12];
  snprintf(pid_str, sizeof(pid_str), "%d", getpid());
  append_to_buf(buf, cap, k, pid_str, a);
  (*p)++;

  return err_none;
}

t_err_type expand_var(t_shell *shell, char **buf, size_t *buf_cap,
                      const char **p, size_t *k, t_arena *a) {
  const char *start = *p;
  size_t len = 0;

  if (is_valid_var_char(**p, true)) {
    while (is_valid_var_char((*p)[len], false)) {
      len++;
    }
  }

  if (len > 0) {
    char *var_name = arena_alloc(a, len + 1);
    strncpy(var_name, start, len);
    var_name[len] = '\0';

    char *val = getenv_local(&shell->env, var_name, a);
    if (val) {
      append_to_buf(buf, buf_cap, k, val, a);
    }
    *p += len;
  } else {
    append_to_buf(buf, buf_cap, k, "$", a);
  }

  return err_none;
}

static size_t get_leading_pattern_offset(const char *src, const char *pattern,
                                         bool longest) {
  if (!pattern || pattern[0] == '\0')
    return 0;

  size_t src_len = strlen(src);
  size_t match_end = 0;
  bool found = false;

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
      found = true;
      if (!longest)
        return sj;
      match_end = sj;
    }
  }

  return found ? match_end : 0;
}

static size_t get_trailing_pattern_len(const char *src, const char *pattern,
                                       bool longest) {
  size_t src_len = strlen(src);
  if (!pattern || pattern[0] == '\0')
    return src_len;

  size_t keep_len = src_len;
  bool found = false;

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
      found = true;
      if (!longest)
        return (size_t)(sj + 1);
      keep_len = (size_t)(sj + 1);
    }
  }

  return found ? keep_len : src_len;
}

t_err_type expand_subshell(t_shell *shell, char **buf, size_t *buf_cap,
                           const char **p, size_t *k, t_arena *a) {
  (*p)++;
  const char *start = *p;
  int paren_depth = 1;
  size_t cmd_len = 0;

  while ((*p)[cmd_len] && paren_depth > 0) {
    if ((*p)[cmd_len] == '(')
      paren_depth++;
    else if ((*p)[cmd_len] == ')')
      paren_depth--;
    if (paren_depth > 0)
      cmd_len++;
  }

  char *cmd_line = arena_alloc(a, cmd_len + 1);
  strncpy(cmd_line, start, cmd_len);
  cmd_line[cmd_len] = '\0';
  *p += cmd_len + 1;
  int fds[2];
  if (pipe(fds) == -1)
    return err_syntax;

  pid_t pid = fork();
  if (pid < 0)
    return err_fatal;

  if (pid == 0) {
    t_token_stream ts;
    init_token_stream(&ts, &shell->arena);
    shell->job_control_flag = 0;

    close(fds[0]);
    dup2(fds[1], STDOUT_FILENO);
    close(fds[1]);

    parse_and_execute(&cmd_line, shell, &ts, false);
    fflush(stdout);
    _exit(shell->last_exit_status);
  }

  close(fds[1]);
  size_t start_k = *k;

  while (1) {
    if (*k + 4096 >= *buf_cap) {
      size_t new_cap = *buf_cap * 2;
      *buf = arena_realloc(a, *buf, new_cap, *buf_cap);
      *buf_cap = new_cap;
    }

    ssize_t n = read(fds[0], *buf + *k, *buf_cap - *k - 1);
    if (n <= 0)
      break;

    *k += n;
  }
  close(fds[0]);
  waitpid(pid, NULL, 0);

  while (*k > start_k && (*buf)[*k - 1] == '\n') {
    (*k)--;
  }
  (*buf)[*k] = '\0';

  return err_none;
}

t_err_type expand_braces(t_shell *shell, char **buf, size_t *buf_cap,
                         const char **p, size_t *k, t_arena *a) {
  (*p)++;
  const char *start = *p;

  int brace_depth = 1;
  const char *end = start;
  while (*end && brace_depth > 0) {
    if (*end == '{')
      brace_depth++;
    else if (*end == '}')
      brace_depth--;

    if (brace_depth > 0)
      end++;
  }
  if (brace_depth != 0 || *end != '}')
    return err_syntax;

  t_param_op op = get_param_op(shell, start);
  if (op == PARAM_OP_ERR)
    return err_syntax;

  if (op == PARAM_OP_LEN) {
    start++;
    size_t var_len = 0;
    while (start + var_len < end &&
           is_valid_var_char(start[var_len], var_len == 0))
      var_len++;

    char *var_name = arena_alloc(a, var_len + 1);
    strncpy(var_name, start, var_len);
    var_name[var_len] = '\0';

    char *val = getenv_local(&shell->env, var_name, a);
    char len_str[20];
    snprintf(len_str, sizeof(len_str), "%zu", val ? strlen(val) : 0);
    append_to_buf(buf, buf_cap, k, len_str, a);
    *p = end + 1;
    return err_none;
  }

  size_t var_name_len = 0;
  while (start + var_name_len < end && !is_op_char(start + var_name_len))
    var_name_len++;

  char *var_name = arena_alloc(a, var_name_len + 1);
  strncpy(var_name, start, var_name_len);
  var_name[var_name_len] = '\0';

  char *val = getenv_local(&shell->env, var_name, a);
  const char *arg = start + var_name_len;

  if (op == PARAM_OP_MINUS || op == PARAM_OP_EQUAL || op == PARAM_OP_PLUS ||
      op == PARAM_OP_QUESTION || op == PARAM_OP_HASH_HASH ||
      op == PARAM_OP_PERCENT_PERCENT)
    arg += 2;
  else if (op != PARAM_OP_NONE)
    arg += 1;

  size_t arg_len = end - arg;
  char *word_raw = arena_alloc(a, arg_len + 1);
  strncpy(word_raw, arg, arg_len);
  word_raw[arg_len] = '\0';

  static int depth = 0;
  depth++;
  char *word = expand_recursive(shell, word_raw, a, depth);
  depth--;
  if (!word) {
    return err_depth;
  }

  switch (op) {
  case PARAM_OP_NONE:
    if (val)
      append_to_buf(buf, buf_cap, k, val, a);
    break;

  case PARAM_OP_MINUS:
    append_to_buf(buf, buf_cap, k, (val && *val) ? val : word, a);
    break;

  case PARAM_OP_EQUAL:
    if (val && *val) {
      append_to_buf(buf, buf_cap, k, val, a);
    } else {
      add_to_env(shell, var_name, word);
      append_to_buf(buf, buf_cap, k, word, a);
    }
    break;

  case PARAM_OP_PLUS:
    if (val && *val)
      append_to_buf(buf, buf_cap, k, word, a);
    break;

  case PARAM_OP_QUESTION:
    if (val && *val)
      append_to_buf(buf, buf_cap, k, val, a);
    else {
      fprintf(stderr, "msh: %s: %s\n", var_name,
              *word ? word : "parameter not set");
      return err_syntax;
    }
    break;

  case PARAM_OP_HASH:
  case PARAM_OP_HASH_HASH:
  case PARAM_OP_PERCENT:
  case PARAM_OP_PERCENT_PERCENT:
    if (val && *val) {
      bool longest =
          (op == PARAM_OP_HASH_HASH || op == PARAM_OP_PERCENT_PERCENT);

      if (op == PARAM_OP_HASH || op == PARAM_OP_HASH_HASH) {
        size_t offset = get_leading_pattern_offset(val, word, longest);
        append_to_buf(buf, buf_cap, k, val + offset, a);
      } else {
        size_t keep_len = get_trailing_pattern_len(val, word, longest);
        if (keep_len > 0) {
          char save = val[keep_len];
          ((char *)val)[keep_len] = '\0';
          append_to_buf(buf, buf_cap, k, val, a);
          ((char *)val)[keep_len] = save;
        }
      }
    }
    break;
  default:
    break;
  }

  *p = end + 1;

  return err_none;
}

t_err_type expand_tilde(t_shell *shell, char **buf, size_t *buf_cap,
                        const char **p, size_t *k, t_arena *a) {
  const char *intp = *p + 1;

  if (*intp == '\0' || *intp == '/' || isspace(*intp)) {
    const char *home = getenv_local_ref(&shell->env, "HOME");
    if (!home)
      home = getenv("HOME");
    if (home) {
      append_to_buf(buf, buf_cap, k, home, a);
      *p = intp;
    } else {
      append_to_buf(buf, buf_cap, k, "~", a);
      (*p)++;
    }
    return err_none;
  }

  const char *user_start = intp;
  size_t u_len = 0;
  while (user_start[u_len] && user_start[u_len] != '/' &&
         !isspace(user_start[u_len])) {
    u_len++;
  }

  char uname[256];
  if (u_len < sizeof(uname)) {
    memcpy(uname, user_start, u_len);
    uname[u_len] = '\0';

    struct passwd *pw = getpwnam(uname);
    if (pw && pw->pw_dir) {
      append_to_buf(buf, buf_cap, k, pw->pw_dir, a);
      *p = user_start + u_len;
      return err_none;
    }
  }

  append_to_buf(buf, buf_cap, k, "~", a);
  (*p)++;
  return err_none;
}

static bool word_boundary(char c) {
  return isspace(c) || c == '(' || c == ')' || c == ';' || c == '|' ||
         c == '&' || c == '=';
}

static t_err_type make_buf(t_shell *shell, t_token *start,
                           const size_t segment_len, t_arena *a, char **buf,
                           size_t *buf_cap) {
  const char *p = start->start;
  const t_token *last = start + (segment_len - 1);
  const char *end = last->start + last->len;

  size_t k = 0;

  bool dq = false;
  bool sq = false;
  bool word_start = true;

  while (p < end) {
    if (*p == '\'' && !dq) {
      sq = !sq;
    } else if (*p == '"' && !sq) {
      dq = !dq;
    }
    if (*p == '~' && !sq && !dq && word_start) {
      expand_tilde(shell, buf, buf_cap, &p, &k, a);
      word_start = false;
      continue;
    }
    if (*p == '$' && !sq && p + 1 < end && !isspace(*(p + 1))) {
      t_exp_handler h = find_handler(p + 1);
      p++;

      t_err_type err = h(shell, buf, buf_cap, &p, &k, a);
      if (err != err_none)
        return err;

      word_start = false;
      continue;
    }
    char c[2] = {*p, '\0'};
    append_to_buf(buf, buf_cap, &k, c, a);
    if (!sq && !dq && word_boundary(*p)) {
      word_start = true;
    } else {
      word_start = false;
    }
    p++;
  }

  (*buf)[k] = '\0';
  return err_none;
}

static char *expand_recursive(t_shell *shell, char *str, t_arena *a,
                              int depth) {

  if (depth > 32) {
    fprintf(stderr, "\nmsh: max recursive depth");
    return NULL;
  }

  if (!str || !*str) {
    char *p = (char *)arena_alloc(a, 1);
    p[0] = '\0';
    return p;
  }

  t_token temp_tok = {.type = TOKEN_SIMPLE, .start = str, .len = strlen(str)};

  size_t buf_cap = 2048;
  char *buf = arena_alloc(a, buf_cap);

  make_buf(shell, &temp_tok, 1, a, &buf, &buf_cap);

  return buf;
}

t_err_type split_ifs(t_shell *shell, char *buf, size_t k, char ***argv,
                     t_arena *a) {
  char *ifs = get_ifs(shell, a);
  unsigned char is_sep[256] = {0};
  for (int i = 0; ifs[i]; i++)
    is_sep[(unsigned char)ifs[i]] = 1;

  size_t count = 0;
  size_t cap = 32;
  char *p = buf;
  bool in_sq = false;
  bool in_dq = false;
  *argv = arena_alloc(a, sizeof(char *) * cap);
  for (size_t i = 0; i < cap; i++)
    (*argv)[i] = NULL;

  while (*p) {
    while (*p && !in_sq && !in_dq && is_sep[(unsigned char)*p] &&
           is_ifs_whitespace(*p)) {
      p++;
    }

    if (!*p)
      break;
    if (count >= cap - 1) {
      size_t old_cap = cap;
      cap *= 2;
      *argv = arena_realloc(a, *argv, cap * sizeof(char *),
                            old_cap * sizeof(char *));
      for (size_t i = old_cap; i < cap; i++)
        (*argv)[i] = NULL;
    }
    (*argv)[count++] = p;

    while (*p) {
      if (*p == '\'' && !in_dq) {
        in_sq = !in_sq;
      } else if (*p == '\"' && !in_sq) {
        in_dq = !in_dq;
      }
      if (!in_sq && !in_dq && is_sep[(unsigned char)*p]) {
        break;
      }
      p++;
    }
    if (*p) {
      char delimiter = *p;
      *p = '\0';
      p++;
      if (!is_ifs_whitespace(delimiter)) {
        if (is_sep[(unsigned char)*p] || !*p) {
          if (count < cap - 1) {
            (*argv)[count++] = p;
          }
        }
      }
    }
  }
  (*argv)[count] = NULL;

  return err_none;
}
void strip_quotes(char *str) {
  size_t r = 0, w = 0;
  bool sq = false, dq = false;

  while (str[r]) {
    if (str[r] == '\'' && !dq) {
      sq = !sq;
      r++;
    } else if (str[r] == '"' && !sq) {
      dq = !dq;
      r++;
    } else if (str[r] == '\\' && !sq) {
      r++;
      if (str[r])
        str[w++] = str[r++];
    } else {
      str[w++] = str[r++];
    }
  }
  str[w] = '\0';
}

static int find_brace_span(const char *s, size_t len, size_t *l, size_t *r) {
  int depth = 0;
  bool sq = false, dq = false;

  for (size_t i = 0; i < len; i++) {
    char c = s[i];

    if (c == '\'' && !dq)
      sq = !sq;
    else if (c == '"' && !sq)
      dq = !dq;

    if (sq || dq)
      continue;

    if (c == '{') {
      if (depth == 0)
        *l = i;
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0) {
        *r = i;
        return 1;
      }
    }
  }
  return 0;
}

static char **split_brace_options(const char *s, t_arena *a) {
  size_t cap = 8, argc = 0;
  char **out = arena_alloc(a, sizeof(char *) * cap);

  int depth = 0;
  bool sq = false, dq = false;
  const char *start = s;

  for (const char *p = s;; p++) {
    char c = *p;

    if (c == '\'' && !dq)
      sq = !sq;
    else if (c == '"' && !sq)
      dq = !dq;

    if (!sq && !dq) {
      if (c == '{')
        depth++;
      else if (c == '}')
        depth--;
    }

    if ((c == ',' && depth == 0 && !sq && !dq) || c == '\0') {
      size_t len = p - start;
      char *part = arena_alloc(a, len + 1);
      memcpy(part, start, len);
      part[len] = '\0';

      if (argc >= cap - 1) {
        out = arena_realloc(a, out, sizeof(char *) * cap * 2,
                            sizeof(char *) * cap);
        cap *= 2;
      }
      out[argc++] = part;

      if (c == '\0')
        break;
      start = p + 1;
    }
  }

  out[argc] = NULL;
  return out;
}

static char **expand_brace_range(const char *s, t_arena *a) {
  const char *dots = strstr(s, "..");
  if (!dots)
    return NULL;

  size_t lhs_len = dots - s;
  const char *rhs_start = dots + 2;

  if ((isdigit(s[0]) || (s[0] == '-' && isdigit(s[1]))) &&
      (isdigit(*rhs_start) ||
       (*rhs_start == '-' && isdigit(*(rhs_start + 1))))) {

    int start = atoi(s);
    int end = atoi(rhs_start);
    int step = (start <= end) ? 1 : -1;
    int count = abs(end - start) + 1;

    char **out = arena_alloc(a, sizeof(char *) * (count + 1));
    for (int i = 0; i < count; i++) {
      char buf[32];
      int n = snprintf(buf, sizeof(buf), "%d", start + (i * step));
      out[i] = arena_alloc(a, n + 1);
      memcpy(out[i], buf, n + 1);
    }
    out[count] = NULL;
    return out;
  } else if (isalpha(s[0]) && isalpha(*rhs_start) && lhs_len == 1 &&
             strlen(rhs_start) == 1) {
    int start = s[0];
    int end = *rhs_start;
    int step = (start <= end) ? 1 : -1;
    int count = abs(end - start) + 1;

    char **out = arena_alloc(a, sizeof(char *) * (count + 1));
    for (int i = 0; i < count; i++) {
      out[i] = arena_alloc(a, 2);
      out[i][0] = (char)(start + (i * step));
      out[i][1] = '\0';
    }
    out[count] = NULL;
    return out;
  }
  return NULL;
}

static char **brace_expand_inner(const char *s, t_arena *a) {
  if (strstr(s, ".."))
    return expand_brace_range(s, a);
  return split_brace_options(s, a);
}

void push_token(t_token_stream *vs, t_token t, t_arena *a) {
  if (vs->tokens_arr_len + 1 >= vs->tokens_arr_cap) {
    size_t old_size = vs->tokens_arr_cap * sizeof(t_token);
    vs->tokens_arr_cap *= 2;
    size_t new_size = vs->tokens_arr_cap * sizeof(t_token);
    vs->tokens = (t_token *)arena_realloc(a, vs->tokens, new_size, old_size);
  }
  vs->tokens[vs->tokens_arr_len++] = t;
}

void expand_braces_to_stream(t_token_stream *vs, t_token *tok, t_arena *a) {
  size_t l = 0, r = 0;

  if (!find_brace_span(tok->start, tok->len, &l, &r)) {
    push_token(vs, *tok, a);
    return;
  }
  size_t pre_len = l;
  char *prefix = arena_alloc(a, pre_len + 1);
  if (pre_len > 0)
    memcpy(prefix, tok->start, pre_len);
  prefix[pre_len] = '\0';

  size_t inner_len = r - l - 1;
  char *inner = arena_alloc(a, inner_len + 1);
  if (inner_len > 0)
    memcpy(inner, tok->start + l + 1, inner_len);
  inner[inner_len] = '\0';

  size_t suf_len = tok->len - r - 1;
  char *suffix = arena_alloc(a, suf_len + 1);
  if (suf_len > 0)
    memcpy(suffix, tok->start + r + 1, suf_len);
  suffix[suf_len] = '\0';

  char **alts = brace_expand_inner(inner, a);
  if (!alts) {
    push_token(vs, *tok, a);
    return;
  }

  for (size_t i = 0; alts[i]; i++) {
    size_t alt_len = strlen(alts[i]);
    size_t total_len = pre_len + alt_len + suf_len;

    char *new_str = arena_alloc(a, total_len + 1);

    memcpy(new_str, prefix, pre_len);
    memcpy(new_str + pre_len, alts[i], alt_len);
    memcpy(new_str + pre_len + alt_len, suffix, suf_len);
    new_str[total_len] = '\0';

    t_token nt = {.type = tok->type, .start = new_str, .len = total_len};

    expand_braces_to_stream(vs, &nt, a);
  }
}
bool has_brace_pattern(t_token *tok) {

  if (!tok || tok->len < 3)
    return false;

  const char *p = tok->start;
  size_t i = 0;
  while (i < tok->len) {
    if (p[i] == '{') {
      if (i == 0 || p[i - 1] != '$') {
        bool found_separator = false;
        bool found_close = false;
        for (size_t j = i + 1; j < tok->len; j++) {
          if (p[j] == '}') {
            found_close = true;
            break;
          }
          if (p[j] == ',') {
            found_separator = true;
          }
          if (p[j] == '.' && j + 1 < tok->len && p[j + 1] == '.') {
            found_separator = true;
          }
        }
        if (found_close && found_separator) {
          return true;
        }
      }
    }
    i++;
  }
  return false;
}

static bool has_glob(const char *s) {
  for (; *s; s++) {
    if (*s == '*' || *s == '?')
      return true;
    if (*s == '[') {
      const char *p = s + 1;
      if (*p == '!' || *p == '^')
        p++;
      if (*p == ']')
        continue;
      while (*p && *p != ']')
        p++;
      if (*p == ']')
        return true;
    }
  }
  return false;
}

static int find_glob_start(char **argv) {
  if (!argv)
    return -1;

  for (int i = 0; argv[i]; i++) {
    if (has_glob(argv[i])) {
      return i;
    }
  }
  return -1;
}

t_err_type expand_make_argv(t_shell *shell, char ***argv, t_token *orig_tokens,
                            const size_t segment_len, t_arena *a) {
  t_token_stream vs;
  init_token_stream(&vs, a);

  for (size_t i = 0; i < segment_len; i++) {
    if (redir_tok_found(&orig_tokens[i])) {
      i++;
      continue;
    }

    if (has_brace_pattern(&orig_tokens[i])) {
      expand_braces_to_stream(&vs, &orig_tokens[i], a);
    } else {
      push_token(&vs, orig_tokens[i], a);
    }
  }

  size_t buf_cap = 65536;
  char *buf = arena_alloc(a, buf_cap);
  if (!buf)
    return err_fatal;

  t_err_type err =
      make_buf(shell, vs.tokens, vs.tokens_arr_len, a, &buf, &buf_cap);
  switch (err) {
  case err_fatal:
    fprintf(stderr, "\nmsh: fatal expansion error");
    break;
  case err_syntax:
    fprintf(stderr, "\nmsh: syntax err expansion");
    break;
  case err_depth:
    fprintf(stderr, "\nmsh: max expansion depth");
    break;
  case err_div_zero:
    fprintf(stderr, "\nmsh: div by zero");
    break;
  case err_overflow:
    fprintf(stderr, "\nmsh: overflow");
    break;
  default:
    if (err != err_none) {
      return err;
    } else {
      break;
    }
  }

  err = split_ifs(shell, buf, strlen(buf), argv, a);
  if (err != err_none)
    return err;

  if (*argv) {
    int m = find_glob_start(*argv);
    if (m >= 0) {
      if (expand_glob_from(shell, argv, m, a) != 0)
        return err_fatal;
    }

    for (int i = 0; (*argv)[i]; i++) {
      strip_quotes((*argv)[i]);
    }
  }

  return err_none;
}
