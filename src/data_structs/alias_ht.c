/**
 * @file alias_ht.c
 * @brief Implementation of alias hashtable functions
 */

#include"alias_ht.h"

/**
 * @brief Pushes a new alias node to the front of the hashtable bucket list
 * @param ht Pointer to the alias hashtable
 * @param alias The alias string
 * @param aliased_cmd The command that the alias represents
 * @return Pointer to the newly created node, or NULL on failure
 */
t_alias_ht_node* push_front_alias_ht_list(t_alias_hashtable* ht, const char* alias, const char* aliased_cmd){

	int hd_idx = hash_alias(alias);

	t_alias_ht_node* alias_node = (t_alias_ht_node*)malloc(sizeof(t_alias_ht_node));
	if(!alias_node){
		perror("node malloc fail: pushfront built in ht");
		return NULL;
	}
	alias_node->alias = (char*)malloc(strlen(alias) + 1);
	if(!alias_node->alias){
		perror("malloc nodekey fail: ht built in");
		free(alias_node);
		return NULL;
	}
	strcpy(alias_node->alias, alias);

    alias_node->aliased_cmd = (char*)malloc(strlen(aliased_cmd) + 1);
	if(!alias_node->aliased_cmd){
		perror("malloc nodekey fail: ht built in");
        free(alias_node->alias);
		free(alias_node);
		return NULL;
	}
	strcpy(alias_node->aliased_cmd, aliased_cmd);

	alias_node->next = ht->buckets[hd_idx];
	ht->buckets[hd_idx] = alias_node;
	
	return alias_node;
}

/**
 * @brief Initializes the alias hashtable
 * @param ht Pointer to the alias hashtable to initialize
 */
void init_alias_hashtable(t_alias_hashtable* ht){

    for (int i = 0; i < DEFSIZE_H; ++i) 
		ht->buckets[i] = NULL;
	ht->alias_count = 0;
}

/**
 * @brief Hash function for alias strings using DJB2 algorithm
 * @param key The alias string to hash
 * @return The computed hash value modulo DEFSIZE_H
 */
unsigned int hash_alias(const char* key){

    if (!key) return 0;

    unsigned int hash = 5381;
    int c;

    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % DEFSIZE_H;
}

/**
 * @brief Inserts a new alias into the hashtable
 * @param ht Pointer to the alias hashtable
 * @param alias The alias string
 * @param aliased_cmd The command that the alias represents
 * @return Pointer to the inserted node, or existing node if alias already exists
 */
t_alias_ht_node* insert_alias(t_alias_hashtable* ht, const char* alias, const char* aliased_cmd){

    if(ht->alias_count == DEFSIZE_H){
        printf("\nMax alias count reached, cannot add more\n");
        fflush(stdout);
        return NULL;
    }

    int idx = hash_alias(alias);
	t_alias_ht_node* nn = NULL;

	t_alias_ht_node* ptr = ht->buckets[idx];
	while(ptr && strcmp(ptr->alias, alias) != 0){
		ptr = ptr->next;
	} //make sure alias doesnt exist //
	if(!ptr){
		nn = push_front_alias_ht_list(ht, alias, aliased_cmd);
		if(!nn){
			perror("push error for builtins ht");
			return NULL;
		}
		ht->alias_count++;
        return nn;
	}

	return ptr;
}

/**
 * @brief Finds an alias node in the hashtable
 * @param alias The alias to search for
 * @param ht Pointer to the alias hashtable
 * @return Pointer to the found node, or NULL if not found
 */
t_alias_ht_node* hash_find_alias(const char* alias, t_alias_hashtable* ht){

    int idx = hash_alias(alias);

	t_alias_ht_node* ptr = ht->buckets[idx];
	while(ptr && strcmp(ptr->alias, alias) != 0){
		ptr = ptr->next;
	}

	return ptr;
}

/**
 * @brief Deletes an alias from the hashtable
 * @param ht Pointer to the alias hashtable
 * @param alias The alias to delete
 * @return 0 on success, -1 if alias not found
 */
int hash_delete_alias(t_alias_hashtable* ht, const char* alias){

    int idx = hash_alias(alias);

	t_alias_ht_node* ptr = ht->buckets[idx];
	t_alias_ht_node* prev = NULL;
	while(ptr && strcmp(ptr->alias, alias) != 0){
		prev = ptr;
		ptr = ptr->next;
	} //make sure value doesnt exist
	
    if (!ptr){
        return -1;
	}

    if (prev){
        prev->next = ptr->next;
	} else {
        ht->buckets[idx] = ptr->next;
	}

    free(ptr->alias);
    free(ptr->aliased_cmd);
    free(ptr);
    ht->alias_count--;

    return 0;
}

/**
 * @brief Flushes all aliases from the hashtable
 * @param ht Pointer to the alias hashtable
 * @return Always returns 0
 */
int flush_alias_ht(t_alias_hashtable* ht){

    int i = 0;
    while(i < DEFSIZE_H){
        t_alias_ht_node* ptr = ht->buckets[i];
        t_alias_ht_node* bomb = NULL;
        
        while(ptr != NULL){
            bomb = ptr;
            ptr = ptr->next;
            free(bomb->alias);
            free(bomb->aliased_cmd);
            free(bomb);
        }

        ht->buckets[i] = NULL;
        i++;
    }

    ht->alias_count = 0;
    return 0;
}

/**
 * @brief Prints all aliases in the hashtable
 * @param ht Pointer to the alias hashtable
 */
void print_alias_ht(t_alias_hashtable* ht){

    int i = 0;
    printf("\nAlias | Command\n");
    while(i < DEFSIZE_H){
        t_alias_ht_node* ptr = ht->buckets[i];
        while(ptr != NULL){
            printf("%s | %s\n", ptr->alias, ptr->aliased_cmd);
            ptr = ptr->next;
        }

        i++;
    }
}

static int realloc_cmd(char** cmd, size_t size){
    
    if(!cmd || size <= 0) 
        return -1;

    char* new_cmd = realloc(*cmd, size);
    if(!new_cmd){
        perror("fatal realloc");
        return -1;
    }

    *cmd = new_cmd;
    return 0;
}   

/**
 * @brief Swaps a command with its alias if found
 * @param cmd The command buffer to check and potentially swap
 * @param ht Pointer to the alias hashtable
 * @return 0 on successful swap, -1 if alias not found
 */
int swap_alias_command(char** cmd, t_alias_hashtable* ht){

    t_alias_ht_node* node = hash_find_alias(*cmd, ht);
    if(!node)
        return -1;

    int alias_len = strlen(node->aliased_cmd);
    
    if(realloc_cmd(cmd, alias_len + 1) == -1){
        perror("fatal");
        return -1;
    }

    strcpy(*cmd, node->aliased_cmd);

    return 0;
}