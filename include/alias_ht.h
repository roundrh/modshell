/**
 * @file alias_ht.h
 * @brief Decleration of alias hashtable functions
 */

#ifndef ALIAS_HT_H
#define ALIAS_HT_H

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

/**
 * @def DEFSIZE_H
 * @brief buckets array default size. (this is unchanged).
 */
#define DEFSIZE_H 128

/**
 * @typedef s_alias_ht_node
 * @brief contains data of alias_ht_node for list
 */
typedef struct s_alias_ht_node {

    char* alias;
    char* aliased_cmd;
    struct s_alias_ht_node* next;
} t_alias_ht_node;

/**
 * @typedef s_alias_hashtable
 * @brief contains data of alias hashtable.
 */
typedef struct s_alias_hashtable {

	t_alias_ht_node* buckets[DEFSIZE_H];
	int alias_count;
} t_alias_hashtable;

/**
 * @brief Pushes a new alias node to the front of the hashtable bucket list
 * @param ht Pointer to the alias hashtable
 * @param alias The alias string
 * @param aliased_cmd The command that the alias represents
 * @return Pointer to the newly created node, or NULL on failure
 */
t_alias_ht_node* push_front_alias_ht_list(t_alias_hashtable* ht, const char* alias, const char* aliased_cmd);

/**
 * @brief Initializes the alias hashtable
 * @param ht Pointer to the alias hashtable to initialize
 */
void init_alias_hashtable(t_alias_hashtable* ht);

/**
 * @brief Hash function for alias strings using DJB2 algorithm
 * @param key The alias string to hash
 * @return The computed hash value modulo DEFSIZE_H
 */
unsigned int hash_alias(const char* key);

/**
 * @brief Swaps a command with its alias if found
 * @param cmd The command buffer to check and potentially swap
 * @param ht Pointer to the alias hashtable
 * @return 0 on successful swap, -1 if alias not found
 */
char* find_alias_command(const char* str, t_alias_hashtable* ht);

/**
 * @brief Inserts a new alias into the hashtable
 * @param ht Pointer to the alias hashtable
 * @param alias The alias string
 * @param aliased_cmd The command that the alias represents
 * @return Pointer to the inserted node, or existing node if alias already exists
 */
t_alias_ht_node* insert_alias(t_alias_hashtable* ht, const char* alias, const char* aliased_cmd);

/**
 * @brief Finds an alias node in the hashtable
 * @param alias The alias to search for
 * @param ht Pointer to the alias hashtable
 * @return Pointer to the found node, or NULL if not found
 */
t_alias_ht_node* hash_find_alias(const char* alias, t_alias_hashtable* ht);

/**
 * @brief Deletes an alias from the hashtable
 * @param ht Pointer to the alias hashtable
 * @param alias The alias to delete
 * @return 0 on success, -1 if alias not found
 */
int hash_delete_alias(t_alias_hashtable* ht, const char* alias);

/**
 * @brief Flushes all aliases from the hashtable
 * @param ht Pointer to the alias hashtable
 * @return Always returns 0
 */
int flush_alias_ht(t_alias_hashtable* ht);

/**
 * @brief Prints all aliases in the hashtable
 * @param ht Pointer to the alias hashtable
 */
void print_alias_ht(t_alias_hashtable* ht);

#endif // ! ALIAS_HT_H