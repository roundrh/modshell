#ifndef DLL_H
#define DLL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @file dll.h
 *
 * This module declares the DLL data structure.
 */

/**
 * @typedef s_dllnode t_dllnode
 * @brief defines the node struct for the dll struct
 *
 * This is a node for a dll of strings
 */
typedef struct s_dllnode {

    char* strbg;
    struct s_dllnode* next;
    struct s_dllnode* prev;
} t_dllnode;

/**
 * @typedef s_dll t_dll
 * @brief defines the dll struct
 *
 * This is a dll of strings used for the history in read_user_inp() found in userinp.c
 */
typedef struct s_dll {

    t_dllnode* head;
    t_dllnode* tail;
    int size;
} t_dll;

/**
 * @param list pointer to dll struct
 * @brief initializes dll pointers head and tail to null, size to 0
 *
 */
int init_dll(t_dll* list);

/**
 * @param strbg string to place in dllnode
 * @param list pointer to dll struct
 * @brief pushes to head of dll
 *
 */
t_dllnode* push_front_dll(const char* strbg, t_dll* list);

/**
 * @param list pointer to dll struct
 * @brief pops head of dll
 *
 */
int pop_front_dll(t_dll* list);

/**
 * @brief prints dll
 * @param list pointer to dll struct
 *
 */
void print_dll(t_dll* list);

/**
 * @param list pointer to dll struct
 * @brief frees all strdup'd strbgs in nodes, and frees node for all nodes in dll.
 *
 */
int free_dll(t_dll* list);

#endif // !DLL_H