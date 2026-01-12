#include"builtins_ht.h"

/**
 * @file builtins_ht.c
 *
 * Implementation of builtins hashtable functions
 */

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
t_ht_node* push_front_ht_list(t_hashtable* ht, const char* key, t_builtin_func builtin_ptr, int forkable){

	int hd_idx = hash_builtin(key);

	t_ht_node* built_in_node = (t_ht_node*)malloc(sizeof(t_ht_node));
	if(!built_in_node){
		perror("node malloc fail: pushfront built in ht");
		return NULL;
	}
	built_in_node->key = (char*)malloc(strlen(key) + 1);
	if(!built_in_node->key){
		perror("malloc nodekey fail: ht built in");
		free(built_in_node);
		return NULL;
	}
	strcpy(built_in_node->key, key);
	built_in_node->builtin_ptr = builtin_ptr;

	
	built_in_node->forkable = forkable;
	built_in_node->next = ht->buckets[hd_idx];
	ht->buckets[hd_idx] = built_in_node;
	
	return built_in_node;
}

/**
 *
 * @param ht pointer to hashtable of builtins
 *
 * @brief pushes a node onto the head of a bucket in the hashtable.
 *
 * mallocd node pushed onto the bucket head, becomes the head.
 */
void init_builtin_hashtable(t_hashtable* ht) {
	for (int i = 0; i < DEFSIZE_H; ++i) 
		ht->buckets[i] = NULL;
	ht->builtins_count = 0;
}

/**
 *
 * @param key string of builtin alias
 *
 * @brief receives alias and calculates hash of alias
 *
 * Uses prime number hash technique to calculate hash value fron passed key value
 */
unsigned int hash_builtin(const char* key) {
    if (!key) return 0;

    unsigned int hash = 5381;
    int c;

    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % DEFSIZE_H;
}

/**
 *
 * @param ht pointer to hashtable
 * @param key string of builtin alias
 * @param builtin_ptr pointer to builtin function
 * @param forkable bool val builtin forkability
 *
 * @brief Inserts builtin into hashtable
 */
t_ht_node* insert_builtin(t_hashtable* ht, const char* key, t_builtin_func builtin_ptr, int forkable) {

	if(!ht) return NULL;

    int idx = hash_builtin(key);
	t_ht_node* nn = NULL;

	t_ht_node* ptr = ht->buckets[idx];
	while(ptr && strcmp(ptr->key, key) != 0){
		ptr = ptr->next;
	} //make sure value doesnt exist //
	if(!ptr){
		nn = push_front_ht_list(ht, key, builtin_ptr, forkable);
		if(!nn){
			perror("push error for builtins ht");
			return NULL;
		}
		ht->builtins_count++;
	}

	return nn;
}

/**
 * @brief finds builtin in hashtable
 * @return pointer to builtin
 * @param ht pointer to hashtable
 * @param key alias to look for
 */
t_ht_node* hash_find_builtin(t_hashtable* ht, const char* key) {
    int idx = hash_builtin(key);

	t_ht_node* ptr = ht->buckets[idx];
	while(ptr && strcmp(ptr->key, key) != 0){
		ptr = ptr->next;
	}

	return ptr;
}

/**
 * @brief deletes builtin in ht
 * @param ht pointer to hashtable
 * @param key alias to delete
 * @return -1 on fail, 0 success
 */
int hash_delete_builtin(t_hashtable* ht, const char* key) {
    int idx = hash_builtin(key);

	t_ht_node* ptr = ht->buckets[idx];
	t_ht_node* prev = NULL;
	while(ptr && strcmp(ptr->key, key) != 0){
		prev = ptr;
		ptr = ptr->next;
	} //make sure value doesnt exist, could replace with hashfind
	
    if (!ptr){
        return -1;
	}

    if (prev){
        prev->next = ptr->next;
	} else {
        ht->buckets[idx] = ptr->next;
	}

    free(ptr->key);
    free(ptr);
    ht->builtins_count--;

    return 0;
}

/**
 * @brief flushes hashtable of builtins
 * @param ht pointer to hashtable
 * @return 0 always
 */
int flush_builtin_ht(t_hashtable* ht) {

    if (!ht) return 0;

    for (int i = 0; i < DEFSIZE_H; i++) {
        t_ht_node* ptr = ht->buckets[i];
        while (ptr != NULL) {
            t_ht_node* next = ptr->next;

            if (ptr->key) {
                free(ptr->key);
                ptr->key = NULL;
            }
            ptr->builtin_ptr = NULL;
            free(ptr);
            ptr = NULL;

            ptr = next;
        }
        ht->buckets[i] = NULL;
    }
    ht->builtins_count = 0;
    return 0;
}


