#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>

#define HT_DEFSIZE 128

typedef struct s_ht_node {
  char *key;
  void *value;
  struct s_ht_node *next;
} t_ht_node;

typedef struct s_hashtable {
  t_ht_node *buckets[HT_DEFSIZE];
  size_t count;
} t_hashtable;

typedef void (*t_ht_free_fn)(void *value);
typedef void (*t_ht_print_fn)(const char *key, void *value);

void ht_init(t_hashtable *ht);
unsigned ht_hash(const char *key);

t_ht_node *ht_insert(t_hashtable *ht, const char *key, void *value);
t_ht_node *ht_find(t_hashtable *ht, const char *key);
int ht_delete(t_hashtable *ht, const char *key, t_ht_free_fn free_fn);
int ht_flush(t_hashtable *ht, t_ht_free_fn free_fn);

void ht_print(t_hashtable *ht, t_ht_print_fn print_fn);

#endif
