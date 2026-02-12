#include "alias.h"
#include "hashtable.h"

void free_alias(void *value) {
  t_alias *a = (t_alias *)value;
  if (a) {
    free((void *)a->cmd); // Free the strdup'd command
    free(a);              // Free the struct itself
  }
}

t_ht_node *insert_alias(t_hashtable *ht, const char *alias, const char *cmd) {
  t_ht_node *existing = ht_find(ht, alias);
  if (existing) {
    free_alias(existing->value);
  }

  t_alias *a = malloc(sizeof(*a));
  a->cmd = strdup(cmd);
  return ht_insert(ht, alias, a);
}

static void print_alias(const char *key, void *value) {
  t_alias *a = (t_alias *)value;
  if (a && a->cmd) {
    printf("%s='%s'\n", key, a->cmd);
  }
}

void print_aliases(t_hashtable *ht) {
  if (!ht || ht->count == 0) {
    return;
  }
  ht_print(ht, print_alias);
}
