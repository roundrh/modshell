#ifndef ALIAS_H
#define ALIAS_H

#include "hashtable.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

typedef struct s_alias {
  const char *cmd;
} t_alias;

t_ht_node *insert_alias(t_hashtable *ht, const char *alias, const char *cmd);
void print_aliases(t_hashtable *ht);
void free_alias(void *value);

#endif // ! H_ALIAS
