#ifndef BUILTINS_HT_H
#define BUILTINS_HT_H

#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @file builtins_ht.h
 *
 * Module declares the hashtable for builtins.
 */

/**
 * @def DEFSIZE_H
 * @brief size of buckets array of hashtable.
 */
#define DEFSIZE_H 128

typedef struct shell_s t_shell; ///< fwd decleration of struct in shell.h

typedef int (*t_builtin_func)(
    t_ast_n *node, t_shell *shell,
    char **argv); ///< function pointer to builtin function

/**
 * @typedef s_ht_node t_ht_node
 * @brief encapsulated data for a node in the chains within hashtable buckts
 *
 * Struct contains data about the ht's node such as the key (command alias),
 * function pointer to built in forkability (boolean value showing if the
 * builtin is forkable or must run in parent), and next pointer for list.
 */
typedef struct s_ht_node {

  char *key;
  t_builtin_func builtin_ptr;
  int forkable;
  struct s_ht_node *next;
} t_ht_node;

/**
 * @typedef s_hashtable t_hashtable
 * @brief contains buckets of lists of t_ht_node & count.
 *
 * builtins_count to track the amount of saved builtins
 */
typedef struct s_hashtable {

  t_ht_node *buckets[DEFSIZE_H];
  int builtins_count;
} t_hashtable;

/**
 *
 * @param ht pointer to hashtable of builtins
 * @param key pointer to key string
 * @param builtin_ptr pointer to function
 * @param forkable boolean val to determine if fork() can be called
 *
 * @brief pushes a node onto the head of a bucket in the hashtable.
 *
 * mallocd node pushed onto the bucket head, becomes the head.
 */
t_ht_node *push_front_ht_list(t_hashtable *ht, const char *key,
                              t_builtin_func builtin_ptr, int forkable);

/**
 *
 * @param ht pointer to hashtable of builtins
 *
 * @brief pushes a node onto the head of a bucket in the hashtable.
 *
 * mallocd node pushed onto the bucket head, becomes the head.
 */
void init_builtin_hashtable(t_hashtable *ht);

/**
 *
 * @param key string of builtin alias
 *
 * @brief receives alias and calculates hash of alias
 *
 * Uses prime number hash technique to calculate hash value fron passed key
 * value
 */
unsigned int hash_builtin(const char *key);

/**
 *
 * @param ht pointer to hashtable
 * @param key string of builtin alias
 * @param builtin_ptr pointer to builtin function
 * @param forkable bool val builtin forkability
 *
 * @brief Inserts builtin into hashtable
 */
t_ht_node *insert_builtin(t_hashtable *ht, const char *key,
                          t_builtin_func builtin_ptr, int forkable);

/**
 * @brief finds builtin in hashtable
 * @return pointer to builtin
 * @param ht pointer to hashtable
 * @param key alias to look for
 */
t_ht_node *hash_find_builtin(t_hashtable *ht, const char *key);

/**
 * @brief deletes builtin in ht
 * @param ht pointer to hashtable
 * @param key alias to delete
 * @return -1 on fail, 0 success
 */
int hash_delete_builtin(t_hashtable *ht, const char *key);

/**
 * @brief flushes hashtable of builtins
 * @param ht pointer to hashtable
 * @return 0 always
 */
int flush_builtin_ht(t_hashtable *ht);
#endif // ! BUILTINS_HT_H
