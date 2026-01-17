#include "dll.h"

/**
 * @file dll.c
 * @brief Implementation of DLL function
 */

/**
 * @brief initiliazes dll struct to null
 * @param list pointer to dll struct
 * @return always returns 0
 */
int init_dll(t_dll* list){

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;

    return 0;
}

/**
 * @brief pushes to head of dll
 * @param list pointer to dll struct
 * @param strbg string to push into dll node
 */
t_dllnode* push_front_dll(const char* strbg, t_dll* list){

    if(!strbg || *strbg == '\0') 
        return NULL;

    t_dllnode* nn = (t_dllnode*)malloc(sizeof(t_dllnode));
    if(!nn){
        perror("malloc pushfrontdll error");
        return NULL;
    }
    nn->strbg = (char*)malloc(strlen(strbg) + 1);
    if(!nn->strbg){
        perror("nn strbg fail");
        free(nn);
        return NULL;
    }
    strcpy(nn->strbg, strbg);

    nn->prev = NULL;
    nn->next = list->head;
    if(list->head){
        list->head->prev = nn;
    } else {
        list->tail = nn;
    }
    list->head = nn;
    list->size++;

    return nn;
}

/**
 * @brief pops the head of dll
 * @param list pointer to dll struct
 */
int pop_front_dll(t_dll* list){

    if(list->size <= 0 || list->head == NULL) { return 1; }

    t_dllnode* bomb = list->head;
    list->head = list->head->next;
    if(list->head){
        list->head->prev = NULL;
    } else{
        list->tail = NULL;
    }

    free(bomb->strbg);
    free(bomb);
    list->size--;
    
    return 0;
}

/**
 * @brief prints dll
 * @param list pointer to dll struct
 */
void print_dll(t_dll* list){

    t_dllnode* ptr = list->head;
    while(ptr){
        printf("%s ", ptr->strbg);
        ptr = ptr->next;
    }
    printf("\n");
    fflush(stdout);
}

/**
 * @brief frees dll
 * @param list pointer to dll struct
 */
int free_dll(t_dll* list){
    if(!list){
        return 0;
    }

    t_dllnode* ptr = list->head;
    t_dllnode* next = NULL;

    while(ptr){
        next = ptr->next;

        free(ptr->strbg);
        free(ptr);

        ptr = next;
    }
    
    list->head = list->tail = NULL;
    list->size = 0;

    return 0;
}
