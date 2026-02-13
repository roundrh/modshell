#include "hashtable.h"
#include <stdlib.h>
#include <string.h>

void ht_init(t_hashtable *ht) {
  memset(ht->buckets, 0, sizeof(ht->buckets));
  ht->count = 0;
}

unsigned ht_hash(const char *key) {
  unsigned long h = 5381;
  int c;

  while ((c = *key++))
    h = ((h << 5) + h) + c;

  return h % HT_DEFSIZE;
}

t_ht_node *ht_find(t_hashtable *ht, const char *key) {
  unsigned idx = ht_hash(key);
  t_ht_node *n = ht->buckets[idx];

  while (n) {
    if (strcmp(n->key, key) == 0)
      return n;
    n = n->next;
  }
  return NULL;
}

t_ht_node *ht_insert(t_hashtable *ht, const char *key, void *value,
                     t_ht_free_fn freefn) {
  unsigned idx = ht_hash(key);
  t_ht_node *n = ht_find(ht, key);

  if (n && freefn) {
    freefn(n->value);
    n->value = value;
    return n;
  } else if (n) {
    return n;
  }

  n = malloc(sizeof(*n));
  if (!n)
    return NULL;

  n->key = strdup(key);
  n->value = value;
  n->next = ht->buckets[idx];
  ht->buckets[idx] = n;
  ht->count++;

  return n;
}

int ht_delete(t_hashtable *ht, const char *key, t_ht_free_fn free_fn) {
  unsigned idx = ht_hash(key);
  t_ht_node **cur = &ht->buckets[idx];

  while (*cur) {
    if (strcmp((*cur)->key, key) == 0) {
      t_ht_node *tmp = *cur;
      *cur = tmp->next;

      if (free_fn)
        free_fn(tmp->value);

      free(tmp->key);
      free(tmp);
      ht->count--;
      return 0;
    }
    cur = &(*cur)->next;
  }
  return -1;
}

int ht_flush(t_hashtable *ht, t_ht_free_fn free_fn) {
  for (int i = 0; i < HT_DEFSIZE; i++) {
    t_ht_node *n = ht->buckets[i];
    while (n) {
      t_ht_node *next = n->next;
      if (free_fn)
        free_fn(n->value);
      free(n->key);
      free(n);
      n = next;
    }
    ht->buckets[i] = NULL;
  }
  ht->count = 0;
  return 0;
}

void ht_print(t_hashtable *ht, t_ht_print_fn print_fn) {
  for (int i = 0; i < HT_DEFSIZE; i++) {
    t_ht_node *n = ht->buckets[i];
    while (n) {
      if (print_fn)
        print_fn(n->key, n->value);
      n = n->next;
    }
  }
}
